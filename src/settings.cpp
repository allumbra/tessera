#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

Settings::Settings(const QString &path)
    : m_path(path.isEmpty() ? QDir::homePath() + "/.local/state/tessera/settings.json" : path) {}

bool Settings::validSource(const QString &source) {
    return source == "builtin" || source == "online" || source == "folder" || source == "mixed";
}

bool Settings::load() {
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024)
        return false;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value("version").toInt() != 1)
        return false;
    const QString s = root.value("source").toString();
    if (validSource(s))
        source = s;
    online = root.value("online").toBool(true);
    folder = root.value("folder").toString();
    return true;
}

bool Settings::save(QString *error) const {
    const QJsonObject root{{"version", 1}, {"source", source}, {"online", online}, {"folder", folder}};
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson()) < 0 || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}
