#include "collage.h"

#include <QJsonArray>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <numeric>

using namespace Collage;

QSizeF Collage::area(const QJsonObject &monitor) {
    double w = monitor.value("width").toDouble(), h = monitor.value("height").toDouble();
    if (w <= 0 || h <= 0)
        return {};
    const double scale = monitor.value("scale").toDouble(1);
    // Transforms 1, 3, 5 and 7 turn the monitor a quarter, so its logical
    // width is the panel's height.
    if (monitor.value("transform").toInt() % 2 == 1)
        std::swap(w, h);
    if (scale > 0) {
        w /= scale;
        h /= scale;
    }
    // Reserved edges (the bar) are logical pixels: left, top, right, bottom.
    const QJsonArray reserved = monitor.value("reserved").toArray();
    if (reserved.size() == 4) {
        w -= reserved.at(0).toDouble() + reserved.at(2).toDouble();
        h -= reserved.at(1).toDouble() + reserved.at(3).toDouble();
    }
    if (w < 50 || h < 50)
        return {};
    return {w, h};
}

QSize Collage::renderSize(const QSizeF &area, double scale, int longest) {
    if (area.isEmpty())
        return {1600, 900};
    QSizeF px = area * std::clamp(scale, 1.0, 2.0);
    const double longSide = std::max(px.width(), px.height());
    if (longSide > longest)
        px *= longest / longSide;
    return px.toSize().expandedTo(QSize(16, 16));
}

double Collage::kept(const QSizeF &source, const QSizeF &target) {
    if (source.isEmpty() || target.isEmpty())
        return 0;
    const double a = source.width() / source.height(), b = target.width() / target.height();
    return std::min(a, b) / std::max(a, b);
}

QRectF Collage::coverCrop(const QSizeF &source, const QSizeF &target) {
    if (source.isEmpty() || target.isEmpty())
        return {};
    const double a = source.width() / source.height(), b = target.width() / target.height();
    if (a > b) {
        // Too wide: the sides go.
        const double w = source.height() * b;
        return QRectF((source.width() - w) / 2, 0, w, source.height());
    }
    const double h = source.width() / b;
    return QRectF(0, (source.height() - h) / 2, source.width(), h);
}

double Collage::gutterFor(const QSizeF &size) {
    return std::max(2.0, std::round(std::min(size.width(), size.height()) * 0.006));
}

namespace {

// Rows laid out across `target`, in order, with `rowSizes` images each. Every
// row spans the full width with its images at their own shape; the rows are
// then scaled together to fill the height, which crops every cell by the
// same factor.
Layout rows(const QList<QSizeF> &sizes, const QList<int> &order, const QList<int> &rowSizes, const QSizeF &target,
            double gutter) {
    Layout out;
    QList<double> heights;
    int at = 0;
    for (int count : rowSizes) {
        double aspects = 0;
        for (int k = 0; k < count; ++k) {
            const QSizeF s = sizes.at(order.at(at + k));
            aspects += s.width() / s.height();
        }
        heights << (target.width() - gutter * (count - 1)) / aspects;
        at += count;
    }
    const double natural = std::accumulate(heights.begin(), heights.end(), 0.0);
    const double available = target.height() - gutter * (rowSizes.size() - 1);
    if (natural <= 0 || available <= 0)
        return out;
    const double stretch = available / natural;
    at = 0;
    double y = 0;
    for (int r = 0; r < rowSizes.size(); ++r) {
        const int count = rowSizes.at(r);
        const double h = heights.at(r) * stretch;
        double aspects = 0;
        for (int k = 0; k < count; ++k) {
            const QSizeF s = sizes.at(order.at(at + k));
            aspects += s.width() / s.height();
        }
        const double width = target.width() - gutter * (count - 1);
        double x = 0;
        for (int k = 0; k < count; ++k) {
            const QSizeF s = sizes.at(order.at(at + k));
            const double w = width * (s.width() / s.height()) / aspects;
            out.cells << Cell{order.at(at + k), QRectF(x, y, w, h)};
            x += w + gutter;
        }
        y += h + gutter;
        at += count;
    }
    // Every cell's shape is off by the same stretch.
    out.loss = 1 - std::min(stretch, 1 / stretch);
    return out;
}

QSizeF transposed(const QSizeF &s) { return {s.height(), s.width()}; }

// A cell too small to be worth looking at spoils a layout.
bool balanced(const Layout &layout, const QSizeF &target) {
    const double areaAll = target.width() * target.height();
    const double shortSide = std::min(target.width(), target.height());
    for (const Cell &c : layout.cells)
        if (c.rect.width() * c.rect.height() < areaAll * 0.07 || std::min(c.rect.width(), c.rect.height()) < shortSide * 0.18)
            return false;
    return true;
}

// Every way to cut `n` images into consecutive rows.
QList<QList<int>> compositions(int n) {
    QList<QList<int>> out;
    for (int mask = 0; mask < (1 << (n - 1)); ++mask) {
        QList<int> parts;
        int run = 1;
        for (int i = 0; i < n - 1; ++i) {
            if (mask & (1 << i)) {
                parts << run;
                run = 1;
            } else {
                ++run;
            }
        }
        parts << run;
        out << parts;
    }
    return out;
}

} // namespace

