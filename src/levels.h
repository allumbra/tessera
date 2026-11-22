#pragma once

#include <QList>
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

// A full puzzle of a tier, shuffled by `seed`: two or three pictures, each
// on its own workspace, using every move the lessons teach.
Level puzzle(int tier, quint32 seed);

// Total workspaces a level needs.
int workspacesNeeded(const Level &level);

} // namespace Levels
