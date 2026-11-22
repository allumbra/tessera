#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "bindings.h"
#include "cut.h"
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
            // Saved atomically: no temporary file left beside it.
            const auto leftovers = QDir(QFileInfo(path).absolutePath()).entryList(QDir::Files);
            QCOMPARE(leftovers, QStringList{"progress.json"});
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
};

QTEST_MAIN(TesseraTests)
#include "tessera_tests.moc"
