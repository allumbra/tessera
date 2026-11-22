#include "cut.h"

#include <QRandomGenerator>
#include <QSizeF>
#include <algorithm>
#include <cmath>

using namespace Cut;

QList<QRectF> Chain::layout(const QRectF &area) const {
    QList<QRectF> out;
    QRectF rest = area;
    for (const Level &level : levels) {
        QRectF first = rest;
        if (level.split == Split::Side) {
            first.setWidth(rest.width() * level.ratio);
            rest.setLeft(first.right());
        } else {
            first.setHeight(rest.height() * level.ratio);
            rest.setTop(first.bottom());
        }
        out << first;
    }
    out << rest;
    return out;
}

Split Cut::natural(const QSizeF &tile) {
    return tile.width() > tile.height() ? Split::Side : Split::Stack;
}

Chain Cut::dealt(int pieces, const QSizeF &screen) {
    Chain chain;
    QSizeF rest = screen;
    for (int i = 0; i + 1 < pieces; ++i) {
        const Split split = natural(rest);
        chain.levels << Level{split, 0.5};
        if (split == Split::Side)
            rest.setWidth(rest.width() / 2);
        else
            rest.setHeight(rest.height() / 2);
    }
    return chain;
}

double Cut::thinnest(const Chain &chain) {
    double least = 1;
    for (const QRectF &r : chain.layout())
        least = std::min({least, r.width(), r.height()});
    return least;
}

Chain Cut::generate(int pieces, quint32 seed, const Options &options) {
    QRandomGenerator random(seed);
    static const double ratios[] = {1.0 / 3, 0.4, 0.6, 2.0 / 3};
    // A handful of tries is plenty to find one with no sliver in it; the
    // plain dealt shape is always acceptable, so it's the last resort.
    for (int attempt = 0; attempt < 64; ++attempt) {
        Chain chain = dealt(pieces);
        for (Level &level : chain.levels) {
            if (random.generateDouble() < options.flipChance)
                level.split = level.split == Split::Side ? Split::Stack : Split::Side;
            if (random.generateDouble() < options.ratioChance)
                level.ratio = ratios[random.bounded(4)];
        }
        // A sixth of the picture is the narrowest a tile gets while there
        // are few pieces; long spirals are allowed to get a bit thinner.
        if (thinnest(chain) >= (pieces <= 4 ? 0.16 : 0.11))
            return chain;
    }
    return dealt(pieces);
}

// ── Reading a tree back from rectangles ────────────────────────────────────

int Tree::find(const QString &leaf) const {
    for (int i = 0; i < nodes.size(); ++i)
        if (nodes.at(i).isLeaf() && nodes.at(i).leaf == leaf)
            return i;
    return -1;
}

QList<int> Tree::spineNodes() const {
    QList<int> out;
    for (int n = root; n >= 0 && !nodes.at(n).isLeaf(); n = nodes.at(n).second)
        out << n;
    return out;
}

QStringList Tree::spine() const {
    QStringList out;
    const QList<int> spineList = spineNodes();
    for (int n : spineList) {
        const Node &first = nodes.at(nodes.at(n).first);
        out << (first.isLeaf() ? first.leaf : QString());
    }
    if (!spineList.isEmpty())
        out << nodes.at(nodes.at(spineList.last()).second).leaf;
    else if (root >= 0)
        out << nodes.at(root).leaf;
    return out;
}

bool Tree::isChain() const {
    if (!valid())
        return false;
    for (int n : spineNodes())
        if (!nodes.at(nodes.at(n).first).isLeaf())
            return false;
    return true;
}

QString Tree::end() const {
    const QStringList s = spine();
    return s.isEmpty() ? QString() : s.last();
}

QString Tree::misplaced() const {
    for (int n : spineNodes()) {
        const int first = nodes.at(n).first;
        if (nodes.at(first).isLeaf())
            continue;
        // The deepest split in this branch has two single windows; taking
        // either out folds that split away.
        int deepest = first;
        QList<int> todo{first};
        int bestDepth = -1;
        while (!todo.isEmpty()) {
            const int i = todo.takeLast();
            const Node &node = nodes.at(i);
            if (node.isLeaf())
                continue;
            int depth = 0;
            for (int p = i; p != first; p = nodes.at(p).parent)
                ++depth;
            if (nodes.at(node.first).isLeaf() && nodes.at(node.second).isLeaf() && depth > bestDepth) {
                bestDepth = depth;
                deepest = i;
            }
            todo << node.first << node.second;
        }
        if (bestDepth >= 0)
            return nodes.at(nodes.at(deepest).second).leaf;
    }
    return {};
}

