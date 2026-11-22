// After Omacut's src/portalfilepicker.cpp by David Heinemeier Hansson (MIT,
// https://github.com/omacom-io/omacut): the same request and response dance
// with the portal, asking for a directory instead of a video file.

#include "portalfilepicker.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QUrl>

namespace {

QString portalToken() { return QStringLiteral("tessera_%1").arg(QRandomGenerator::global()->generate()); }

QByteArray portalPathBytes(const QString &path) {
    QByteArray bytes = path.toUtf8();
    bytes.append('\0');
    return bytes;
}

QString startFolder(const QString &current) {
    if (!current.isEmpty() && QDir(current).exists())
        return current;
    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    return QDir(pictures).exists() ? pictures : QDir::homePath();
}

} // namespace

PortalFilePicker::PortalFilePicker(QObject *parent) : FilePicker(parent) {}

bool PortalFilePicker::connectToRequestPath(const QString &path) {
    m_pendingPath = path;
    return QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.portal.Desktop"), m_pendingPath,
                                                 QStringLiteral("org.freedesktop.portal.Request"),
                                                 QStringLiteral("Response"), this,
                                                 SLOT(handleResponse(uint, QVariantMap)));
}

void PortalFilePicker::closePendingRequest() {
    if (m_pendingPath.isEmpty())
        return;
    QDBusInterface request(QStringLiteral("org.freedesktop.portal.Desktop"), m_pendingPath,
                           QStringLiteral("org.freedesktop.portal.Request"), QDBusConnection::sessionBus());
    request.asyncCall(QStringLiteral("Close"));
}

void PortalFilePicker::chooseFolder(const QString &current) {
    // A request whose Response never arrives (the portal crashed with the
    // dialog up) must not wedge every later dialog behind it.
    if (m_pending) {
        closePendingRequest();
        clearPending();
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface portal(QStringLiteral("org.freedesktop.portal.Desktop"), QStringLiteral("/org/freedesktop/portal/desktop"),
                          QStringLiteral("org.freedesktop.portal.FileChooser"), bus);
    if (!portal.isValid()) {
        emit failed(tr("The desktop's file chooser (xdg-desktop-portal) isn't available."));
        return;
    }

    QVariantMap options;
    options.insert(QStringLiteral("accept_label"), tr("Use this folder"));
    options.insert(QStringLiteral("modal"), true);
    options.insert(QStringLiteral("multiple"), false);
    // FileChooser version 3 and later pick a folder with this.
    options.insert(QStringLiteral("directory"), true);
    options.insert(QStringLiteral("current_folder"), portalPathBytes(startFolder(current)));

    // Listen at the request path the portal derives from our handle_token
    // before calling, so the response can't slip past.
    const QString token = portalToken();
    options.insert(QStringLiteral("handle_token"), token);
    QString sender = bus.baseService().mid(1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    const QString predictedPath = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);

    m_pending = true;
    if (!connectToRequestPath(predictedPath)) {
        clearPending();
        emit failed(tr("Couldn't listen for the file chooser's answer."));
        return;
    }

    const int generation = ++m_pendingGeneration;
    auto *watcher = new QDBusPendingCallWatcher(
        portal.asyncCall(QStringLiteral("OpenFile"), QString(), tr("Choose a folder of pictures"), options), this);
    m_pendingWatcher = watcher;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation] {
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (m_pendingWatcher == watcher)
            m_pendingWatcher = nullptr;
        if (generation != m_pendingGeneration || !m_pending)
            return;
        if (reply.isError()) {
            clearPending();
            emit failed(tr("The file chooser failed: %1").arg(reply.error().message()));
            return;
        }
        // Old portals can answer at another path than the token predicts.
        const QString actualPath = reply.value().path();
        if (actualPath != m_pendingPath) {
            QDBusConnection::sessionBus().disconnect(QStringLiteral("org.freedesktop.portal.Desktop"), m_pendingPath,
                                                     QStringLiteral("org.freedesktop.portal.Request"),
                                                     QStringLiteral("Response"), this,
                                                     SLOT(handleResponse(uint, QVariantMap)));
            if (!connectToRequestPath(actualPath)) {
                clearPending();
                emit failed(tr("Couldn't listen for the file chooser's answer."));
            }
        }
    });
}

void PortalFilePicker::handleResponse(uint response, const QVariantMap &results) {
    clearPending();
    if (response != 0) {
        emit cancelled();
        return;
    }
    const QStringList uris = results.value(QStringLiteral("uris")).toStringList();
    const QUrl url(uris.value(0));
    if (!url.isLocalFile()) {
        emit cancelled();
        return;
    }
    emit folderSelected(url.toLocalFile());
}

void PortalFilePicker::clearPending() {
    if (m_pendingWatcher) {
        m_pendingWatcher->deleteLater();
        m_pendingWatcher = nullptr;
    }
    if (!m_pendingPath.isEmpty())
        QDBusConnection::sessionBus().disconnect(QStringLiteral("org.freedesktop.portal.Desktop"), m_pendingPath,
                                                 QStringLiteral("org.freedesktop.portal.Request"),
                                                 QStringLiteral("Response"), this,
                                                 SLOT(handleResponse(uint, QVariantMap)));
    m_pendingPath.clear();
    m_pending = false;
}
