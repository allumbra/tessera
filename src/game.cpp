#include "game.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <algorithm>

#include "collage.h"

using Levels::Start;

static QString tr_(const char *context, const char *text) { return QCoreApplication::translate(context, text); }

// ── Pieces ────────────────────────────────────────────────────────────────

QVariant PieceModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case IdRole: return r.id;
    case TitleRole: return r.title;
    case ImageRole: return r.image;
    case LabelRole: return r.label;
    case CellRole: return r.cell;
    case FocusedRole: return r.focused;
    case InPlaceRole: return r.inPlace;
    case ImageSizeRole: return r.imageSize;
    }
    return {};
}

QHash<int, QByteArray> PieceModel::roleNames() const {
    return {{IdRole, "pieceId"}, {TitleRole, "windowTitle"}, {ImageRole, "image"}, {LabelRole, "label"},
            {CellRole, "cell"}, {FocusedRole, "focused"}, {InPlaceRole, "inPlace"}, {ImageSizeRole, "imageSize"}};
}

PieceModel::Row *PieceModel::find(const QString &id) {
    for (auto &r : m_rows)
        if (r.id == id)
            return &r;
    return nullptr;
}

PieceModel::Row *PieceModel::findAddress(const QString &address) {
    if (address.isEmpty())
        return nullptr;
    for (auto &r : m_rows)
        if (r.address == address)
            return &r;
    return nullptr;
}

void PieceModel::add(const Row &row) {
    beginInsertRows({}, m_rows.size(), m_rows.size());
    m_rows.append(row);
    endInsertRows();
}

void PieceModel::clear() {
    if (m_rows.isEmpty())
        return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

void PieceModel::update(const QString &id, bool focused, bool inPlace) {
    for (int i = 0; i < m_rows.size(); ++i) {
        Row &r = m_rows[i];
        if (r.id != id || (r.focused == focused && r.inPlace == inPlace))
            continue;
        r.focused = focused;
        r.inPlace = inPlace;
        emit dataChanged(index(i), index(i), {FocusedRole, InPlaceRole});
    }
}

void PieceModel::relabel(const std::function<QString(const QString &)> &label) {
    for (int i = 0; i < m_rows.size(); ++i)
        m_rows[i].label = label(m_rows.at(i).id);
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(m_rows.size() - 1), {LabelRole});
}

// ── Game ──────────────────────────────────────────────────────────────────

// Stats live beside the progress file, so a test's temporary progress keeps
// its stats there too.
static QString statsPathFor(const QString &progressPath) {
    return progressPath.isEmpty() ? QString() : QFileInfo(progressPath).absolutePath() + "/stats.json";
}

Game::Game(EventSource *events, Hyprctl *hyprctl, const QString &progressPath, QObject *parent)
    : QObject(parent), m_events(events), m_hyprctl(hyprctl), m_progress(progressPath),
      m_stats(statsPathFor(progressPath)), m_ownGallery(std::make_unique<Gallery>()),
      m_pid(QCoreApplication::applicationPid()) {
    m_gallery = m_ownGallery.get();
    m_progress.load();
    m_stats.load();
    m_now = [] { return QDateTime::currentMSecsSinceEpoch(); };
    m_level = Levels::lessons().first();
    connect(m_events, &EventSource::received, this, &Game::handleEvent);
    connect(m_events, &EventSource::statusChanged, this, &Game::sourceChanged);
    connect(&m_poll, &QTimer::timeout, this, [this] {
        if (m_state == "waiting")
            allocate();
        else if (m_state == "dealing" && !m_busy)
            dealPicture();
        else if (m_state == "playing")
            evaluate();
    });
    m_tick.setInterval(100);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        emit tick();
        if (m_state == "playing" && m_level.lesson)
            updateHints();
    });
    m_spawnTimeout.setSingleShot(true);
    connect(&m_spawnTimeout, &QTimer::timeout, this, &Game::bindFromClients);
    // Events say something moved; the state is read a moment later, once
    // Hyprland has laid the tiles out again.
    m_soon.setSingleShot(true);
    m_soon.setInterval(40);
    connect(&m_soon, &QTimer::timeout, this, [this] {
        if (m_state == "playing")
            evaluate();
    });
}

Game::~Game() = default;

void Game::setGallery(Gallery *gallery) {
    m_gallery = gallery ? gallery : m_ownGallery.get();
}

void Game::setBindings(const QString &menuText, const QString &problem) {
    m_bindings = Bindings::parse(menuText);
    m_notice = problem;
    emit changed();
}

void Game::start() {
    m_error.clear();
    m_events->start();
    sourceChanged();
    // Tessera's cuts are dwindle splits; another layout can't be solved.
    const QString layout = m_hyprctl->query("getoption general:layout").toObject().value("str").toString();
    if (!layout.isEmpty() && layout != "dwindle")
        m_notice = tr("Hyprland's layout here is %1. Tessera's pictures are cut the way the dwindle layout splits "
                      "windows, so they can only be solved on workspaces that use dwindle.").arg(layout);
    emit changed();
}

void Game::sourceChanged() {
    if (m_events->connected()) {
        if (m_state == "connecting" || (m_state == "error" && m_sourceFailed)) {
            m_sourceFailed = false;
            m_error.clear();
            setState("menu");
        }
        return;
    }
    if (!m_events->error().isEmpty()) {
        // Without events nothing the player does can be seen, so the game
        // stops and says why rather than pretending.
        clearPieces();
        m_poll.stop();
        m_tick.stop();
        m_queue.clear();
        m_busy = false;
        m_sourceFailed = true;
        m_error = m_events->error();
        setState("error");
    }
}

void Game::setState(const QString &state) {
    m_state = state;
    if (state == "waiting")
        m_poll.start(m_timings.wait);
    else if (state == "dealing" || state == "playing")
        m_poll.start(m_timings.poll);
    else
        m_poll.stop();
    if (state == "playing")
        m_tick.start();
    else if (state != "solved")
        m_tick.stop();
    emit changed();
}

void Game::say(const QString &text) {
    m_toast = text;
    emit toastChanged();
}

void Game::fail(const QString &text) {
    clearPieces();
    m_tick.stop();
    m_sourceFailed = false;
    m_error = text;
    setState("error");
}

void Game::save() {
    QString error;
    m_saveError = m_progress.save(&error) ? QString() : tr("Can't save progress: %1").arg(error);
}

void Game::saveStats() {
    QString error;
    if (!m_stats.save(&error))
        m_saveError = tr("Can't save stats: %1").arg(error);
    emit statsChanged();
}

QString Game::levelTitle() const { return tr_("Levels", m_level.title); }
QString Game::teaches() const { return tr_("Levels", m_level.teaches); }
QString Game::intro() const { return tr_("Levels", m_level.intro); }

QString Game::chord(const QString &action) const { return Bindings::chordFor(m_bindings, action); }

// Workspace chords stand for all ten on the card: Super+1 becomes Super+1…0.
QVariantList Game::introChords() const {
    QVariantList out;
    for (const QString &action : m_level.actions) {
        QString c = chord(action);
        if (action.endsWith(" workspace 1") && c.endsWith(" 1"))
            c = c.left(c.size() - 1) + QStringLiteral("1…0");
        if (!c.isEmpty())
            out << c;
    }
    return out;
}

