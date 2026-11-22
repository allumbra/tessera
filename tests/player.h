#pragma once

// A player who does exactly what the hints say, pressing the chord each one
// shows on the fake desktop. If the hints are right and every cut can be
// solved, this player always finishes; the tests and the screenshot harness
// both play with it.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include "fakehyprland.h"
#include "game.h"

class Player {
public:
    Player(Game &game, FakeDesktop &desktop) : game(game), desktop(desktop) {}

    Game &game;
    FakeDesktop &desktop;
    QStringList log;

    static int idOf(int slot) { return slot == 0 ? 10 : slot; }

    // Presses one chord, as Omarchy's defaults bind it. False for a chord
    // this player doesn't know.
    bool press(const QString &chord) {
        const QString c = chord.split(" / ").first();
        log << c;
        const int plus = c.lastIndexOf(" + ");
        const QString mods = plus < 0 ? QString() : c.left(plus);
        const QString key = plus < 0 ? c : c.mid(plus + 3);
        auto arrow = [&]() -> char {
            if (key == "LEFT") return 'l';
            if (key == "RIGHT") return 'r';
            if (key == "UP") return 'u';
            if (key == "DOWN") return 'd';
            return 0;
        };
        bool number = false;
        const int n = key.toInt(&number);
        if (c == "SUPER + ARROWS") {
            // "Focus one of its pieces": any tile on the workspace in view.
            for (const auto &w : desktop.windows)
                if (w.pid == desktop.own() && w.title != "Tessera" && w.workspace == desktop.activeWs) {
                    desktop.focus(w.address);
                    return true;
                }
            return false;
        }
        if (mods == "SUPER" && arrow()) desktop.pressFocus(arrow());
        else if (mods == "SUPER SHIFT" && arrow()) desktop.pressSwap(arrow());
        else if (mods == "SUPER ALT" && arrow()) desktop.pressIntoGroup(arrow());
        else if (mods == "SUPER" && number) desktop.switchTo(idOf(n));
        else if (mods == "SUPER SHIFT" && number) desktop.pressMoveTo(idOf(n));
        else if (c == "SUPER + J") desktop.pressToggleSplit();
        else if (c == "SUPER + T") desktop.pressFloat();
        else if (c == "SUPER + G") desktop.pressGroup();
        else if (c == "SUPER ALT + G") desktop.pressOutOfGroup();
        else if (c == "SUPER ALT + TAB") desktop.pressGroupNext();
        else if (c == "ALT + TAB") desktop.pressCycle();
        else if (c == "SUPER + S") desktop.pressScratchpad();
        else if (c == "SUPER ALT + S") desktop.pressToScratchpad();
        else if (key == "MINUS" || key == "EQUAL") {
            const double step = mods.contains("ALT") ? 25 : mods.contains("CTRL") ? 300 : 100;
            const double d = key == "MINUS" ? -step : step;
            if (mods.contains("SHIFT"))
                desktop.pressResize(0, d);
            else
                desktop.pressResize(d, 0);
        } else {
            return false;
        }
        return true;
    }

    void settle(int ms = 30) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            QThread::msleep(2);
        }
    }

    // One move: walk to where a picture is being dealt, or follow the first
    // hint that has keys.
    bool step() {
        settle();
        if (game.state() == "dealing") {
            if (!game.objectiveChord().isEmpty())
                return press(game.objectiveChord());
            return true;
        }
        if (game.state() != "playing")
            return false;
        for (const auto &h : game.currentHints())
            if (!h.chord.isEmpty())
                return press(h.chord);
        // Nothing to press here: look at a piece, or go where work is left.
        for (const auto &w : desktop.windows)
            if (w.pid == desktop.own() && w.title != "Tessera" && w.workspace == desktop.activeWs) {
                desktop.focus(w.address);
                return true;
            }
        return false;
    }

    // Plays until solved; false if it gets stuck or takes too long.
    bool solve(int maxMoves = 600) {
        for (int i = 0; i < maxMoves; ++i) {
            if (game.state() == "solved")
                return true;
            if (!step()) {
                settle(60);
                if (game.state() == "solved")
                    return true;
                if (!step())
                    return false;
            }
        }
        settle(60);
        return game.state() == "solved";
    }
};
