#pragma once

#include <QHash>
#include <QList>
#include <QString>

#include "commands.h"

// How the player uses each kind of shortcut, and how each run went. Kept in
// ~/.local/state/tessera/stats.json beside progress.json (which stays as it
// was), saved atomically.
class Stats {
public:
    struct Command {
        int uses = 0;
        int good = 0, neutral = 0, detour = 0;
        int aided = 0;           // done with its keycaps on screen, or after asking for help
        QList<int> reactions;    // milliseconds, the most recent last
        qint64 last = 0;         // when, ms since the epoch
        QHash<QString, int> actions; // uses by exact Omarchy action
    };
    struct Run {
        QString level;   // the level's id: "medium", "practice", "swap"
        bool lesson = false, practice = false;
        qint64 when = 0, ms = 0;
        int moves = 0;   // changing moves taken
        int par = 0;     // the fewest changing moves the deal needs
        int looks = 0;   // focus and workspace moves, which par doesn't count
    };

    // The level a command is at, from its score; see proficiency().
    enum Level { None, Novice, Learning, Practised, Fluent, Master };
    struct Proficiency {
        double score = 0; // 0..100
        Level level = None;
        double accuracy = 0, unaided = 0, pace = 0, experience = 0;
    };

    static constexpr int MaxReactions = 41;
    static constexpr int MaxRuns = 40;

    explicit Stats(const QString &path = {});
    QString path() const { return m_path; }

    void record(const Commands::Move &move, Commands::Verdict verdict, qint64 reactionMs, bool aided);
    void addRun(const Run &run);

    const QHash<QString, Command> &commands() const { return m_commands; }
    Command command(const QString &family) const { return m_commands.value(family); }
    const QList<Run> &runs() const { return m_runs; }

    // Proficiency rewards being right and needing no help, with volume only
    // building confidence in that:
    //
    //   n          = good + neutral + detour (judged uses)
    //   accuracy   = (good + neutral / 2) / n
    //   unaided    = 1 - aided / n
    //   pace       = clamp((8 s - median reaction) / 6.5 s, 0, 1)
    //   experience = 1 - e^(-n / 6)
    //   score      = 100 * experience * accuracy * (0.6 + 0.3 * unaided + 0.1 * pace)
    //
    // Novice below 20, Learning from 20, Practised from 40, Fluent from 60,
    // Master from 80. Always following shown keys tops out at Fluent; a 50%
    // hit rate tops out at Practised, however often it's used.
    static Proficiency proficiency(const Command &command);
    static qint64 median(const QList<int> &values);

    // How much practice each family needs, 0..1: unused and weak ones most.
    QHash<QString, double> weakness() const;

    bool load();
    bool save(QString *error = nullptr) const;

private:
    QString m_path;
    QHash<QString, Command> m_commands;
    QList<Run> m_runs;
};