QVariantList Game::lessons() const {
    QVariantList out;
    const auto &all = Levels::lessons();
    for (int i = 0; i < all.size(); ++i) {
        const auto &l = all.at(i);
        // A lesson opens once the one before it is done, so moves arrive in
        // the order the later ones build on.
        const bool unlocked = i == 0 || m_progress.stars(all.at(i - 1).id) > 0 || m_progress.stars(l.id) > 0;
        out << QVariantMap{{"title", tr_("Levels", l.title)}, {"teaches", tr_("Levels", l.teaches)},
                           {"stars", m_progress.stars(l.id)}, {"unlocked", unlocked}};
    }
    return out;
}

bool Game::puzzlesUnlocked() const {
    for (const auto &l : Levels::lessons())
        if (m_progress.stars(l.id) == 0)
            return false;
    return true;
}

QVariantList Game::tiers() const {
    QVariantList out;
    const auto &all = Levels::tiers();
    for (int i = 0; i < all.size(); ++i) {
        const bool unlocked = puzzlesUnlocked()
                              && (i == 0 || m_progress.best(all.at(i - 1).id) > 0 || m_progress.best(all.at(i).id) > 0);
        out << QVariantMap{{"title", tr_("Levels", all.at(i).title)}, {"about", tr_("Levels", all.at(i).about)},
                           {"best", m_progress.best(all.at(i).id)}, {"unlocked", unlocked}};
    }
    return out;
}

const Gallery::Prepared *Game::preparedFor(const QString &art) const {
    const auto it = m_prepared.find(art);
    return it == m_prepared.end() ? nullptr : &it.value();
}

static QString creditTitle(const Gallery::Prepared &p, int k) {
    const QString source = p.sources.value(k);
    if (source.startsWith("builtin:"))
        if (const auto *work = Art::find(source.mid(8)))
            return tr_("Art", work->title);
    return p.credits.value(k).title;
}

QString Game::pictureTitle(int picture) const {
    if (picture < 0 || picture >= m_desk.pictures.size())
        return {};
    const auto *p = preparedFor(m_desk.pictures.at(picture).key);
    if (!p)
        return {};
    const QString first = creditTitle(*p, 0);
    return p->collage ? tr("%1 and %n more", "", int(p->credits.size()) - 1).arg(first) : first;
}

// The short name a picture's pieces go by: "Wave" for "Wave 3". Two pictures
// with the same name get told apart, since window titles must differ.
QString Game::pictureName(int picture) const {
    auto base = [this](int i) -> QString {
        const auto *p = preparedFor(m_desk.pictures.value(i).key);
        if (!p)
            return m_desk.pictures.value(i).key;
        if (!p->builtinName.isEmpty())
            if (const auto *work = Art::find(p->builtinName))
                return tr_("Art", work->name);
        return p->name;
    };
    const QString name = base(picture);
    int same = 0;
    for (int i = 0; i < picture; ++i)
        if (base(i) == name)
            ++same;
    static const char *numerals[] = {"", " II", " III", " IV", " V"};
    return name + QString::fromLatin1(numerals[std::min(same, 4)]);
}

QString Game::pieceName(const QString &id) const {
    const auto *p = m_desk.piece(id);
    if (!p)
        return {};
    if (p->picture < 0)
        return tr("The stray piece");
    return tr("%1 %2").arg(pictureName(p->picture)).arg(p->index + 1);
}

QVariantList Game::pictures() const {
    QVariantList out;
    for (int i = 0; i < m_desk.pictures.size(); ++i) {
        const auto &picture = m_desk.pictures.at(i);
        const auto *prep = preparedFor(picture.key);
        const auto cells = picture.chain.layout();
        QVariantList cellList;
        for (const auto &piece : m_desk.pieces)
            if (piece.picture == i) {
                const QRectF r = cells.value(piece.index);
                cellList << QVariantMap{{"x", r.x()}, {"y", r.y()}, {"w", r.width()}, {"h", r.height()},
                                        {"placed", m_eval.pieces.value(piece.id).inPlace},
                                        {"number", piece.index + 1}};
            }
        QVariantList credits;
        if (prep)
            for (int k = 0; k < prep->credits.size(); ++k) {
                const Credit &c = prep->credits.at(k);
                // The player's own files say so, and nothing more: their
                // path stays on this machine and off the board.
                credits << QVariantMap{{"title", creditTitle(*prep, k)},
                                       {"artist", c.artist},
                                       {"date", c.date},
                                       {"license", c.local ? tr("Your own picture")
                                                   : c.license == "Public domain (CC0)" ? tr("Public domain (CC0)")
                                                   : c.license == "Public domain" ? tr("Public domain")
                                                                                  : c.license},
                                       {"origin", c.local ? tr("Your folder") : c.origin},
                                       {"url", c.local ? QString() : c.url},
                                       {"local", c.local}};
            }
        const Credit first = prep ? prep->credits.value(0) : Credit();
        const auto state = m_eval.pictures.value(i);
        out << QVariantMap{{"title", pictureTitle(i)},
                           {"artist", prep && !prep->collage ? first.artist : QString()},
                           {"year", prep && !prep->collage ? first.date : QString()},
                           {"image", prep ? prep->url() : QString()},
                           {"aspect", prep && !prep->size.isEmpty() ? double(prep->size.width()) / prep->size.height()
                                                                     : 16.0 / 9.0},
                           {"collage", prep && prep->collage},
                           {"credits", credits},
                           {"slot", Puzzle::slot(picture.workspace)},
                           {"placed", state.placed},
                           {"total", picture.chain.pieces()},
                           {"complete", state.complete},
                           {"here", picture.workspace != 0 && picture.workspace == m_activeWs},
                           {"chord", picture.workspace > 0
                                         ? chord(QString("Switch to workspace %1").arg(Puzzle::slot(picture.workspace)))
                                         : QString()},
                           {"cells", cellList}};
    }
    return out;
}

QVariantList Game::hints() const {
    QVariantList out;
    if (m_state != "playing")
        return out;
    // Lessons hold the answer back for a while: first only the goal, then
    // what to do, then the keys, then why.
    if (m_level.lesson && m_hintTier == 0)
        return out;
    for (const auto &h : m_hints)
        out << QVariantMap{{"text", h.text},
                           {"chord", m_hintTier >= 2 ? h.chord : QString()},
                           {"detail", m_hintTier >= 3 ? h.detail : QString()},
                           {"piece", h.piece}};
    return out;
}

qint64 Game::elapsed() const {
    if (m_state == "playing")
        return std::max<qint64>(0, m_now() - m_started);
    if (m_state == "solved")
        return m_finished - m_started;
    return 0;
}

qint64 Game::best() const { return m_level.lesson || m_practice ? 0 : m_progress.best(m_level.id); }

// Four arrow chords that differ only in the arrow read as one.
QString Game::arrowChord(const QStringList &actions) const {
    QString base;
    for (const auto &a : actions) {
        const QString c = chord(a);
        const int plus = c.lastIndexOf(" + ");
        if (plus < 0 || c.contains(" / "))
            return chord(actions.first());
        if (base.isEmpty())
            base = c.left(plus);
        else if (base != c.left(plus))
            return chord(actions.first());
    }
    return base + " + ARROWS";
}

