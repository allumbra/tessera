#pragma once

#include <QHash>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>

#include "puzzle.h"

// Which window shortcut the player just used, worked out from what it did.
// Hyprland takes every Super chord before an app sees it, so Tessera can't
// read the keys; it reads their effects instead:
//
// - from Hyprland's events: a workspace switch (workspacev2), carrying a
//   window to a workspace or the scratchpad (movewindowv2), floating and
//   tiling (changefloatingmode), grouping (togglegroup), going into and out
//   of a group (moveintogroup, moveoutofgroup), the scratchpad shown or hidden
//   (activespecialv2), and focus (activewindowv2), where the empty event that
//   follows a switch inside a group and a window re-sent for a new title are
//   both ignored;
// - from two snapshots of the windows a poll apart: a swap (two tiles traded
//   places), a turned split (the same tiles, the other way), and a resize (a
//   split line moved, how far and which way).
//
// A focus that only follows something else (a workspace switch, a window
// arriving) isn't a command of its own. Neither is anything while the game
// itself is acting on its windows. A change made with the mouse is left out
// where it shows: the pointer moved between the snapshots and isn't resting
// where Hyprland warps it after a keyboard focus change, the middle of the
// focused window.
namespace Commands {

struct Family {
    QString id;
    const char *name;      // translatable, in the "Game" context's wording
    QStringList actions;   // the Omarchy actions it covers
    double expected;       // how often a puzzle calls for it, relatively
    bool look;             // it changes what's in view, never where a piece is
};

const QList<Family> &families();
const Family *family(const QString &id);
// The family an Omarchy action belongs to ("Swap window up" → "swap").
QString familyOf(const QString &action);
// The action without its size: "Expand window left a lot" → "Expand window left".
QString baseAction(const QString &action);

struct Move {
    QString family;
    QString action;  // the Omarchy action, as specific as the effect shows
    QString piece;   // the piece it acted on or focused
    qint64 at = 0;
    double pixels = 0; // how far a resize moved the line
};

struct Snapshot {
    Puzzle::Desk desk;                       // windows by piece id, focus, workspace
    QHash<QString, QString> pieceByAddress;  // the game's own piece windows
    QPointF cursor;
    bool cursorKnown = false;
    bool valid = false;
};

class Detector {
public:
    // Starts afresh from what's on the desktop now.
    void reset(const Snapshot &now, qint64 time);
    // Nothing until `until` is the player's: the game is acting.
    void suppress(qint64 until);

    // One Hyprland event, as it arrives.
    void event(const QString &name, const QString &data, qint64 time);
    // A fresh look at the desktop: the moves found since the last one.
    QList<Move> snapshot(const Snapshot &now, qint64 time);

    int mouseChanges() const { return m_mouse; }
    // How long after a structural event a focus change counts as its echo.
    static constexpr qint64 Echo = 150;

private:
    struct Pending {
        Move move;
        bool pointerCheck = false; // dropped if the mouse did it
        bool groupDirection = false;
    };
    void add(const Move &move, bool pointerCheck, bool groupDirection = false);
    QString pieceOf(const QString &address) const { return m_last.pieceByAddress.value(address); }

    Snapshot m_last;
    QString m_focus;       // the last window with focus, never empty
    int m_workspace = 0;
    qint64 m_suppress = 0, m_echoUntil = 0, m_movedAt = -1000000, m_scratchAt = -1000000;
    int m_movedTo = 0;
    QString m_movedName;
    QList<Pending> m_pending;
    int m_mouse = 0;
};

enum class Verdict { Good, Neutral, Detour };

struct Situation {
    Puzzle::Desk desk;
    Puzzle::Evaluation evaluation;
    QList<Hints::Hint> hints;
};

// The hint that was the recommendation: the first that names a move.
const Hints::Hint *recommendation(const QList<Hints::Hint> &hints);
// Whether a move is the one a hint asked for (a resize of any size in the
// asked direction counts).
bool matches(const Move &move, const Hints::Hint &hint);

// Good: what the hint recommended, or measurable progress. Neutral: a look
// around that brings the needed piece closer, or any look when there was no
// recommendation. Detour: anything that undid progress or didn't help.
Verdict judge(const Move &move, const Situation &before, const Situation &after);

} // namespace Commands
