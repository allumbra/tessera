#include "levels.h"

#include <QRandomGenerator>
#include <QSet>
#include <QtGlobal>
#include <algorithm>

using namespace Levels;
using Cut::Split;

const QList<Art::Work> &Art::all() {
    static const QList<Work> works{
        {"wave", QT_TRANSLATE_NOOP("Art", "Wave"), QT_TRANSLATE_NOOP("Art", "The Great Wave off Kanagawa"),
         "Katsushika Hokusai", "c. 1831",
         "https://commons.wikimedia.org/wiki/File:Tsunami_by_hokusai_19th_century.jpg", "Public domain"},
        {"starry", QT_TRANSLATE_NOOP("Art", "Starry Night"), QT_TRANSLATE_NOOP("Art", "The Starry Night"),
         "Vincent van Gogh", "1889",
         "https://commons.wikimedia.org/wiki/File:Van_Gogh_-_Starry_Night_-_Google_Art_Project.jpg", "Public domain"},
        {"sunrise", QT_TRANSLATE_NOOP("Art", "Sunrise"), QT_TRANSLATE_NOOP("Art", "Impression, Sunrise"),
         "Claude Monet", "1872", "https://commons.wikimedia.org/wiki/File:Monet_-_Impression,_Sunrise.jpg",
         "Public domain"},
        {"sierra", QT_TRANSLATE_NOOP("Art", "Sierra"), QT_TRANSLATE_NOOP("Art", "Among the Sierra Nevada, California"),
         "Albert Bierstadt", "1868",
         "https://commons.wikimedia.org/wiki/"
         "File:Albert_Bierstadt_-_Among_the_Sierra_Nevada,_California_-_Google_Art_Project.jpg",
         "Public domain"},
        {"nasa", QT_TRANSLATE_NOOP("Art", "Saturn"), QT_TRANSLATE_NOOP("Art", "Saturn during Equinox"),
         "NASA / JPL / Space Science Institute", "2008",
         "https://commons.wikimedia.org/wiki/File:Saturn_during_Equinox.jpg", "Public domain"},
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
         QT_TRANSLATE_NOOP("Levels", "Two pictures with turned splits, odd sizes, a pile and the scratchpad.")},
        {"hard", QT_TRANSLATE_NOOP("Levels", "Museum"),
         QT_TRANSLATE_NOOP("Levels", "Three pictures and everything: piles, the scratchpad and a stray.")},
    };
    return all;
}