QString Game::numberedChord(const QString &action) const {
    QString c = chord(action);
    return c.endsWith(" 1") ? c.left(c.size() - 1) + QStringLiteral("1…0") : c;
}

QString Game::familyChord(const QString &family) const {
    if (family == "focus")
        return arrowChord({"Focus on left window", "Focus on right window", "Focus on above window", "Focus on below window"});
    if (family == "swap")
        return arrowChord({"Swap window to the left", "Swap window to the right", "Swap window up", "Swap window down"});
    if (family == "group-in")
        return arrowChord({"Move window to group on left", "Move window to group on right", "Move window to group on top",
                           "Move window to group on bottom"});
    if (family == "workspace")
        return numberedChord("Switch to workspace 1");
    if (family == "move-workspace")
        return numberedChord("Move window to workspace 1");
    if (family == "resize-x")
        return chord("Expand window left") + " / " + chord("Shrink window left");
    if (family == "resize-y")
        return chord("Shrink window up") + " / " + chord("Expand window down");
    const auto *f = Commands::family(family);
    return f ? chord(f->actions.first()) : QString();
}

QVariantList Game::cheatSheet() const {
    auto row = [](const QString &name, const QString &chord) { return QVariantMap{{"name", name}, {"chord", chord}}; };
    auto arrows = [this](const QStringList &actions) { return arrowChord(actions); };
    auto numbered = [this](const QString &action) { return numberedChord(action); };
    return {
        row(tr("Focus a window"), arrows({"Focus on left window", "Focus on right window", "Focus on above window",
                                          "Focus on below window"})),
        row(tr("Swap windows"), arrows({"Swap window to the left", "Swap window to the right", "Swap window up",
                                        "Swap window down"})),
        row(tr("Go to a workspace"), numbered("Switch to workspace 1")),
        row(tr("Carry a window to a workspace"), numbered("Move window to workspace 1")),
        row(tr("Turn the split"), chord("Toggle window split")),
        row(tr("Float or tile"), chord("Toggle window floating/tiling")),
        row(tr("Move the line left or right"), chord("Expand window left") + " / " + chord("Shrink window left")),
        row(tr("Move the line up or down"), chord("Shrink window up") + " / " + chord("Expand window down")),
        row(tr("Pop out of a pile"), chord("Move active window out of group")),
        row(tr("Next piece in a pile"), chord("Next window in group")),
        row(tr("Tuck into a neighbour's pile"), arrows({"Move window to group on left", "Move window to group on right",
                                                        "Move window to group on top", "Move window to group on bottom"})),
        row(tr("Make or break a pile"), chord("Toggle window grouping")),
        row(tr("Show or hide the scratchpad"), chord("Toggle scratchpad")),
        row(tr("Put a window in the scratchpad"), chord("Move window to scratchpad")),
        row(tr("Hint"), "H"),
        row(tr("Start, next"), "SPACE"),
        row(tr("Your shortcut stats"), "S"),
        row(tr("Where pictures come from"), "P"),
        row(tr("Deal again"), "R"),
        row(tr("Levels"), "ESCAPE"),
        row(tr("These shortcuts"), "?"),
        row(tr("Quit"), "Q"),
    };
}

static QString levelName(Stats::Level level) {
    switch (level) {
    case Stats::None: return QCoreApplication::translate("Game", "Not yet");
    case Stats::Novice: return QCoreApplication::translate("Game", "Novice");
    case Stats::Learning: return QCoreApplication::translate("Game", "Learning");
    case Stats::Practised: return QCoreApplication::translate("Game", "Practised");
    case Stats::Fluent: return QCoreApplication::translate("Game", "Fluent");
    case Stats::Master: return QCoreApplication::translate("Game", "Master");
    }
    return {};
}

// One row per kind of move, in the order a puzzle calls for them most, so
// the many focus moves and swaps read as expected rather than as a lean.
QVariantList Game::commandStats() const {
    QVariantList out;
    double total = 0;
    for (const auto &f : Commands::families())
        total += f.expected;
    for (const auto &f : Commands::families()) {
        const Stats::Command c = m_stats.command(f.id);
        const Stats::Proficiency p = Stats::proficiency(c);
        const int judged = c.good + c.neutral + c.detour;
        out << QVariantMap{{"id", f.id},
                           {"name", tr_("Game", f.name)},
                           {"chord", familyChord(f.id)},
                           {"level", int(p.level)},
                           {"levelName", levelName(p.level)},
                           {"score", p.score},
                           {"uses", c.uses},
                           {"good", judged ? double(c.good) / judged : 0.0},
                           {"neutral", judged ? double(c.neutral) / judged : 0.0},
                           {"detour", judged ? double(c.detour) / judged : 0.0},
                           {"aided", judged ? double(c.aided) / judged : 0.0},
                           {"reaction", Stats::median(c.reactions)},
                           {"expected", f.expected / total},
                           {"last", c.last}};
    }
    return out;
}

QVariantList Game::recentRuns() const {
    QVariantList out;
    const auto &runs = m_stats.runs();
    for (int i = runs.size() - 1; i >= 0 && out.size() < 8; --i) {
        const auto &r = runs.at(i);
        QString title = r.level;
        if (r.practice)
            title = tr_("Levels", "Practice weak spots");
        for (const auto &t : Levels::tiers())
            if (t.id == r.level)
                title = tr_("Levels", t.title);
        for (const auto &l : Levels::lessons())
            if (l.id == r.level)
                title = tr_("Levels", l.title);
        out << QVariantMap{{"title", title}, {"when", r.when}, {"ms", r.ms}, {"moves", r.moves},
                           {"par", r.par}, {"looks", r.looks}, {"lesson", r.lesson}};
    }
    return out;
}

QList<int> Game::workspaces() const {
    QList<int> out;
    for (const auto &p : m_desk.pictures)
        out << p.workspace;
    out << m_spares;
    return out;
}

void Game::retranslate() {
    m_pieces.relabel([this](const QString &id) { return pieceName(id); });
    if (m_state == "playing")
        updateHints();
    emit changed();
}

// ── Flow ──────────────────────────────────────────────────────────────────

void Game::primary() {
    if (m_state == "menu") {
        const auto &all = Levels::lessons();
        for (int i = 0; i < all.size(); ++i)
            if (m_progress.stars(all.at(i).id) == 0) {
                playLesson(i);
                return;
            }
        playTier(0);
    } else if (m_state == "intro") {
        begin();
    } else if (m_state == "solved") {
        if (m_practice)
            toMenu();
        else if (m_level.lesson && m_levelIndex + 1 < Levels::lessons().size())
            playLesson(m_levelIndex + 1);
        else if (m_level.lesson)
            playTier(0);
        else
            toMenu();
    } else if (m_state == "error") {
        restart();
    }
}

// Brings the board into view, for keys pressed on a piece whose picture is
// about to close: otherwise the player is left on an empty workspace. Only
// the game's own board, checked against its process id, is ever focused.
void Game::showBoard() {
    if (m_board.isEmpty())
        return;
    refresh();
    if (client(m_board).value("pid").toInteger() != m_pid)
        return;
    // The focus change that follows is the game's, not a move to judge.
    m_detector.suppress(m_now() + 400);
    QString error;
    m_hyprctl->focusWindow(m_board, &error);
}

