#pragma once

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QRectF>
#include <QSizeF>

// A picture has to have the shape of the screen it's put together on: the
// pieces stretch over whatever tiles Hyprland gives them, and the tiles fill
// the monitor's work area, so a picture of any other shape would come out
// squashed. A source image close enough to that shape is cover-cropped to
// it; anything else becomes a collage of two to four images in justified
// rows (or columns), each cell cover-cropped, with thin gutters between them.
// Everything here is plain geometry and painting, so it runs off the UI
// thread and the tests can check it at any aspect.
namespace Collage {

// The work area of a monitor as `hyprctl -j monitors` reports it, in logical
// pixels: turned by its transform, divided by its scale, less what bars
// reserve along its edges. Empty when the monitor doesn't say.
QSizeF area(const QJsonObject &monitor);

// The pixel size to render a picture at for a work area: the area at the
// monitor's scale, kept within `longest` pixels along its longer side.
QSize renderSize(const QSizeF &area, double scale, int longest = 2560);

// The share of an image of shape `source` a cover-crop to `target` keeps: 1
// when they have the same shape.
double kept(const QSizeF &source, const QSizeF &target);

// The part of an image of size `source` that fills `target` when scaled
// without distortion, centred.
QRectF coverCrop(const QSizeF &source, const QSizeF &target);

struct Cell {
    int source = -1; // index into the images the layout was made for
    QRectF rect;     // where it goes, in the picture's own coordinates
};

struct Layout {
    QList<Cell> cells;
    // How much of the sources the crops throw away, weighted by cell area:
    // 0 when every cell has its image's own shape.
    double loss = 1;
    bool valid() const { return !cells.isEmpty(); }
};

// The best justified arrangement of `sources` (their sizes, any order) in a
// picture of size `target`, rows or columns, `gutter` apart.
Layout justify(const QList<QSizeF> &sources, const QSizeF &target, double gutter);

struct Plan {
    QList<int> sources; // indices into the pool, the first always 0
    Layout layout;      // cells name positions in `sources`
};

// Picks the images for one picture from a pool, whose first entry is the one
// it must show. That one alone, cover-cropped, when it keeps at least
// `closeEnough` of itself; otherwise the collage of two to `maxCells` pool
// images (looking at the first `look` of them) that crops the least.
Plan plan(const QList<QSizeF> &pool, const QSizeF &target, double gutter, double closeEnough = 0.8,
          int maxCells = 4, int look = 8);

// Paints a layout made for `size` with the given images (indexed by the
// cells' `source`), gutters in `gutterColor`.
QImage render(const QList<QImage> &images, const Layout &layout, const QSize &size,
              const QColor &gutterColor = QColor("#0e0e10"));

// The gutter for a picture of this size: thin, a fraction of its short side.
double gutterFor(const QSizeF &size);

} // namespace Collage