namespace {

// What a deal holds, before it's shuffled.
struct Recipe {
    QList<int> pieces;
    Cut::Options options;
    bool floating = false, scratchpad = false, stray = false;
    int pile = 0;
    // The guarantees that every family of moves is needed at least once.
    bool flipFirst = false;   // the first split of the first picture turned
    bool sideRatio = false;   // some side-by-side split off centre
    bool stackRatio = false;  // some stacked split off centre
    bool unsortedFirst = false; // the first picture's own tiles out of order
};

Split opposite(Split s) { return s == Split::Side ? Split::Stack : Split::Side; }

const double Ratios[] = {1.0 / 3, 0.4, 0.6, 2.0 / 3};

// Moves one split of one of the chains off centre, preferring one that
// already runs the right way, without making a sliver.
bool offCentre(QList<Cut::Chain> &chains, Split split, QRandomGenerator &random, int skipPicture, int skipLevel) {
    for (const Cut::Level &l : [&] {
             QList<Cut::Level> all;
             for (const auto &c : chains)
                 all << c.levels;
             return all;
         }())
        if (l.split == split && std::abs(l.ratio - 0.5) > 0.05)
            return true;
    for (bool turn : {false, true}) {
        QList<QPair<int, int>> spots;
        for (int p = 0; p < chains.size(); ++p)
            for (int k = 0; k < chains.at(p).levels.size(); ++k)
                if (!(p == skipPicture && k == skipLevel) && (turn || chains.at(p).levels.at(k).split == split))
                    spots << qMakePair(p, k);
        std::shuffle(spots.begin(), spots.end(), random);
        for (const auto &[p, k] : spots)
            for (int r = 0; r < 4; ++r) {
                Cut::Chain c = chains.at(p);
                c.levels[k].split = split;
                c.levels[k].ratio = Ratios[(r + random.bounded(4)) % 4];
                if (!Cut::hasSliver(c)) {
                    chains[p] = c;
                    return true;
                }
            }
    }
    return false;
}

Level build(const Recipe &recipe, quint32 seed, const QSizeF &screen) {
    QRandomGenerator random(seed);
    Level level;
    level.lesson = false;

    const int count = recipe.pieces.size();
    QList<Cut::Chain> chains;
    for (int i = 0; i < count; ++i) {
        Cut::Chain chain = Cut::generate(recipe.pieces.at(i), random.generate(), recipe.options, screen);
        if (i == 0 && recipe.flipFirst) {
            // Turned against the way dwindle deals it, so Super+J is needed.
            const Split natural = Cut::dealt(chain.pieces(), screen).levels.first().split;
            for (int tries = 0; tries < 64; ++tries) {
                Cut::Chain c = chain;
                c.levels[0].split = opposite(natural);
                if (!Cut::hasSliver(c)) {
                    chain = c;
                    break;
                }
                chain = Cut::generate(recipe.pieces.at(i), random.generate(), recipe.options, screen);
            }
        }
        chains << chain;
    }
    if (recipe.sideRatio)
        offCentre(chains, Split::Side, random, recipe.flipFirst ? 0 : -1, 0);
    if (recipe.stackRatio)
        offCentre(chains, Split::Stack, random, recipe.flipFirst ? 0 : -1, 0);

    for (int i = 0; i < count; ++i) {
        PictureSpec p;
        p.art = QString::number(i);
        p.chain = chains.at(i);
        for (int k = 0; k < recipe.pieces.at(i); ++k)
            p.pieces << PieceSpec{k, Start::Tiled, 0};
        level.pictures << p;
    }

    // Hand out the troubles: every picture lends a piece to the next one's
    // workspace; then one floats, one pile, one in the scratchpad, spread
    // over the pictures so each has something.
    auto freeOf = [&](int picture) {
        QList<PieceSpec *> free;
        for (auto &s : level.pictures[picture].pieces)
            if (s.start == Start::Tiled)
                free << &s;
        return free;
    };
    auto pick = [&](int picture) -> PieceSpec * {
        const QList<PieceSpec *> free = freeOf(picture);
        // At least two of a picture's own pieces stay tiled at home, so the
        // dealt spiral there has a shape to work with.
        if (free.size() <= 2)
            return nullptr;
        return free.at(random.bounded(int(free.size())));
    };
    // The preferred picture, or else the one with the most to spare.
    auto pickAny = [&](int preferred) -> PieceSpec * {
        if (auto *s = pick(preferred))
            return s;
        int best = -1;
        for (int i = 0; i < count; ++i)
            if (freeOf(i).size() > 2 && (best < 0 || freeOf(i).size() > freeOf(best).size()))
                best = i;
        return best < 0 ? nullptr : pick(best);
    };
    for (int i = 0; i < count && count > 1; ++i)
        if (auto *s = pick(i)) {
            s->start = Start::Away;
            s->away = (i + 1) % count;
        }
    if (recipe.floating)
        if (auto *s = pickAny(0))
            s->start = Start::Floating;
    if (recipe.pile > 0) {
        int where = count > 1 ? 1 : 0;
        if (freeOf(where).size() < 4)
            for (int i = 0; i < count; ++i)
                if (freeOf(i).size() > freeOf(where).size())
                    where = i;
        for (int k = 0; k < recipe.pile; ++k)
            if (auto *s = pick(where))
                s->start = Start::Pile;
    }
    if (recipe.scratchpad)
        if (auto *s = pickAny(count - 1))
            s->start = Start::Scratchpad;
    if (recipe.stray)
        level.strays << Stray{QStringLiteral("s0"), 0, Start::Tiled};

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
        if (i == 0 && recipe.unsortedFirst) {
            // Two of its own pieces open the deal, so the first split is
            // between them and stays put while visitors leave; and its own
            // pieces come in an order that needs a swap.
            QList<Deal> own, rest;
            for (const Deal &d : tiles)
                (d.picture == 0 ? own : rest) << d;
            bool ordered = true;
            for (int k = 1; k < own.size(); ++k)
                ordered = ordered && own.at(k).index > own.at(k - 1).index;
            if (ordered && own.size() >= 2)
                std::swap(own[0], own[1]);
            tiles = own.mid(0, 2) + rest + own.mid(2);
            if (own.size() > 2)
                std::shuffle(tiles.begin() + 2, tiles.end(), random);
        }
        level.pictures[i].tiles = tiles;
    }
    return level;
}

} // namespace

Level Levels::puzzle(int tier, quint32 seed, const QSizeF &screen) {
    tier = std::clamp(tier, 0, int(tiers().size()) - 1);
    Recipe r;
    if (tier == 0) {
        r.pieces = {3, 4};
        r.options = {0.25, 0.0};
        r.floating = true;
    } else {
        r.pieces = tier == 1 ? QList<int>{5, 5} : QList<int>{4, 5, 5};
        r.options = tier == 1 ? Cut::Options{0.3, 0.3} : Cut::Options{0.35, 0.4};
        r.floating = r.scratchpad = true;
        r.pile = tier == 1 ? 2 : 3;
        r.stray = tier == 2;
        r.flipFirst = r.sideRatio = r.stackRatio = r.unsortedFirst = true;
    }
    Level level = build(r, seed, screen);
    level.id = tiers().at(tier).id;
    level.title = tiers().at(tier).title;
    level.teaches = QT_TRANSLATE_NOOP("Levels", "Everything, against the clock");
    level.intro = tiers().at(tier).about;
    return level;
}

