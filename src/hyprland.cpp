#include "hyprland.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

Hypr::Event Hypr::parseLine(const QByteArray &line) {
    const QString text = QString::fromUtf8(line).trimmed();
    const int separator = text.indexOf(">>");
    if (separator < 0)
        return {text, {}};
    return {text.left(separator), text.mid(separator + 2)};
}

QStringList Hypr::fields(const QString &data, int parts) {
    QStringList out;
    int from = 0;
    while (out.size() < parts - 1) {
        const int comma = data.indexOf(',', from);
        if (comma < 0)
            break;
        out << data.mid(from, comma - from);
        from = comma + 1;
    }
    out << data.mid(from);
    while (out.size() < parts)
        out << QString();
    return out;
}

QString Hypr::address(const QString &raw) {
    QString a = raw.trimmed().toLower();
    if (a.startsWith("0x"))
        a = a.mid(2);
    return a;
}

// Hyprland's instance directory. After Hyprland restarts, the signature this
// process was started with names a directory that's gone, so the newest
// instance that has an event socket is used instead.
static QString instanceDirectory() {
    const QString signature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    if (signature.isEmpty())
        return {};
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR") + "/hypr/";
    const QString named = runtime + signature;
    if (QFileInfo::exists(named + "/.socket2.sock"))
        return named;
    QString newest;
    QDateTime newestTime;
    for (const auto &info : QDir(runtime).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!QFileInfo::exists(info.filePath() + "/.socket2.sock"))
            continue;
        if (newest.isEmpty() || info.lastModified() > newestTime) {
            newest = info.filePath();
            newestTime = info.lastModified();
        }
    }
    return newest.isEmpty() ? named : newest;
}

QString Hypr::eventSocketPath() {
    if (!qEnvironmentVariableIsEmpty("TESSERA_HYPR_SOCKET"))
        return qEnvironmentVariable("TESSERA_HYPR_SOCKET");
    const QString dir = instanceDirectory();
    return dir.isEmpty() ? QString() : dir + "/.socket2.sock";
}

QString Hypr::requestSocketPath() {
    if (!qEnvironmentVariableIsEmpty("TESSERA_HYPR_REQUEST_SOCKET"))
        return qEnvironmentVariable("TESSERA_HYPR_REQUEST_SOCKET");
    const QString dir = instanceDirectory();
    return dir.isEmpty() ? QString() : dir + "/.socket.sock";
}

HyprlandEvents::HyprlandEvents(const QString &path, QObject *parent)
    : EventSource(parent), m_path(path), m_followInstance(path == Hypr::eventSocketPath()) {
    m_retry.setSingleShot(true);
    m_retry.setInterval(2000);
    connect(&m_retry, &QTimer::timeout, this, &HyprlandEvents::start);
    connect(&m_socket, &QLocalSocket::readyRead, this, &HyprlandEvents::readLines);
    connect(&m_socket, &QLocalSocket::connected, this, [this] {
        m_error.clear();
        emit statusChanged();
    });
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        fail(tr("Can't read Hyprland's events at %1: %2").arg(m_path, m_socket.errorString()));
    });
    connect(&m_socket, &QLocalSocket::disconnected, this, [this] {
        fail(tr("Hyprland closed its event socket."));
    });
}

// The socket says goodbye as it's destroyed; by then there's no one left to
// tell, and the retry timer it would start is already gone.
HyprlandEvents::~HyprlandEvents() {
    disconnect(&m_socket, nullptr, this, nullptr);
    m_socket.abort();
}

void HyprlandEvents::start() {
    if (m_path.isEmpty()) {
        fail(tr("Tessera needs Hyprland, and Hyprland isn't running here (HYPRLAND_INSTANCE_SIGNATURE is not set)."));
        return;
    }
    if (m_socket.state() != QLocalSocket::UnconnectedState)
        return;
    // The default path follows Hyprland across a restart.
    if (m_followInstance && !QFileInfo::exists(m_path)) {
        const QString now = Hypr::eventSocketPath();
        if (!now.isEmpty())
            m_path = now;
    }
    m_buffer.clear();
    m_socket.connectToServer(m_path, QIODevice::ReadOnly);
}

void HyprlandEvents::fail(const QString &error) {
    // Hyprland restarting or a reload drops the socket; keep trying, and the
    // window says what's wrong in the meantime. The same failure again is
    // only not announced twice.
    if (!m_path.isEmpty())
        m_retry.start();
    if (m_error == error && !connected())
        return;
    m_error = error;
    emit statusChanged();
}

void HyprlandEvents::readLines() {
    m_buffer += m_socket.readAll();
    // A read can end mid-line, so only whole lines are handed on.
    int newline;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (line.isEmpty())
            continue;
        const auto e = Hypr::parseLine(line);
        emit received(e.name, e.data);
    }
}

