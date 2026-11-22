#include "puzzle.h"

#include <QCoreApplication>
#include <algorithm>
#include <cmath>

using namespace Puzzle;

const Piece *Desk::piece(const QString &id) const {
    for (const auto &p : pieces)
        if (p.id == id)
            return &p;
    return nullptr;
}

QString Desk::groupLeaf(const QString &pieceId) const {
    const Window w = windows.value(pieceId);
    if (w.group.size() < 2)
        return pieceId;
    // Every tab of a group shares the group's tile; the one showing names it,
    // and failing that the first member Tessera knows.
    QString first;
    for (auto it = windows.begin(); it != windows.end(); ++it) {
        if (!it->present || !w.group.contains(it->address))
            continue;
        if (!it->hidden)
            return it.key();
        if (first.isEmpty() || it.key() < first)
            first = it.key();
    }
    return first.isEmpty() ? pieceId : first;
}

QHash<QString, QRectF> Desk::tiles(int workspace) const {
    QHash<QString, QRectF> out;
    for (auto it = windows.begin(); it != windows.end(); ++it) {
        const Window &w = it.value();
        if (!w.present || w.special || w.floating || w.workspace != workspace)
            continue;
        const QString leaf = groupLeaf(it.key());
        if (!out.contains(leaf))
            out.insert(leaf, windows.value(leaf).rect.isValid() ? windows.value(leaf).rect : w.rect);
    }
    return out;
}

QHash<QString, QRectF> Desk::allTiles(int workspace) const {
    QHash<QString, QRectF> out = tiles(workspace);
    const auto others = foreign.value(workspace);
    for (int i = 0; i < others.size(); ++i)
        out.insert(QString("foreign:%1").arg(i), others.at(i));
    return out;
}

bool Evaluation::complete() const {
    if (pictures.isEmpty())
        return false;
    return std::all_of(pictures.begin(), pictures.end(), [](const PictureState &p) { return p.complete; });
}

static QRectF bounds(const QHash<QString, QRectF> &tiles) {
    QRectF out;
    for (const auto &r : tiles)
        out = out.isNull() ? r : out.united(r);
    return out;
}

Evaluation Puzzle::evaluate(const Desk &desk) {
    Evaluation e;
    for (int i = 0; i < desk.pictures.size(); ++i) {
        const Picture &picture = desk.pictures.at(i);
        const QHash<QString, QRectF> all = desk.allTiles(picture.workspace);
        // The tiles fill the workspace's area, so their bounds are the
        // picture's frame whatever the monitor, bar, and gaps.
        const QRectF frame = bounds(all);
        const QList<QRectF> cells = picture.chain.layout();
        PictureState state;
        for (const Piece &piece : desk.pieces) {
            if (piece.picture != i)
                continue;
            ++state.total;
            PieceState ps;
            const Window w = desk.windows.value(piece.id);
            ps.home = w.present && !w.special && w.workspace == picture.workspace;
            ps.tiled = ps.home && !w.floating && w.group.isEmpty();
            if (ps.tiled && frame.width() > 0 && frame.height() > 0) {
                const QRectF r = w.rect;
                ps.normalized = QRectF((r.left() - frame.left()) / frame.width(), (r.top() - frame.top()) / frame.height(),
                                       r.width() / frame.width(), r.height() / frame.height());
                const QRectF target = cells.value(piece.index);
                const double t = desk.tolerance;
                // Gaps and borders shave a little off each tile, which the
                // tolerance absorbs; a resize step is well inside it.
                ps.inPlace = std::abs(ps.normalized.left() - target.left()) <= t
                             && std::abs(ps.normalized.right() - target.right()) <= t
                             && std::abs(ps.normalized.top() - target.top()) <= t
                             && std::abs(ps.normalized.bottom() - target.bottom()) <= t;
            }
            if (ps.inPlace)
                ++state.placed;
            e.pieces.insert(piece.id, ps);
        }
        // Done when every piece is in place and nothing else shares the
        // workspace: no stray pieces, floating or tiled, and no other window.
        bool clutter = !desk.foreign.value(picture.workspace).isEmpty();
        for (auto it = desk.windows.begin(); it != desk.windows.end(); ++it) {
            const Piece *p = desk.piece(it.key());
            if (p && p->picture != i && it->present && !it->special && it->workspace == picture.workspace)
                clutter = true;
        }
        state.complete = state.total > 0 && state.placed == state.total && !clutter;
        e.pictures << state;
    }
    return e;
}