Layout Collage::justify(const QList<QSizeF> &sources, const QSizeF &target, double gutter) {
    Layout best, bestAny;
    if (sources.isEmpty() || target.isEmpty())
        return best;
    for (const QSizeF &s : sources)
        if (s.isEmpty())
            return best;
    QList<int> order(sources.size());
    std::iota(order.begin(), order.end(), 0);
    const auto parts = compositions(sources.size());
    QList<QSizeF> flipped;
    for (const QSizeF &s : sources)
        flipped << transposed(s);
    do {
        for (const auto &rowSizes : parts) {
            for (bool columns : {false, true}) {
                Layout l = columns ? rows(flipped, order, rowSizes, transposed(target), gutter)
                                   : rows(sources, order, rowSizes, target, gutter);
                if (!l.valid())
                    continue;
                if (columns)
                    for (Cell &c : l.cells)
                        c.rect = QRectF(c.rect.y(), c.rect.x(), c.rect.height(), c.rect.width());
                if (l.loss < bestAny.loss - 1e-9)
                    bestAny = l;
                if (balanced(l, target) && l.loss < best.loss - 1e-9)
                    best = l;
            }
        }
    } while (std::next_permutation(order.begin(), order.end()));
    return best.valid() ? best : bestAny;
}

Plan Collage::plan(const QList<QSizeF> &pool, const QSizeF &target, double gutter, double closeEnough, int maxCells,
                   int look) {
    Plan out;
    if (pool.isEmpty() || target.isEmpty() || pool.first().isEmpty())
        return out;
    const QRectF whole(QPointF(0, 0), target);
    Plan single{{0}, Layout{{Cell{0, whole}}, 1 - kept(pool.first(), target)}};
    if (kept(pool.first(), target) >= closeEnough || pool.size() == 1)
        return single;

    // Each extra cell costs a little, so a collage only grows when that
    // saves a real crop.
    const double perCell = 0.03;
    Plan best = single;
    double bestScore = single.layout.loss + 0.15; // a heavy crop is worse than a collage
    const int n = std::min<int>(pool.size(), std::max(2, look));
    QList<int> others;
    for (int i = 1; i < n; ++i)
        if (!pool.at(i).isEmpty())
            others << i;
    // Subsets of the others, by bit mask, of up to maxCells - 1 images.
    for (int mask = 1; mask < (1 << others.size()); ++mask) {
        const int count = __builtin_popcount(unsigned(mask));
        if (count + 1 > maxCells)
            continue;
        QList<int> chosen{0};
        for (int i = 0; i < others.size(); ++i)
            if (mask & (1 << i))
                chosen << others.at(i);
        QList<QSizeF> sizes;
        for (int i : chosen)
            sizes << pool.at(i);
        const Layout l = justify(sizes, target, gutter);
        if (!l.valid() || !balanced(l, target))
            continue;
        const double score = l.loss + perCell * (chosen.size() - 2);
        if (score < bestScore - 1e-9) {
            bestScore = score;
            best = Plan{chosen, l};
        }
    }
    return best;
}

QImage Collage::render(const QList<QImage> &images, const Layout &layout, const QSize &size, const QColor &gutterColor) {
    QImage out(size, QImage::Format_RGB32);
    out.fill(gutterColor);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    for (const Cell &c : layout.cells) {
        if (c.source < 0 || c.source >= images.size() || images.at(c.source).isNull())
            continue;
        // Whole pixels, so neighbouring cells never blur into the gutter.
        const QRect target(QPoint(int(std::round(c.rect.left())), int(std::round(c.rect.top()))),
                           QPoint(int(std::round(c.rect.right())) - 1, int(std::round(c.rect.bottom())) - 1));
        if (target.isEmpty())
            continue;
        const QImage &src = images.at(c.source);
        const QRect crop = coverCrop(src.size(), target.size()).toAlignedRect().intersected(src.rect());
        p.drawImage(target, src.copy(crop).scaled(target.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    }
    p.end();
    return out;
}
