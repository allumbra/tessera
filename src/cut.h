#pragma once

#include <QHash>
#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>

// How a picture is cut. Hyprland's dwindle layout only ever splits one tile
// in two, so a cut that can't be made that way can't be solved either. Every
// picture is therefore cut as the shape dwindle itself grows: each split
// takes the tile left over by the one before and halves it again, a spiral
// of tiles. A picture of n pieces has n - 1 splits; piece k is the first
// (left or top) half of split k, and the last piece is the second half of
// the last split.
namespace Cut {

enum class Split { Side, Stack }; // Side: left | right. Stack: top / bottom.

struct Level {
    Split split = Split::Side;
    double ratio = 0.5; // the first half's share of the tile
};

struct Chain {
    QList<Level> levels;
    int pieces() const { return levels.size() + 1; }
    // Each piece's tile inside `area`, in piece order.
    QList<QRectF> layout(const QRectF &area = QRectF(0, 0, 1, 1)) const;
};

// The split dwindle picks by itself for a tile: side by side when the tile is
// wider than tall (`width_multiplier` 1), stacked otherwise.
Split natural(const QSizeF &tile);

// The splits a run of new windows gets when each one splits the window
// before it on a screen of this shape, all at half and half. A picture's
// pieces are dealt this way, so this is where every puzzle starts from.
Chain dealt(int pieces, const QSizeF &screen = QSizeF(16, 9));

struct Options {
    double flipChance = 0.0;  // a split turned the other way (Super+J)
    double ratioChance = 0.0; // a split away from half and half (resize)
};

// A cut of `pieces` pieces, the same for the same seed. Splits start out as
// dwindle would make them; some are turned or moved off centre by chance,
// but never so that a piece ends up thinner than a sliver.
Chain generate(int pieces, quint32 seed, const Options &options = {});

// The smallest share of the picture's width or height any piece takes.
double thinnest(const Chain &chain);

// A binary split tree read back from window rectangles. Dwindle leaves gaps
// between tiles, so two groups count as split apart when the gap between
// them is clear of every window.
struct Tree {
    struct Node {
        QString leaf; // the window, for a leaf
        Split split = Split::Side;
        double ratio = 0.5;
        int first = -1, second = -1, parent = -1;
        QRectF rect;
        bool isLeaf() const { return first < 0; }
    };
    QList<Node> nodes;
    int root = -1;

    bool valid() const { return root >= 0; }
    int find(const QString &leaf) const;
    // Leaves along the spine, the path of second halves from the root, plus
    // the last second half: the order of pieces when the tree is a chain.
    QStringList spine() const;
    // The spine's own nodes, from the root down.
    QList<int> spineNodes() const;
    // A chain is the spiral shape: every split's first half is one window.
    bool isChain() const;
    // The window at the end of the spiral, the last second half.
    QString end() const;
    // A window that is in the wrong branch: inside a first half that holds
    // more than one window, chosen beside a single sibling so that taking it
    // out shrinks that branch. Empty for a chain.
    QString misplaced() const;
};

Tree infer(const QHash<QString, QRectF> &rects);

} // namespace Cut
