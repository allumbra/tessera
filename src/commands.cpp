#include "commands.h"

#include <QLineF>
#include <QSet>
#include <QtGlobal>
#include <cmath>

#include "hyprland.h"

using namespace Commands;

const QList<Family> &Commands::families() {
    static const QList<Family> all = [] {
        QStringList workspaces, carries;
        for (int n = 1; n <= 10; ++n) {
            workspaces << QString("Switch to workspace %1").arg(n);
            carries << QString("Move window to workspace %1").arg(n);
        }
        auto sized = [](const QStringList &base) {
            QStringList out;
            for (const QString &b : base)
                out << b << b + " a little" << b + " a lot";
            return out;
        };
        // Expected: roughly how often a puzzle needs each, so the stats can
        // say that a swap used ten times as often as a float is normal.
        return QList<Family>{
            {"focus", QT_TRANSLATE_NOOP("Game", "Focus a window"),
             {"Focus on left window", "Focus on right window", "Focus on above window", "Focus on below window"}, 10, true},
            {"swap", QT_TRANSLATE_NOOP("Game", "Swap windows"),
             {"Swap window to the left", "Swap window to the right", "Swap window up", "Swap window down"}, 8, false},
            {"workspace", QT_TRANSLATE_NOOP("Game", "Go to a workspace"), workspaces, 3, true},
            {"move-workspace", QT_TRANSLATE_NOOP("Game", "Carry a window to a workspace"), carries, 2, false},
            {"split", QT_TRANSLATE_NOOP("Game", "Turn the split"), {"Toggle window split"}, 1.5, false},
            {"float", QT_TRANSLATE_NOOP("Game", "Float or tile"), {"Toggle window floating/tiling"}, 1, false},
            {"resize-x", QT_TRANSLATE_NOOP("Game", "Move the line left or right"),
             sized({"Expand window left", "Shrink window left"}), 2, false},
            {"resize-y", QT_TRANSLATE_NOOP("Game", "Move the line up or down"),
             sized({"Shrink window up", "Expand window down"}), 2, false},
            {"group-out", QT_TRANSLATE_NOOP("Game", "Pop out of a pile"), {"Move active window out of group"}, 2, false},
            {"group-next", QT_TRANSLATE_NOOP("Game", "Next piece in a pile"), {"Next window in group"}, 1, true},
            {"group-in", QT_TRANSLATE_NOOP("Game", "Tuck into a neighbour's pile"),
             {"Move window to group on left", "Move window to group on right", "Move window to group on top",
              "Move window to group on bottom"}, 0.5, false},
            {"group", QT_TRANSLATE_NOOP("Game", "Make or break a pile"), {"Toggle window grouping"}, 0.5, false},
            {"scratchpad", QT_TRANSLATE_NOOP("Game", "Show or hide the scratchpad"), {"Toggle scratchpad"}, 1, true},
            {"to-scratchpad", QT_TRANSLATE_NOOP("Game", "Put a window in the scratchpad"), {"Move window to scratchpad"},
             1, false},
            {"focus-next", QT_TRANSLATE_NOOP("Game", "Focus the next window"), {"Focus on next window"}, 1, true},
        };
    }();
    return all;
}

const Family *Commands::family(const QString &id) {
    for (const auto &f : families())
        if (f.id == id)
            return &f;
    return nullptr;
}

QString Commands::familyOf(const QString &action) {
    for (const auto &f : families())
        if (f.actions.contains(action))
            return f.id;
    return {};
}

QString Commands::baseAction(const QString &action) {
    for (const char *suffix : {" a little", " a lot"})
        if (action.endsWith(QLatin1String(suffix)))
            return action.left(action.size() - int(qstrlen(suffix)));
    return action;
}

static QString focusAction(char d) {
    switch (d) {
    case 'l': return "Focus on left window";
    case 'r': return "Focus on right window";
    case 'u': return "Focus on above window";
    default: return "Focus on below window";
    }
}

static QString swapAction(char d) {
    switch (d) {
    case 'l': return "Swap window to the left";
    case 'r': return "Swap window to the right";
    case 'u': return "Swap window up";
    default: return "Swap window down";
    }
}

