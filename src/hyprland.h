#pragma once

#include <QJsonValue>
#include <QLocalSocket>
#include <QObject>
#include <QStringList>
#include <QTimer>

// Hyprland grabs every Super chord before an app sees the key, so Tessera never
// reads the shortcut itself. It watches what the shortcut did instead, through
// Hyprland's event socket, and asks Hyprland for the state of the desktop
// through its request socket.
namespace Hypr {

struct Event {
    QString name;
    QString data;
};

// One line of socket2, "NAME>>DATA", without the newline.
Event parseLine(const QByteArray &line);

// Splits event data on commas into at most `parts` fields, so a window title
// with commas in it stays whole in the last field.
QStringList fields(const QString &data, int parts);

// Events name windows by bare hex ("55ebd0b3f9b0") and hyprctl -j prints the
// same window as "0x55ebd0b3f9b0"; both become the bare form.
QString address(const QString &raw);

QString eventSocketPath();
QString requestSocketPath();

} // namespace Hypr

// A stream of Hyprland events. The real one reads socket2; tests and the
// screenshot harness feed recorded lines instead.
class EventSource : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    virtual void start() = 0;
    virtual bool connected() const = 0;
    virtual QString error() const = 0;

signals:
    void received(const QString &name, const QString &data);
    void statusChanged();
};

class HyprlandEvents : public EventSource {
    Q_OBJECT

public:
    explicit HyprlandEvents(const QString &path = Hypr::eventSocketPath(), QObject *parent = nullptr);
    ~HyprlandEvents() override;

    void start() override;
    bool connected() const override { return m_socket.state() == QLocalSocket::ConnectedState; }
    QString error() const override { return m_error; }

private:
    void readLines();
    void fail(const QString &error);

    QString m_path;
    bool m_followInstance = false;
    QString m_error;
    QLocalSocket m_socket;
    QByteArray m_buffer;
    QTimer m_retry;
};

// Questions to Hyprland, and the few things Tessera ever asks it to do, each
// to one window named by address. The game checks that the address is one of
// its own piece windows, by process id, right before calling any of these;
// this class only speaks the protocol.
class Hyprctl : public QObject {
    Q_OBJECT

public:
    explicit Hyprctl(QObject *parent = nullptr);

    // "clients", "activewindow", "activeworkspace", "workspaces", "monitors",
    // "getoption general:layout".
    virtual QJsonValue query(const QString &what);

    // `workspace` is an id ("4") or a name ("special:scratchpad").
    virtual bool moveSilently(const QString &address, const QString &workspace, QString *error = nullptr);
    virtual bool setFloating(const QString &address, bool floating, QString *error = nullptr);
    virtual bool focusWindow(const QString &address, QString *error = nullptr);
    // Makes the window a group of its own (or dissolves its group).
    virtual bool toggleGroup(const QString &address, QString *error = nullptr);
    // Moves the window into the group next to it, 'l', 'r', 'u' or 'd'.
    virtual bool moveIntoGroup(const QString &address, char direction, QString *error = nullptr);
    // The same two acting on whichever window has focus, for a Hyprland that
    // doesn't take a window in these. Only called right after the game has
    // checked that focus is on its own window.
    virtual bool toggleFocusedGroup(QString *error = nullptr);
    virtual bool moveFocusedIntoGroup(char direction, QString *error = nullptr);

protected:
    // Hyprland's Lua dispatcher first, as Omarchy's own scripts do, and the
    // classic dispatcher if that one isn't understood.
    bool run(const QString &lua, const QStringList &legacy, QString *error);

private:
    QString m_socket;
    bool m_followInstance = true;
    QString m_program;
};