void Game::playLesson(int index) {
    if (index < 0 || index >= Levels::lessons().size() || (m_state == "error" && m_sourceFailed))
        return;
    if (!lessons().at(index).toMap().value("unlocked").toBool())
        return;
    clearPieces();
    m_desk = {};
    m_eval = {};
    m_practice = false;
    m_levelSpec = m_level = Levels::forScreen(Levels::lessons().at(index), screenArea());
    m_levelSeed = quint32(index + 1);
    m_levelIndex = index;
    m_progress.setCurrent(m_level.id);
    save();
    setState("intro");
}

void Game::playTier(int index) {
    if (index < 0 || index >= Levels::tiers().size() || (m_state == "error" && m_sourceFailed))
        return;
    if (!tiers().at(index).toMap().value("unlocked").toBool())
        return;
    clearPieces();
    m_desk = {};
    m_eval = {};
    const quint32 seed = m_fixedSeed ? m_seed : QRandomGenerator::global()->generate();
    m_practice = false;
    m_levelSpec = m_level = Levels::puzzle(index, seed, screenArea());
    m_levelSeed = seed;
    m_levelIndex = index;
    setState("intro");
}

void Game::playPractice() {
    if ((m_state == "error" && m_sourceFailed) || !puzzlesUnlocked())
        return;
    clearPieces();
    m_desk = {};
    m_eval = {};
    const quint32 seed = m_fixedSeed ? m_seed : QRandomGenerator::global()->generate();
    m_practice = true;
    m_levelSpec = m_level = Levels::practice(seed, m_stats.weakness(), screenArea());
    m_levelSeed = seed;
    m_levelIndex = Levels::tiers().size();
    setState("intro");
}

void Game::begin() {
    // Only from a level's card, or to deal the same level again.
    if ((m_state == "error" && m_sourceFailed) || m_state == "menu" || m_state == "connecting")
        return;
    clearPieces();
    m_error.clear();
    m_desk = {};
    m_eval = {};
    m_hints.clear();
    m_spares.clear();
    m_newBest = false;
    m_stars = 0;
    m_toast.clear();
    m_summary.clear();
    m_pictureNote.clear();
    m_prepared.clear();
    emit toastChanged();

    // The pictures are made first, to the shape of the board's monitor.
    m_level = m_levelSpec;
    m_area = screenArea();
    Gallery::Request request;
    for (const auto &spec : m_level.pictures)
        request.musts << (m_level.lesson ? spec.art : QString());
    request.strays = m_level.strays.size();
    request.area = m_area;
    request.pixels = Collage::renderSize(m_area, m_scale);
    request.seed = m_levelSeed;
    request.lesson = m_level.lesson;
    m_objective = tr("Getting the pictures ready…");
    m_objectiveChord.clear();
    setState("preparing");
    const int generation = m_generation;
    m_gallery->prepare(request, [this, generation](const Gallery::Result &result) {
        if (generation == m_generation && m_state == "preparing")
            prepared(result);
    });
}

void Game::prepared(const Gallery::Result &result) {
    if (!result.ok() || result.pictures.size() < m_level.pictures.size() || result.strays.size() < m_level.strays.size()) {
        fail(tr("Couldn't make the pictures for this puzzle. Press R to try again."));
        return;
    }
    // Each picture's art key becomes the picture made for it; the same
    // picture twice (a tiny folder) still needs pieces of its own.
    m_prepared.clear();
    auto keep = [this](Gallery::Prepared p) {
        const QString base = p.key;
        for (int n = 2; m_prepared.contains(p.key); ++n)
            p.key = base + QString("x%1").arg(n);
        m_prepared.insert(p.key, p);
        return p.key;
    };
    for (int i = 0; i < m_level.pictures.size(); ++i)
        m_level.pictures[i].art = keep(result.pictures.at(i));
    for (int k = 0; k < m_level.strays.size(); ++k)
        m_level.strays[k].art = keep(result.strays.at(k));
    m_pictureNote = result.note;
    if (!m_pictureNote.isEmpty())
        say(m_pictureNote);

    for (int i = 0; i < m_level.pictures.size(); ++i) {
        const auto &spec = m_level.pictures.at(i);
        m_desk.pictures << Puzzle::Picture{spec.art, spec.chain, 0};
        const auto cells = spec.chain.layout();
        for (const auto &s : spec.pieces)
            m_desk.pieces << Puzzle::Piece{QString("%1-%2").arg(spec.art).arg(s.index + 1), i, s.index, spec.art,
                                           cells.value(s.index)};
    }
    for (int k = 0; k < m_level.strays.size(); ++k)
        m_desk.pieces << Puzzle::Piece{QString("stray-%1").arg(k + 1), -1, k, m_level.strays.at(k).art,
                                       QRectF(0.38, 0.3, 0.3, 0.4)};
    m_eval = Puzzle::evaluate(m_desk);
    setState("waiting");
    allocate();
}

void Game::restart() {
    if (m_state == "error" && m_sourceFailed) {
        start();
        return;
    }
    if (m_state == "playing" || m_state == "dealing" || m_state == "waiting" || m_state == "solved"
        || m_state == "error" || m_state == "preparing") {
        // A puzzle deals afresh, shuffled anew; a lesson is the same again.
        if (!m_level.lesson && !m_fixedSeed) {
            m_levelSeed = QRandomGenerator::global()->generate();
            m_levelSpec = m_practice ? Levels::practice(m_levelSeed, m_stats.weakness(), screenArea())
                                     : Levels::puzzle(m_levelIndex, m_levelSeed, screenArea());
        }
        begin();
    }
}

void Game::toMenu() {
    if (m_state == "preparing")
        m_gallery->cancel();
    if (m_state == "playing")
        saveStats();
    clearPieces();
    m_desk = {};
    m_eval = {};
    m_hints.clear();
    if (!(m_state == "error" && m_sourceFailed)) {
        m_error.clear();
        setState("menu");
    }
}

void Game::shutdown() {
    save();
    if (!m_judged.isEmpty())
        m_stats.save();
    clearPieces();
    m_poll.stop();
    m_tick.stop();
}

void Game::hint() {
    if (m_state != "playing")
        return;
    m_askedTier = std::min(3, std::max(m_askedTier, m_hintTier) + 1);
    m_helpAsked = true;
    updateHints();
}

bool Game::requestPieceClose(const QString &id) {
    if (!midPuzzle())
        return true;
    say(tr("%1 stays until the puzzle is done. Press Q to quit Tessera.").arg(pieceName(id)));
    return false;
}

// ── The desktop ───────────────────────────────────────────────────────────

