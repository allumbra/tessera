#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <QCryptographicHash>
#include <QJsonArray>
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>

#include "artcache.h"
#include "bindings.h"
#include "collage.h"
#include "commands.h"
#include "cut.h"
#include "fakenetwork.h"
#include "filepicker.h"
#include "folder.h"
#include "gallery.h"
#include "museum.h"
#include "settings.h"
#include "stats.h"
#include "dwindle.h"
#include "fakehyprland.h"
#include "game.h"
#include "hyprland.h"
#include "levels.h"
#include "player.h"
#include "progress.h"
#include "puzzle.h"
#include "theme.h"
#include "translator.h"

// A made-up slice of `omarchy-menu-keybindings --print` with the cases the
// parser handles: alternatives, a chord listed twice, media keys, the mouse,
// and one rebound move (swap left on H).
static const char *Sample = R"(SUPER + W / SUPER + Q               → Close window
SUPER + J                           → Toggle window split
SUPER SHIFT + H                     → Swap window to the left
SUPER SHIFT + 4                     → Move window to workspace 4
ALT + TAB                           → Focus on next window
ALT + TAB                           → Reveal active window on top
SUPER + LEFT MOUSE BUTTON           → Move window
XF86AudioMute                       → Mute
)";

static Game::Timings fast(bool hintsNow = true) {
    Game::Timings t;
    t.poll = 10;
    t.settle = 12;
    t.spawn = 400;
    t.wait = 15;
    t.tier1 = hintsNow ? 0 : 1000000;
    t.tier2 = hintsNow ? 0 : 1000000;
    t.tier3 = hintsNow ? 0 : 1000000;
    return t;
}

static void writeFile(const QString &path, const QByteArray &contents) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
}

// A desktop with the board on workspace 1 and the game on it.
struct Rig {
    QTemporaryDir dir;
    FakeDesktop desktop;
    std::unique_ptr<Game> game;
    QString board;

    explicit Rig(bool unlocked = false, bool hintsNow = true) {
        if (unlocked) {
            QJsonObject stars, best;
            for (const auto &l : Levels::lessons())
                stars.insert(l.id, 1);
            for (const auto &t : Levels::tiers())
                best.insert(t.id, 999999);
            writeFile(path(), QJsonDocument(QJsonObject{{"version", 1}, {"stars", stars}, {"best", best}}).toJson());
        }
        board = desktop.open("Tessera", desktop.own(), "tessera", false);
        game = std::make_unique<Game>(&desktop.events, &desktop, path());
        game->setTimings(fast(hintsNow));
        desktop.attach(game.get());
        game->start();
    }
    QString path() const { return dir.filePath("progress.json"); }

    bool waitFor(const QString &state, int ms = 3000) {
        QElapsedTimer t;
        t.start();
        while (game->state() != state && t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            QThread::msleep(2);
        }
        return game->state() == state;
    }
    // Walks to each workspace the game deals on, and waits for play.
    bool deal() {
        Player p(*game, desktop);
        QElapsedTimer t;
        t.start();
        while (game->state() != "playing" && t.elapsed() < 5000) {
            if (game->state() == "dealing" && !game->objectiveChord().isEmpty())
                p.press(game->objectiveChord());
            p.settle(20);
            if (game->state() == "error")
                return false;
        }
        return game->state() == "playing";
    }
};

// Spelled without a raw string: moc reads this file too and trips over one.
static QString callPattern() {
    return QStringLiteral("(?:\\b(?:qsTr|tr)\\(|QT_TRANSLATE_NOOP\\(\"\\w+\",|translate\\(\"\\w+\",)\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
}

static QString chordOf(const QString &action) { return Bindings::chordFor({}, action); }

static QByteArray fixture(const QString &name) {
    QFile f(QStringLiteral(SRCDIR) + "/../tests/fixtures/" + name);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", qPrintable(name));
    return f.readAll();
}

static QByteArray jpeg(const QSize &size) {
    QImage img(size, QImage::Format_RGB32);
    img.fill(QColor("#806040"));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "JPEG", 80);
    return bytes;
}

static Puzzle::Window window(const QString &address, int ws, const QRectF &rect, const QStringList &group = {}) {
    Puzzle::Window w;
    w.address = address;
    w.workspace = ws;
    w.rect = rect;
    w.group = group;
    w.present = true;
    return w;
}

static Commands::Snapshot snap(const QHash<QString, Puzzle::Window> &windows, const QString &focused, int ws) {
    Commands::Snapshot s;
    s.desk.windows = windows;
    s.desk.focused = focused;
    s.desk.activeWorkspace = ws;
    for (auto it = windows.begin(); it != windows.end(); ++it)
        s.pieceByAddress.insert(it->address, it.key());
    s.cursor = QPointF(500, 500);
    s.cursorKnown = true;
    s.valid = true;
    return s;
}