Level Levels::practice(quint32 seed, const QHash<QString, double> &weakness, const QSizeF &screen) {
    QRandomGenerator random(seed ^ 0x9e3779b9u);
    auto w = [&](std::initializer_list<const char *> families) {
        double most = 0;
        for (const char *f : families)
            most = std::max(most, std::clamp(weakness.value(QString::fromLatin1(f), 1.0), 0.0, 1.0));
        return most;
    };
    struct Trouble {
        const char *name;
        double weight;
    };
    QList<Trouble> troubles{
        {"float", w({"float", "focus-next"})},
        {"pile", w({"group-out", "group-next", "group-in", "group"})},
        {"scratchpad", w({"scratchpad", "to-scratchpad"})},
        {"split", w({"split"})},
        {"side", w({"resize-x"})},
        {"stack", w({"resize-y"})},
    };
    // The two weakest always; each of the others by chance, the weaker the
    // likelier.
    QList<Trouble> byWeight = troubles;
    std::stable_sort(byWeight.begin(), byWeight.end(), [](const Trouble &a, const Trouble &b) { return a.weight > b.weight; });
    QSet<QString> on;
    for (int k = 0; k < 2; ++k)
        on.insert(QString::fromLatin1(byWeight.at(k).name));
    for (const Trouble &t : troubles)
        if (random.generateDouble() < 0.1 + 0.8 * t.weight)
            on.insert(QString::fromLatin1(t.name));

    Recipe r;
    r.pieces = {5, 5};
    r.options = {0.1 + 0.5 * w({"split"}), 0.1};
    r.floating = on.contains("float");
    r.pile = on.contains("pile") ? 2 : 0;
    r.scratchpad = on.contains("scratchpad");
    r.stray = on.contains("scratchpad") && w({"to-scratchpad"}) >= 0.5;
    r.flipFirst = on.contains("split");
    r.sideRatio = on.contains("side");
    r.stackRatio = on.contains("stack");
    r.unsortedFirst = true;
    Level level = build(r, random.generate(), screen);
    level.id = QStringLiteral("practice");
    level.title = QT_TRANSLATE_NOOP("Levels", "Practice weak spots");
    level.teaches = QT_TRANSLATE_NOOP("Levels", "The moves you need most");
    level.intro = QT_TRANSLATE_NOOP("Levels", "A puzzle made of the moves your stats say you're least sure of. Keys "
                                              "show after a moment, or when you press H.");
    return level;
}

Level Levels::forScreen(const Level &level, const QSizeF &screen) {
    if (!level.lesson || screen.height() <= screen.width() || level.id == "resize" || level.id == "tall")
        return level;
    Level out = level;
    for (auto &p : out.pictures)
        for (auto &l : p.chain.levels)
            l.split = opposite(l.split);
    return out;
}

int Levels::par(const Level &level, const QSizeF &screen) {
    int moves = int(level.strays.size());
    for (int i = 0; i < level.pictures.size(); ++i) {
        const PictureSpec &p = level.pictures.at(i);
        for (const PieceSpec &s : p.pieces)
            if (s.start != Start::Tiled)
                ++moves;
        const Cut::Chain dealt = Cut::dealt(p.chain.pieces(), screen);
        for (int k = 0; k < p.chain.levels.size(); ++k) {
            if (p.chain.levels.at(k).split != dealt.levels.value(k).split)
                ++moves;
            if (std::abs(p.chain.levels.at(k).ratio - 0.5) > 0.03)
                ++moves;
        }
        // Its own pieces dealt here: those outside the longest run already
        // in order each need at least one swap.
        QList<int> order;
        for (const Deal &d : p.tiles)
            if (d.picture == i)
                order << d.index;
        QList<int> longest(order.size(), 1);
        int run = order.isEmpty() ? 0 : 1;
        for (int a = 0; a < order.size(); ++a)
            for (int b = 0; b < a; ++b)
                if (order.at(b) < order.at(a)) {
                    longest[a] = std::max(longest.at(a), longest.at(b) + 1);
                    run = std::max(run, longest.at(a));
                }
        moves += int(order.size()) - run;
    }
    return moves;
}

int Levels::workspacesNeeded(const Level &level) { return level.pictures.size() + level.spares; }