void Game::refresh() {
    m_clients = m_hyprctl->query("clients").toArray();
    const auto workspace = m_hyprctl->query("activeworkspace").toObject();
    if (workspace.contains("id"))
        m_activeWs = workspace.value("id").toInt();
    const auto monitors = m_hyprctl->query("monitors").toArray();
    for (const auto &value : monitors) {
        const auto m = value.toObject();
        if (m.value("focused").toBool(monitors.size() == 1))
            m_scratchpadShown = m.value("specialWorkspace").toObject().value("name").toString().startsWith("special:scratchpad");
    }
    if (m_lastWindow.isEmpty()) {
        const auto active = m_hyprctl->query("activewindow").toObject();
        if (active.contains("address"))
            m_lastWindow = Hypr::address(active.value("address").toString());
    }

    QSet<int> watched;
    for (const auto &p : m_desk.pictures)
        if (p.workspace > 0)
            watched.insert(p.workspace);
    m_desk.foreign.clear();
    QHash<QString, QJsonObject> byAddress;
    for (const auto &value : m_clients) {
        const auto c = value.toObject();
        const QString address = Hypr::address(c.value("address").toString());
        byAddress.insert(address, c);
        const int ws = c.value("workspace").toObject().value("id").toInt();
        if (c.value("pid").toInteger() == m_pid) {
            // The board is the window plainly titled "Tessera"; the pieces
            // share the process but carry their names in the title.
            if (c.value("title").toString() == "Tessera") {
                m_board = address;
                m_boardWs = ws;
            }
            continue;
        }
        if (watched.contains(ws) && c.value("mapped").toBool(true) && !c.value("floating").toBool()) {
            const auto at = c.value("at").toArray(), size = c.value("size").toArray();
            m_desk.foreign[ws] << QRectF(at.at(0).toDouble(), at.at(1).toDouble(), size.at(0).toDouble(),
                                         size.at(1).toDouble());
        }
    }

    for (const auto &row : m_pieces.rows()) {
        Puzzle::Window w;
        w.address = row.address;
        const QJsonObject c = byAddress.value(row.address);
        if (!row.address.isEmpty() && !c.isEmpty() && c.value("pid").toInteger() == m_pid) {
            const auto ws = c.value("workspace").toObject();
            w.present = c.value("mapped").toBool(true);
            w.workspace = ws.value("id").toInt();
            w.special = w.workspace < 0 || ws.value("name").toString().startsWith("special:");
            w.floating = c.value("floating").toBool();
            for (const auto &g : c.value("grouped").toArray())
                w.group << Hypr::address(g.toString());
            w.hidden = c.value("hidden").toBool();
            const auto at = c.value("at").toArray(), size = c.value("size").toArray();
            w.rect = QRectF(at.at(0).toDouble(), at.at(1).toDouble(), size.at(0).toDouble(), size.at(1).toDouble());
        }
        m_desk.windows.insert(row.id, w);
    }
    m_desk.activeWorkspace = m_activeWs;
    m_desk.scratchpadShown = m_scratchpadShown;
    m_desk.focused = pieceAt(m_lastWindow);
}

QJsonObject Game::client(const QString &address) const {
    for (const auto &value : m_clients) {
        const auto c = value.toObject();
        if (Hypr::address(c.value("address").toString()) == address)
            return c;
    }
    return {};
}

QString Game::pieceAt(const QString &address) const {
    if (address.isEmpty())
        return {};
    for (const auto &row : m_pieces.rows())
        if (row.address == address)
            return row.id;
    return {};
}

// Every dispatch goes through here. The window must be one the game opened
// as this piece, and Hyprland must list it, right now, under Tessera's own
// process id: a title alone could be anyone's, and an address could have been
// reused after a window closed.
bool Game::ownPiece(const QString &id, QString *address) {
    auto *row = m_pieces.find(id);
    if (!row || row->address.isEmpty())
        return false;
    refresh();
    const QJsonObject c = client(row->address);
    if (c.isEmpty() || c.value("pid").toInteger() != m_pid || pieceAt(row->address) != id)
        return false;
    *address = row->address;
    return true;
}

// Focuses one of the game's own pieces and confirms Hyprland agrees, for the
// dispatchers that only act on whatever has focus.
bool Game::focusOwnPiece(const QString &id, QString *error) {
    QString address;
    if (!ownPiece(id, &address))
        return false;
    auto active = [this] {
        return Hypr::address(m_hyprctl->query("activewindow").toObject().value("address").toString());
    };
    if (active() == address)
        return true;
    return m_hyprctl->focusWindow(address, error) && active() == address;
}

// ── Setting up ────────────────────────────────────────────────────────────

// Omarchy gives each monitor its own ten workspaces, block * 10 + 1 to
// block * 10 + 10, and records which block is whose in
// ~/.local/state/omarchy/monitor-workspaces ("block<TAB>connector<TAB>..."),
// read here and never written. Without it, the board's own workspace says.
int Game::monitorBlock() const {
    int block = std::max(0, (m_boardWs - 1) / 10);
    const int monitorId = client(m_board).value("monitor").toInt(-1);
    QString connector;
    for (const auto &value : m_hyprctl->query("monitors").toArray()) {
        const auto m = value.toObject();
        if (m.value("id").toInt(-2) == monitorId)
            connector = m.value("name").toString();
    }
    const QString path = qEnvironmentVariableIsEmpty("TESSERA_MONITOR_WORKSPACES")
        ? QDir::homePath() + "/.local/state/omarchy/monitor-workspaces"
        : qEnvironmentVariable("TESSERA_MONITOR_WORKSPACES");
    QFile file(path);
    if (!connector.isEmpty() && file.open(QIODevice::ReadOnly)) {
        for (const QString &line : QString::fromUtf8(file.readAll()).split('\n')) {
            const QStringList f = line.split('\t');
            bool ok = false;
            const int b = f.value(0).toInt(&ok);
            if (ok && f.size() >= 2 && f.at(1) == connector)
                block = b;
        }
    }
    return block;
}

// Pieces only go where nothing else is: empty workspaces on the board's
// monitor. The game waits, and says which, until there are enough.
void Game::allocate() {
    refresh();
    const int need = Levels::workspacesNeeded(m_level);
    if (m_board.isEmpty() || m_boardWs <= 0) {
        m_objective = tr("Looking for Tessera's window on the desktop…");
        m_objectiveChord.clear();
        emit changed();
        return;
    }
    const int block = monitorBlock();
    QSet<int> busy;
    for (const auto &value : m_hyprctl->query("workspaces").toArray()) {
        const auto w = value.toObject();
        if (w.value("windows").toInt() > 0)
            busy.insert(w.value("id").toInt());
    }
    for (const auto &value : m_clients)
        busy.insert(value.toObject().value("workspace").toObject().value("id").toInt());
    QList<int> order;
    const int home = Puzzle::slot(m_boardWs);
    for (int s = home + 1; s <= 9; ++s)
        order << s;
    for (int s = 1; s < home && s <= 9; ++s)
        order << s;
    order << 10;
    QList<int> free;
    for (int s : order) {
        const int id = block * 10 + s;
        if (id != m_boardWs && !busy.contains(id))
            free << id;
    }
    if (free.size() < need) {
        QStringList names;
        for (int id : free)
            names << QString::number(Puzzle::slot(id));
        m_objective = free.isEmpty()
            ? tr("Tessera needs %n empty workspace(s) on this monitor, and every one has windows on it. Move or "
                 "close the windows on one, and the game starts by itself.", "", need)
            : tr("Tessera needs %n empty workspace(s) on this monitor; only %1 empty now. Move or close the windows "
                 "on another one, and the game starts by itself.", "", need).arg(names.join(", "));
        m_objectiveChord.clear();
        emit changed();
        return;
    }
    for (int i = 0; i < m_desk.pictures.size(); ++i)
        m_desk.pictures[i].workspace = free.at(i);
    m_spares = free.mid(m_desk.pictures.size(), m_level.spares);
    m_dealing = 0;
    m_queue.clear();
    m_busy = false;
    setState("dealing");
    dealPicture();
}

