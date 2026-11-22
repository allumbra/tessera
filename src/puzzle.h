#pragma once

#include <QHash>
#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <functional>

#include "cut.h"

// The state of a game on the desktop, in plain values: which pictures are
// being put together on which workspaces, where Hyprland has each piece's
// window right now, and what the player is looking at. Everything that
// decides whether a picture is done, and what to tell the player next, works
// on this, so the tests can build any situation without a desktop.
namespace Puzzle {

struct Picture {
    QString key;        // the artwork, see Art::find
    Cut::Chain chain;   // how it's cut
    int workspace = 0;  // where it has to be put together
};

struct Piece {
    QString id;          // "wave-2"
    int picture = -1;    // index into Desk::pictures; -1 for a stray piece
    int index = 0;       // its place in the cut
    QString art;         // the artwork the piece shows (a stray's own)
    QRectF cell;         // the slice of the artwork it shows, in 0..1
};

// One piece window as hyprctl -j clients reports it.
struct Window {
    QString address;
    int workspace = 0;
    bool special = false; // on a special workspace (the scratchpad)
    bool floating = false;
    QStringList group;    // the addresses of the group it's in, if any
    bool hidden = false;  // a group's tab that isn't showing
    QRectF rect;
    bool present = false; // mapped and known to Hyprland
};

struct Desk {
    QList<Picture> pictures;
    QList<Piece> pieces;
    QHash<QString, Window> windows;            // by piece id
    QHash<int, QList<QRectF>> foreign;         // other apps' tiled windows, by workspace
    int activeWorkspace = 0;
    bool scratchpadShown = false;
    QString focused;                            // the focused piece's id, if any
    double tolerance = 0.045;                   // of the picture's width and height

    const Piece *piece(const QString &id) const;
    // The piece ids tiled on a workspace, a group counting once under the
    // id of the piece that's showing.
    QHash<QString, QRectF> tiles(int workspace) const;
    // Every tile on a workspace, other apps' windows included (as
    // "foreign:N"), so the tree read from it is the one Hyprland has.
    QHash<QString, QRectF> allTiles(int workspace) const;
    QString groupLeaf(const QString &pieceId) const;
};

struct PieceState {
    bool home = false;    // on its picture's workspace
    bool tiled = false;   // tiled there, not floating or in a group
    bool inPlace = false; // and its tile matches its slice of the picture
    QRectF normalized;    // its tile inside the picture's area, in 0..1
};

struct PictureState {
    int placed = 0, total = 0;
    bool complete = false;
};

struct Evaluation {
    QHash<QString, PieceState> pieces;
    QList<PictureState> pictures;
    bool complete() const;
};

Evaluation evaluate(const Desk &desk);

// Which way to go from one rectangle to another: 'l', 'r', 'u' or 'd'.
char direction(const QRectF &from, const QRectF &to);

// The tile a directional shortcut from `id` lands on, among `tiles`.
QString neighbour(const QHash<QString, QRectF> &tiles, const QString &id, char dir);

// The first move of the shortest walk over neighbouring tiles from one tile
// to another, so that following directional shortcuts always gets there;
// the plain direction when there's no walk.
char stepToward(const QHash<QString, QRectF> &tiles, const QString &from, const QString &to);

// Workspaces as the player knows them: Super+1 to Super+0 on each monitor.
int slot(int workspace);

} // namespace Puzzle

// What to do next, worked out from what the player is looking at and which
// piece has focus. Each hint is one move: what it's for, and the user's own
// chord for it.
namespace Hints {

struct Hint {
    QString text;
    QString chord;
    QString detail; // the longer explanation behind the move
    QString piece;  // the piece it's about, if any
};

using Chords = std::function<QString(const QString &action)>;
using Names = std::function<QString(const QString &pieceId)>;
using Titles = std::function<QString(int picture)>;

QList<Hint> compute(const Puzzle::Desk &desk, const Puzzle::Evaluation &evaluation, const Chords &chords,
                    const Names &names, const Titles &titles);

} // namespace Hints
