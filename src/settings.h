#pragma once

#include <QString>

// Where pictures come from, and the folder the player picked, kept in
// ~/.local/state/tessera/settings.json and saved atomically.
class Settings {
public:
    explicit Settings(const QString &path = {});

    QString path() const { return m_path; }

    // "builtin", "online", "folder" or "mixed".
    QString source = QStringLiteral("mixed");
    // Fetching art from the museums; on unless the player turns it off.
    bool online = true;
    QString folder;

    static bool validSource(const QString &source);

    bool load();
    bool save(QString *error = nullptr) const;

private:
    QString m_path;
};