// Each picture is dealt on its own workspace with the player looking at it:
// a new window opens on the workspace in view and splits the focused tile,
// which is what makes the dealt pieces fall into dwindle's spiral.
void Game::dealPicture() {
    if (m_dealing < 0 || m_dealing >= m_desk.pictures.size())
        return;
    refresh();
    const int ws = m_desk.pictures.at(m_dealing).workspace;
    const QString slot = QString::number(Puzzle::slot(ws));
    m_objective = tr("Go to workspace %1 and keep your hands off the mouse for a moment: %2 will be dealt there.")
                      .arg(slot, pictureTitle(m_dealing));
    m_objectiveChord = chord(QString("Switch to workspace %1").arg(slot));
    if (m_activeWs != ws) {
        emit changed();
        return;
    }
    // Something of the player's may have arrived since the workspaces were
    // picked; then the game picks again rather than deal beside it.
    for (const auto &value : m_clients) {
        const auto c = value.toObject();
        if (c.value("pid").toInteger() != m_pid && c.value("workspace").toObject().value("id").toInt() == ws) {
            say(tr("Workspace %1 isn't empty any more, so Tessera looks for another.").arg(slot));
            clearPieces();
            setState("waiting");
            allocate();
            return;
        }
    }
    const QString layout = m_hyprctl->query("activeworkspace").toObject().value("tiledLayout").toString();
    if (!layout.isEmpty() && layout != "dwindle") {
        m_objective = tr("Workspace %1 uses the %2 layout, and Tessera's pictures are cut for dwindle. Cycle the "
                         "layout until it's dwindle.").arg(slot, layout);
        m_objectiveChord = chord("Cycle workspace layout");
        emit changed();
        return;
    }
    m_objective = tr("Dealing %1…").arg(pictureTitle(m_dealing));
    m_objectiveChord.clear();
    emit changed();
    if (m_queue.isEmpty())
        queueDeal(m_dealing);
    m_busy = true;
    runNext();
}

void Game::queueDeal(int picture) {
    const auto &spec = m_level.pictures.at(picture);
    auto idOf = [this](int p, int index) {
        if (p < 0)
            return QString("stray-%1").arg(index + 1);
        const auto &s = m_level.pictures.at(p);
        return QString("%1-%2").arg(s.art).arg(index + 1);
    };
    for (const auto &d : spec.tiles)
        m_queue << Action{Action::Spawn, idOf(d.picture, d.index)};
    for (Start start : {Start::Floating, Start::Scratchpad, Start::Spare})
        for (const auto &s : spec.pieces)
            if (s.start == start) {
                m_queue << Action{Action::Spawn, idOf(picture, s.index)};
                m_queue << Action{start == Start::Floating ? Action::Float
                                  : start == Start::Scratchpad ? Action::Scratchpad : Action::Spare,
                                  idOf(picture, s.index)};
            }
    bool first = true;
    for (const auto &s : spec.pieces)
        if (s.start == Start::Pile) {
            m_queue << Action{Action::Spawn, idOf(picture, s.index)};
            m_queue << Action{first ? Action::PileStart : Action::PileJoin, idOf(picture, s.index)};
            if (first)
                m_pileLeader = idOf(picture, s.index);
            first = false;
        }
}

void Game::afterSettle(std::function<void()> next) {
    QTimer::singleShot(m_timings.settle, this, [this, generation = m_generation, next] {
        if (generation == m_generation)
            next();
    });
}

void Game::verify(const QString &id, const std::function<bool(const QJsonObject &)> &ok, const QString &what,
                  std::function<void()> retry) {
    refresh();
    auto *row = m_pieces.find(id);
    const QJsonObject c = row ? client(row->address) : QJsonObject();
    if (!c.isEmpty() && ok(c)) {
        runNext();
        return;
    }
    if (retry) {
        retry();
        return;
    }
    fail(tr("Couldn't set up the puzzle: %1 didn't work for %2. Press R to try again.").arg(what, pieceName(id)));
}

void Game::runNext() {
    if (m_queue.isEmpty()) {
        m_busy = false;
        ++m_dealing;
        if (m_dealing < m_desk.pictures.size())
            dealPicture();
        else
            startPlaying();
        return;
    }
    const int ws = m_desk.pictures.at(m_dealing).workspace;
    const Action a = m_queue.first();
    if (a.kind == Action::Spawn) {
        refresh();
        // A new window opens where the player is looking, so dealing waits
        // while they're elsewhere.
        if (m_activeWs != ws) {
            m_busy = false;
            dealPicture();
            return;
        }
        m_queue.removeFirst();
        spawn(a.piece);
        return;
    }
    m_queue.removeFirst();
    QString address, error;
    if (!ownPiece(a.piece, &address)) {
        fail(tr("Couldn't find %1's window to set it up. Press R to try again.").arg(pieceName(a.piece)));
        return;
    }
    auto groupOf = [](const QJsonObject &c) {
        QStringList out;
        for (const auto &g : c.value("grouped").toArray())
            out << Hypr::address(g.toString());
        return out;
    };
    switch (a.kind) {
    case Action::Float:
        if (!m_hyprctl->setFloating(address, true, &error))
            return fail(tr("Couldn't float %1: %2. Press R to try again.").arg(pieceName(a.piece), error));
        afterSettle([=] {
            verify(a.piece, [](const QJsonObject &c) { return c.value("floating").toBool(); }, tr("Floating"));
        });
        break;
    case Action::Scratchpad:
    case Action::Spare: {
        const QString target = a.kind == Action::Scratchpad ? QString("special:scratchpad") : QString::number(m_spares.value(0));
        if (!m_hyprctl->moveSilently(address, target, &error))
            return fail(tr("Couldn't move %1 away: %2. Press R to try again.").arg(pieceName(a.piece), error));
        afterSettle([=] {
            verify(a.piece, [target](const QJsonObject &c) {
                const auto w = c.value("workspace").toObject();
                return w.value("name").toString() == target || QString::number(w.value("id").toInt()) == target;
            }, tr("Moving it away"));
        });
        break;
    }
    case Action::PileStart: {
        // Hyprland's group dispatchers act on the focused window, and Omarchy
        // never passes them a window, so a "window =" argument isn't trusted:
        // if it were ignored, the player's own window would be grouped. The
        // pile is only made once Hyprland says focus is on this very piece.
        auto grouped = [=](const QJsonObject &c) { return groupOf(c).contains(address); };
        QString e;
        if (!focusOwnPiece(a.piece, &e))
            return fail(tr("Couldn't stack the pile: %1 wouldn't take focus. Press R to try again.").arg(pieceName(a.piece)));
        if (!m_hyprctl->toggleFocusedGroup(&e))
            return fail(tr("Couldn't stack the pile: %1. Press R to try again.").arg(e));
        afterSettle([=] { verify(a.piece, grouped, tr("Making a pile")); });
        break;
    }
    case Action::PileJoin: {
        // A new window joins the focused group by itself (group:auto_group);
        // otherwise it is moved into the pile beside it, again only once it
        // has focus.
        QString leader;
        if (!ownPiece(m_pileLeader, &leader))
            return fail(tr("Couldn't find the pile. Press R to try again."));
        auto joined = [=](const QJsonObject &c) { return groupOf(c).contains(leader); };
        auto move = [=] {
            refresh();
            const QJsonObject me = client(address), them = client(leader);
            auto rect = [](const QJsonObject &c) {
                const auto at = c.value("at").toArray(), size = c.value("size").toArray();
                return QRectF(at.at(0).toDouble(), at.at(1).toDouble(), size.at(0).toDouble(), size.at(1).toDouble());
            };
            const char dir = Puzzle::direction(rect(me), rect(them));
            QString e;
            if (!focusOwnPiece(a.piece, &e))
                return fail(tr("Couldn't stack the pile: %1 wouldn't take focus. Press R to try again.").arg(pieceName(a.piece)));
            if (!m_hyprctl->moveFocusedIntoGroup(dir, &e))
                return fail(tr("Couldn't add %1 to the pile: %2. Press R to try again.").arg(pieceName(a.piece), e));
            afterSettle([=] { verify(a.piece, joined, tr("Adding to the pile")); });
        };
        verify(a.piece, joined, tr("Adding to the pile"), move);
        break;
    }
    case Action::Spawn:
        break;
    }
}

