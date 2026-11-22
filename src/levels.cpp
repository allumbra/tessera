#include "levels.h"

#include <QRandomGenerator>
#include <QtGlobal>
#include <algorithm>

using namespace Levels;
using Cut::Split;

const QList<Art::Work> &Art::all() {
    static const QList<Work> works{
        {"wave", QT_TRANSLATE_NOOP("Art", "Wave"), QT_TRANSLATE_NOOP("Art", "The Great Wave off Kanagawa"),
         "Katsushika Hokusai", "c. 1831"},
        {"starry", QT_TRANSLATE_NOOP("Art", "Starry Night"), QT_TRANSLATE_NOOP("Art", "The Starry Night"),
         "Vincent van Gogh", "1889"},
        {"sunrise", QT_TRANSLATE_NOOP("Art", "Sunrise"), QT_TRANSLATE_NOOP("Art", "Impression, Sunrise"),
         "Claude Monet", "1872"},
        {"sierra", QT_TRANSLATE_NOOP("Art", "Sierra"), QT_TRANSLATE_NOOP("Art", "Among the Sierra Nevada, California"),
         "Albert Bierstadt", "1868"},
        {"nasa", QT_TRANSLATE_NOOP("Art", "Saturn"), QT_TRANSLATE_NOOP("Art", "Saturn during Equinox"),
         "NASA / JPL / Space Science Institute", "2008"},
    };
    return works;
}

const Art::Work *Art::find(const QString &key) {
    for (const auto &w : all())
        if (w.key == key)
            return &w;
    return nullptr;
}

QString Art::image(const QString &key) { return "qrc:/images/" + key + ".jpg"; }

namespace {

Cut::Chain chain(std::initializer_list<Cut::Level> levels) {
    Cut::Chain c;
    for (const auto &l : levels)
        c.levels << l;
    return c;
}

PictureSpec picture(const QString &art, const Cut::Chain &cut, QList<PieceSpec> pieces, QList<Deal> tiles) {
    return {art, cut, pieces, tiles};
}

} // namespace