Hyprctl::Hyprctl(QObject *parent)
    : QObject(parent),
      m_socket(Hypr::requestSocketPath()),
      m_followInstance(qEnvironmentVariableIsEmpty("TESSERA_HYPR_REQUEST_SOCKET")),
      m_program(qEnvironmentVariableIsEmpty("TESSERA_HYPRCTL") ? QStandardPaths::findExecutable("hyprctl")
                                                               : qEnvironmentVariable("TESSERA_HYPRCTL")) {}

// The request socket answers one command per connection and then hangs up,
// which is what hyprctl does under the hood; asking it directly skips
// starting a process several times a second while a lesson polls.
QJsonValue Hyprctl::query(const QString &what) {
    if (m_followInstance && !QFileInfo::exists(m_socket))
        m_socket = Hypr::requestSocketPath();
    if (m_socket.isEmpty())
        return {};
    QLocalSocket socket;
    socket.connectToServer(m_socket);
    if (!socket.waitForConnected(300))
        return {};
    socket.write("j/" + what.toUtf8());
    socket.flush();
    QByteArray reply;
    while (socket.state() == QLocalSocket::ConnectedState && socket.waitForReadyRead(500))
        reply += socket.readAll();
    reply += socket.readAll();
    const auto document = QJsonDocument::fromJson(reply);
    if (document.isArray())
        return document.array();
    if (document.isObject())
        return document.object();
    return {};
}

bool Hyprctl::run(const QString &lua, const QStringList &legacy, QString *error) {
    if (m_program.isEmpty()) {
        if (error)
            *error = tr("hyprctl isn't installed");
        return false;
    }
    auto attempt = [&](const QStringList &arguments, QString *output) {
        QProcess process;
        process.start(m_program, arguments);
        if (!process.waitForFinished(2000)) {
            process.kill();
            process.waitForFinished(500);
            *output = tr("hyprctl didn't answer");
            return false;
        }
        *output = QString::fromUtf8(process.readAllStandardOutput() + process.readAllStandardError()).trimmed();
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0
               && !output->contains("error", Qt::CaseInsensitive) && !output->contains("invalid", Qt::CaseInsensitive)
               && !output->contains("unrecognized", Qt::CaseInsensitive);
    };
    QString output;
    if (attempt({"dispatch", lua}, &output))
        return true;
    QString classic;
    if (!legacy.isEmpty() && attempt(QStringList{"dispatch"} + legacy, &classic))
        return true;
    if (error)
        *error = classic.isEmpty() ? output : classic;
    return false;
}

static QString selector(const QString &address) { return "address:0x" + Hypr::address(address); }

bool Hyprctl::moveSilently(const QString &address, const QString &workspace, QString *error) {
    return run(QString("hl.dsp.window.move({ workspace = \"%1\", follow = false, window = \"%2\" })")
                   .arg(workspace, selector(address)),
               {"movetoworkspacesilent", QString("%1,%2").arg(workspace, selector(address))}, error);
}

bool Hyprctl::setFloating(const QString &address, bool floating, QString *error) {
    return run(QString("hl.dsp.window.float({ action = \"%1\", window = \"%2\" })")
                   .arg(floating ? "on" : "off", selector(address)),
               {floating ? "setfloating" : "settiled", selector(address)}, error);
}

bool Hyprctl::focusWindow(const QString &address, QString *error) {
    return run(QString("hl.dsp.focus({ window = \"%1\" })").arg(selector(address)),
               {"focuswindow", selector(address)}, error);
}

// The classic group dispatchers only act on the focused window, so these
// have no classic fallback; see toggleFocusedGroup.
bool Hyprctl::toggleGroup(const QString &address, QString *error) {
    return run(QString("hl.dsp.group.toggle({ window = \"%1\" })").arg(selector(address)), {}, error);
}

bool Hyprctl::moveIntoGroup(const QString &address, char direction, QString *error) {
    const QString d = direction == 'l' ? "l" : direction == 'r' ? "r" : direction == 'u' ? "u" : "d";
    return run(QString("hl.dsp.window.move({ into_group = \"%1\", window = \"%2\" })").arg(d, selector(address)), {},
               error);
}

bool Hyprctl::toggleFocusedGroup(QString *error) {
    return run("hl.dsp.group.toggle()", {"togglegroup"}, error);
}

bool Hyprctl::moveFocusedIntoGroup(char direction, QString *error) {
    const QString d(QChar::fromLatin1(direction));
    return run(QString("hl.dsp.window.move({ into_group = \"%1\" })").arg(d), {"moveintogroup", d}, error);
}