void Game::spawn(const QString &id) {
    const auto *p = m_desk.piece(id);
    if (!p)
        return;
    PieceModel::Row row;
    row.id = id;
    // Titles must be unique: they are how the game recognises each window
    // as it opens, before checking its process id.
    row.title = QString("Tessera · %1").arg(pieceName(id));
    row.label = pieceName(id);
    if (const auto *prep = preparedFor(p->art)) {
        row.image = prep->url();
        row.imageSize = prep->size;
    }
    row.cell = p->cell;
    m_titles.insert(id, row.title);
    m_pending = id;
    m_spawnTries = 0;
    m_pieces.add(row);
    m_spawnTimeout.start(m_timings.spawn);
}

void Game::bind(const QString &id, const QString &address) {
    auto *row = m_pieces.find(id);
    if (!row || !row->address.isEmpty() || m_pending != id)
        return;
    refresh();
    // Only ever a window of Tessera's own: the title alone could be anyone's.
    if (client(address).value("pid").toInteger() != m_pid || !pieceAt(address).isEmpty())
        return;
    row->address = address;
    m_pending.clear();
    m_spawnTimeout.stop();
    const int ws = m_desk.pictures.at(m_dealing).workspace;
    afterSettle([this, id, ws] {
        refresh();
        // Opened somewhere else after all (the player switched workspaces in
        // that moment): it's carried back before anything else happens.
        auto *r = m_pieces.find(id);
        const int at = r ? client(r->address).value("workspace").toObject().value("id").toInt() : 0;
        QString address, error;
        if (at != ws && ownPiece(id, &address) && !m_hyprctl->moveSilently(address, QString::number(ws), &error))
            return fail(tr("Couldn't put %1 on workspace %2: %3").arg(pieceName(id)).arg(Puzzle::slot(ws)).arg(error));
        runNext();
    });
}

// The openwindow event normally names each piece; if it was missed, the
// piece is found among the clients by Tessera's pid and its title.
void Game::bindFromClients() {
    if (m_pending.isEmpty())
        return;
    refresh();
    for (const auto &value : m_clients) {
        const auto c = value.toObject();
        if (c.value("pid").toInteger() == m_pid && c.value("title").toString() == m_titles.value(m_pending)) {
            bind(m_pending, Hypr::address(c.value("address").toString()));
            return;
        }
    }
    if (++m_spawnTries < 3) {
        m_spawnTimeout.start(m_timings.spawn);
        return;
    }
    fail(tr("Tessera's piece windows didn't show up on the desktop. Press R to try again."));
}

void Game::startPlaying() {
    m_started = m_now();
    // Practice holds the keys back a moment, as lessons do, so a move can be
    // made unaided; the timed puzzles show them at once.
    m_hintTier = m_level.lesson ? 0 : m_practice ? 1 : 2;
    m_askedTier = 0;
    m_objective = m_level.lesson || m_practice ? intro() : tr("Put every picture together, one on each workspace.");
    m_objectiveChord.clear();
    m_judged.clear();
    m_runMoves = m_runLooks = 0;
    m_par = Levels::par(m_level, m_area);
    m_helpAsked = false;
    m_hintKey.clear();
    m_situationSince = m_lastMoveAt = m_started;
    setState("playing");
    refresh();
    m_eval = Puzzle::evaluate(m_desk);
    // What's on the desktop now is where the player starts; the echoes of
    // dealing that may still be on their way are the game's own.
    m_detector.reset(snapshot(), m_now());
    m_detector.suppress(m_now() + 200);
    evaluate();
}

QSizeF Game::screenArea() {
    refresh();
    const auto monitors = m_hyprctl->query("monitors").toArray();
    const int id = client(m_board).value("monitor").toInt(-1);
    QJsonObject monitor;
    for (const auto &value : monitors) {
        const auto m = value.toObject();
        if (m.value("id").toInt(-2) == id || (monitor.isEmpty() && m.value("focused").toBool()))
            monitor = m;
    }
    if (monitor.isEmpty() && !monitors.isEmpty())
        monitor = monitors.first().toObject();
    m_scale = monitor.value("scale").toDouble(1);
    const QSizeF area = Collage::area(monitor);
    m_area = area.isEmpty() ? QSizeF(1920, 1054) : area;
    return m_area;
}

Commands::Snapshot Game::snapshot() {
    Commands::Snapshot s;
    s.desk = m_desk;
    for (const auto &row : m_pieces.rows())
        if (!row.address.isEmpty())
            s.pieceByAddress.insert(row.address, row.id);
    const QJsonObject cursor = m_hyprctl->query("cursorpos").toObject();
    if (cursor.contains("x") && cursor.contains("y")) {
        s.cursor = QPointF(cursor.value("x").toDouble(), cursor.value("y").toDouble());
        s.cursorKnown = true;
    }
    s.valid = true;
    return s;
}

void Game::judge(const Commands::Move &move, const Commands::Situation &before, const Commands::Situation &after) {
    if (move.family.isEmpty())
        return;
    const Commands::Verdict verdict = Commands::judge(move, before, after);
    const Hints::Hint *wanted = Commands::recommendation(before.hints);
    // Aided: its keys were on screen for exactly this move, or the player
    // asked for help since the last one.
    const bool shown = wanted && Commands::matches(move, *wanted) && !wanted->chord.isEmpty() && m_hintTier >= 2;
    const bool aided = shown || m_helpAsked;
    // From the moment the situation arose: the recommendation appeared, or
    // the last move was made, whichever is later.
    const qint64 reaction = std::max<qint64>(0, move.at - std::max(m_situationSince, m_lastMoveAt));
    m_stats.record(move, verdict, reaction, aided);
    m_judged << Judged{move, verdict, aided, reaction};
    const auto *f = Commands::family(move.family);
    if (f && f->look)
        ++m_runLooks;
    else
        ++m_runMoves;
    m_lastMoveAt = move.at;
    m_helpAsked = false;
    if (m_practice)
        m_askedTier = 0;
}

void Game::clearPieces() {
    for (const auto &r : m_pieces.rows())
        if (!r.address.isEmpty())
            m_dead.insert(r.address);
    m_pieces.clear();
    m_desk.windows.clear();
    m_titles.clear();
    m_pending.clear();
    m_pileLeader.clear();
    m_queue.clear();
    m_busy = false;
    m_dealing = -1;
    m_spawnTimeout.stop();
    ++m_generation;
}

