#pragma once

// A pretend Hyprland for the tests and the screenshot harness: windows on
// workspaces tiled by the dwindle model, floating windows, groups, the
// scratchpad, focus, and the events a real Hyprland would send when they
// change. Nothing here touches the real desktop.

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QRectF>
#include <QSet>
#include <QTimer>
#include <map>

#include "dwindle.h"
#include "game.h"
#include "hyprland.h"

class FakeEvents : public EventSource {
    Q_OBJECT

public:
    using EventSource::EventSource;
    void start() override { emit statusChanged(); }
    bool connected() const override { return m_connected; }
    QString error() const override { return m_connected ? QString() : QStringLiteral("no socket"); }
    void setConnected(bool connected) {
        m_connected = connected;
        emit statusChanged();
    }
    void feed(const QString &line) {
        const auto e = Hypr::parseLine(line.toUtf8());
        emit received(e.name, e.data);
    }

private:
    bool m_connected = true;
};

class FakeDesktop : public Hyprctl {
    Q_OBJECT

public:
    static constexpr int Scratchpad = -98;

    struct Window {
        QString address, title, cls;
        qint64 pid = 0;
        int workspace = 1;
        bool floating = false;
        QRectF floatRect;
        QString group; // the group's id, if in one
    };
    struct Group {
        QStringList members;
        int current = 0;
    };

    FakeEvents events;
    QList<Window> windows;
    std::map<int, Dwindle> layouts;
    QHash<QString, Group> groups;
    QHash<int, QString> lastFocused; // per workspace
    QString active;
    int activeWs = 1;
    bool scratchpadShown = false;
    int nextAddress = 0x1000, nextGroup = 1;
    QStringList dispatched;          // every dispatch the game made, with the address
    QString layoutName = "dwindle";
    bool groupTakesWindow = true;    // whether hl.dsp.group.toggle accepts a window
    bool autoGroup = true;
    // Where windows entering a workspace's tiles go, when a test wants to
    // force a wrong branch: the address of the tile to split.
    QString insertBeside;
    // A window the game must never touch; any dispatch on it fails the test.
    QSet<QString> untouchable;
    bool touchedForeign = false;

    explicit FakeDesktop(QObject *parent = nullptr) : Hyprctl(parent) {}

    qint64 own() const { return QCoreApplication::applicationPid(); }

    // Opens piece windows as the game asks for them, the way Qt and Hyprland
    // would: mapped, tiled, focused, announced.
    void attach(Game *game) {
        auto &model = game->pieceModel();
        connect(&model, &QAbstractItemModel::rowsInserted, this, [this, &model](const QModelIndex &, int first, int last) {
            for (int row = first; row <= last; ++row) {
                const QString title = model.rows().at(row).title;
                QTimer::singleShot(0, this, [this, title] { open(title, own(), "tessera"); });
            }
        });
        auto sweep = [this, &model] {
            QSet<QString> alive;
            for (const auto &r : model.rows())
                alive.insert(r.title);
            for (const auto &w : QList<Window>(windows))
                if (w.pid == own() && w.title != "Tessera" && !alive.contains(w.title))
                    close(w.address);
        };
        connect(&model, &QAbstractItemModel::modelReset, this, sweep);
        connect(&model, &QAbstractItemModel::rowsRemoved, this, sweep);
    }

    Window *find(const QString &address) {
        for (auto &w : windows)
            if (w.address == Hypr::address(address))
                return &w;
        return nullptr;
    }
    Window *byTitle(const QString &title) {
        for (auto &w : windows)
            if (w.title == title)
                return &w;
        return nullptr;
    }
    QString addressOf(const QString &title) {
        auto *w = byTitle(title);
        return w ? w->address : QString();
    }
    QStringList ownPieces() const {
        QStringList out;
        for (const auto &w : windows)
            if (w.pid == own() && w.title != "Tessera")
                out << w.address;
        return out;
    }

    // ── The layout ─────────────────────────────────────────────────────

    QString leafOf(const Window &w) const { return w.group.isEmpty() ? w.address : "group:" + w.group; }
    Dwindle &layout(int ws) {
        return layouts.try_emplace(ws).first->second;
    }

