#pragma once

// After Omacut's src/portalfilepicker.h by David Heinemeier Hansson (MIT,
// https://github.com/omacom-io/omacut), reduced to choosing a folder.

#include <QVariantMap>

#include "filepicker.h"

class QDBusPendingCallWatcher;

// The folder dialog of org.freedesktop.portal.FileChooser over D-Bus, so the
// desktop's own picker opens (never QFileDialog).
class PortalFilePicker : public FilePicker {
    Q_OBJECT

public:
    explicit PortalFilePicker(QObject *parent = nullptr);

    void chooseFolder(const QString &current) override;

private slots:
    void handleResponse(uint response, const QVariantMap &results);

private:
    bool connectToRequestPath(const QString &path);
    void clearPending();
    // Best-effort Close of the pending portal request, if there is one.
    void closePendingRequest();

    QString m_pendingPath;
    bool m_pending = false;
    // Bumped per request, so a late reply for a replaced request is ignored.
    int m_pendingGeneration = 0;
    QDBusPendingCallWatcher *m_pendingWatcher = nullptr;
};
