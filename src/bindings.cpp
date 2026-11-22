#include "bindings.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace {

// Keys a quiz can't fairly ask about: hardware media keys, the mouse, and the
// lone right Alt that push-to-talk holds.
bool askable(const QString &chord) {
    static const QRegularExpression excluded(R"((XF86|MOUSE|mouse_|Alt_R|code:))");
    if (chord.isEmpty() || excluded.match(chord).hasMatch())
        return false;
    // A bare key with no modifier (Escape, Print) is either a system key or a
    // dictation cancel, not a desktop shortcut worth learning, except Print.
    return chord.contains('+') || chord == "PRINT";
}

} // namespace

QList<Binding> Bindings::parse(const QString &text) {
    struct Row { QStringList chords; QString action; };
    QList<Row> rows;
    QHash<QString, int> chordUses;

    for (const QString &line : text.split('\n')) {
        const int arrow = line.indexOf(QStringLiteral("→"));
        if (arrow < 0)
            continue;
        const QString action = line.mid(arrow + 1).trimmed();
        if (action.isEmpty())
            continue;
        QStringList chords;
        for (const QString &part : line.left(arrow).split(" / ")) {
            const QString chord = part.simplified();
            if (askable(chord))
                chords << chord;
        }
        if (chords.isEmpty())
            continue;
        for (const auto &chord : chords)
            ++chordUses[chord];
        rows.append({chords, action});
    }

    // Merge rows that do the same thing under one action, keeping the first
    // row's place, and drop chords that mean two things.
    QList<Binding> result;
    QHash<QString, int> byAction;
    for (const Row &row : rows) {
        QStringList chords;
        for (const auto &chord : row.chords)
            if (chordUses.value(chord) == 1)
                chords << chord;
        if (chords.isEmpty())
            continue;
        if (byAction.contains(row.action)) {
            Binding &existing = result[byAction.value(row.action)];
            for (const auto &chord : chords)
                if (!existing.chords.contains(chord))
                    existing.chords << chord;
        } else {
            byAction.insert(row.action, result.size());
            result.append({chords, row.action});
        }
    }
    return result;
}

QStringList Bindings::keys(const QString &chord) {
    const int plus = chord.lastIndexOf(" + ");
    if (plus < 0)
        return {chord.trimmed()};
    QStringList keys = chord.left(plus).split(' ', Qt::SkipEmptyParts);
    keys << chord.mid(plus + 3).trimmed();
    return keys;
}

QString Bindings::chordFor(const QList<Binding> &bindings, const QString &action) {
    for (const auto &binding : bindings)
        if (binding.action == action)
            return binding.chordText();
    return defaultChord(action);
}

// Omarchy's defaults for the moves Tessera uses, from
// default/hypr/bindings/tiling.lua.
QString Bindings::defaultChord(const QString &action) {
    static const QHash<QString, QString> defaults = [] {
        QHash<QString, QString> d{
            {"Close window", "SUPER + W"},
            {"Focus on left window", "SUPER + LEFT"},
            {"Focus on right window", "SUPER + RIGHT"},
            {"Focus on above window", "SUPER + UP"},
            {"Focus on below window", "SUPER + DOWN"},
            {"Swap window to the left", "SUPER SHIFT + LEFT"},
            {"Swap window to the right", "SUPER SHIFT + RIGHT"},
            {"Swap window up", "SUPER SHIFT + UP"},
            {"Swap window down", "SUPER SHIFT + DOWN"},
            {"Toggle window floating/tiling", "SUPER + T"},
            {"Toggle window split", "SUPER + J"},
            {"Expand window left", "SUPER + MINUS"},
            {"Shrink window left", "SUPER + EQUAL"},
            {"Expand window left a little", "SUPER ALT + MINUS"},
            {"Shrink window left a little", "SUPER ALT + EQUAL"},
            {"Expand window left a lot", "SUPER CTRL + MINUS"},
            {"Shrink window left a lot", "SUPER CTRL + EQUAL"},
            {"Shrink window up", "SUPER SHIFT + MINUS"},
            {"Expand window down", "SUPER SHIFT + EQUAL"},
            {"Shrink window up a little", "SUPER SHIFT ALT + MINUS"},
            {"Expand window down a little", "SUPER SHIFT ALT + EQUAL"},
            {"Shrink window up a lot", "SUPER SHIFT CTRL + MINUS"},
            {"Expand window down a lot", "SUPER SHIFT CTRL + EQUAL"},
            {"Move window to scratchpad", "SUPER ALT + S"},
            {"Toggle scratchpad", "SUPER + S"},
            {"Toggle window grouping", "SUPER + G"},
            {"Move active window out of group", "SUPER ALT + G"},
            {"Move window to group on left", "SUPER ALT + LEFT"},
            {"Move window to group on right", "SUPER ALT + RIGHT"},
            {"Move window to group on top", "SUPER ALT + UP"},
            {"Move window to group on bottom", "SUPER ALT + DOWN"},
            {"Next window in group", "SUPER ALT + TAB"},
            {"Focus on next window", "ALT + TAB"},
            {"Cycle workspace layout", "SUPER + L"},
        };
        for (int n = 1; n <= 10; ++n) {
            const QString key = QString::number(n % 10);
            d.insert(QString("Switch to workspace %1").arg(n), "SUPER + " + key);
            d.insert(QString("Move window to workspace %1").arg(n), "SUPER SHIFT + " + key);
        }
        return d;
    }();
    return defaults.value(action);
}