    // A window joins a workspace's tiles beside the one focused last there,
    // as Hyprland does with use_active_for_splits.
    void tile(Window &w) {
        Dwindle &d = layout(w.workspace);
        QString beside = insertBeside;
        if (beside.isEmpty() || !d.contains(beside)) {
            beside = lastFocused.value(w.workspace);
            if (auto *f = find(beside))
                beside = leafOf(*f);
        }
        d.insert(leafOf(w), d.contains(beside) ? beside : QString());
    }
    void untile(Window &w) {
        if (!w.group.isEmpty()) {
            Group &g = groups[w.group];
            const QString leaf = leafOf(w);
            g.members.removeAll(w.address);
            w.group.clear();
            if (g.members.isEmpty()) {
                layout(w.workspace).remove(leaf);
                groups.remove(leaf.mid(6));
            } else {
                g.current = std::clamp(g.current, 0, int(g.members.size()) - 1);
            }
            return;
        }
        if (!w.floating)
            layout(w.workspace).remove(w.address);
    }

    QRectF rectOf(const Window &w) const {
        if (w.floating)
            return w.floatRect;
        const auto it = layouts.find(w.workspace);
        return it == layouts.end() ? QRectF() : it->second.rects().value(leafOf(w));
    }

    // ── What Hyprland does by itself ───────────────────────────────────

    QString open(const QString &title, qint64 pid, const QString &cls, bool announce = true) {
        Window w;
        w.address = QString::number(nextAddress++, 16);
        w.title = title;
        w.cls = cls;
        w.pid = pid;
        w.workspace = activeWs;
        auto *focused = find(active);
        // A new window opened on a focused group joins it (auto_group).
        if (autoGroup && focused && !focused->group.isEmpty() && focused->workspace == activeWs) {
            w.group = focused->group;
            Group &g = groups[w.group];
            g.members.insert(g.current + 1, w.address);
            g.current = g.current + 1;
            windows << w;
        } else {
            windows << w;
            tile(windows.last());
        }
        if (announce) {
            events.feed(QString("openwindow>>%1,%2,%3,%4").arg(w.address).arg(activeWs).arg(cls, title));
            focus(w.address);
        }
        return w.address;
    }

    void close(const QString &address) {
        for (int i = 0; i < windows.size(); ++i)
            if (windows.at(i).address == address) {
                untile(windows[i]);
                windows.removeAt(i);
                events.feed("closewindow>>" + address);
                if (active == address)
                    active.clear();
                return;
            }
    }

    void focus(const QString &address) {
        active = address;
        if (auto *w = find(address)) {
            lastFocused[w->workspace] = address;
            if (!w->group.isEmpty())
                groups[w->group].current = groups[w->group].members.indexOf(address);
        }
        events.feed("activewindowv2>>" + address);
    }

    void switchTo(int workspace) {
        activeWs = workspace;
        events.feed(QString("workspacev2>>%1,%1").arg(workspace));
        // Hyprland focuses the window last focused there, or any.
        QString last = lastFocused.value(workspace);
        if (auto *w = find(last); !w || w->workspace != workspace) {
            last.clear();
            for (const auto &x : windows)
                if (x.workspace == workspace && (x.group.isEmpty() || groups.value(x.group).members.value(groups.value(x.group).current) == x.address)) {
                    last = x.address;
                    break;
                }
        }
        if (!last.isEmpty())
            focus(last);
        else {
            active.clear();
            events.feed("activewindowv2>>");
        }
    }

    void moveTo(const QString &address, int workspace, bool follow) {
        auto *w = find(address);
        if (!w)
            return;
        const bool wasFloating = w->floating;
        untile(*w);
        w->workspace = workspace;
        w->floating = wasFloating;
        if (!w->floating)
            tile(*w);
        events.feed(QString("movewindowv2>>%1,%2,%3").arg(w->address).arg(workspace)
                        .arg(workspace == Scratchpad ? QString("special:scratchpad") : QString::number(workspace)));
        if (follow) {
            switchTo(workspace);
            focus(address);
        }
    }

    void setFloat(const QString &address, bool floating) {
        auto *w = find(address);
        if (!w || w->floating == floating)
            return;
        if (floating) {
            untile(*w);
            w->floating = true;
            w->floatRect = QRectF(720, 400, 480, 270);
        } else {
            w->floating = false;
            tile(*w);
        }
        events.feed(QString("changefloatingmode>>%1,%2").arg(address).arg(floating ? 1 : 0));
    }

    void makeGroup(const QString &address) {
        auto *w = find(address);
        if (!w || w->floating)
            return;
        if (!w->group.isEmpty()) {
            // Toggling a group breaks it up: the others get tiles beside it.
            const QString id = w->group;
            const QStringList members = groups.value(id).members;
            const QString leaf = "group:" + id;
            Dwindle &d = layout(w->workspace);
            d.rename(leaf, members.first());
            groups.remove(id);
            for (const auto &m : members)
                if (auto *x = find(m))
                    x->group.clear();
            for (int i = 1; i < members.size(); ++i)
                d.insert(members.at(i), members.first());
            events.feed("togglegroup>>0," + members.join(","));
            return;
        }
        const QString id = QString::number(nextGroup++);
        layout(w->workspace).rename(w->address, "group:" + id);
        w->group = id;
        groups.insert(id, Group{{w->address}, 0});
        events.feed("togglegroup>>1," + w->address);
    }