char Puzzle::direction(const QRectF &from, const QRectF &to) {
    const QPointF d = to.center() - from.center();
    if (std::abs(d.x()) >= std::abs(d.y()))
        return d.x() >= 0 ? 'r' : 'l';
    return d.y() >= 0 ? 'd' : 'u';
}

QString Puzzle::neighbour(const QHash<QString, QRectF> &tiles, const QString &id, char dir) {
    if (!tiles.contains(id))
        return {};
    const QRectF me = tiles.value(id);
    QString best;
    double bestOverlap = 0, bestDistance = 1e9;
    for (auto it = tiles.begin(); it != tiles.end(); ++it) {
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
        // Only tiles sharing the edge, across the gap between them.
        if (distance < -2 || distance > 60 || overlap <= 0)
            continue;
        // The nearest edge first, then the longest shared stretch of it.
        if (distance < bestDistance - 2 || (std::abs(distance - bestDistance) <= 2 && overlap > bestOverlap)) {
            best = it.key();
            bestDistance = distance;
            bestOverlap = overlap;
        }
    }
    return best;
}

char Puzzle::stepToward(const QHash<QString, QRectF> &tiles, const QString &from, const QString &to) {
    if (!tiles.contains(from) || !tiles.contains(to) || from == to)
        return direction(tiles.value(from), tiles.value(to));
    QHash<QString, char> first; // the first move on the way to each tile
    QList<QString> queue{from};
    first.insert(from, 0);
    while (!queue.isEmpty()) {
        const QString at = queue.takeFirst();
        for (char d : {'l', 'r', 'u', 'd'}) {
            const QString n = neighbour(tiles, at, d);
            if (n.isEmpty() || first.contains(n))
                continue;
            first.insert(n, at == from ? d : first.value(at));
            if (n == to)
                return first.value(n);
            queue << n;
        }
    }
    return direction(tiles.value(from), tiles.value(to));
}

int Puzzle::slot(int workspace) { return workspace <= 0 ? 0 : ((workspace - 1) % 10) + 1; }

// ── Hints ────────────────────────────────────────────────────────────────

namespace {

QString tr(const char *text) { return QCoreApplication::translate("Hints", text); }

struct Context {
    const Desk &desk;
    const Evaluation &e;
    const Hints::Chords &chords;
    const Hints::Names &names;
    const Hints::Titles &titles;