const QList<Level> &Levels::lessons() {
    static const QList<Level> all = [] {
        QList<Level> l;
        const Cut::Level side{Split::Side, 0.5}, stack{Split::Stack, 0.5};

        Level swap;
        swap.id = "swap";
        swap.title = QT_TRANSLATE_NOOP("Levels", "Trading Places");
        swap.teaches = QT_TRANSLATE_NOOP("Levels", "Swap windows");
        swap.intro = QT_TRANSLATE_NOOP("Levels", "Two halves of a picture sit the wrong way round. Swap the focused "
                                                 "piece with its neighbour.");
        swap.actions = {"Swap window to the left", "Swap window to the right"};
        swap.pictures << picture("wave", chain({side}), {{0}, {1}}, {{0, 1}, {0, 0}});
        l << swap;

        Level focus;
        focus.id = "focus";
        focus.title = QT_TRANSLATE_NOOP("Levels", "Look Around");
        focus.teaches = QT_TRANSLATE_NOOP("Levels", "Move focus, then swap");
        focus.intro = QT_TRANSLATE_NOOP("Levels", "Three pieces, shuffled. Walk the focus between them with Super and "
                                                  "the arrows, and swap each one into its place.");
        focus.actions = {"Focus on left window", "Focus on right window", "Swap window up", "Swap window down"};
        focus.pictures << picture("starry", chain({side, stack}), {{0}, {1}, {2}}, {{0, 2}, {0, 0}, {0, 1}});
        l << focus;

        Level split;
        split.id = "split";
        split.title = QT_TRANSLATE_NOOP("Levels", "Turning the Line");
        split.teaches = QT_TRANSLATE_NOOP("Levels", "Toggle the split");
        split.intro = QT_TRANSLATE_NOOP("Levels", "This picture is cut across, but its pieces stand side by side. Turn "
                                                  "the split between them.");
        split.actions = {"Toggle window split"};
        split.pictures << picture("sunrise", chain({stack}), {{0}, {1}}, {{0, 0}, {0, 1}});
        l << split;

        Level away;
        away.id = "workspace";
        away.title = QT_TRANSLATE_NOOP("Levels", "Lost and Found");
        away.teaches = QT_TRANSLATE_NOOP("Levels", "Move a window to a workspace");
        away.intro = QT_TRANSLATE_NOOP("Levels", "Half the picture wandered off to another workspace. Go there, and "
                                                 "carry it home.");
        away.actions = {"Switch to workspace 1", "Move window to workspace 1"};
        away.pictures << picture("sierra", chain({side}), {{0}, {1, Start::Spare}}, {{0, 0}});
        away.spares = 1;
        l << away;

        Level floating;
        floating.id = "float";
        floating.title = QT_TRANSLATE_NOOP("Levels", "Afloat");
        floating.teaches = QT_TRANSLATE_NOOP("Levels", "Float and tile");
        floating.intro = QT_TRANSLATE_NOOP("Levels", "One piece floats over the picture instead of taking its tile. "
                                                     "Tile it.");
        floating.actions = {"Toggle window floating/tiling"};
        floating.pictures << picture("wave", chain({side}), {{0}, {1, Start::Floating}}, {{0, 0}});
        l << floating;

        Level pile;
        pile.id = "pile";
        pile.title = QT_TRANSLATE_NOOP("Levels", "The Pile");
        pile.teaches = QT_TRANSLATE_NOOP("Levels", "Take windows out of a group");
        pile.intro = QT_TRANSLATE_NOOP("Levels", "Two pieces are stacked in one tile, a Hyprland group. Flip through "
                                                 "it and pop the pieces out.");
        pile.actions = {"Move active window out of group", "Next window in group"};
        pile.pictures << picture("starry", chain({side, stack}), {{0}, {1, Start::Pile}, {2, Start::Pile}}, {{0, 0}});
        l << pile;

        Level resize;
        resize.id = "resize";
        resize.title = QT_TRANSLATE_NOOP("Levels", "Golden Section");
        resize.teaches = QT_TRANSLATE_NOOP("Levels", "Resize sideways");
        resize.intro = QT_TRANSLATE_NOOP("Levels", "The pieces are in order, but the line between them is in the "
                                                   "wrong place. Move it until the picture fits.");
        resize.actions = {"Expand window left", "Shrink window left", "Expand window left a little",
                          "Shrink window left a little"};
        resize.pictures << picture("sierra", chain({{Split::Side, 2.0 / 3}}), {{0}, {1}}, {{0, 0}, {0, 1}});
        l << resize;

        Level tall;
        tall.id = "tall";
        tall.title = QT_TRANSLATE_NOOP("Levels", "High and Low");
        tall.teaches = QT_TRANSLATE_NOOP("Levels", "Resize up and down");
        tall.intro = QT_TRANSLATE_NOOP("Levels", "The sky needs less room than it has. Move the line under it up.");
        tall.actions = {"Shrink window up", "Expand window down", "Shrink window up a little",
                        "Expand window down a little"};
        tall.pictures << picture("sunrise", chain({side, {Split::Stack, 1.0 / 3}}), {{0}, {1}, {2}},
                                 {{0, 0}, {0, 1}, {0, 2}});
        l << tall;

        Level scratch;
        scratch.id = "scratchpad";
        scratch.title = QT_TRANSLATE_NOOP("Levels", "Out of Sight");
        scratch.teaches = QT_TRANSLATE_NOOP("Levels", "Use the scratchpad");
        scratch.intro = QT_TRANSLATE_NOOP("Levels", "A stray piece from another picture is in the way, and one that "
                                                    "belongs here is hiding in the scratchpad. Swap them over.");
        scratch.actions = {"Move window to scratchpad", "Toggle scratchpad", "Move window to workspace 1"};
        scratch.pictures << picture("wave", chain({side}), {{0}, {1, Start::Scratchpad}}, {{0, 0}, {-1, 0}});
        scratch.strays << Stray{"nasa", 0, Start::Tiled};
        l << scratch;

        return l;
    }();
    return all;
}

const QList<Tier> &Levels::tiers() {
    static const QList<Tier> all{
        {"easy", QT_TRANSLATE_NOOP("Levels", "Gallery"),
         QT_TRANSLATE_NOOP("Levels", "Two pictures, with pieces on the wrong workspace and one floating.")},
        {"medium", QT_TRANSLATE_NOOP("Levels", "Exhibition"),
         QT_TRANSLATE_NOOP("Levels", "Two pictures with turned splits, odd sizes and a pile.")},
        {"hard", QT_TRANSLATE_NOOP("Levels", "Museum"),
         QT_TRANSLATE_NOOP("Levels", "Three pictures and everything: piles, the scratchpad and a stray.")},
    };
    return all;
}