    void intoGroup(const QString &address, char dir) {
        auto *w = find(address);
        if (!w || w->floating || !w->group.isEmpty())
            return;
        const QString n = layout(w->workspace).neighbour(leafOf(*w), dir);
        if (!n.startsWith("group:"))
            return; // into_group only joins an existing group
        layout(w->workspace).remove(w->address);
        w->group = n.mid(6);
        Group &g = groups[w->group];
        g.members.insert(g.current + 1, w->address);
        g.current = g.current + 1;
        events.feed("moveintogroup>>" + w->address);
        focus(w->address);
    }

    void outOfGroup(const QString &address) {
        auto *w = find(address);
        if (!w || w->group.isEmpty())
            return;
        const QString id = w->group;
        Group &g = groups[id];
        const QString leaf = "group:" + id;
        if (g.members.size() == 1) {
            // A group of one just stops being a group, in its tile.
            layout(w->workspace).rename(leaf, w->address);
            groups.remove(id);
            w->group.clear();
        } else {
            g.members.removeAll(w->address);
            g.current = std::clamp(g.current, 0, int(g.members.size()) - 1);
            w->group.clear();
            // The window comes out into a new tile split off the group's.
            layout(w->workspace).insert(w->address, leaf);
        }
        events.feed("moveoutofgroup>>" + w->address);
        focus(w->address);
    }

    // ── The player's shortcuts, on the focused window ───────────────────

    Window *activeWindow() { return find(active); }

    void pressFocus(char dir) {
        auto *w = activeWindow();
        if (!w || w->floating)
            return;
        const QString n = layout(w->workspace).neighbour(leafOf(*w), dir);
        if (n.isEmpty())
            return;
        if (n.startsWith("group:")) {
            const Group &g = groups.value(n.mid(6));
            focus(g.members.at(g.current));
        } else {
            focus(n);
        }
    }
    void pressSwap(char dir) {
        auto *w = activeWindow();
        if (!w || w->floating)
            return;
        Dwindle &d = layout(w->workspace);
        const QString n = d.neighbour(leafOf(*w), dir);
        if (!n.isEmpty())
            d.swap(leafOf(*w), n);
    }
    void pressToggleSplit() {
        if (auto *w = activeWindow(); w && !w->floating)
            layout(w->workspace).toggleSplit(leafOf(*w));
    }
    void pressResize(double dx, double dy) {
        if (auto *w = activeWindow(); w && !w->floating)
            layout(w->workspace).resize(leafOf(*w), dx, dy);
    }
    void pressMoveTo(int workspace) {
        if (auto *w = activeWindow())
            moveTo(w->address, workspace, true);
    }
    void pressFloat() {
        if (auto *w = activeWindow())
            setFloat(w->address, !w->floating);
    }
    void pressGroup() {
        if (auto *w = activeWindow())
            makeGroup(w->address);
    }
    void pressIntoGroup(char dir) {
        if (auto *w = activeWindow())
            intoGroup(w->address, dir);
    }
    void pressOutOfGroup() {
        if (auto *w = activeWindow())
            outOfGroup(w->address);
    }
    // Each switch inside a group is followed by an empty activewindowv2, as
    // on Hyprland 0.56.
    void pressGroupNext() {
        auto *w = activeWindow();
        if (!w || w->group.isEmpty())
            return;
        Group &g = groups[w->group];
        g.current = (g.current + 1) % g.members.size();
        const QString next = g.members.at(g.current);
        active = next;
        lastFocused[w->workspace] = next;
        events.feed("activewindowv2>>" + next);
        events.feed("activewindowv2>>");
    }
    // Alt+Tab: the next window on the workspace in view, floating ones too.
    void pressCycle() {
        QStringList here;
        for (const auto &w : windows)
            if (w.workspace == activeWs || (scratchpadShown && w.workspace == Scratchpad))
                if (w.group.isEmpty() || groups.value(w.group).members.value(groups.value(w.group).current) == w.address)
                    here << w.address;
        if (here.isEmpty())
            return;
        focus(here.at((here.indexOf(active) + 1) % here.size()));
    }
    void pressScratchpad() {
        scratchpadShown = !scratchpadShown;
        events.feed(scratchpadShown ? QString("activespecialv2>>%1,special:scratchpad,HDMI-A-1").arg(Scratchpad)
                                    : QString("activespecialv2>>,,HDMI-A-1"));
        if (scratchpadShown) {
            for (const auto &w : windows)
                if (w.workspace == Scratchpad) {
                    focus(w.address);
                    break;
                }
        }
    }
    void pressToScratchpad() {
        if (auto *w = activeWindow())
            moveTo(w->address, Scratchpad, false);
    }

