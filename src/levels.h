#pragma once

#include <QHash>
#include <QList>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include "cut.h"

// The pictures, all public domain (see CREDITS.md), compiled into the binary.
namespace Art {

struct Work {
    QString key;
    const char *name;   // short, for piece names: "Wave 3"
    const char *title;  // the artwork's full title
    const char *artist;
    const char *year;
    const char *url;     // the file it was made from
    const char *license;
};

const QList<Work> &all();
const Work *find(const QString &key);
QString image(const QString &key); // a qrc: URL

} // namespace Art

namespace Levels {

// Where a piece is when the game hands it over.
enum class Start {
    Tiled,      // in the picture's own workspace, among the tiles
    Away,       // tiled in another picture's workspace
    Floating,   // floating over its own workspace
    Pile,       // stacked in a group in its own workspace
    Scratchpad, // in the scratchpad
    Spare,      // alone on an otherwise empty workspace
};

struct PieceSpec {
    int index = 0;            // its place in the cut
    Start start = Start::Tiled;
    int away = 0;             // for Away: the picture whose workspace it's on
};

// A piece dealt as a tile: one of a picture's, or a stray (picture -1,
// index into Level::strays).
struct Deal {
    int picture = 0;
    int index = 0;
};

struct PictureSpec {
    QString art;
    Cut::Chain chain;
    QList<PieceSpec> pieces;
    // The tiles dealt on this picture's workspace, in order, each new one
    // splitting the one before into the spiral dwindle makes: the
    // picture's own tiled pieces, visitors from other pictures, and strays.
    // The rest follow: floating, scratchpad and spare pieces, and the pile
    // last, since a new window joins the focused group.
    QList<Deal> tiles;
};

struct Stray {
    QString art;
    int picture = 0; // whose workspace it's dealt on
    Start start = Start::Tiled;
};

struct Level {
    QString id;
    bool lesson = true;
    const char *title = "";
    const char *teaches = "";  // the move, in a few words
    const char *intro = "";    // what to do, on the card before it starts
    QStringList actions;       // the shortcuts the card shows, as Omarchy names them
    QList<PictureSpec> pictures;
    QList<Stray> strays;
    int spares = 0;            // extra empty workspaces it needs
};

const QList<Level> &lessons();

struct Tier {
    QString id;
    const char *title;
    const char *about;
};
const QList<Tier> &tiers();

// A full puzzle of a tier, shuffled by `seed`, for a screen of this shape:
// two or three pictures, each on its own workspace. Exhibition and Museum
// deals always need every family of moves at least once: swap, focus, a
// workspace switch, carrying a window over, turning a split, tiling a
// floating window, a pile to take apart, a resize each way, the scratchpad.
// The pictures' art keys are placeholders ("0", "1", …, strays "s0") until
// the game has made the pictures.
Level puzzle(int tier, quint32 seed, const QSizeF &screen = QSizeF(16, 9));

// A two-picture puzzle weighted towards the moves the player is weakest
// at: `weakness` maps a command family to 0 (mastered) .. 1 (needs work).
Level practice(quint32 seed, const QHash<QString, double> &weakness, const QSizeF &screen = QSizeF(16, 9));

// A lesson made for a screen of this shape. On a portrait screen dwindle
// stacks where it would split side by side, so the cuts turn with it; the
// two resize lessons keep their direction, since that's what they teach.
Level forScreen(const Level &level, const QSizeF &screen);

// The fewest changing moves a deal can be solved in: one per piece that has
// to come home, be tiled, leave the scratchpad or come out of a pile, one
// per stray, one per split that has to turn or be resized, and one swap per
// piece dealt out of order. Looking around (focus, switching workspaces,
// showing the scratchpad) isn't counted.
int par(const Level &level, const QSizeF &screen = QSizeF(16, 9));

// Total workspaces a level needs.
int workspacesNeeded(const Level &level);

} // namespace Levels
