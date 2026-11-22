#include "progress.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>

Progress::Progress(const QString &path)
    : m_path(path.isEmpty() ? QDir::homePath() + "/.local/state/tessera/progress.json" : path) {}

void Progress::setStars(const QString &lesson, int stars) {
    m_stars[lesson] = std::max(m_stars.value(lesson), std::clamp(stars, 0, 3));
}

int Progress::totalStars() const {
    int total = 0;
    for (int s : m_stars)
        total += s;
    return total;
}

bool Progress::recordTime(const QString &tier, qint64 ms) {
    if (ms <= 0)
        return false;
    const qint64 old = m_best.value(tier);
    if (old > 0 && old <= ms)
        return false;
    m_best[tier] = ms;
    return true;
}

bool Progress::load() {
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value("version").toInt() != 1)
        return false;
    m_stars.clear();
    const QJsonObject stars = root.value("stars").toObject();
    for (auto it = stars.begin(); it != stars.end(); ++it)
        m_stars.insert(it.key(), std::clamp(it.value().toInt(), 0, 3));
    m_best.clear();
    const QJsonObject best = root.value("best").toObject();
    for (auto it = best.begin(); it != best.end(); ++it)
        if (it.value().toInteger() > 0)
            m_best.insert(it.key(), it.value().toInteger());
    m_current = root.value("current").toString();
    return true;
}

bool Progress::save(QString *error) const {
    QJsonObject stars, best;
    for (auto it = m_stars.begin(); it != m_stars.end(); ++it)
        stars.insert(it.key(), it.value());
    for (auto it = m_best.begin(); it != m_best.end(); ++it)
        best.insert(it.key(), it.value());
    const QJsonObject root{{"version", 1}, {"stars", stars}, {"best", best}, {"current", m_current}};

    QDir().mkpath(QFileInfo(m_path).absolutePath());
    // QSaveFile writes beside the target and renames over it, so a crash
    // mid-save never leaves a half-written progress file.
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson()) < 0 || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}