static QString groupAction(char d) {
    switch (d) {
    case 'l': return "Move window to group on left";
    case 'r': return "Move window to group on right";
    case 'u': return "Move window to group on top";
    default: return "Move window to group on bottom";
    }
}

// ── Detecting ─────────────────────────────────────────────────────────────

void Detector::reset(const Snapshot &now, qint64 time) {
    m_last = now;
    m_pending.clear();
    m_workspace = now.desk.activeWorkspace;
    const QString focused = now.desk.focused;
    m_focus.clear();
    for (auto it = now.pieceByAddress.begin(); it != now.pieceByAddress.end(); ++it)
        if (it.value() == focused)
            m_focus = it.key();
    m_echoUntil = time;
}

void Detector::suppress(qint64 until) {
    m_suppress = std::max(m_suppress, until);
    m_pending.clear();
}

void Detector::add(const Move &move, bool pointerCheck, bool groupDirection) {
    Pending p;
    p.move = move;
    p.move.family = familyOf(move.action);
    p.pointerCheck = pointerCheck;
    p.groupDirection = groupDirection;
    m_pending << p;
}

void Detector::event(const QString &name, const QString &data, qint64 time) {
    if (name == "activewindowv2") {
        const QString address = Hypr::address(data);
        // Empty after every switch inside a group, and the same window again
        // when only its title changed: neither is a move.
        if (address.isEmpty() || address == m_focus)
            return;
        const QString previous = m_focus;
        m_focus = address;
        if (time < m_suppress || time <= m_echoUntil)
            return;
        const QString to = pieceOf(address), from = pieceOf(previous);
        // Only moves between Tessera's own pieces are read; from the board
        // or another app the direction can't be told.
        if (to.isEmpty() || from.isEmpty())
            return;
        const Puzzle::Window a = m_last.desk.windows.value(from), b = m_last.desk.windows.value(to);
        Move m;
        m.piece = to;
        m.at = time;
        if (a.group.contains(address))
            m.action = "Next window in group";
        else if (a.present && b.present && !a.floating && !b.floating && !a.special && !b.special
                 && a.workspace == b.workspace && a.rect.isValid() && b.rect.isValid())
            m.action = focusAction(Puzzle::direction(a.rect, b.rect));
        else
            m.action = "Focus on next window";
        add(m, true);
        return;
    }
    if (name == "workspacev2") {
        const int id = Hypr::fields(data, 2).at(0).toInt();
        if (id == m_workspace)
            return;
        m_workspace = id;
        // The focus that comes with a new workspace is the switch's.
        m_echoUntil = time + Echo;
        if (time < m_suppress || id <= 0)
            return;
        // Carrying a window along (Super+Shift+N follows it) is one move.
        if (time - m_movedAt <= Echo && m_movedTo == id)
            return;
        Move m;
        m.action = QString("Switch to workspace %1").arg(Puzzle::slot(id));
        m.at = time;
        add(m, true);
        return;
    }
    if (name == "openwindow" || name == "closewindow") {
        m_echoUntil = time + Echo;
        return;
    }
    if (time < m_suppress)
        return;
    if (name == "movewindowv2") {
        const QStringList f = Hypr::fields(data, 3);
        const QString piece = pieceOf(Hypr::address(f.at(0)));
        if (piece.isEmpty())
            return;
        const int ws = f.at(1).toInt();
        const QString wsName = f.at(2);
        // A group moves as one, with an event per member.
        const bool same = time - m_movedAt <= Echo && wsName == m_movedName;
        m_movedAt = time;
        m_movedTo = ws;
        m_movedName = wsName;
        m_echoUntil = time + Echo;
        if (same)
            return;
        Move m;
        m.piece = piece;
        m.at = time;
        m.action = wsName.startsWith("special:") ? QString("Move window to scratchpad")
                                                 : QString("Move window to workspace %1").arg(Puzzle::slot(ws));
        add(m, false);
        return;
    }
    QString action;
    bool groupDirection = false;
    QString address;
    if (name == "changefloatingmode") {
        action = "Toggle window floating/tiling";
        address = Hypr::fields(data, 2).at(0);
    } else if (name == "togglegroup") {
        action = "Toggle window grouping";
        address = Hypr::fields(data, 3).at(1);
    } else if (name == "moveintogroup") {
        action = "Move window to group on left"; // the side is read from the next snapshot
        groupDirection = true;
        address = data;
    } else if (name == "moveoutofgroup") {
        action = "Move active window out of group";
        address = data;
    } else if (name == "activespecialv2" || name == "activespecial") {
        // Hyprland sends both forms of this one; it counts once.
        if (time - m_scratchAt <= Echo)
            return;
        m_scratchAt = time;
        m_echoUntil = time + Echo;
        Move m;
        m.action = "Toggle scratchpad";
        m.at = time;
        add(m, false);
        return;
    } else {
        return;
    }
    const QString piece = pieceOf(Hypr::address(address));
    if (piece.isEmpty())
        return;
    m_echoUntil = time + Echo;
    Move m;
    m.action = action;
    m.piece = piece;
    m.at = time;
    add(m, false, groupDirection);
}