// ── Playing ───────────────────────────────────────────────────────────────

void Game::evaluate() {
    if (m_state != "playing")
        return;
    const Commands::Situation before{m_desk, m_eval, m_hints};
    refresh();
    m_eval = Puzzle::evaluate(m_desk);
    const QList<Commands::Move> moves = m_detector.snapshot(snapshot(), m_now());
    if (!moves.isEmpty()) {
        const Commands::Situation after{m_desk, m_eval, {}};
        for (const auto &move : moves)
            judge(move, before, after);
    }
    for (const auto &row : m_pieces.rows())
        m_pieces.update(row.id, row.id == m_desk.focused, m_eval.pieces.value(row.id).inPlace);
    updateHints();
    emit changed();
    if (m_eval.complete())
        finish();
}

void Game::updateHints() {
    if (m_state != "playing")
        return;
    int tier = m_askedTier;
    const QList<Hints::Hint> before = m_hints;
    const int tierBefore = m_hintTier;
    const QString focusBefore = m_focusedPiece;
    m_hints = Hints::compute(
        m_desk, m_eval, [this](const QString &action) { return chord(action); },
        [this](const QString &id) { return pieceName(id); }, [this](int p) { return pictureTitle(p); });
    m_focusedPiece = m_desk.focused;
    // A new recommendation is a new situation: reaction times count from it.
    const Hints::Hint *wanted = Commands::recommendation(m_hints);
    const QString key = wanted ? wanted->action + '|' + wanted->family + '|' + wanted->piece : QString();
    if (key != m_hintKey) {
        m_hintKey = key;
        m_situationSince = m_now();
    }
    if (m_level.lesson) {
        const qint64 t = m_now() - m_started;
        tier = std::max(tier, t >= m_timings.tier3 ? 3 : t >= m_timings.tier2 ? 2 : t >= m_timings.tier1 ? 1 : 0);
        m_hintTier = std::max(m_hintTier, tier);
    } else if (m_practice) {
        // The keys for each new situation come after a moment.
        const qint64 waited = m_now() - std::max(m_situationSince, m_lastMoveAt);
        m_hintTier = std::max({tier, 1, waited >= m_timings.practiceKeys ? 2 : 1});
    } else {
        m_hintTier = std::max({m_hintTier, tier, 2});
    }
    auto same = [](const QList<Hints::Hint> &a, const QList<Hints::Hint> &b) {
        if (a.size() != b.size())
            return false;
        for (int i = 0; i < a.size(); ++i)
            if (a.at(i).text != b.at(i).text || a.at(i).chord != b.at(i).chord)
                return false;
        return true;
    };
    if (!same(before, m_hints) || tierBefore != m_hintTier || focusBefore != m_focusedPiece)
        emit changed();
}

// The keys back to the board's workspace, for pieces that celebrate far from
// it.
QString Game::boardChord() const {
    if (m_boardWs <= 0)
        return {};
    return chord(QString("Switch to workspace %1").arg(Puzzle::slot(m_boardWs)));
}

void Game::finish() {
    m_finished = m_now();
    Stats::Run run;
    run.level = m_level.id;
    run.lesson = m_level.lesson;
    run.practice = m_practice;
    run.when = QDateTime::currentMSecsSinceEpoch();
    run.ms = m_finished - m_started;
    run.moves = m_runMoves;
    run.par = m_par;
    run.looks = m_runLooks;
    m_stats.addRun(run);
    // The summary: moves against par, and the kinds of move that went best
    // and worst this time.
    QHash<QString, QPair<int, int>> tally; // family -> good, detour
    for (const auto &j : m_judged) {
        auto &t = tally[j.move.family];
        if (j.verdict == Commands::Verdict::Good)
            ++t.first;
        else if (j.verdict == Commands::Verdict::Detour)
            ++t.second;
    }
    QString best, worst;
    double bestScore = -1, worstScore = -1;
    for (const auto &f : Commands::families()) {
        const auto t = tally.value(f.id);
        if (t.first + t.second == 0)
            continue;
        const double good = double(t.first) / (t.first + t.second);
        if (t.first > 0 && (good > bestScore || (good == bestScore && t.first > tally.value(best).first))) {
            bestScore = good;
            best = f.id;
        }
        if (t.second > 0 && (1 - good > worstScore || (1 - good == worstScore && t.second > tally.value(worst).second))) {
            worstScore = 1 - good;
            worst = f.id;
        }
    }
    if (worst == best)
        worst.clear();
    m_summary = QVariantMap{{"moves", m_runMoves},
                            {"par", m_par},
                            {"looks", m_runLooks},
                            {"best", best.isEmpty() ? QString() : tr_("Game", Commands::family(best)->name)},
                            {"bestChord", best.isEmpty() ? QString() : familyChord(best)},
                            {"worst", worst.isEmpty() ? QString() : tr_("Game", Commands::family(worst)->name)},
                            {"worstChord", worst.isEmpty() ? QString() : familyChord(worst)}};
    saveStats();
    if (m_level.lesson) {
        m_stars = m_hintTier <= 1 ? 3 : m_hintTier == 2 ? 2 : 1;
        m_progress.setStars(m_level.id, m_stars);
        m_progress.setCurrent(m_levelIndex + 1 < Levels::lessons().size() ? Levels::lessons().at(m_levelIndex + 1).id
                                                                          : m_level.id);
    } else if (!m_practice) {
        m_newBest = m_progress.recordTime(m_level.id, m_finished - m_started);
    }
    save();
    setState("solved");
    emit tick();
    emit solved();
}

// ── Events ────────────────────────────────────────────────────────────────

void Game::handleEvent(const QString &name, const QString &data) {
    // The detector reads every event first, while the game's own idea of the
    // desktop still shows what it was before.
    if (m_state == "playing")
        m_detector.event(name, data, m_now());
    if (name == "openwindow") {
        const QStringList f = Hypr::fields(data, 4);
        if (!m_pending.isEmpty() && f.at(3) == m_titles.value(m_pending))
            bind(m_pending, Hypr::address(f.at(0)));
        return;
    }
    if (name == "closewindow") {
        const QString address = Hypr::address(data);
        if (m_dead.contains(address))
            return;
        const QString id = pieceAt(address);
        if (!id.isEmpty() && midPuzzle())
            fail(tr("%1's window was closed, so this puzzle can't be finished. Press R to deal it again.")
                     .arg(pieceName(id)));
        else if (!id.isEmpty())
            m_dead.insert(address);
        return;
    }
    if (name == "activewindowv2") {
        // Empty after every switch inside a group: the window focused a
        // moment ago is still the one that counts.
        const QString address = Hypr::address(data);
        if (!address.isEmpty())
            m_lastWindow = address;
    } else if (name == "workspacev2") {
        m_activeWs = Hypr::fields(data, 2).at(0).toInt();
    } else if (name == "activespecialv2") {
        m_scratchpadShown = Hypr::fields(data, 3).at(1).startsWith("special:scratchpad");
    } else if (name != "movewindowv2" && name != "changefloatingmode" && name != "togglegroup"
               && name != "moveintogroup" && name != "moveoutofgroup" && name != "fullscreen") {
        return;
    }
    if (m_state == "playing")
        m_soon.start();
}