namespace {

struct Item {
    QString id;
    QRectF rect;
};

// Splits a set of windows along one axis wherever no window crosses the
// line. Returns the candidate cuts as the number of windows before them.
QList<int> cuts(QList<Item> &items, bool side) {
    std::sort(items.begin(), items.end(), [side](const Item &a, const Item &b) {
        return side ? a.rect.left() < b.rect.left() : a.rect.top() < b.rect.top();
    });
    QList<int> out;
    double reach = -1e9;
    // Dwindle's tiles never overlap; a couple of pixels of slack absorbs
    // rounding at fractional scales.
    const double slack = 3;
    for (int i = 0; i + 1 < items.size(); ++i) {
        reach = std::max(reach, side ? items.at(i).rect.right() : items.at(i).rect.bottom());
        double next = 1e9;
        for (int j = i + 1; j < items.size(); ++j)
            next = std::min(next, side ? items.at(j).rect.left() : items.at(j).rect.top());
        if (reach <= next + slack)
            out << i + 1;
    }
    return out;
}

int build(Tree &tree, QList<Item> items, int parent) {
    const int index = tree.nodes.size();
    tree.nodes.append(Tree::Node{});
    tree.nodes[index].parent = parent;
    QRectF bounds;
    for (const auto &item : items)
        bounds = bounds.isNull() ? item.rect : bounds.united(item.rect);
    tree.nodes[index].rect = bounds;
    if (items.size() == 1) {
        tree.nodes[index].leaf = items.first().id;
        return index;
    }
    // Of every line that splits the windows cleanly, the one with the fewest
    // windows before it reads the tree the way dwindle grows it, as a spiral
    // where it can be one.
    QList<Item> bySide = items, byStack = items;
    const QList<int> sideCuts = cuts(bySide, true), stackCuts = cuts(byStack, false);
    if (sideCuts.isEmpty() && stackCuts.isEmpty())
        return -1;
    const bool side = !sideCuts.isEmpty() && (stackCuts.isEmpty() || sideCuts.first() <= stackCuts.first());
    const QList<Item> &sorted = side ? bySide : byStack;
    const int at = side ? sideCuts.first() : stackCuts.first();
    const QList<Item> first = sorted.mid(0, at), second = sorted.mid(at);
    QRectF firstBounds;
    for (const auto &item : first)
        firstBounds = firstBounds.isNull() ? item.rect : firstBounds.united(item.rect);
    QRectF secondBounds;
    for (const auto &item : second)
        secondBounds = secondBounds.isNull() ? item.rect : secondBounds.united(item.rect);
    tree.nodes[index].split = side ? Split::Side : Split::Stack;
    // The split line sits in the middle of the gap between the halves.
    const double line = side ? (firstBounds.right() + secondBounds.left()) / 2
                             : (firstBounds.bottom() + secondBounds.top()) / 2;
    const double extent = side ? bounds.width() : bounds.height();
    tree.nodes[index].ratio = extent > 0 ? (line - (side ? bounds.left() : bounds.top())) / extent : 0.5;
    const int a = build(tree, first, index);
    const int b = build(tree, second, index);
    if (a < 0 || b < 0)
        return -1;
    tree.nodes[index].first = a;
    tree.nodes[index].second = b;
    return index;
}

} // namespace

Tree Cut::infer(const QHash<QString, QRectF> &rects) {
    Tree tree;
    if (rects.isEmpty())
        return tree;
    QList<Item> items;
    for (auto it = rects.begin(); it != rects.end(); ++it)
        items << Item{it.key(), it.value()};
    // Sorted first, so the same windows always read back the same way.
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.id < b.id; });
    const int root = build(tree, items, -1);
    if (root < 0)
        return {};
    tree.root = root;
    return tree;
}
