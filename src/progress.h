#pragma once

#include <QHash>
#include <QString>

// What the player has done across runs: the best stars per lesson, the best
// time per puzzle tier, and the level to resume at. Kept in
// ~/.local/state/tessera/progress.json and saved atomically.
class Progress {
public:
    explicit Progress(const QString &path = {});

    QString path() const { return m_path; }

    int stars(const QString &lesson) const { return m_stars.value(lesson); }
    // Keeps the best result; a worse replay never takes stars away.
    void setStars(const QString &lesson, int stars);
    int totalStars() const;

    // Milliseconds, 0 when never solved.
    qint64 best(const QString &tier) const { return m_best.value(tier); }
    // Records a time; true when it beats the best so far.
    bool recordTime(const QString &tier, qint64 ms);

    QString current() const { return m_current; }
    void setCurrent(const QString &level) { m_current = level; }

    bool load();
    bool save(QString *error = nullptr) const;

private:
    QString m_path;
    QHash<QString, int> m_stars;
    QHash<QString, qint64> m_best;
    QString m_current;
};