    QString name(const QString &id) const { return names(id); }
    QString slotText(int ws) const { return QString::number(slot(ws)); }
    QString moveTo(int ws) const { return chords(QString("Move window to workspace %1").arg(slot(ws))); }
    QString goTo(int ws) const { return chords(QString("Switch to workspace %1").arg(slot(ws))); }
    QString focus(char d) const {
        switch (d) {
        case 'l': return chords("Focus on left window");
        case 'r': return chords("Focus on right window");
        case 'u': return chords("Focus on above window");
        default: return chords("Focus on below window");
        }
    }
    QString swap(char d) const {
        switch (d) {
        case 'l': return chords("Swap window to the left");
        case 'r': return chords("Swap window to the right");
        case 'u': return chords("Swap window up");
        default: return chords("Swap window down");
        }
    }
    QString intoGroup(char d) const {
        switch (d) {
        case 'l': return chords("Move window to group on left");
        case 'r': return chords("Move window to group on right");
        case 'u': return chords("Move window to group on top");
        default: return chords("Move window to group on bottom");
        }
    }
    static QString way(char d) {
        switch (d) {
        case 'l': return tr("left");
        case 'r': return tr("right");
        case 'u': return tr("up");
        default: return tr("down");
        }
    }
};

using Hints::Hint;

Hint hint(const QString &text, const QString &chord, const QString &detail = {}, const QString &piece = {}) {
    return {text, chord, detail, piece};
}

// What a picture still needs, before its tiles can be arranged: stray
// pieces out, missing pieces in. Empty when every piece is tiled there.
QList<Hint> gathering(const Context &c, int pictureIndex, const QString &from) {
    QList<Hint> out;
    const Picture &picture = c.desk.pictures.at(pictureIndex);
    const int ws = picture.workspace;
    const auto tiles = c.desk.allTiles(ws);
    const QRectF here = tiles.value(from);
    auto towards = [&](const QString &id) -> QString {
        const QRectF there = c.desk.windows.value(id).rect;
        if (!here.isValid() || !there.isValid())
            return QString();
        return c.focus(tiles.contains(id) ? stepToward(tiles, from, id) : direction(here, there));
    };

    // Pieces that don't belong here go first, or they take up tiles.
    for (const Piece &p : c.desk.pieces) {
        const Window w = c.desk.windows.value(p.id);
        if (p.picture == pictureIndex || !w.present || w.special || w.workspace != ws)
            continue;
        if (p.picture < 0)
            out << hint(tr("%1 doesn't belong to any picture here. Focus it and tuck it into the scratchpad.").arg(c.name(p.id)),
                        towards(p.id), tr("Super+arrows walk focus over to it; the scratchpad keeps it out of the way."), p.id);
        else
            out << hint(tr("%1 belongs to %2, not here. Focus it and send it to workspace %3.")
                            .arg(c.name(p.id), c.titles(p.picture), c.slotText(c.desk.pictures.at(p.picture).workspace)),
                        towards(p.id), tr("Super+arrows walk focus over to it."), p.id);
        return out;
    }
    if (!c.desk.foreign.value(ws).isEmpty()) {
        out << hint(tr("A window that isn't Tessera's is on workspace %1. Move it somewhere else to finish %2.")
                        .arg(c.slotText(ws), c.titles(pictureIndex)),
                    QString());
        return out;
    }

    for (const Piece &p : c.desk.pieces) {
        if (p.picture != pictureIndex)
            continue;
        const Window w = c.desk.windows.value(p.id);
        if (!w.present)
            continue;
        if (w.special) {
            out << hint(tr("%1 is hiding in the scratchpad. Show the scratchpad to find it.").arg(c.name(p.id)),
                        c.chords("Toggle scratchpad"),
                        tr("The scratchpad slides over whatever workspace you're on; the same keys hide it again."), p.id);
        } else if (w.workspace != ws) {
            out << hint(tr("%1 is on workspace %2. Go and get it.").arg(c.name(p.id), c.slotText(w.workspace)),
                        c.goTo(w.workspace), {}, p.id);
        } else if (w.floating) {
            out << hint(tr("%1 floats above the picture. Bring focus to it, then tile it.").arg(c.name(p.id)),
                        c.chords("Focus on next window"),
                        tr("Super+arrows only walk between tiles; Alt+Tab, or the mouse, reaches a floating window."),
                        p.id);
        } else if (w.group.size() > 1) {
            out << hint(tr("%1 is stacked in a pile with other pieces. Focus the pile to take them out.").arg(c.name(p.id)),
                        towards(c.desk.groupLeaf(p.id)),
                        tr("A pile is a Hyprland group: tabs that share one tile."), p.id);
        } else {
            continue;
        }
        return out;
    }
    return out;
}

// The window next to `id` that is a pile, and which way it is.
QString pileBeside(const Context &c, const QHash<QString, QRectF> &tiles, const QString &id, char *dir) {
    for (char d : {'r', 'd', 'l', 'u'}) {
        const QString n = neighbour(tiles, id, d);
        if (!n.isEmpty() && !c.desk.windows.value(n).group.isEmpty()) {
            *dir = d;
            return n;
        }
    }
    return {};
}

// How a picture's tiles differ from its cut, for the focused piece.
QList<Hint> arranging(const Context &c, int pictureIndex, const QString &focused) {
    QList<Hint> out;
    const Picture &picture = c.desk.pictures.at(pictureIndex);
    const auto tiles = c.desk.tiles(picture.workspace);
    const Cut::Tree tree = Cut::infer(tiles);
    if (!tree.valid())
        return out;
    const QRectF mine = tiles.value(focused);

    auto focusOn = [&](const QString &id, const QString &text, const QString &detail = {}) {
        out << hint(text, mine.isValid() ? c.focus(stepToward(tiles, focused, id)) : QString(), detail, id);
    };

    if (!tree.isChain()) {
        // Swaps and turns never change which tile splits off which, so a
        // piece in the wrong branch moves the long way, with keys only: into
        // a pile beside it, the pile into the last tile, and out again,
        // which splits the last tile and grows the spiral at its end.
        const QString wrong = tree.misplaced();
        char pileDir = 0;
        const QString pile = pileBeside(c, tiles, wrong, &pileDir);
        const QString procedure = tr("Pieces only change branch by leaving their tile: tuck it into a pile next to "
                                     "it, swap the pile into the last tile (the smallest, where the spiral ends), "
                                     "and pop the piece out there.");
        if (wrong == focused) {
            if (!pile.isEmpty()) {
                out << hint(tr("Tuck %1 into the pile %2 of it.").arg(c.name(focused), Context::way(pileDir)),
                            c.intoGroup(pileDir), procedure, focused);
            } else {
                char d = stepToward(tiles, focused, tree.end());
                QString n = neighbour(tiles, focused, d);
                for (char alt : {'r', 'd', 'l', 'u'})
                    if (n.isEmpty() && !(n = neighbour(tiles, focused, alt)).isEmpty())
                        d = alt;
                out << hint(tr("%1 sits in the wrong branch of the picture. To move it, first make %2 next to it a "
                               "pile: focus it.").arg(c.name(focused), c.name(n)),
                            c.focus(d), procedure, focused);
            }
        } else if (pile.isEmpty() && mine.isValid()
                   && [&] {
                          for (char d : {'r', 'd', 'l', 'u'})
                              if (neighbour(tiles, wrong, d) == focused)
                                  return true;
                          return false;
                      }()) {
            out << hint(tr("Make %1 a pile, so %2 can join it.").arg(c.name(focused), c.name(wrong)),
                        c.chords("Toggle window grouping"), procedure, focused);
        } else {
            focusOn(wrong, tr("%1 sits in the wrong branch of the picture. Focus it.").arg(c.name(wrong)), procedure);
        }
        return out;
    }

    const QStringList spine = tree.spine();
    const QList<int> nodes = tree.spineNodes();
    const int n = spine.size();
    const auto &levels = picture.chain.levels;
    if (n != picture.chain.pieces() || nodes.size() != levels.size())
        return out;
    auto indexOf = [&](const QString &id) { return c.desk.piece(id) ? c.desk.piece(id)->index : -1; };
    auto split = [&](int level) { return tree.nodes.at(nodes.at(level)).split; };
    auto ratio = [&](int level) { return tree.nodes.at(nodes.at(level)).ratio; };
    auto ratioOff = [&](int level) { return std::abs(ratio(level) - levels.at(level).ratio) > c.desk.tolerance * 0.8; };
    auto levelWrong = [&](int level) {
        return split(level) != levels.at(level).split || ratioOff(level) || indexOf(spine.at(level)) != level
               || (level == n - 2 && indexOf(spine.at(n - 1)) != n - 1);
    };

    const int d = spine.indexOf(focused);
    if (d < 0)
        return out;
    const int level = std::min(d, n - 2);

    if (level >= 0 && split(level) != levels.at(level).split) {
        out << hint(tr("The split beside %1 runs the wrong way. Turn it.").arg(c.name(focused)),
                    c.chords("Toggle window split"),
                    tr("Toggling the split turns the line between this tile and its neighbour, side by side to "
                       "stacked and back."),
                    focused);
        return out;
    }
    // Pieces go into their places in spiral order, the first tile first. A
    // piece walks there through the tiles not yet filled, which always form
    // one connected stretch of the spiral, so a placed piece is never pushed
    // out again and every walk ends.
    int open = -1;
    for (int k = 0; k < n; ++k)
        if (indexOf(spine.at(k)) != k) {
            open = k;
            break;
        }
    if (open >= 0) {
        QString wanted;
        for (const Piece &p : c.desk.pieces)
            if (p.picture == pictureIndex && p.index == open)
                wanted = p.id;
        if (wanted == focused) {
            QHash<QString, QRectF> free;
            for (int k = open; k < n; ++k)
                free.insert(spine.at(k), tiles.value(spine.at(k)));
            const char dir = stepToward(free, focused, spine.at(open));
            out << hint(tr("%1 belongs further %2. Swap it that way.").arg(c.name(focused), Context::way(dir)), c.swap(dir),
                        tr("A swap trades places with the tile next to it, so the picture's shape stays the same."),
                        focused);
        } else if (tiles.contains(wanted)) {
            focusOn(wanted, tr("%1 goes into its place next: focus it.").arg(c.name(wanted)),
                    tr("Pieces go in one by one, from the biggest tile along the spiral."));
        }
        return out;
    }
    if (level >= 0 && ratioOff(level)) {
        const auto &node = tree.nodes.at(nodes.at(level));
        const bool side = node.split == Cut::Split::Side;
        const double extent = side ? node.rect.width() : node.rect.height();
        // Moving the split line by `delta` pixels; the first half grows when
        // the line moves right or down.
        const double delta = (levels.at(level).ratio - ratio(level)) * extent;
        const bool last = d == n - 1;
        const bool grow = last ? delta < 0 : delta > 0;
        const double size = std::abs(delta);
        const QString amount = size >= 300 ? " a lot" : size >= 100 ? "" : " a little";
        QString action;
        if (side)
            action = (delta > 0 ? "Shrink window left" : "Expand window left") + amount;
        else
            action = (delta > 0 ? "Expand window down" : "Shrink window up") + amount;
        const QString text = side ? (grow ? tr("Make %1 wider.") : tr("Make %1 narrower."))
                                  : (grow ? tr("Make %1 taller.") : tr("Make %1 shorter."));
        out << hint(text.arg(c.name(focused)), c.chords(action),
                    side ? tr("Super+Minus and Super+Equal move the line beside it left and right; add Alt for a little, Ctrl for a lot.")
                         : tr("Super+Shift+Minus and Super+Shift+Equal move the line below it up and down; add Alt for a little, Ctrl for a lot."),
                    focused);
        return out;
    }
    // This piece's own split is right: whatever is still off is up the
    // spiral, or elsewhere in the picture.
    for (int k = 0; k + 1 < n; ++k) {
        if (!levelWrong(k))
            continue;
        const QString other = spine.at(k) == focused ? spine.at(k + 1) : spine.at(k);
        if (c.e.pieces.value(focused).inPlace)
            focusOn(other, tr("%1 is in place. %2 isn't yet: focus it.").arg(c.name(focused), c.name(other)));
        else
            focusOn(other, tr("%1 is off because of %2. Focus that one first.").arg(c.name(focused), c.name(other)));
        return out;
    }
    return out;
}

QList<Hint> nextPicture(const Context &c, int done) {
    QList<Hint> out;
    for (int i = 0; i < c.desk.pictures.size(); ++i) {
        if (i == done || c.e.pictures.value(i).complete)
            continue;
        out << hint(tr("Next: %1 on workspace %2.").arg(c.titles(i), c.slotText(c.desk.pictures.at(i).workspace)),
                    c.desk.activeWorkspace == c.desk.pictures.at(i).workspace ? QString() : c.goTo(c.desk.pictures.at(i).workspace));
        break;
    }
    return out;
}

} // namespace