// Prepares pictures and waits for them, as the game would.
static Gallery::Result prepareNow(Gallery &gallery, const Gallery::Request &request) {
    Gallery::Result out;
    bool done = false;
    gallery.prepare(request, [&](const Gallery::Result &r) {
        out = r;
        done = true;
    });
    QElapsedTimer t;
    t.start();
    while (!done && t.elapsed() < 10000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return out;
}

// Hands back a folder, as the portal would after the player chose it.
class FakePicker : public FilePicker {
public:
    QString answer;
    void chooseFolder(const QString &) override { emit folderSelected(answer); }
};

class TesseraTests : public QObject {
    Q_OBJECT

private slots:
    // ── Bindings and events ────────────────────────────────────────────

    void bindingsUseThePlayersChords() {
        const auto bindings = Bindings::parse(QString::fromUtf8(Sample));
        QCOMPARE(Bindings::chordFor(bindings, "Swap window to the left"), QString("SUPER SHIFT + H"));
        QCOMPARE(Bindings::chordFor(bindings, "Close window"), QString("SUPER + W / SUPER + Q"));
        // Listed twice for two actions, so it's dropped, and the default is used.
        QCOMPARE(Bindings::chordFor(bindings, "Focus on next window"), QString("ALT + TAB"));
        // Not in the menu: Omarchy's default.
        QCOMPARE(Bindings::chordFor(bindings, "Move active window out of group"), QString("SUPER ALT + G"));
        QCOMPARE(Bindings::chordFor(bindings, "Move window to workspace 10"), QString("SUPER SHIFT + 0"));
        QCOMPARE(Bindings::chordFor(bindings, "Shrink window up a little"), QString("SUPER SHIFT ALT + MINUS"));
    }

    void eventsParse() {
        const auto e = Hypr::parseLine("openwindow>>55ebd0b3f9b0,3,tessera,Tessera · Wave, 2");
        QCOMPARE(e.name, QString("openwindow"));
        const auto f = Hypr::fields(e.data, 4);
        QCOMPARE(f.at(0), QString("55ebd0b3f9b0"));
        QCOMPARE(f.at(3), QString("Tessera · Wave, 2"));
        QCOMPARE(Hypr::address("0x55EBD0B3F9B0"), QString("55ebd0b3f9b0"));
        const auto empty = Hypr::parseLine("activewindowv2>>");
        QCOMPARE(empty.name, QString("activewindowv2"));
        QVERIFY(empty.data.isEmpty());
        QCOMPARE(Hypr::fields("-98,special:scratchpad,HDMI-A-1", 3).at(1), QString("special:scratchpad"));
    }

    // ── Cuts ───────────────────────────────────────────────────────────

    void dealtCutsAlternate() {
        const auto c = Cut::dealt(5);
        QCOMPARE(c.pieces(), 5);
        QCOMPARE(c.levels.at(0).split, Cut::Split::Side);
        QCOMPARE(c.levels.at(1).split, Cut::Split::Stack);
        QCOMPARE(c.levels.at(2).split, Cut::Split::Side);
        double area = 0;
        const auto cells = c.layout();
        for (const auto &r : cells)
            area += r.width() * r.height();
        QVERIFY(std::abs(area - 1) < 1e-9);
        for (int i = 0; i < cells.size(); ++i)
            for (int j = i + 1; j < cells.size(); ++j)
                QVERIFY(cells.at(i).intersected(cells.at(j)).width() * cells.at(i).intersected(cells.at(j)).height() < 1e-9);
    }

    void generatedCutsAreRepeatableAndHaveNoSlivers() {
        for (quint32 seed = 1; seed < 200; ++seed) {
            const auto a = Cut::generate(6, seed, {0.4, 0.5});
            const auto b = Cut::generate(6, seed, {0.4, 0.5});
            QCOMPARE(a.pieces(), 6);
            for (int i = 0; i < a.levels.size(); ++i) {
                QCOMPARE(a.levels.at(i).split, b.levels.at(i).split);
                QCOMPARE(a.levels.at(i).ratio, b.levels.at(i).ratio);
            }
            QVERIFY(Cut::thinnest(a) >= 0.11);
        }
    }

    // Every cut is a spiral of dwindle splits, so dealing its pieces one
    // after the other, turning and resizing, reproduces it exactly.
    void everyCutCanBeBuiltWithDwindle() {
        for (quint32 seed = 1; seed < 60; ++seed) {
            for (int n = 2; n <= 6; ++n) {
                const auto cut = Cut::generate(n, seed, {0.5, 0.5});
                Dwindle d;
                for (int k = 0; k < n; ++k)
                    d.insert(QString::number(k));
                // Turn and size each split from the top, as a player would
                // with Super+J and the resize keys.
                for (int k = 0; k + 1 < n; ++k) {
                    const QString leaf = QString::number(k);
                    const Cut::Tree before = Cut::infer(d.rects());
                    const int node = before.spineNodes().at(k);
                    if (before.nodes.at(node).split != cut.levels.at(k).split)
                        d.toggleSplit(leaf);
                    for (int tries = 0; tries < 60; ++tries) {
                        const Cut::Tree t = Cut::infer(d.rects());
                        const auto &nd = t.nodes.at(t.spineNodes().at(k));
                        const double extent = nd.split == Cut::Split::Side ? nd.rect.width() : nd.rect.height();
                        const double delta = (cut.levels.at(k).ratio - nd.ratio) * extent;
                        if (std::abs(delta) < 12)
                            break;
                        const double step = std::abs(delta) >= 100 ? 100 : 25;
                        const double move = delta > 0 ? step : -step;
                        d.resize(leaf, nd.split == Cut::Split::Side ? move : 0, nd.split == Cut::Split::Stack ? move : 0);
                    }
                }
                Puzzle::Desk desk;
                desk.pictures << Puzzle::Picture{"wave", cut, 4};
                const auto rects = d.rects();
                for (int k = 0; k < n; ++k) {
                    desk.pieces << Puzzle::Piece{QString::number(k), 0, k, "wave", {}};
                    Puzzle::Window w;
                    w.present = true;
                    w.workspace = 4;
                    w.rect = rects.value(QString::number(k));
                    desk.windows.insert(QString::number(k), w);
                }
                QVERIFY2(Puzzle::evaluate(desk).complete(), qPrintable(QString("seed %1, %2 pieces").arg(seed).arg(n)));
            }
        }
    }

    void treesReadBackFromWindows() {
        Dwindle d;
        for (const char *id : {"a", "b", "c", "d"})
            d.insert(id);
        Cut::Tree t = Cut::infer(d.rects());
        QVERIFY(t.isChain());
        QCOMPARE(t.spine(), (QStringList{"a", "b", "c", "d"}));
        QCOMPARE(t.end(), QString("d"));
        QCOMPARE(t.nodes.at(t.spineNodes().at(1)).split, Cut::Split::Stack);
        QVERIFY(std::abs(t.nodes.at(t.root).ratio - 0.5) < 0.01);

        // A window that split the first tile instead of the last one sits
        // in the wrong branch.
        Dwindle w;
        w.insert("a");
        w.insert("b");
        w.insert("c", "a");
        t = Cut::infer(w.rects());
        QVERIFY(!t.isChain());
        QVERIFY(QStringList({"a", "c"}).contains(t.misplaced()));
    }

    // ── Completion ─────────────────────────────────────────────────────

    void completionAllowsForGapsAndLittleErrors() {
        Puzzle::Desk desk;
        desk.pictures << Puzzle::Picture{"wave", Cut::Chain{{{Cut::Split::Side, 0.5}}}, 4};
        desk.pieces << Puzzle::Piece{"wave-1", 0, 0, "wave", {}} << Puzzle::Piece{"wave-2", 0, 1, "wave", {}};
        auto place = [&](const QString &id, const QRectF &r) {
            Puzzle::Window w;
            w.present = true;
            w.workspace = 4;
            w.rect = r;
            desk.windows.insert(id, w);
        };
        // Omarchy's gaps and borders: 12 outside, 7 between.
        place("wave-1", QRectF(12, 38, 941, 1030));
        place("wave-2", QRectF(967, 38, 941, 1030));
        QVERIFY(Puzzle::evaluate(desk).complete());

        // A resize step off still counts...
        place("wave-1", QRectF(12, 38, 941 + 50, 1030));
        place("wave-2", QRectF(967 + 50, 38, 941 - 50, 1030));
        QVERIFY(Puzzle::evaluate(desk).complete());
        // ...a big one doesn't.
        place("wave-1", QRectF(12, 38, 941 + 200, 1030));
        place("wave-2", QRectF(967 + 200, 38, 941 - 200, 1030));
        QVERIFY(!Puzzle::evaluate(desk).complete());
        QCOMPARE(Puzzle::evaluate(desk).pictures.at(0).placed, 0);

        // Swapped pieces aren't in place.
        place("wave-1", QRectF(967, 38, 941, 1030));
        place("wave-2", QRectF(12, 38, 941, 1030));
        QVERIFY(!Puzzle::evaluate(desk).complete());

        place("wave-1", QRectF(12, 38, 941, 1030));
        place("wave-2", QRectF(967, 38, 941, 1030));
        // A floating piece, a stray or another app's window spoils it.
        desk.windows["wave-2"].floating = true;
        QVERIFY(!Puzzle::evaluate(desk).complete());
        desk.windows["wave-2"].floating = false;
        desk.pieces << Puzzle::Piece{"stray-1", -1, 0, "nasa", {}};
        Puzzle::Window stray;
        stray.present = true;
        stray.workspace = 4;
        stray.floating = true;
        stray.rect = QRectF(700, 400, 480, 270);
        desk.windows.insert("stray-1", stray);
        QVERIFY(!Puzzle::evaluate(desk).complete());
        desk.windows["stray-1"].special = true;
        desk.windows["stray-1"].workspace = -98;
        QVERIFY(Puzzle::evaluate(desk).complete());
        desk.foreign[4] << QRectF(0, 0, 10, 10);
        QVERIFY(!Puzzle::evaluate(desk).complete());
    }

    // ── Context hints ──────────────────────────────────────────────────

    void hintsFitTheSituation() {
        auto names = [](const QString &id) { return id; };
        auto titles = [](int i) { return QString("Picture %1").arg(i); };
        // Two pictures: wave on workspace 4 (side by side), sunrise on 5.
        Puzzle::Desk desk;
        desk.pictures << Puzzle::Picture{"wave", Cut::Chain{{{Cut::Split::Side, 0.5}}}, 4}
                      << Puzzle::Picture{"sunrise", Cut::Chain{{{Cut::Split::Stack, 0.5}}}, 5};
        desk.pieces << Puzzle::Piece{"w1", 0, 0, "wave", {}} << Puzzle::Piece{"w2", 0, 1, "wave", {}}
                    << Puzzle::Piece{"s1", 1, 0, "sunrise", {}} << Puzzle::Piece{"s2", 1, 1, "sunrise", {}}
                    << Puzzle::Piece{"x", -1, 0, "nasa", {}};
        auto set = [&](const QString &id, int ws, const QRectF &r, bool floating = false) {
            Puzzle::Window w;
            w.present = true;
            w.address = id;
            w.workspace = ws;
            w.special = ws < 0;
            w.floating = floating;
            w.rect = r;
            desk.windows.insert(id, w);
        };
        const QRectF left(12, 38, 941, 1030), right(967, 38, 941, 1030), top(12, 38, 1896, 508), bottom(12, 560, 1896, 508);
        auto first = [&]() {
            const auto hints = Hints::compute(desk, Puzzle::evaluate(desk), chordOf, names, titles);
            return hints.isEmpty() ? Hints::Hint{} : hints.first();
        };

        // A piece on the wrong workspace: carry it to its own, with its number.
        set("w1", 4, QRectF(12, 38, 1896, 1030));
        set("w2", 5, right);
        set("s1", 5, left);
        set("s2", 3, QRectF(12, 38, 1896, 1030));
        set("x", -98, QRectF(700, 400, 480, 270));
        desk.activeWorkspace = 5;
        desk.focused = "w2";
        QCOMPARE(first().chord, QString("SUPER SHIFT + 4"));
        // Looking at a picture whose piece is elsewhere: go and get it.
        desk.focused = "s1";
        QCOMPARE(first().chord, chordOf("Focus on right window")); // the visitor next to it first
        // The visitor gone, the missing piece: go to workspace 3.
        set("w2", 4, right);
        set("w1", 4, left);
        set("s1", 5, QRectF(12, 38, 1896, 1030));
        QCOMPARE(first().chord, QString("SUPER + 3"));

        // Floating: tile it.
        set("w2", 4, QRectF(700, 400, 480, 270), true);
        set("w1", 4, QRectF(12, 38, 1896, 1030));
        desk.activeWorkspace = 4;
        desk.focused = "w2";
        QCOMPARE(first().chord, QString("SUPER + T"));
        // From another piece, a floating one is reached with Alt+Tab.
        desk.focused = "w1";
        QCOMPARE(first().chord, QString("ALT + TAB"));

        // Swapped neighbours: swap towards its place.
        set("w1", 4, right);
        set("w2", 4, left);
        desk.focused = "w1";
        QCOMPARE(first().chord, QString("SUPER SHIFT + LEFT"));
        // The other one is pointed at the piece that goes in first.
        desk.focused = "w2";
        QCOMPARE(first().chord, QString("SUPER + RIGHT"));

        // Stacked where the cut is side by side: turn the split.
        set("w1", 4, top);
        set("w2", 4, bottom);
        desk.focused = "w1";
        QCOMPARE(first().chord, QString("SUPER + J"));

        // The line in the wrong place: the left piece too narrow, so its
        // right edge moves right, a lot.
        set("w1", 4, QRectF(12, 38, 500, 1030));
        set("w2", 4, QRectF(526, 38, 1382, 1030));
        desk.focused = "w1";
        QCOMPARE(first().chord, QString("SUPER CTRL + EQUAL"));
        // The right piece is too wide: the same line moves right.
        desk.focused = "w2";
        QCOMPARE(first().chord, QString("SUPER CTRL + EQUAL"));
        set("w1", 4, QRectF(12, 38, 860, 1030));
        set("w2", 4, QRectF(886, 38, 1022, 1030));
        desk.focused = "w1";
        QCOMPARE(first().chord, QString("SUPER ALT + EQUAL"));

        // In a pile at the end of the spiral: pop it out.
        set("w1", 4, left);
        set("w2", 4, right);
        desk.windows["w2"].group = {"w2", "s9"};
        desk.focused = "w2";
        QCOMPARE(first().chord, QString("SUPER ALT + G"));
        desk.windows["w2"].group.clear();

        // The scratchpad: hidden, show it; shown, send the piece home.
        set("s1", 5, QRectF(12, 38, 1896, 1030));
        set("s2", -98, QRectF(700, 400, 480, 270));
        desk.activeWorkspace = 5;
        desk.focused = "s1";
        QCOMPARE(first().chord, QString("SUPER + S"));
        desk.scratchpadShown = true;
        desk.focused = "s2";
        QCOMPARE(first().chord, QString("SUPER SHIFT + 5"));

        // A stray in a picture: into the scratchpad with it.
        desk.scratchpadShown = false;
        set("s2", 5, bottom);
        set("s1", 5, top);
        set("x", 5, QRectF(700, 400, 480, 270), true);
        desk.focused = "x";
        QCOMPARE(first().chord, QString("SUPER ALT + S"));

        // On the board's workspace with nothing focused: where to go next.
        set("x", -98, QRectF(700, 400, 480, 270));
        desk.activeWorkspace = 1;
        desk.focused.clear();
        set("w1", 4, right);
        set("w2", 4, left);
        QCOMPARE(first().chord, QString("SUPER + 4"));
    }

    void hintsRepairAWrongBranch() {
        // Wave in three: a | (b / c). The third piece split the first tile.
        Puzzle::Desk desk;
        desk.pictures << Puzzle::Picture{"wave", Cut::dealt(3), 4};
        Dwindle d;
        d.insert("p1");
        d.insert("p2");
        d.insert("p3", "p1");
        for (int k = 0; k < 3; ++k)
            desk.pieces << Puzzle::Piece{QString("p%1").arg(k + 1), 0, k, "wave", {}};
        const auto rects = d.rects();
        for (auto it = rects.begin(); it != rects.end(); ++it) {
            Puzzle::Window w;
            w.present = true;
            w.workspace = 4;
            w.rect = it.value();
            desk.windows.insert(it.key(), w);
        }
        desk.activeWorkspace = 4;
        const Cut::Tree tree = Cut::infer(desk.tiles(4));
        desk.focused = tree.misplaced();
        auto hints = Hints::compute(desk, Puzzle::evaluate(desk), chordOf, [](const QString &s) { return s; },
                                    [](int) { return QString("Wave"); });
        QVERIFY(!hints.isEmpty());
        // No pile next to it yet: walk to a neighbour to make one.
        QVERIFY(hints.first().chord.startsWith("SUPER + "));
        QVERIFY(!hints.first().detail.isEmpty());
    }

    // ── Playing through, by the hints ──────────────────────────────────

    void everyLessonCanBeSolvedByFollowingTheHints() {
        for (int i = 0; i < Levels::lessons().size(); ++i) {
            Rig rig(true);
            QVERIFY(rig.waitFor("menu"));
            rig.game->playLesson(i);
            QCOMPARE(rig.game->state(), QString("intro"));
            rig.game->begin();
            Player player(*rig.game, rig.desktop);
            QVERIFY2(player.solve(), qPrintable(QString("lesson %1 (%2) stuck in %3: %4 | %5")
                                                   .arg(i).arg(Levels::lessons().at(i).id, rig.game->state(),
                                                               rig.game->error(), player.log.mid(player.log.size() - 12).join(", "))));
            QVERIFY(!rig.desktop.touchedForeign);
        }
    }

    void everyPuzzleCanBeSolvedByFollowingTheHints_data() {
        QTest::addColumn<int>("tier");
        QTest::addColumn<int>("seed");
        QTest::addColumn<bool>("wrongBranch");
        for (int tier = 0; tier < 3; ++tier)
            for (int seed = 1; seed <= 8; ++seed)
                QTest::addRow("tier %d seed %d", tier, seed) << tier << seed << (seed % 3 == 0);
    }

    // Some runs send every returning piece into the first tile, the worst
    // place for it, so the repair by pile is played too.
    void everyPuzzleCanBeSolvedByFollowingTheHints() {
        QFETCH(int, tier);
        QFETCH(int, seed);
        QFETCH(bool, wrongBranch);
        Rig rig(true);
        QVERIFY(rig.waitFor("menu"));
        rig.game->setSeed(quint32(seed));
        rig.game->playTier(tier);
        QCOMPARE(rig.game->state(), QString("intro"));
        rig.game->begin();
        QVERIFY2(rig.deal(), qPrintable(rig.game->error()));
        if (wrongBranch) {
            const int ws = rig.game->desk().pictures.first().workspace;
            for (const auto &w : rig.desktop.windows)
                if (w.pid == rig.desktop.own() && w.workspace == ws && w.group.isEmpty() && !w.floating) {
                    rig.desktop.insertBeside = rig.desktop.layout(ws).windows().first();
                    break;
                }
        }
        Player player(*rig.game, rig.desktop);
        const bool solved = player.solve(900);
        rig.desktop.insertBeside.clear();
        QVERIFY2(solved, qPrintable(QString("stuck: %1 | %2").arg(
                             rig.game->currentHints().isEmpty() ? QString("no hint") : rig.game->currentHints().first().text,
                             player.log.mid(player.log.size() - 16).join(", "))));
        QVERIFY(!rig.desktop.touchedForeign);
    }

    // ── Safety ─────────────────────────────────────────────────────────

    void foreignWindowsAreNeverTouchedEvenWithAPiecesTitle() {
        Rig rig(true);
        QVERIFY(rig.waitFor("menu"));
        // Another program opens a window with exactly the title of the next
        // piece, just before the piece itself.
        QString twin;
        connect(&rig.game->pieceModel(), &QAbstractItemModel::rowsInserted, &rig.desktop,
                [&](const QModelIndex &, int first, int) {
                    const QString title = rig.game->pieceModel().rows().at(first).title;
                    if (twin.isEmpty()) {
                        twin = rig.desktop.open(title, 4242, "impostor");
                        rig.desktop.untouchable.insert(twin);
                    }
                }, Qt::DirectConnection);
        rig.game->playLesson(8); // the scratchpad lesson: moves, a stray and a hidden piece
        rig.game->begin();
        // The twin now sits on the workspace being dealt; the game must
        // notice it isn't empty and look elsewhere, never touching it.
        Player player(*rig.game, rig.desktop);
        QElapsedTimer t;
        t.start();
        while (rig.game->state() != "playing" && t.elapsed() < 5000) {
            player.step();
            if (rig.game->state() == "error")
                break;
        }
        QVERIFY(!twin.isEmpty());
        QVERIFY(!rig.desktop.touchedForeign);
        for (const auto &row : rig.game->pieceModel().rows())
            QVERIFY(row.address != twin);
        for (const auto &d : rig.desktop.dispatched)
            QVERIFY2(!d.endsWith(twin), qPrintable(d));
        QCOMPARE(rig.desktop.find(twin)->pid, qint64(4242));
        // The game leaves the other window where it is (it says so in the
        // hints, see below); the player moves it off, as only they may.
        QCOMPARE(rig.desktop.find(twin)->workspace, rig.game->desk().pictures.first().workspace);
        rig.desktop.moveTo(twin, 9, false);
        QVERIFY(player.solve());
        QVERIFY(!rig.desktop.touchedForeign);
        // Every dispatch went to a window of Tessera's own.
        for (const auto &d : rig.desktop.dispatched) {
            const QString address = d.section(' ', -1);
            auto *w = rig.desktop.find(address);
            QVERIFY2(!w || w->pid == rig.desktop.own(), qPrintable(d));
        }
    }

    void otherAppsInAPictureAreReportedNotMoved() {
        Rig rig(true);
        QVERIFY(rig.waitFor("menu"));
        rig.game->playLesson(0);
        rig.game->begin();
        QVERIFY(rig.deal());
        const int ws = rig.game->desk().pictures.first().workspace;
        const QString terminal = rig.desktop.open("foot", 777, "foot");
        rig.desktop.untouchable.insert(terminal);
        Player p(*rig.game, rig.desktop);
        p.settle(80);
        QCOMPARE(rig.desktop.find(terminal)->workspace, ws);
        bool reported = false;
        for (const auto &h : rig.game->currentHints())
            reported = reported || h.text.contains("isn't Tessera's");
        rig.desktop.focus(rig.desktop.ownPieces().first());
        p.settle(60);
        for (const auto &h : rig.game->currentHints())
            reported = reported || h.text.contains("isn't Tessera's");
        QVERIFY(reported);
        QVERIFY(!rig.desktop.touchedForeign);
        QVERIFY(!rig.game->evaluation().complete());
    }

    void piecesOnlyGoToEmptyWorkspaces() {
        Rig rig(true);
        QVERIFY(rig.waitFor("menu"));
        // Everything but workspace 7 is busy.
        for (int ws = 2; ws <= 10; ++ws)
            if (ws != 7) {
                rig.desktop.activeWs = ws;
                rig.desktop.open(QString("app %1").arg(ws), 1000 + ws, "app", false);
            }
        rig.desktop.activeWs = 1;
        rig.game->playLesson(3); // needs two workspaces
        rig.game->begin();
        Player p(*rig.game, rig.desktop);
        p.settle(60);
        QCOMPARE(rig.game->state(), QString("waiting"));
        QVERIFY(rig.game->objective().contains("7"));
        QCOMPARE(rig.game->pieceModel().rowCount(), 0);
        // Freeing one more lets it start, on exactly the free ones.
        rig.desktop.close(rig.desktop.addressOf("app 9"));
        QVERIFY(rig.waitFor("dealing"));
        QCOMPARE(rig.game->workspaces(), (QList<int>{7, 9}));
    }

    // A second monitor's workspaces are 11 to 20 and so on; Omarchy's block
    // map says which monitor has which.
    void workspacesComeFromTheBoardsMonitorBlock() {
        QTemporaryDir dir;
        writeFile(dir.filePath("blocks"), "1\tHDMI-A-1\tSamsung\n0\tDP-1\tOther\n");
        qputenv("TESSERA_MONITOR_WORKSPACES", dir.filePath("blocks").toUtf8());
        Rig rig(true);
        QVERIFY(rig.waitFor("menu"));
        rig.desktop.find(rig.board)->workspace = 11;
        rig.desktop.activeWs = 11;
        rig.game->playLesson(3);
        rig.game->begin();
        QVERIFY(rig.waitFor("dealing"));
        QCOMPARE(rig.game->workspaces(), (QList<int>{12, 13}));
        QVERIFY(rig.game->objectiveChord().endsWith("+ 2"));
        qunsetenv("TESSERA_MONITOR_WORKSPACES");
    }

    void otherLayoutsAreSaidSo() {
        Rig rig(true);
        QVERIFY(rig.waitFor("menu"));
        rig.desktop.layoutName = "scrolling";
        rig.game->playLesson(0);
        rig.game->begin();
        Player p(*rig.game, rig.desktop);
        p.settle(40);
        p.press(rig.game->objectiveChord());
        p.settle(60);
        QCOMPARE(rig.game->state(), QString("dealing"));
        QVERIFY(rig.game->objective().contains("scrolling"));
        QCOMPARE(rig.game->pieceModel().rowCount(), 0);
    }

    // Space on a solved lesson's piece goes on to the next lesson, whose
    // pieces aren't dealt yet: the board comes into view, not an empty
    // workspace.
    void nextFromAPieceShowsTheBoard() {
        Rig rig(true);
        QVERIFY(rig.waitFor("menu"));
        rig.game->playLesson(0);
        rig.game->begin();
        Player player(*rig.game, rig.desktop);
        QVERIFY(player.solve());
        QCOMPARE(rig.game->state(), QString("solved"));
        QVERIFY(rig.desktop.active != rig.board);
        rig.game->showBoard();
        rig.game->primary();
        QCOMPARE(rig.desktop.active, Hypr::address(rig.board));
        QCOMPARE(rig.game->state(), QString("intro"));
        QVERIFY(!rig.desktop.touchedForeign);
    }

    void pilesAreOnlyMadeOnAFocusedPiece() {
        Rig rig(true);
        QVERIFY(rig.waitFor("menu"));
        rig.game->playLesson(5);
        rig.game->begin();
        QVERIFY2(rig.deal(), qPrintable(rig.game->error()));
        // The address form isn't trusted (if Hyprland ignored the window, the
        // player's focused window would be grouped), so it never runs.
        QCOMPARE(rig.desktop.dispatched.indexOf(QRegularExpression("^group [^-].*")), -1);
        QCOMPARE(rig.desktop.dispatched.indexOf(QRegularExpression("^intogroup [lrud] .*")), -1);
        // The focused-window form ran, and only on a piece.
        const int groupAt = rig.desktop.dispatched.indexOf(QRegularExpression("^group-focused .*"));
        QVERIFY2(groupAt >= 0, qPrintable(rig.desktop.dispatched.join(", ")));
        QVERIFY(rig.game->pieceModel().findAddress(rig.desktop.dispatched.at(groupAt).section(' ', -1)));
        // The pile really is there, and holds both pieces.
        int grouped = 0;
        for (const auto &w : rig.desktop.windows)
            grouped += w.group.isEmpty() ? 0 : 1;
        QCOMPARE(grouped, 2);
    }

    void aSetupStepThatFailsIsReported() {
        Rig rig(true);
        QVERIFY(rig.waitFor("menu"));
        rig.desktop.autoGroup = false;
        // Nothing to move it into either: the pile can't be made.
        rig.game->playLesson(5);
        rig.game->begin();
        Player p(*rig.game, rig.desktop);
        QElapsedTimer t;
        t.start();
        while (rig.game->state() != "error" && rig.game->state() != "playing" && t.elapsed() < 4000) {
            if (rig.game->state() == "dealing" && !rig.game->objectiveChord().isEmpty())
                p.press(rig.game->objectiveChord());
            p.settle(20);
        }
        // Either the fallback move into the pile worked or it says why.
        QVERIFY2(rig.game->state() == "error" || rig.game->state() == "playing",
                 qPrintable(rig.game->state() + " " + rig.desktop.dispatched.join(", ")));
        if (rig.game->state() == "error")
            QVERIFY(rig.game->error().contains("pile"));
        else {
            int grouped = 0;
            for (const auto &w : rig.desktop.windows)
                grouped += w.group.isEmpty() ? 0 : 1;
            QVERIFY2(grouped == 2, qPrintable(rig.desktop.dispatched.join(", ")));
        }
    }

    // ── Quitting ───────────────────────────────────────────────────────

    void quittingClosesEveryPiece() {
        Rig rig(true);
        QVERIFY(rig.waitFor("menu"));
        rig.game->playTier(0);
        rig.game->begin();
        QVERIFY(rig.deal());
        QVERIFY(!rig.desktop.ownPieces().isEmpty());
        rig.game->shutdown();
        Player(*rig.game, rig.desktop).settle(30);
        QCOMPARE(rig.game->pieceModel().rowCount(), 0);
        QVERIFY(rig.desktop.ownPieces().isEmpty());
        QVERIFY(QFile::exists(rig.path()));
    }

    void aPieceThatClosesStopsThePuzzle() {
        Rig rig(true);
        QVERIFY(rig.waitFor("menu"));
        rig.game->playLesson(0);
        rig.game->begin();
        QVERIFY(rig.deal());
        QVERIFY(!rig.game->requestPieceClose(rig.game->pieceModel().rows().first().id));
        rig.desktop.close(rig.desktop.ownPieces().first());
        QVERIFY(rig.waitFor("error"));
        QCOMPARE(rig.game->pieceModel().rowCount(), 0);
        QVERIFY(rig.desktop.ownPieces().isEmpty());
    }

    // ── Focus ──────────────────────────────────────────────────────────

    void focusSurvivesTheEmptyEventAfterAGroupSwitch() {
        Rig rig(true);
        QVERIFY(rig.waitFor("menu"));
        rig.game->playLesson(5);
        rig.game->begin();
        QVERIFY(rig.deal());
        QString pileMember;
        for (const auto &w : rig.desktop.windows)
            if (!w.group.isEmpty())
                pileMember = w.address;
        rig.desktop.focus(pileMember);
        rig.desktop.pressGroupNext();
        Player p(*rig.game, rig.desktop);
        p.settle(60);
        QVERIFY(!rig.game->focusedPiece().isEmpty());
        QCOMPARE(rig.game->pieceModel().findAddress(rig.desktop.active)->id, rig.game->focusedPiece());
        // A title change re-sends the same window: still the same focus.
        rig.desktop.events.feed("activewindowv2>>" + rig.desktop.active);
        p.settle(30);
        QCOMPARE(rig.game->pieceModel().findAddress(rig.desktop.active)->id, rig.game->focusedPiece());
    }

    // ── Timer, stars, progression ──────────────────────────────────────

    void bestTimesAreKept() {
        qint64 now = 1000;
        QString path;
        {
            Rig rig(true);
            path = rig.path();
            rig.game->setClock([&] { return now; });
            rig.game->setSeed(7);
            QVERIFY(rig.waitFor("menu"));
            rig.game->playTier(0);
            rig.game->begin();
            QVERIFY(rig.deal());
            now += 83400;
            QVERIFY(Player(*rig.game, rig.desktop).solve());
            QCOMPARE(rig.game->elapsed(), qint64(83400));
            QVERIFY(rig.game->newBest());
            QCOMPARE(rig.game->best(), qint64(83400));

            // Slower the second time: the best stays.
            rig.game->restart();
            QVERIFY(rig.deal());
            now += 120000;
            QVERIFY(Player(*rig.game, rig.desktop).solve());
            QVERIFY(!rig.game->newBest());
            QCOMPARE(rig.game->best(), qint64(83400));
            rig.game->shutdown();
            Progress saved(path);
            QVERIFY(saved.load());
            QCOMPARE(saved.best("easy"), qint64(83400));
            // Saved atomically: no temporary file left beside it, only the
            // stats that live next to it.
            const auto leftovers = QDir(QFileInfo(path).absolutePath()).entryList(QDir::Files);
            QCOMPARE(leftovers, (QStringList{"progress.json", "stats.json"}));
        }
    }

    void lessonsOpenInOrderAndStarsCountHints() {
        Rig rig(false, false);
        QVERIFY(rig.waitFor("menu"));
        QVERIFY(rig.game->lessons().at(0).toMap().value("unlocked").toBool());
        QVERIFY(!rig.game->lessons().at(1).toMap().value("unlocked").toBool());
        QVERIFY(!rig.game->puzzlesUnlocked());
        rig.game->playLesson(1);
        QCOMPARE(rig.game->state(), QString("menu"));
        rig.game->playTier(0);
        QCOMPARE(rig.game->state(), QString("menu"));

        // Solved with no hint shown: three stars, and the next one opens.
        rig.game->playLesson(0);
        rig.game->begin();
        QVERIFY(rig.deal());
        QVERIFY(rig.game->hints().isEmpty()); // the answer is held back at first
        QVERIFY(Player(*rig.game, rig.desktop).solve());
        QCOMPARE(rig.game->stars(), 3);
        QVERIFY(rig.game->lessons().at(1).toMap().value("unlocked").toBool());

        // Asking for every hint: one star, and the keys were shown.
        rig.game->playLesson(1);
        rig.game->begin();
        QVERIFY(rig.deal());
        rig.game->hint();
        rig.game->hint();
        QVERIFY(!rig.game->hints().isEmpty());
        QVERIFY(!rig.game->hints().first().toMap().value("chord").toString().isEmpty());
        rig.game->hint();
        QCOMPARE(rig.game->hintTier(), 3);
        QVERIFY(Player(*rig.game, rig.desktop).solve());
        QCOMPARE(rig.game->stars(), 1);
        // A worse replay never takes stars away.
        QCOMPARE(rig.game->progress().stars("swap"), 3);
    }

    void puzzlesOpenAfterTheLessons() {
        Rig rig(true);
        QVERIFY(rig.waitFor("menu"));
        QVERIFY(rig.game->puzzlesUnlocked());
        for (int i = 0; i < Levels::tiers().size(); ++i)
            QVERIFY(rig.game->tiers().at(i).toMap().value("unlocked").toBool());
        const auto level = Levels::puzzle(2, 3);
        QCOMPARE(level.pictures.size(), 3);
        bool pile = false, scratch = false, away = false, floating = false;
        for (const auto &p : level.pictures)
            for (const auto &s : p.pieces) {
                pile = pile || s.start == Levels::Start::Pile;
                scratch = scratch || s.start == Levels::Start::Scratchpad;
                away = away || s.start == Levels::Start::Away;
                floating = floating || s.start == Levels::Start::Floating;
            }
        QVERIFY(pile && scratch && away && floating);
        QCOMPARE(level.strays.size(), 1);
    }

    void withoutHyprlandItSaysSo() {
        Rig rig;
        rig.desktop.events.setConnected(false);
        QCOMPARE(rig.game->state(), QString("error"));
        QVERIFY(!rig.game->error().isEmpty());
        rig.desktop.events.setConnected(true);
        QCOMPARE(rig.game->state(), QString("menu"));
    }

    // ── Theme and language ─────────────────────────────────────────────

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        writeFile(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding() {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        writeFile(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        writeFile(response, "{\"int\":0}");
        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        writeFile(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFollowsTheLanguageFile() {
        QTemporaryDir dir;
        writeFile(dir.path() + "/language", "pt-BR\n");
        writeFile(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        writeFile(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms() {
        QTemporaryDir dir;
        writeFile(dir.path() + "/language", "ru\n");
        writeFile(dir.path() + "/ru.json",
                  R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
    }

    // Every string the app shows is in both catalogs, so nothing turns up in
    // English in Danish or German.
    void catalogsAreComplete() {
        QSet<QString> sources;
        const QRegularExpression call(callPattern());
        const QRegularExpression split(QStringLiteral("\"\\s*\\n\\s*\""));
        QDirIterator it(QStringLiteral(SRCDIR), {"*.cpp", "*.qml"});
        while (it.hasNext()) {
            QFile f(it.next());
            QVERIFY(f.open(QIODevice::ReadOnly));
            QString text = QString::fromUtf8(f.readAll());
            text.replace(split, QString());
            auto m = call.globalMatch(text);
            while (m.hasNext()) {
                QString s = m.next().captured(1);
                s.replace("\\\"", "\"");
                if (!s.isEmpty() && s != "%1 %2" && !s.startsWith("Tessera · "))
                    sources.insert(s);
            }
        }
        QVERIFY(sources.size() > 100);
        for (const char *code : {"da", "de"}) {
            QFile f(QString(":/i18n/%1.json").arg(code));
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QJsonObject catalog = QJsonDocument::fromJson(f.readAll()).object();
            QStringList missing;
            for (const auto &s : sources)
                if (!catalog.contains(s))
                    missing << s;
            missing.sort();
            QVERIFY2(missing.isEmpty(), qPrintable(QString("%1 is missing %2 strings: %3")
                                                       .arg(code).arg(missing.size()).arg(missing.join(" | "))));
        }
    }

    // ── Pictures: shape, crop, collage ─────────────────────────────────

    void monitorsGiveTheirWorkArea() {
        const QJsonObject plain{{"width", 3840}, {"height", 2160}, {"scale", 2}, {"transform", 0},
                                {"reserved", QJsonArray{0, 26, 0, 0}}};
        QCOMPARE(Collage::area(plain), QSizeF(1920, 1054));
        // Turned a quarter: the panel's height is the width.
        const QJsonObject turned{{"width", 2560}, {"height", 1440}, {"scale", 1}, {"transform", 1},
                                 {"reserved", QJsonArray{0, 30, 0, 0}}};
        QCOMPARE(Collage::area(turned), QSizeF(1440, 2530));
        QVERIFY(Collage::area(QJsonObject{}).isEmpty());
        QCOMPARE(Collage::renderSize(QSizeF(1920, 1054), 2), QSize(2560, 1405));
        QCOMPARE(Collage::renderSize(QSizeF(1280, 700), 1), QSize(1280, 700));
    }

    void coverCropKeepsTheMiddle() {
        QCOMPARE(Collage::coverCrop(QSizeF(2000, 1000), QSizeF(1, 1)), QRectF(500, 0, 1000, 1000));
        QCOMPARE(Collage::coverCrop(QSizeF(1000, 2000), QSizeF(2, 1)), QRectF(0, 750, 1000, 500));
        QCOMPARE(Collage::coverCrop(QSizeF(1600, 900), QSizeF(16, 9)), QRectF(0, 0, 1600, 900));
        QCOMPARE(Collage::kept(QSizeF(1600, 900), QSizeF(1600, 900)), 1.0);
        QVERIFY(qAbs(Collage::kept(QSizeF(1600, 900), QSizeF(1920, 1200)) - 0.9) < 1e-9);
        // Rendered alone, a source fills its whole picture.
        QImage red(400, 100, QImage::Format_RGB32);
        red.fill(Qt::red);
        const Collage::Layout whole{{Collage::Cell{0, QRectF(0, 0, 200, 200)}}, 0};
        const QImage out = Collage::render({red}, whole, QSize(200, 200));
        QCOMPARE(out.size(), QSize(200, 200));
        QCOMPARE(out.pixelColor(0, 0), QColor(Qt::red));
        QCOMPARE(out.pixelColor(199, 199), QColor(Qt::red));
    }

    void collagesFitEveryMonitorShape_data() {
        QTest::addColumn<QSizeF>("target");
        QTest::addColumn<bool>("collage");
        QTest::newRow("16:9") << QSizeF(1920, 1054) << false;
        QTest::newRow("16:10") << QSizeF(1920, 1174) << false;
        QTest::newRow("21:9") << QSizeF(2560, 1054) << true;
        QTest::newRow("32:9") << QSizeF(5120, 1414) << true;
        QTest::newRow("portrait") << QSizeF(1080, 1894) << true;
    }

    void collagesFitEveryMonitorShape() {
        QFETCH(QSizeF, target);
        QFETCH(bool, collage);
        // The built-in works, a portrait and a square one.
        const QList<QSizeF> pool{{1600, 900}, {1600, 900}, {1200, 1600}, {1600, 900}, {1000, 1000}, {1600, 900}};
        const double gutter = Collage::gutterFor(target);
        const Collage::Plan plan = Collage::plan(pool, target, gutter);
        QVERIFY(plan.layout.valid());
        QCOMPARE(plan.sources.first(), 0);
        QCOMPARE(plan.sources.size() > 1, collage);
        QVERIFY(plan.sources.size() <= 4);
        QCOMPARE(plan.layout.cells.size(), plan.sources.size());
        // The cells lie inside the picture, don't overlap, and fill it but
        // for the gutters.
        const QRectF frame(QPointF(0, 0), target);
        double area = 0;
        for (int i = 0; i < plan.layout.cells.size(); ++i) {
            const QRectF a = plan.layout.cells.at(i).rect;
            QVERIFY2(frame.adjusted(-0.5, -0.5, 0.5, 0.5).contains(a), qPrintable(QString("cell %1 outside").arg(i)));
            area += a.width() * a.height();
            for (int j = i + 1; j < plan.layout.cells.size(); ++j) {
                const QRectF o = a.intersected(plan.layout.cells.at(j).rect);
                QVERIFY(o.width() * o.height() < 1);
            }
        }
        QVERIFY(area >= frame.width() * frame.height() * 0.95);
        // No cell crops away more than a quarter of its image.
        QVERIFY2(plan.layout.loss <= 0.25, qPrintable(QString::number(plan.layout.loss)));
        // Painted, the gutter shows between two cells.
        if (collage) {
            QList<QImage> images;
            for (int k : plan.sources) {
                QImage img(pool.at(k).toSize(), QImage::Format_RGB32);
                img.fill(Qt::white);
                images << img;
            }
            const QSize size = target.toSize() / 4;
            Collage::Layout scaled = plan.layout;
            for (auto &c : scaled.cells)
                c.rect = QRectF(c.rect.x() / 4, c.rect.y() / 4, c.rect.width() / 4, c.rect.height() / 4);
            const QImage out = Collage::render(images, scaled, size);
            QCOMPARE(out.size(), size);
            const QRectF first = scaled.cells.at(0).rect;
            const bool beside = first.right() < size.width() - 2;
            const QPoint gap = beside ? QPoint(int(first.right()) + 1, int(first.center().y()))
                                      : QPoint(int(first.center().x()), int(first.bottom()) + 1);
            QCOMPARE(out.pixelColor(gap), QColor("#0e0e10"));
            QCOMPARE(out.pixelColor(first.center().toPoint()), QColor(Qt::white));
        }
    }

    void galleryMakesPicturesToTheScreensShape() {
        QTemporaryDir dir;
        Gallery::Options o;
        o.settingsPath = dir.filePath("settings.json");
        o.cacheDir = dir.filePath("cache");
        Gallery gallery(o);
        Gallery::Request r;
        r.musts = {"wave", ""};
        r.strays = 1;
        r.area = QSizeF(2560, 1054);
        r.pixels = QSize(1280, 527);
        r.lesson = true;
        const Gallery::Result result = prepareNow(gallery, r);
        QCOMPARE(result.pictures.size(), 2);
        QCOMPARE(result.strays.size(), 1);
        // A lesson's own work comes first, in a collage this wide.
        QCOMPARE(result.pictures.first().sources.first(), QString("builtin:wave"));
        QVERIFY(result.pictures.first().collage);
        QCOMPARE(result.pictures.first().credits.size(), result.pictures.first().sources.size());
        QCOMPARE(result.pictures.first().size, QSize(1280, 527));
        QCOMPARE(gallery.image(result.pictures.first().key).size(), QSize(1280, 527));
        // Two pictures never share a work.
        for (const QString &s : result.pictures.at(1).sources)
            QVERIFY(!result.pictures.first().sources.contains(s));
        // Rendered pictures are cached, written whole.
        QVERIFY(QFileInfo::exists(gallery.cache().picturePath(result.pictures.first().key)));
    }

    // Mixed means new pictures: with museum art cached, puzzles and lessons
    // use it, and the built-in works only fill in.
    void mixedPrefersMuseumArtOverTheBuiltIns() {
        QTemporaryDir dir;
        {
            ArtCache cache(dir.filePath("cache"));
            for (int k = 1; k <= 6; ++k) {
                QImage img(1600, 900, QImage::Format_RGB32);
                img.fill(QColor::fromHsv(k * 50, 200, 200));
                Source src;
                src.key = QString("aic:%1").arg(k);
                src.name = src.credit.title = QString("Work %1").arg(k);
                QVERIFY(cache.storeArt(src, img));
            }
        }
        Gallery::Options o;
        o.settingsPath = dir.filePath("settings.json");
        o.cacheDir = dir.filePath("cache");
        Gallery gallery(o);
        QCOMPARE(gallery.source(), QString("mixed"));
        for (bool lesson : {false, true}) {
            Gallery::Request r;
            r.musts = lesson ? QStringList{"wave"} : QStringList{"", ""};
            r.lesson = lesson;
            r.area = QSizeF(1920, 1054);
            const auto result = prepareNow(gallery, r);
            QVERIFY(!result.pictures.isEmpty());
            for (const auto &p : result.pictures)
                for (const QString &src : p.sources)
                    QVERIFY2(src.startsWith("aic:"), qPrintable(src));
        }
    }

    void offlineFallsBackToTheBuiltInPicturesAndSaysSo() {
        QTemporaryDir dir;
        Gallery::Options o;
        o.settingsPath = dir.filePath("settings.json");
        o.cacheDir = dir.filePath("cache");
        o.network = false;
        {
            Gallery gallery(o);
            QVERIFY(gallery.online()); // on unless turned off
            QCOMPARE(gallery.source(), QString("mixed"));
            gallery.setSource("online");
            Gallery::Request r;
            r.musts = {"", ""};
            r.area = QSizeF(1920, 1054);
            const auto result = prepareNow(gallery, r);
            QCOMPARE(result.pictures.size(), 2);
            QCOMPARE(result.note, QString("Tessera is offline, so this puzzle uses the built-in pictures."));
            for (const auto &p : result.pictures)
                for (const QString &s : p.sources)
                    QVERIFY(s.startsWith("builtin:"));
            gallery.setOnline(false);
        }
        // Both settings are remembered.
        Gallery again(o);
        QCOMPARE(again.source(), QString("online"));
        QVERIFY(!again.online());
        Gallery::Request r;
        r.musts = {""};
        QCOMPARE(prepareNow(again, r).note, QString("Online pictures are turned off, so this puzzle uses the built-in ones."));
    }

    void aCollagePuzzleCanBeSolvedOnAWideMonitor() {
        Rig rig(true);
        rig.desktop.setMonitor(2560, 1080);
        QVERIFY(rig.waitFor("menu"));
        rig.game->setSeed(3);
        rig.game->playTier(1);
        rig.game->begin();
        QVERIFY2(rig.deal(), qPrintable(rig.game->error()));
        QCOMPARE(rig.game->screen(), QSizeF(2560, 1054));
        for (const auto &p : rig.game->desk().pictures) {
            const auto *prep = &rig.game->prepared().find(p.key).value();
            QVERIFY(prep->collage);
            QVERIFY(qAbs(double(prep->size.width()) / prep->size.height() - 2560.0 / 1054) < 0.01);
        }
        QVERIFY(Player(*rig.game, rig.desktop).solve(900));
    }

    void everyLessonCanBeSolvedOnAPortraitMonitor() {
        for (int i = 0; i < Levels::lessons().size(); ++i) {
            Rig rig(true);
            rig.desktop.setMonitor(1080, 1920);
            QVERIFY(rig.waitFor("menu"));
            rig.game->playLesson(i);
            rig.game->begin();
            Player player(*rig.game, rig.desktop);
            QVERIFY2(player.solve(), qPrintable(QString("lesson %1 on portrait: %2").arg(Levels::lessons().at(i).id,
                                                                                         player.log.join(", ").right(200))));
        }
    }

    // ── Pictures: the player's folder ──────────────────────────────────

    void theFolderIsReadNeverWritten() {
        QTemporaryDir dir;
        const QString folder = dir.filePath("Pictures");
        QDir().mkpath(folder + "/inner");
        auto picture = [&](const QString &name, QSize size, const char *format) {
            QImage img(size, QImage::Format_RGB32);
            img.fill(QColor("#3070a0"));
            QVERIFY(img.save(folder + "/" + name, format));
        };
        picture("a-beach.jpg", {1600, 1000}, "JPEG");
        picture("b-tall.png", {900, 1300}, "PNG");
        picture("c-wide.webp", {2400, 900}, "WEBP");
        picture("inner/deeper.jpg", {800, 600}, "JPEG"); // not read: one level only
        picture(".hidden.jpg", {800, 600}, "JPEG");
        writeFile(folder + "/notes.txt", "not a picture");
        writeFile(folder + "/broken.jpg", "not really a jpeg");
        QHash<QString, QPair<QByteArray, QDateTime>> before;
        QDirIterator it(folder, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadOnly));
            before.insert(path, {QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256), QFileInfo(path).lastModified()});
        }

        const Folder::Scan scan = Folder::scan(folder);
        QCOMPARE(scan.sources.size(), 3);
        QCOMPARE(scan.skipped, 1);
        QCOMPARE(scan.sources.at(0).size, QSize(1600, 1000));
        QCOMPARE(scan.sources.at(2).size, QSize(2400, 900));
        for (const auto &s : scan.sources)
            QVERIFY(s.credit.local);
        QVERIFY(Folder::scan(folder, 2).truncated);
        QCOMPARE(Folder::scan(dir.filePath("gone")).error, QString("missing"));

        // Chosen through the (fake) portal, remembered, and made into a puzzle.
        Gallery::Options o;
        o.settingsPath = dir.filePath("state/settings.json");
        o.cacheDir = dir.filePath("cache");
        Gallery gallery(o);
        FakePicker picker;
        picker.answer = folder;
        gallery.setPicker(&picker);
        gallery.chooseFolder();
        gallery.rescanNow();
        QCOMPARE(gallery.folder(), folder);
        QCOMPARE(gallery.source(), QString("mixed"));
        gallery.setSource("folder");
        QCOMPARE(gallery.folderCount(), 3);
        Gallery::Request r;
        r.musts = {"", ""};
        r.area = QSizeF(1920, 1054);
        const auto result = prepareNow(gallery, r);
        QCOMPARE(result.pictures.size(), 2);
        for (const auto &p : result.pictures) {
            for (const QString &s : p.sources)
                QVERIFY(s.startsWith("folder:"));
            // Pictures made of the player's files stay in memory only.
            QVERIFY(!QFileInfo::exists(gallery.cache().picturePath(p.key)));
        }
        QVERIFY(Settings(o.settingsPath).load());
        Settings saved(o.settingsPath);
        saved.load();
        QCOMPARE(saved.folder, folder);

        // Nothing in the folder changed, and nothing was added to it.
        QHash<QString, QPair<QByteArray, QDateTime>> after;
        QDirIterator again(folder, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (again.hasNext()) {
            const QString path = again.next();
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadOnly));
            after.insert(path, {QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256), QFileInfo(path).lastModified()});
        }
        QCOMPARE(after, before);
    }

    // ── Pictures: the museums ──────────────────────────────────────────

    void museumJsonOnlyYieldsPublicDomainWorks() {
        const auto aic = Museum::parseAicSearch(fixture("aic-search.json"));
        QStringList keys;
        for (const auto &c : aic)
            keys << c.source.key;
        // Not the one under copyright, not "true" as a string, not the image
        // id with a path in it, not the untitled one, not the string id.
        QCOMPARE(keys, (QStringList{"aic:14655", "aic:80607", "aic:61129"}));
        const auto &first = aic.first();
        QCOMPARE(first.image, QUrl("https://www.artic.edu/iiif/2/3a608f55-d76e-fa96-d0b1-0789fbc48f1e/full/1686,/0/default.jpg"));
        QCOMPARE(first.source.credit.title, QString("Two Sisters (On the Terrace)"));
        QCOMPARE(first.source.credit.artist, QString("Pierre-Auguste Renoir"));
        QCOMPARE(first.source.credit.date, QString("1881"));
        QCOMPARE(first.source.credit.url, QString("https://www.artic.edu/artworks/14655"));
        QCOMPARE(first.source.credit.license, QString("Public domain (CC0)"));
        QCOMPARE(first.source.credit.origin, QString("Art Institute of Chicago"));
        QCOMPARE(first.hint, QSize(19848, 24629));
        // Control and direction characters go; markup stays inert text.
        QCOMPARE(aic.last().source.credit.title, QString("<b>Bold</b> title with controls"));
        QCOMPARE(aic.last().source.credit.artist, QString("Henri de Toulouse-Lautrec"));
        for (const auto &c : aic)
            QVERIFY(Museum::allowedUrl(c.image));

        QCOMPARE(Museum::parseMetSearch(fixture("met-search.json")), (QList<int>{340468, 10181, 359362, 335112}));
        const auto met = Museum::parseMetObject(fixture("met-object.json"));
        QVERIFY(met.has_value());
        QCOMPARE(met->source.key, QString("met:10181"));
        QCOMPARE(met->image, QUrl("https://images.metmuseum.org/CRDImages/ad/original/ap29.35.jpg"));
        QCOMPARE(met->source.credit.artist, QString("Ralph Albert Blakelock"));
        QCOMPARE(met->source.credit.date, QString("1885–95"));
        QCOMPARE(met->source.credit.url, QString("https://www.metmuseum.org/art/collection/search/10181"));
        QVERIFY(!Museum::parseMetObject(fixture("met-object-copyright.json")).has_value());
        QVERIFY(!Museum::parseMetObject(fixture("met-object-elsewhere.json")).has_value());
        QVERIFY(Museum::parseAicSearch("<html>Just a moment...</html>").isEmpty());
        QVERIFY(Museum::parseAicSearch(QByteArray(Museum::MaxJsonBytes + 1, ' ')).isEmpty());

        QVERIFY(Museum::allowedUrl(QUrl("https://api.artic.edu/api/v1/artworks")));
        QVERIFY(!Museum::allowedUrl(QUrl("http://api.artic.edu/api/v1/artworks")));
        QVERIFY(!Museum::allowedUrl(QUrl("https://api.artic.edu.evil.example/")));
        QVERIFY(!Museum::allowedUrl(QUrl("https://user@api.artic.edu/")));
        QVERIFY(!Museum::allowedUrl(QUrl("https://api.artic.edu:8443/")));
        // The User-Agent names the app and nothing else.
        QVERIFY(QRegularExpression("^Tessera/[0-9.]+ \\(jigsaw puzzle\\)$").match(QString::fromUtf8(Museum::userAgent())).hasMatch());
    }

    void downloadedImagesAreCheckedBeforeUse() {
        QString error;
        QVERIFY(Museum::decodeImage("<html></html>", &error).isNull());
        QVERIFY(!error.isEmpty());
        QVERIFY(Museum::decodeImage(jpeg(QSize(300, 300)), &error).isNull()); // too small to play with
        QVERIFY(Museum::decodeImage(jpeg(QSize(600, 800)), &error).isNull());
        const QImage ok = Museum::decodeImage(jpeg(QSize(3000, 1000)), &error, 1200);
        QCOMPARE(ok.size(), QSize(1200, 400)); // scaled down as it decodes
        QVERIFY(Museum::decodeImage(QByteArray(Museum::MaxImageBytes + 1, 'x'), &error).isNull());
    }

    void theMuseumClientFetchesOnlyWhatTheRulesAllow() {
        QTemporaryDir dir;
        ArtCache cache(dir.filePath("cache"));
        MuseumClient client(&cache);
        FakeNetwork net;
        const QByteArray image = jpeg(QSize(1400, 1000));
        net.respond = [&](const QNetworkRequest &r) -> FakeNetwork::Answer {
            const QString host = r.url().host();
            if (host == "api.artic.edu")
                return {200, "application/json", fixture("aic-search.json")};
            if (host == "www.artic.edu")
                return {200, "image/jpeg", image};
            if (r.url().path().contains("/search"))
                return {200, "application/json", fixture("met-search.json")};
            if (host == "collectionapi.metmuseum.org")
                return {200, "application/json", fixture("met-object.json")};
            // The Met's image host sometimes answers with a bot-check page.
            return {200, "text/html", "<html>blocked</html>"};
        };
        client.setNetwork(&net);
        QSignalSpy done(&client, &MuseumClient::finished);
        client.fetch(3);
        QVERIFY(done.wait(5000));
        QCOMPARE(done.first().at(0).toInt(), 3);
        QCOMPARE(cache.online().size(), 3);
        for (const auto &s : cache.online()) {
            QVERIFY(s.key.startsWith("aic:"));
            QCOMPARE(s.credit.license, QString("Public domain (CC0)"));
            QCOMPARE(s.size, QSize(1400, 1000));
        }
        for (const auto &r : net.requests) {
            QVERIFY(Museum::allowedUrl(r.url()));
            QCOMPARE(r.header(QNetworkRequest::UserAgentHeader).toByteArray(), Museum::userAgent());
            QVERIFY(r.transferTimeout() > 0);
        }
        // The index came back whole, and a new cache reads it.
        ArtCache reread(dir.filePath("cache"));
        QVERIFY(reread.load());
        QCOMPARE(reread.online().size(), 3);
    }

    void theCacheKeepsUnderItsCapAndWritesWholeFiles() {
        QTemporaryDir dir;
        ArtCache cache(dir.filePath("cache"), 1);
        // Noise doesn't compress, so each image is a known size.
        auto noise = [](int seed) {
            QImage img(300, 300, QImage::Format_RGB32);
            QRandomGenerator random{quint32(seed)};
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    img.setPixel(x, y, random.generate());
            return img;
        };
        cache.setCap(1ll << 40);
        Source s;
        s.credit.title = "A";
        s.name = "A";
        s.key = "aic:1";
        QVERIFY(cache.storeArt(s, noise(1)));
        const qint64 one = cache.size();
        QVERIFY(one > 20000);
        cache.setCap(one * 3 + one / 2);
        writeFile(dir.filePath("cache/art/someone-elses.txt"), "kept");
        for (int k = 2; k <= 6; ++k) {
            s.key = QString("aic:%1").arg(k);
            cache.markUsed("aic:1", QDateTime::currentMSecsSinceEpoch() + 100000); // the first stays in use
            QVERIFY(cache.storeArt(s, noise(k)));
        }
        QVERIFY(cache.size() <= cache.cap());
        QVERIFY(cache.has("aic:1"));   // used most recently
        QVERIFY(!cache.has("aic:2"));  // the oldest went first
        QVERIFY(cache.has("aic:6"));
        QVERIFY(QFileInfo::exists(dir.filePath("cache/art/someone-elses.txt")));
        // Index and files agree, and no temporary file is left over.
        for (const auto &src : cache.online())
            QVERIFY(QFileInfo::exists(src.path));
        const QStringList files = QDir(dir.filePath("cache/art")).entryList(QDir::Files);
        for (const QString &f : files)
            QVERIFY2(f.endsWith(".jpg") || f == "someone-elses.txt", qPrintable(f));
        QVERIFY(!ArtCache::safeKey("../x"));
        QVERIFY(!ArtCache::safeKey("aic:1/../../x"));
        QVERIFY(ArtCache::writeAtomically(dir.filePath("cache/t.json"), "{}"));
        QCOMPARE(QDir(dir.filePath("cache")).entryList(QDir::Files), QStringList({"credits.json", "t.json"}));
    }

    // ── Moves: detection ───────────────────────────────────────────────

    void movesAreReadFromEvents() {
        // Two tiles side by side on workspace 2, and a pile on workspace 3.
        auto snapshot = [](const QString &focused, int ws) {
            QHash<QString, Puzzle::Window> w;
            w.insert("a", window("aa", 2, QRectF(10, 36, 945, 1034)));
            w.insert("b", window("bb", 2, QRectF(965, 36, 945, 1034)));
            w.insert("c", window("cc", 3, QRectF(10, 36, 1900, 1034), {"cc", "dd"}));
            w.insert("d", window("dd", 3, QRectF(10, 36, 1900, 1034), {"cc", "dd"}));
            w["d"].hidden = true;
            return snap(w, focused, ws);
        };
        Commands::Detector d;
        qint64 t = 1000;
        d.reset(snapshot("a", 2), t);
        auto feed = [&](const QString &line) {
            const auto e = Hypr::parseLine(line.toUtf8());
            d.event(e.name, e.data, t);
        };
        auto moves = [&](const Commands::Snapshot &s) {
            t += 300;
            QStringList out;
            for (const auto &m : d.snapshot(s, t))
                out << m.action;
            return out;
        };
        QCOMPARE(moves(snapshot("a", 2)), QStringList());
        feed("activewindowv2>>bb");
        QCOMPARE(moves(snapshot("b", 2)), QStringList{"Focus on right window"});
        // The same window again (its title changed) is no move.
        feed("activewindowv2>>bb");
        QCOMPARE(moves(snapshot("b", 2)), QStringList());
        // A switch brings its own focus along: one move.
        feed("workspacev2>>3,3");
        feed("activewindowv2>>cc");
        QCOMPARE(moves(snapshot("c", 3)), QStringList{"Switch to workspace 3"});
        // Inside a group: the next tab, then the empty event that follows.
        t += 200;
        feed("activewindowv2>>dd");
        feed("activewindowv2>>");
        QCOMPARE(moves(snapshot("d", 3)), QStringList{"Next window in group"});
        // Carried along to another workspace: one move, not three.
        feed("movewindowv2>>dd,2,2");
        feed("workspacev2>>2,2");
        feed("activewindowv2>>dd");
        QCOMPARE(moves(snapshot("d", 2)), QStringList{"Move window to workspace 2"});
        t += 200;
        feed("movewindowv2>>aa,-98,special:scratchpad");
        feed("changefloatingmode>>bb,1");
        feed("togglegroup>>1,bb");
        feed("moveoutofgroup>>cc");
        QCOMPARE(moves(snapshot("d", 2)), (QStringList{"Move window to scratchpad", "Toggle window floating/tiling",
                                                        "Toggle window grouping", "Move active window out of group"}));
        // Hyprland names the scratchpad toggle twice; it counts once.
        t += 200;
        feed("activespecial>>special:scratchpad,HDMI-A-1");
        feed("activespecialv2>>-98,special:scratchpad,HDMI-A-1");
        QCOMPARE(moves(snapshot("d", 2)), QStringList{"Toggle scratchpad"});
        // Windows that aren't Tessera's are nobody's business.
        feed("changefloatingmode>>ffff,1");
        QCOMPARE(moves(snapshot("d", 2)), QStringList());
        // While the game acts, nothing counts.
        d.suppress(t + 1000);
        feed("changefloatingmode>>aa,0");
        QCOMPARE(moves(snapshot("d", 2)), QStringList());
    }

    void movesAreReadFromGeometry() {
        auto two = [](QRectF a, QRectF b, QPointF cursor = {500, 500}) {
            QHash<QString, Puzzle::Window> w;
            w.insert("a", window("aa", 2, a));
            w.insert("b", window("bb", 2, b));
            auto s = snap(w, "a", 2);
            s.cursor = cursor;
            return s;
        };
        const QRectF left(10, 36, 945, 1034), right(965, 36, 945, 1034);
        const QRectF top(10, 36, 1900, 512), bottom(10, 558, 1900, 512);
        Commands::Detector d;
        qint64 t = 0;
        d.reset(two(left, right), t);
        auto next = [&](const Commands::Snapshot &s) {
            t += 300;
            const auto moves = d.snapshot(s, t);
            return moves.isEmpty() ? QString() : moves.size() > 1 ? QString("several") : moves.first().action;
        };
        QCOMPARE(next(two(right, left)), QString("Swap window to the right"));
        QCOMPARE(next(two(top, bottom)), QString("Toggle window split"));
        QCOMPARE(next(two(QRectF(10, 36, 1900, 612), QRectF(10, 658, 1900, 412))), QString("Expand window down"));
        QCOMPARE(next(two(QRectF(10, 36, 1900, 587), QRectF(10, 633, 1900, 437))), QString("Shrink window up a little"));
        QCOMPARE(next(two(QRectF(10, 36, 1900, 287), QRectF(10, 333, 1900, 737))), QString("Shrink window up a lot"));
        QCOMPARE(next(two(QRectF(10, 36, 1900, 287), QRectF(10, 333, 1900, 737))), QString());
        QCOMPARE(next(two(left, right)), QString("Toggle window split"));
        QCOMPARE(next(two(QRectF(10, 36, 1045, 1034), QRectF(1065, 36, 845, 1034))), QString("Shrink window left"));
        QCOMPARE(next(two(QRectF(10, 36, 745, 1034), QRectF(765, 36, 1145, 1034))), QString("Expand window left a lot"));
        // Dragged with the mouse: the pointer moved and doesn't rest where a
        // keyboard focus change would warp it.
        QCOMPARE(next(two(left, right, QPointF(900, 300))), QString());
        QCOMPARE(d.mouseChanges(), 1);
        // From the keyboard the pointer is warped to the focused window's middle.
        QCOMPARE(next(two(right, left, right.center())), QString("Swap window to the right"));
    }

    void theGameCountsOnlyThePlayersMoves() {
        Rig rig(true);
        QVERIFY(rig.waitFor("menu"));
        rig.game->playLesson(5); // the pile: dealing groups, floats, focuses
        rig.game->begin();
        QVERIFY(rig.deal());
        Player p(*rig.game, rig.desktop);
        p.settle(300);
        // Everything the game did while dealing is its own.
        QVERIFY(rig.game->judged().isEmpty());
        auto *w = rig.desktop.activeWindow();
        QVERIFY(w && !w->group.isEmpty());
        rig.desktop.pressGroupNext(); // then Hyprland's empty activewindowv2
        p.settle(80);
        QCOMPARE(rig.game->judged().size(), 1);
        QCOMPARE(rig.game->judged().first().move.family, QString("group-next"));
        rig.desktop.pressOutOfGroup();
        p.settle(80);
        QCOMPARE(rig.game->judged().size(), 2);
        QCOMPARE(rig.game->judged().last().move.action, QString("Move active window out of group"));
        // Popping a piece out is what the hint asked for, and is progress.
        QCOMPARE(int(rig.game->judged().last().verdict), int(Commands::Verdict::Good));
        // Bringing the board up is the game's doing, and so is what follows.
        rig.game->showBoard();
        rig.desktop.pressFloat();
        p.settle(80);
        QCOMPARE(rig.game->judged().size(), 2);
    }

    // ── Moves: judging and proficiency ─────────────────────────────────

    void movesAreJudgedAgainstTheHint() {
        // Three tiles in a row, a picture cut in three thirds.
        Puzzle::Desk desk;
        Cut::Chain chain;
        chain.levels = {{Cut::Split::Side, 1.0 / 3}, {Cut::Split::Side, 0.5}};
        desk.pictures << Puzzle::Picture{"p", chain, 2};
        for (int k = 0; k < 3; ++k)
            desk.pieces << Puzzle::Piece{QString("p-%1").arg(k + 1), 0, k, "p", chain.layout().at(k)};
        auto place = [](Puzzle::Desk d, const QStringList &order, const QString &focused) {
            for (int k = 0; k < 3; ++k)
                d.windows.insert(order.at(k), window(order.at(k), 2, QRectF(10 + k * 635, 36, 625, 1034)));
            d.focused = focused;
            d.activeWorkspace = 2;
            return d;
        };
        auto situation = [](const Puzzle::Desk &d, QList<Hints::Hint> hints = {}) {
            return Commands::Situation{d, Puzzle::evaluate(d), hints};
        };
        auto move = [](const QString &action) {
            Commands::Move m;
            m.action = action;
            m.family = Commands::familyOf(action);
            return m;
        };
        Hints::Hint swapLeft;
        swapLeft.action = "Swap window to the left";
        swapLeft.piece = "p-1";
        Hints::Hint focusThird;
        focusThird.action = "Focus on next window";
        focusThird.piece = "p-3";

        const auto shuffled = place(desk, {"p-2", "p-1", "p-3"}, "p-1");
        const auto fixed = place(desk, {"p-1", "p-2", "p-3"}, "p-1");
        using V = Commands::Verdict;
        // What the hint said: good.
        QCOMPARE(Commands::judge(move("Swap window to the left"), situation(shuffled, {swapLeft}), situation(fixed)), V::Good);
        // Not what it said, but progress all the same: good.
        QCOMPARE(Commands::judge(move("Swap window up"), situation(shuffled), situation(fixed)), V::Good);
        // Undoing progress: a detour.
        QCOMPARE(Commands::judge(move("Swap window to the right"), situation(fixed, {focusThird}), situation(shuffled)), V::Detour);
        // A look towards the piece the hint is about: neutral; away: a detour.
        const auto atFirst = place(desk, {"p-1", "p-2", "p-3"}, "p-1");
        const auto atSecond = place(desk, {"p-1", "p-2", "p-3"}, "p-2");
        QCOMPARE(Commands::judge(move("Focus on right window"), situation(atFirst, {focusThird}), situation(atSecond)), V::Neutral);
        QCOMPARE(Commands::judge(move("Focus on left window"), situation(atSecond, {focusThird}), situation(atFirst)), V::Detour);
        // Reaching it by another way is as good as the hinted key.
        const auto atThird = place(desk, {"p-1", "p-2", "p-3"}, "p-3");
        QCOMPARE(Commands::judge(move("Focus on right window"), situation(atSecond, {focusThird}), situation(atThird)), V::Good);
        // A change that changes nothing nobody asked for: a detour.
        QCOMPARE(Commands::judge(move("Toggle window grouping"), situation(atFirst, {focusThird}), situation(atFirst)), V::Detour);
        // A resize of another size in the asked direction matches.
        Hints::Hint wider;
        wider.action = "Shrink window left";
        QVERIFY(Commands::matches(move("Shrink window left a little"), wider));
        QVERIFY(!Commands::matches(move("Expand window left"), wider));
    }

    void proficiencyRewardsAccuracyAndUnaidedUse() {
        auto command = [](int good, int neutral, int detour, int aided, int reactionMs) {
            Stats::Command c;
            c.good = good;
            c.neutral = neutral;
            c.detour = detour;
            c.uses = good + neutral + detour;
            c.aided = aided;
            for (int i = 0; i < c.uses; ++i)
                c.reactions << reactionMs;
            return c;
        };
        QCOMPARE(Stats::proficiency(Stats::Command{}).level, Stats::None);
        // Ten right, unaided and quick: a master.
        const auto best = Stats::proficiency(command(10, 0, 0, 0, 1200));
        QCOMPARE(best.level, Stats::Master);
        QVERIFY(qAbs(best.score - 100 * (1 - std::exp(-10.0 / 6))) < 1e-6);
        // The same but always with the keys on screen: fluent at most.
        QCOMPARE(Stats::proficiency(command(200, 0, 0, 200, 1200)).level, Stats::Fluent);
        // Volume alone gets nowhere: right half the time stays practised.
        QVERIFY(Stats::proficiency(command(500, 0, 500, 0, 1000)).level <= Stats::Practised);
        // One lucky use is still a novice.
        QCOMPARE(Stats::proficiency(command(1, 0, 0, 0, 1000)).level, Stats::Novice);
        // Slower is lower, everything else equal.
        QVERIFY(Stats::proficiency(command(10, 0, 0, 0, 7000)).score < best.score);
        // Neutral counts half.
        QVERIFY(qAbs(Stats::proficiency(command(5, 10, 5, 0, 2000)).accuracy - 0.5) < 1e-9);
        QCOMPARE(Stats::median({5, 1, 3}), qint64(3));
        QCOMPARE(Stats::median({4, 1, 3, 2}), qint64(2));
        QCOMPARE(Stats::median({}), qint64(-1));
    }

    void statsAreKeptAndTheProgressFileStaysAsItWas() {
        QTemporaryDir dir;
        const QString path = dir.filePath("stats.json");
        {
            Stats stats(path);
            Commands::Move m;
            m.action = "Swap window up";
            m.family = "swap";
            m.at = 12345;
            stats.record(m, Commands::Verdict::Good, 900, false);
            stats.record(m, Commands::Verdict::Detour, 2100, true);
            m.action = "Toggle window split";
            m.family = "split";
            stats.record(m, Commands::Verdict::Neutral, 1500, false);
            for (int i = 0; i < Stats::MaxRuns + 5; ++i)
                stats.addRun(Stats::Run{"medium", false, false, 1000 + i, 90000, 20 + i, 14, 30});
            QVERIFY(stats.save());
        }
        Stats loaded(path);
        QVERIFY(loaded.load());
        const auto swap = loaded.command("swap");
        QCOMPARE(swap.uses, 2);
        QCOMPARE(swap.good, 1);
        QCOMPARE(swap.detour, 1);
        QCOMPARE(swap.aided, 1);
        QCOMPARE(swap.reactions, (QList<int>{900, 2100}));
        QCOMPARE(swap.actions.value("Swap window up"), 2);
        QCOMPARE(swap.last, qint64(12345));
        QCOMPARE(loaded.command("split").neutral, 1);
        QCOMPARE(loaded.runs().size(), Stats::MaxRuns);
        QCOMPARE(loaded.runs().last().moves, 20 + Stats::MaxRuns + 4);
        QCOMPARE(loaded.runs().last().par, 14);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files), QStringList{"stats.json"});
        writeFile(path, "{ not json");
        QVERIFY(!Stats(path).load());

        // A whole game keeps its progress file in the old shape.
        Rig rig(true);
        QVERIFY(rig.waitFor("menu"));
        rig.game->playLesson(0);
        rig.game->begin();
        QVERIFY(rig.deal());
        Player(*rig.game, rig.desktop).settle(300); // past the moment the game's own echoes are ignored
        QVERIFY(Player(*rig.game, rig.desktop).solve());
        rig.game->shutdown();
        QFile progress(rig.path());
        QVERIFY(progress.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(progress.readAll()).object();
        const QStringList keys = root.keys();
        QCOMPARE(QSet<QString>(keys.begin(), keys.end()), (QSet<QString>{"version", "stars", "best", "current"}));
        QCOMPARE(root.value("version").toInt(), 1);
        Stats after(QFileInfo(rig.path()).absolutePath() + "/stats.json");
        QVERIFY(after.load());
        QCOMPARE(after.runs().size(), 1);
        QCOMPARE(after.runs().first().level, QString("swap"));
        QVERIFY(after.command("swap").uses >= 1);
    }

    // ── Every command, every deal ──────────────────────────────────────

    void fullTiersNeedEveryFamilyOfMoves() {
        for (const QSizeF screen : {QSizeF(1920, 1054), QSizeF(1080, 1894)})
            for (int tier = 1; tier <= 2; ++tier)
                for (quint32 seed = 1; seed <= 300; ++seed) {
                    const auto level = Levels::puzzle(tier, seed, screen);
                    const QString where = QString("tier %1 seed %2").arg(tier).arg(seed);
                    QSet<Levels::Start> starts;
                    int pile = 0;
                    bool sideRatio = false, stackRatio = false;
                    for (const auto &p : level.pictures) {
                        for (const auto &s : p.pieces) {
                            starts.insert(s.start);
                            pile += s.start == Levels::Start::Pile;
                        }
                        for (const auto &l : p.chain.levels) {
                            sideRatio = sideRatio || (l.split == Cut::Split::Side && qAbs(l.ratio - 0.5) > 0.05);
                            stackRatio = stackRatio || (l.split == Cut::Split::Stack && qAbs(l.ratio - 0.5) > 0.05);
                        }
                        QVERIFY2(!Cut::hasSliver(p.chain), qPrintable(where));
                    }
                    QVERIFY2(level.pictures.size() >= 2, qPrintable(where));           // workspace switch
                    QVERIFY2(starts.contains(Levels::Start::Away), qPrintable(where)); // carrying over
                    QVERIFY2(starts.contains(Levels::Start::Floating), qPrintable(where));
                    QVERIFY2(starts.contains(Levels::Start::Scratchpad), qPrintable(where));
                    QVERIFY2(pile >= 2, qPrintable(where));
                    QVERIFY2(sideRatio && stackRatio, qPrintable(where));
                    // The first split turned against how it's dealt, between
                    // two pieces that stay; and the first picture out of order.
                    const auto &first = level.pictures.first();
                    QVERIFY2(first.chain.levels.first().split
                                 != Cut::dealt(first.chain.pieces(), screen).levels.first().split, qPrintable(where));
                    QVERIFY2(first.tiles.at(0).picture == 0 && first.tiles.at(1).picture == 0, qPrintable(where));
                    QList<int> own;
                    for (const auto &d : first.tiles)
                        if (d.picture == 0)
                            own << d.index;
                    QVERIFY2(!std::is_sorted(own.begin(), own.end()), qPrintable(where));
                    QVERIFY2(Levels::par(level, screen) >= 8, qPrintable(where));
                }
    }

    void playingAFullTierUsesEveryFamily_data() {
        QTest::addColumn<int>("tier");
        QTest::addColumn<int>("seed");
        for (int tier = 1; tier <= 2; ++tier)
            for (int seed = 11; seed <= 16; ++seed)
                QTest::addRow("tier %d seed %d", tier, seed) << tier << seed;
    }

    // Played to the end by following the hints, every family of moves turns
    // up in what the detector saw.
    void playingAFullTierUsesEveryFamily() {
        QFETCH(int, tier);
        QFETCH(int, seed);
        Rig rig(true);
        QVERIFY(rig.waitFor("menu"));
        rig.game->setSeed(quint32(seed));
        rig.game->playTier(tier);
        rig.game->begin();
        QVERIFY(rig.deal());
        Player player(*rig.game, rig.desktop);
        QVERIFY(player.solve(900));
        QSet<QString> seen;
        for (const auto &j : rig.game->judged())
            seen.insert(j.move.family);
        QStringList missing;
        for (const QStringList &any : QList<QStringList>{{"swap"}, {"focus"}, {"workspace"}, {"move-workspace"}, {"split"},
                                                          {"float"}, {"group-out", "group-next"}, {"resize-x"}, {"resize-y"},
                                                          {"scratchpad", "to-scratchpad"}})
            if (std::none_of(any.begin(), any.end(), [&](const QString &f) { return seen.contains(f); }))
                missing << any.join("|");
        QVERIFY2(missing.isEmpty(), qPrintable("missing " + missing.join(", ") + " in " + QStringList(seen.values()).join(", ")));
        QVERIFY(rig.game->moves() >= rig.game->par());
        QCOMPARE(rig.game->summary().value("par").toInt(), rig.game->par());
    }

    void practiceLeansOnTheWeakestMoves() {
        auto weakness = [](std::initializer_list<const char *> weak) {
            QHash<QString, double> w;
            for (const auto &f : Commands::families())
                w.insert(f.id, 0.0);
            for (const char *f : weak)
                w.insert(QString::fromLatin1(f), 1.0);
            return w;
        };
        auto has = [](const Levels::Level &l, Levels::Start start) {
            for (const auto &p : l.pictures)
                for (const auto &s : p.pieces)
                    if (s.start == start)
                        return true;
            return false;
        };
        auto stackRatio = [](const Levels::Level &l) {
            for (const auto &p : l.pictures)
                for (const auto &lv : p.chain.levels)
                    if (lv.split == Cut::Split::Stack && qAbs(lv.ratio - 0.5) > 0.05)
                        return true;
            return false;
        };
        int scratchWhenWeak = 0, scratchWhenStrong = 0, stackWhenWeak = 0, floatWhenStrong = 0;
        for (quint32 seed = 1; seed <= 200; ++seed) {
            const auto weakScratch = Levels::practice(seed, weakness({"scratchpad", "to-scratchpad", "resize-y"}));
            scratchWhenWeak += has(weakScratch, Levels::Start::Scratchpad);
            stackWhenWeak += stackRatio(weakScratch);
            const auto weakFloat = Levels::practice(seed, weakness({"float", "split"}));
            scratchWhenStrong += has(weakFloat, Levels::Start::Scratchpad);
            floatWhenStrong += has(weakScratch, Levels::Start::Floating);
            QCOMPARE(weakFloat.id, QString("practice"));
            QVERIFY(has(weakFloat, Levels::Start::Floating));
        }
        QCOMPARE(scratchWhenWeak, 200);
        QCOMPARE(stackWhenWeak, 200);
        QVERIFY2(scratchWhenStrong < 60, qPrintable(QString::number(scratchWhenStrong)));
        QVERIFY2(floatWhenStrong < 60, qPrintable(QString::number(floatWhenStrong)));

        // And it plays: a practice run from the menu, to the end.
        Rig rig(true);
        QVERIFY(rig.waitFor("menu"));
        rig.game->setSeed(5);
        rig.game->playPractice();
        QCOMPARE(rig.game->state(), QString("intro"));
        QVERIFY(rig.game->practice());
        rig.game->begin();
        QVERIFY(rig.deal());
        // The keys wait a moment in practice.
        QCOMPARE(rig.game->hintTier(), 1);
        QVERIFY(Player(*rig.game, rig.desktop).solve(900));
        QCOMPARE(rig.game->best(), qint64(0)); // no best time for practice
        QVERIFY(rig.game->stats().runs().last().practice);
    }
};

QTEST_MAIN(TesseraTests)
#include "tessera_tests.moc"