    // ── hyprctl ────────────────────────────────────────────────────────

    QJsonValue query(const QString &what) override {
        auto json = [this](const Window &w) {
            QJsonObject o;
            o["address"] = "0x" + w.address;
            o["title"] = w.title;
            o["class"] = w.cls;
            o["pid"] = QJsonValue(w.pid);
            o["mapped"] = true;
            o["monitor"] = 0;
            o["floating"] = w.floating;
            QJsonArray grouped;
            bool hidden = false;
            if (!w.group.isEmpty()) {
                const Group &g = groups.value(w.group);
                for (const auto &m : g.members)
                    grouped << "0x" + m;
                hidden = g.members.value(g.current) != w.address;
            }
            o["grouped"] = grouped;
            o["hidden"] = hidden;
            QJsonObject ws;
            ws["id"] = w.workspace;
            ws["name"] = w.workspace == Scratchpad ? QString("special:scratchpad") : QString::number(w.workspace);
            o["workspace"] = ws;
            const QRectF r = rectOf(w);
            o["at"] = QJsonArray{r.x(), r.y()};
            o["size"] = QJsonArray{r.width(), r.height()};
            return o;
        };
        if (what == "clients") {
            QJsonArray out;
            for (const auto &w : windows)
                out << json(w);
            return out;
        }
        if (what == "activewindow") {
            for (const auto &w : windows)
                if (w.address == active)
                    return json(w);
            return QJsonObject{};
        }
        if (what == "activeworkspace")
            return QJsonObject{{"id", activeWs}, {"name", QString::number(activeWs)}, {"tiledLayout", layoutName}};
        if (what == "workspaces") {
            QMap<int, int> counts;
            for (const auto &w : windows)
                ++counts[w.workspace];
            QJsonArray out;
            for (auto it = counts.begin(); it != counts.end(); ++it)
                out << QJsonObject{{"id", it.key()}, {"windows", it.value()}, {"tiledLayout", layoutName}};
            return out;
        }
        if (what == "monitors") {
            QJsonObject special{{"id", scratchpadShown ? Scratchpad : 0},
                                {"name", scratchpadShown ? QString("special:scratchpad") : QString()}};
            return QJsonArray{QJsonObject{{"id", 0}, {"name", "HDMI-A-1"}, {"focused", true}, {"specialWorkspace", special},
                                          {"activeWorkspace", QJsonObject{{"id", activeWs}}}}};
        }
        if (what == "getoption general:layout")
            return QJsonObject{{"option", "general:layout"}, {"str", layoutName}};
        return {};
    }

    bool record(const QString &what, const QString &address) {
        dispatched << what + " " + Hypr::address(address);
        if (untouchable.contains(Hypr::address(address)))
            touchedForeign = true;
        auto *w = find(address);
        return w != nullptr;
    }

    bool moveSilently(const QString &address, const QString &workspace, QString *) override {
        if (!record("move " + workspace, address))
            return false;
        moveTo(Hypr::address(address), workspace == "special:scratchpad" ? Scratchpad : workspace.toInt(), false);
        return true;
    }
    bool setFloating(const QString &address, bool floating, QString *) override {
        if (!record(floating ? "float" : "tile", address))
            return false;
        setFloat(Hypr::address(address), floating);
        return true;
    }
    bool focusWindow(const QString &address, QString *) override {
        if (!record("focus", address))
            return false;
        focus(Hypr::address(address));
        return true;
    }
    bool toggleGroup(const QString &address, QString *error) override {
        if (!record("group", address))
            return false;
        if (!groupTakesWindow) {
            if (error)
                *error = "hl.dsp.group.toggle: unrecognized arguments";
            return false;
        }
        makeGroup(Hypr::address(address));
        return true;
    }
    bool moveIntoGroup(const QString &address, char dir, QString *) override {
        if (!record(QString("intogroup %1").arg(dir), address))
            return false;
        intoGroup(Hypr::address(address), dir);
        return true;
    }
    bool toggleFocusedGroup(QString *) override {
        record("group-focused", active);
        makeGroup(active);
        return true;
    }
    bool moveFocusedIntoGroup(char dir, QString *) override {
        record(QString("intogroup-focused %1").arg(dir), active);
        intoGroup(active, dir);
        return true;
    }
};
