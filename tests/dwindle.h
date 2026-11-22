#pragma once

// A model of Hyprland's dwindle layout for one workspace, for the tests and
// the screenshot harness. It follows the rules Tessera relies on:
//
// - A window entering the layout splits one tile (the caller says which:
//   Hyprland uses the focused window, or the one under the pointer). The
//   tile splits along its longer side and the newcomer takes the second
//   half (force_split = 2), and the split keeps its direction afterwards
//   (preserve_split).
// - Leaving folds the split away; the sibling takes the whole tile back.
// - Swapping exchanges two windows' tiles; toggling the split turns the
//   window's own split; a keyboard resize moves the nearest split of the
//   matching direction by the given number of pixels.
//
// Gaps and borders inset every tile, as on Omarchy (gaps 5 inside, 10
// outside, border 2).

#include <QHash>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <memory>

class Dwindle {
public:
    explicit Dwindle(const QRectF &area = QRectF(0, 26, 1920, 1054)) : m_area(area) {}

    double gapIn = 5, gapOut = 10, border = 2;

    bool contains(const QString &id) const { return leaf(id) != nullptr; }
    int count() const { return windows().size(); }

    QStringList windows() const {
        QStringList out;
        collect(m_root.get(), out);
        return out;
    }

    // The last tile of the spiral, where dwindle puts a window when nothing
    // else says where.
    QString end() const {
        Node *n = m_root.get();
        while (n && !n->isLeaf())
            n = n->b.get();
        return n ? n->window : QString();
    }

    void insert(const QString &id, const QString &beside = {}) {
        if (contains(id))
            return;
        layout();
        auto fresh = std::make_unique<Node>();
        fresh->window = id;
        if (!m_root) {
            m_root = std::move(fresh);
            layout();
            return;
        }
        Node *target = leaf(beside.isEmpty() ? end() : beside);
        if (!target)
            target = leaf(end());
        // The tile becomes a split: the window that was there keeps the
        // first half, the newcomer gets the second.
        auto old = std::make_unique<Node>();
        old->window = target->window;
        target->window.clear();
        target->side = target->box.width() > target->box.height();
        target->ratio = 0.5;
        old->parent = target;
        fresh->parent = target;
        target->a = std::move(old);
        target->b = std::move(fresh);
        layout();
    }

    void remove(const QString &id) {
        Node *n = leaf(id);
        if (!n)
            return;
        Node *parent = n->parent;
        if (!parent) {
            m_root.reset();
            return;
        }
        std::unique_ptr<Node> sibling = std::move(parent->a.get() == n ? parent->b : parent->a);
        Node *grand = parent->parent;
        sibling->parent = grand;
        if (!grand)
            m_root = std::move(sibling);
        else if (grand->a.get() == parent)
            grand->a = std::move(sibling);
        else
            grand->b = std::move(sibling);
        layout();
    }

    void swap(const QString &a, const QString &b) {
        Node *x = leaf(a), *y = leaf(b);
        if (x && y)
            std::swap(x->window, y->window);
        layout();
    }

    void rename(const QString &from, const QString &to) {
        if (Node *n = leaf(from))
            n->window = to;
    }

    bool toggleSplit(const QString &id) {
        Node *n = leaf(id);
        if (!n || !n->parent)
            return false;
        n->parent->side = !n->parent->side;
        layout();
        return true;
    }

    void resize(const QString &id, double dx, double dy) {
        Node *n = leaf(id);
        if (!n)
            return;
        auto move = [&](bool side, double delta) {
            if (delta == 0)
                return;
            for (Node *p = n->parent; p; p = p->parent) {
                if (p->side != side)
                    continue;
                const double extent = side ? p->box.width() : p->box.height();
                p->ratio = std::clamp(p->ratio + delta / extent, 0.05, 0.95);
                return;
            }
        };
        move(true, dx);
        move(false, dy);
        layout();
    }

    // Window geometry as hyprctl reports it: the tile minus gaps and border.
    QHash<QString, QRectF> rects() const {
        QHash<QString, QRectF> out;
        rectsOf(m_root.get(), out);
        return out;
    }

    // The window a directional move from `id` lands on: one sharing that
    // edge, with the longest stretch of it.
    QString neighbour(const QString &id, char dir) const {
        const auto all = rects();
        if (!all.contains(id))
            return {};
        const QRectF me = all.value(id);
        const double reach = 2 * (gapIn + border) + 2;
        QString best;
        double bestOverlap = 0;
        for (auto it = all.begin(); it != all.end(); ++it) {
            if (it.key() == id)
                continue;
            const QRectF o = it.value();
            double distance = 0, overlap = 0;
            switch (dir) {
            case 'l': distance = me.left() - o.right(); overlap = std::min(me.bottom(), o.bottom()) - std::max(me.top(), o.top()); break;
            case 'r': distance = o.left() - me.right(); overlap = std::min(me.bottom(), o.bottom()) - std::max(me.top(), o.top()); break;
            case 'u': distance = me.top() - o.bottom(); overlap = std::min(me.right(), o.right()) - std::max(me.left(), o.left()); break;
            case 'd': distance = o.top() - me.bottom(); overlap = std::min(me.right(), o.right()) - std::max(me.left(), o.left()); break;
            }
            if (distance >= -1 && distance <= reach && overlap > bestOverlap) {
                bestOverlap = overlap;
                best = it.key();
            }
        }
        return best;
    }

private:
    struct Node {
        QString window;
        bool side = true;
        double ratio = 0.5;
        std::unique_ptr<Node> a, b;
        Node *parent = nullptr;
        QRectF box;
        bool isLeaf() const { return !a; }
    };

    Node *leaf(const QString &id) const {
        if (id.isEmpty())
            return nullptr;
        return find(m_root.get(), id);
    }
    static Node *find(Node *n, const QString &id) {
        if (!n)
            return nullptr;
        if (n->isLeaf())
            return n->window == id ? n : nullptr;
        if (Node *x = find(n->a.get(), id))
            return x;
        return find(n->b.get(), id);
    }
    static void collect(Node *n, QStringList &out) {
        if (!n)
            return;
        if (n->isLeaf())
            out << n->window;
        else {
            collect(n->a.get(), out);
            collect(n->b.get(), out);
        }
    }
    void layout() { place(m_root.get(), m_area); }
    static void place(Node *n, const QRectF &box) {
        if (!n)
            return;
        n->box = box;
        if (n->isLeaf())
            return;
        QRectF first = box, second = box;
        if (n->side) {
            first.setWidth(box.width() * n->ratio);
            second.setLeft(first.right());
        } else {
            first.setHeight(box.height() * n->ratio);
            second.setTop(first.bottom());
        }
        place(n->a.get(), first);
        place(n->b.get(), second);
    }
    void rectsOf(Node *n, QHash<QString, QRectF> &out) const {
        if (!n)
            return;
        if (!n->isLeaf()) {
            rectsOf(n->a.get(), out);
            rectsOf(n->b.get(), out);
            return;
        }
        auto inset = [&](double edge, double screenEdge) {
            return (std::abs(edge - screenEdge) < 0.5 ? gapOut : gapIn) + border;
        };
        const QRectF b = n->box;
        out.insert(n->window, QRectF(QPointF(b.left() + inset(b.left(), m_area.left()), b.top() + inset(b.top(), m_area.top())),
                                     QPointF(b.right() - inset(b.right(), m_area.right()),
                                             b.bottom() - inset(b.bottom(), m_area.bottom())))
                                  .toRect());
    }

    QRectF m_area;
    std::unique_ptr<Node> m_root;
};
