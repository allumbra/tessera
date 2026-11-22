#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// One thing the desktop does, and the chords that do it. Chords read the way
// Omarchy's own keybindings menu prints them ("SUPER SHIFT + RETURN"), and an
// action with an alternative chord carries both.
struct Binding {
    QStringList chords;
    QString action;

    QString chordText() const { return chords.join(" / "); }
};

namespace Bindings {

// Parses `omarchy-menu-keybindings --print`, which lists the user's live
// bindings most important first, into the bindings worth a question. Media
// keys, mouse buttons and push-to-talk are left out: nobody needs drilling on
// the volume key, and a chord that does two different things depending on
// context has no single right answer, so those go too. The menu's order is
// kept, since it doubles as the game's difficulty curve.
QList<Binding> parse(const QString &text);

// The individual keys of one chord, for drawing keycaps: "SUPER SHIFT + 1"
// becomes SUPER, SHIFT, 1.
QStringList keys(const QString &chord);

// The user's chord for an Omarchy action ("Close window"), as their keybindings
// menu prints it, or Omarchy's default when the menu didn't list it (the
// command failed, or the user unbound it). Empty when neither knows it.
QString chordFor(const QList<Binding> &bindings, const QString &action);
QString defaultChord(const QString &action);

} // namespace Bindings