static QRectF bounds(const QList<QRectF> &rects) {
    QRectF out;
    for (const QRectF &r : rects)
        out = out.isNull() ? r : out.united(r);
    return out;
}

static bool near(const QRectF &a, const QRectF &b, double slack = 3) {
    return std::abs(a.left() - b.left()) <= slack && std::abs(a.top() - b.top()) <= slack
           && std::abs(a.right() - b.right()) <= slack && std::abs(a.bottom() - b.bottom()) <= slack;
}

QList<Move> Detector::snapshot(const Snapshot &now, qint64 time) {
    QList<Move> out;
    if (!m_last.valid || time < m_suppress) {
        // The game was acting: whatever changed was its doing.
        m_pending.clear();
        m_last = now;
        return out;
    }

    // Hyprland warps the pointer to the middle of a window it focuses from
    // the keyboard; a pointer that moved and rests anywhere else moved it.
    bool mouse = false;
    if (now.cursorKnown && m_last.cursorKnown && QLineF(now.cursor, m_last.cursor).length() > 2) {
        const Puzzle::Window f = now.desk.windows.value(now.desk.focused);
        mouse = !(f.present && f.rect.isValid() && QLineF(now.cursor, f.rect.center()).length() <= 8);
    }

    for (const Pending &p : std::as_const(m_pending)) {
        Move m = p.move;
        if (p.pointerCheck && mouse) {
            ++m_mouse;
            continue;
        }
        if (p.groupDirection) {
            // The side it went: towards where the group it joined was.
            const Puzzle::Window w = now.desk.windows.value(m.piece), before = m_last.desk.windows.value(m.piece);
            QRectF group;
            for (auto it = m_last.desk.windows.begin(); it != m_last.desk.windows.end(); ++it)
                if (it.key() != m.piece && w.group.contains(it->address) && it->rect.isValid())
                    group = it->rect;
            if (before.rect.isValid() && group.isValid())
                m.action = groupAction(Puzzle::direction(before.rect, group));
        }
        m.family = familyOf(m.action);
        out << m;
    }
    m_pending.clear();

    // Geometry: on each workspace whose tiles are the same windows as before,
    // what changed shape.
    QSet<int> spaces;
    for (const auto &w : now.desk.windows)
        if (w.present && !w.special)
            spaces.insert(w.workspace);
    for (int ws : spaces) {
        const auto before = m_last.desk.tiles(ws), after = now.desk.tiles(ws);
        if (before.size() < 2 || before.keys().size() != after.keys().size())
            continue;
        bool sameLeaves = true;
        for (auto it = before.begin(); it != before.end(); ++it)
            sameLeaves = sameLeaves && after.contains(it.key());
        if (!sameLeaves)
            continue;
        QHash<QString, QRectF> changedBefore, changedAfter;
        for (auto it = before.begin(); it != before.end(); ++it)
            if (!near(it.value(), after.value(it.key()))) {
                changedBefore.insert(it.key(), it.value());
                changedAfter.insert(it.key(), after.value(it.key()));
            }
        if (changedBefore.size() < 2)
            continue;
        if (mouse) {
            ++m_mouse;
            continue;
        }
        // The changed tiles cover the same ground before and after: one split
        // node of the tree, turned, traded or moved.
        if (!near(bounds(changedBefore.values()), bounds(changedAfter.values()), 6))
            continue;
        const QString focusLeaf = m_last.desk.groupLeaf(m_last.desk.focused);
        Move m;
        m.at = time;
        if (changedBefore.size() == 2) {
            const QString a = changedBefore.keys().at(0), b = changedBefore.keys().at(1);
            if (near(changedAfter.value(a), changedBefore.value(b), 6) && near(changedAfter.value(b), changedBefore.value(a), 6)) {
                const QString mover = changedBefore.contains(focusLeaf) ? focusLeaf : a;
                m.piece = mover;
                m.action = swapAction(Puzzle::direction(changedBefore.value(mover), changedAfter.value(mover)));
                m.family = "swap";
                out << m;
                continue;
            }
        }
        const Cut::Tree tb = Cut::infer(changedBefore), ta = Cut::infer(changedAfter);
        if (!tb.valid() || !ta.valid() || tb.nodes.at(tb.root).isLeaf() || ta.nodes.at(ta.root).isLeaf())
            continue;
        const auto &rb = tb.nodes.at(tb.root), &ra = ta.nodes.at(ta.root);
        m.piece = changedBefore.contains(focusLeaf) ? focusLeaf : changedBefore.keys().first();
        if (rb.split != ra.split) {
            m.action = "Toggle window split";
            m.family = "split";
            out << m;
            continue;
        }
        const bool side = rb.split == Cut::Split::Side;
        const double lineBefore = side ? rb.rect.left() + rb.ratio * rb.rect.width() : rb.rect.top() + rb.ratio * rb.rect.height();
        const double lineAfter = side ? ra.rect.left() + ra.ratio * ra.rect.width() : ra.rect.top() + ra.ratio * ra.rect.height();
        const double d = lineAfter - lineBefore;
        if (std::abs(d) < 3)
            continue;
        // Omarchy's resize keys move the line by 25 (Alt), 100, or 300 (Ctrl)
        // pixels; several presses inside one poll read as one longer move.
        const double size = std::abs(d);
        const QString amount = size <= 50 ? " a little" : size >= 220 ? " a lot" : "";
        m.action = (side ? (d < 0 ? "Expand window left" : "Shrink window left")
                         : (d < 0 ? "Shrink window up" : "Expand window down")) + amount;
        m.family = side ? "resize-x" : "resize-y";
        m.pixels = size;
        out << m;
    }
    m_last = now;
    return out;
}