QList<Hints::Hint> Hints::compute(const Desk &desk, const Evaluation &e, const Chords &chords, const Names &names,
                                  const Titles &titles) {
    const Context c{desk, e, chords, names, titles};
    QList<Hint> out;

    // The focused piece counts only while it's in sight: Hyprland keeps the
    // last focus around when the player switches to an empty workspace.
    const Piece *f = desk.piece(desk.focused);
    if (f) {
        const Window w = desk.windows.value(f->id);
        const bool visible = w.present && (w.special ? desk.scratchpadShown : w.workspace == desk.activeWorkspace);
        if (!visible)
            f = nullptr;
    }

    if (f) {
        const Window w = desk.windows.value(f->id);
        if (f->picture < 0) {
            QString wanted;
            for (const Piece &p : desk.pieces)
                if (p.picture >= 0 && desk.windows.value(p.id).present && desk.windows.value(p.id).special)
                    wanted = p.id;
            if (w.special && !wanted.isEmpty())
                out << hint(tr("%1 is tidied away. %2 is in the scratchpad too: focus it.").arg(c.name(f->id), c.name(wanted)),
                            c.focus(direction(w.rect, desk.windows.value(wanted).rect)), {}, wanted);
            else if (w.special)
                out << hint(tr("%1 is tidied away. Hide the scratchpad again.").arg(c.name(f->id)), chords("Toggle scratchpad"));
            else
                out << hint(tr("%1 doesn't belong to any picture here. Tuck it into the scratchpad.").arg(c.name(f->id)),
                            chords("Move window to scratchpad"),
                            tr("The scratchpad is a hidden workspace; Super+S shows and hides it."), f->id);
            return out;
        }
        const Picture &picture = desk.pictures.at(f->picture);
        const int ws = picture.workspace;
        if (w.special) {
            out << hint(tr("Found %1. Send it to workspace %2.").arg(c.name(f->id), c.slotText(ws)), c.moveTo(ws),
                        tr("It lands beside the tile you focused last on workspace %1.").arg(c.slotText(ws)), f->id);
            return out;
        }
        if (w.workspace != ws) {
            out << hint(tr("%1 belongs on workspace %2. Carry it there.").arg(c.name(f->id), c.slotText(ws)), c.moveTo(ws),
                        tr("It lands beside the tile you focused last on workspace %1, so pick the last tile of the "
                           "spiral before you leave.").arg(c.slotText(ws)),
                        f->id);
            return out;
        }
        if (w.floating) {
            out << hint(tr("%1 is floating. Tile it.").arg(c.name(f->id)), chords("Toggle window floating/tiling"),
                        tr("A window joins the tiles beside the one you focused last, or the one under the pointer."),
                        f->id);
            return out;
        }
        if (w.group.size() > 1) {
            const auto tiles = desk.tiles(ws);
            const QString leaf = desk.groupLeaf(f->id);
            const Cut::Tree tree = Cut::infer(tiles);
            if (tiles.size() == 1 || tree.end() == leaf) {
                out << hint(tr("%1 is in a pile. Pop it out.").arg(c.name(f->id)), chords("Move active window out of group"),
                            tr("Each piece pops out into a new tile beside the pile. Super+Alt+Tab shows the next piece in it."),
                            f->id);
            } else {
                const char dir = stepToward(tiles, leaf, tree.end());
                out << hint(tr("Pop pieces out where the spiral ends: swap the pile %1 into the last tile first.")
                                .arg(Context::way(dir)),
                            c.swap(dir),
                            tr("A piece popped out of a pile splits the pile's tile. In the last tile that grows the "
                               "spiral the way the picture is cut."),
                            f->id);
            }
            return out;
        }
        if (w.group.size() == 1) {
            const auto tiles = desk.tiles(ws);
            const Cut::Tree tree = Cut::infer(tiles);
            const QString wrong = tree.valid() && !tree.isChain() ? tree.misplaced() : QString();
            bool besideWrong = false;
            for (char d : {'r', 'd', 'l', 'u'})
                besideWrong = besideWrong || (!wrong.isEmpty() && neighbour(tiles, wrong, d) == f->id);
            if (besideWrong && gathering(c, f->picture, f->id).isEmpty())
                out << hint(tr("Now focus %1 and tuck it into this pile.").arg(c.name(wrong)),
                            c.focus(stepToward(tiles, f->id, wrong)), {}, wrong);
            else
                out << hint(tr("%1 is still a pile of one. Take it out of the group.").arg(c.name(f->id)),
                            chords("Move active window out of group"), {}, f->id);
            return out;
        }
        if (e.pictures.value(f->picture).complete) {
            out << hint(tr("%1 is complete!").arg(titles(f->picture)), QString());
            out << nextPicture(c, f->picture);
            return out;
        }
        out << gathering(c, f->picture, f->id);
        if (!out.isEmpty())
            return out;
        out << arranging(c, f->picture, f->id);
        if (out.isEmpty())
            for (const Piece &p : desk.pieces) {
                const Window pw = desk.windows.value(p.id);
                if (p.picture == f->picture && pw.present && pw.group.size() == 1) {
                    const auto tiles = desk.tiles(ws);
                    out << hint(tr("%1 is still a pile of one. Focus it and take it out of the group.").arg(c.name(p.id)),
                                c.focus(stepToward(tiles, f->id, p.id)), {}, p.id);
                    break;
                }
            }
        return out;
    }

    // Nothing of the game's has focus here: say what this workspace is for.
    for (int i = 0; i < desk.pictures.size(); ++i) {
        if (desk.pictures.at(i).workspace != desk.activeWorkspace)
            continue;
        if (e.pictures.value(i).complete) {
            out << hint(tr("%1 is complete!").arg(titles(i)), QString());
            out << nextPicture(c, i);
            return out;
        }
        if (!desk.allTiles(desk.activeWorkspace).isEmpty())
            out << hint(tr("This is where %1 goes. Focus one of its pieces to see what it needs.").arg(titles(i)),
                        chords("Focus on left window").isEmpty() ? QString() : QStringLiteral("SUPER + ARROWS"));
        out << gathering(c, i, QString());
        return out;
    }
    for (const Piece &p : desk.pieces) {
        const Window pw = desk.windows.value(p.id);
        if (pw.present && !pw.special && pw.workspace == desk.activeWorkspace) {
            out << hint(tr("%1 is here. Focus it to see where it goes.").arg(c.name(p.id)), QStringLiteral("SUPER + ARROWS"));
            return out;
        }
    }
    if (desk.scratchpadShown) {
        out << hint(tr("This is the scratchpad. Focus a piece in it, or hide it again."), chords("Toggle scratchpad"));
        return out;
    }
    out << nextPicture(c, -1);
    return out;
}