Level Levels::puzzle(int tier, quint32 seed) {
    QRandomGenerator random(seed);
    tier = std::clamp(tier, 0, int(tiers().size()) - 1);
    struct Shape {
        QList<int> pieces;
        Cut::Options options;
        bool floating, pile, scratchpad, stray;
        int pileSize;
    };
    static const Shape shapes[] = {
        {{3, 4}, {0.25, 0.0}, true, false, false, false, 0},
        {{4, 5}, {0.3, 0.3}, true, true, false, false, 2},
        {{4, 5, 5}, {0.35, 0.4}, true, true, true, true, 3},
    };
    const Shape &shape = shapes[tier];

    Level level;
    level.id = tiers().at(tier).id;
    level.lesson = false;
    level.title = tiers().at(tier).title;
    level.teaches = QT_TRANSLATE_NOOP("Levels", "Everything, against the clock");
    level.intro = tiers().at(tier).about;

    QStringList arts;
    for (const auto &w : Art::all())
        arts << w.key;
    std::shuffle(arts.begin(), arts.end(), random);

    const int count = shape.pieces.size();
    for (int i = 0; i < count; ++i) {
        PictureSpec p;
        p.art = arts.at(i);
        p.chain = Cut::generate(shape.pieces.at(i), random.generate(), shape.options);
        for (int k = 0; k < shape.pieces.at(i); ++k)
            p.pieces << PieceSpec{k, Start::Tiled, 0};
        level.pictures << p;
    }

    // Hand out the troubles: every picture lends a piece to the next one's
    // workspace; then one floats, one pile, one in the scratchpad, spread
    // over the pictures so each has something.
    auto pick = [&](int picture) -> PieceSpec * {
        QList<PieceSpec *> free;
        for (auto &s : level.pictures[picture].pieces)
            if (s.start == Start::Tiled)
                free << &s;
        // At least two of a picture's own pieces stay tiled at home, so the
        // dealt spiral there has a shape to work with.
        if (free.size() <= 2)
            return nullptr;
        return free.at(random.bounded(int(free.size())));
    };
    for (int i = 0; i < count; ++i)
        if (auto *s = pick(i)) {
            s->start = Start::Away;
            s->away = (i + 1) % count;
        }
    if (shape.floating)
        if (auto *s = pick(0))
            s->start = Start::Floating;
    if (shape.pile) {
        const int where = count > 1 ? 1 : 0;
        for (int k = 0; k < shape.pileSize; ++k)
            if (auto *s = pick(where))
                s->start = Start::Pile;
    }
    if (shape.scratchpad)
        if (auto *s = pick(count - 1))
            s->start = Start::Scratchpad;
    if (shape.stray) {
        level.strays << Stray{arts.value(count, arts.last()), 0, Start::Tiled};
    }

    // Tiles on each workspace: its own tiled pieces and its visitors, mixed,
    // and never already in order.
    for (int i = 0; i < count; ++i) {
        QList<Deal> tiles;
        for (const auto &s : level.pictures.at(i).pieces)
            if (s.start == Start::Tiled)
                tiles << Deal{i, s.index};
        for (int j = 0; j < count; ++j)
            for (const auto &s : level.pictures.at(j).pieces)
                if (s.start == Start::Away && s.away == i)
                    tiles << Deal{j, s.index};
        for (int k = 0; k < level.strays.size(); ++k)
            if (level.strays.at(k).picture == i)
                tiles << Deal{-1, k};
        auto sorted = [&] {
            for (int k = 1; k < tiles.size(); ++k)
                if (tiles.at(k).picture != tiles.at(k - 1).picture || tiles.at(k).index < tiles.at(k - 1).index)
                    return false;
            return true;
        };
        for (int tries = 0; tries < 8 && (tries == 0 || sorted()); ++tries)
            std::shuffle(tiles.begin(), tiles.end(), random);
        level.pictures[i].tiles = tiles;
    }
    return level;
}

int Levels::workspacesNeeded(const Level &level) { return level.pictures.size() + level.spares; }