// ── Judging ───────────────────────────────────────────────────────────────

const Hints::Hint *Commands::recommendation(const QList<Hints::Hint> &hints) {
    for (const auto &h : hints)
        if (!h.action.isEmpty() || !h.family.isEmpty())
            return &h;
    return nullptr;
}

bool Commands::matches(const Move &move, const Hints::Hint &hint) {
    if (!hint.action.isEmpty())
        return baseAction(move.action) == baseAction(hint.action);
    return !hint.family.isEmpty() && move.family == hint.family;
}

Verdict Commands::judge(const Move &move, const Situation &before, const Situation &after) {
    const Hints::Hint *wanted = recommendation(before.hints);
    if (wanted && matches(move, *wanted))
        return Verdict::Good;
    const int delta = Puzzle::progress(after.desk, after.evaluation) - Puzzle::progress(before.desk, before.evaluation);
    if (delta > 0)
        return Verdict::Good;
    if (delta < 0)
        return Verdict::Detour;
    const Family *f = family(move.family);
    if (!f || !f->look)
        return Verdict::Detour;
    if (!wanted || wanted->piece.isEmpty())
        return Verdict::Neutral;
    // A look around: fine if it brings the piece the hint is about closer.
    const QString target = wanted->piece;
    if (after.desk.focused == target && before.desk.focused != target)
        return Verdict::Good;
    const int was = Puzzle::distance(before.desk, before.desk.focused, target);
    const int is = Puzzle::distance(after.desk, after.desk.focused, target);
    if (was >= 0 && is >= 0 && is < was)
        return Verdict::Neutral;
    const Puzzle::Window t = after.desk.windows.value(target);
    if (move.family == "workspace" && t.present && !t.special && t.workspace == after.desk.activeWorkspace
        && before.desk.activeWorkspace != t.workspace)
        return Verdict::Neutral;
    if ((move.family == "scratchpad") && t.present && t.special && after.desk.scratchpadShown)
        return Verdict::Neutral;
    return Verdict::Detour;
}
