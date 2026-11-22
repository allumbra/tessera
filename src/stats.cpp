#include "stats.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

Stats::Stats(const QString &path)
    : m_path(path.isEmpty() ? QDir::homePath() + "/.local/state/tessera/stats.json" : path) {}

void Stats::record(const Commands::Move &move, Commands::Verdict verdict, qint64 reactionMs, bool aided) {
    if (move.family.isEmpty())
        return;
    Command &c = m_commands[move.family];
    ++c.uses;
    switch (verdict) {
    case Commands::Verdict::Good: ++c.good; break;
    case Commands::Verdict::Neutral: ++c.neutral; break;
    case Commands::Verdict::Detour: ++c.detour; break;
    }
    if (aided)
        ++c.aided;
    if (reactionMs >= 0) {
        c.reactions << int(std::min<qint64>(reactionMs, 10 * 60 * 1000));
        while (c.reactions.size() > MaxReactions)
            c.reactions.removeFirst();
    }
    c.last = move.at;
    if (!move.action.isEmpty())
        ++c.actions[move.action];
}

void Stats::addRun(const Run &run) {
    m_runs << run;
    while (m_runs.size() > MaxRuns)
        m_runs.removeFirst();
}

qint64 Stats::median(const QList<int> &values) {
    if (values.isEmpty())
        return -1;
    QList<int> v = values;
    std::sort(v.begin(), v.end());
    const int n = v.size();
    return n % 2 ? v.at(n / 2) : (qint64(v.at(n / 2 - 1)) + v.at(n / 2)) / 2;
}

Stats::Proficiency Stats::proficiency(const Command &c) {
    Proficiency p;
    const int n = c.good + c.neutral + c.detour;
    if (n <= 0)
        return p;
    p.accuracy = (c.good + 0.5 * c.neutral) / n;
    p.unaided = 1.0 - std::clamp(double(c.aided) / n, 0.0, 1.0);
    const qint64 m = median(c.reactions);
    p.pace = m < 0 ? 0.5 : std::clamp((8000.0 - m) / 6500.0, 0.0, 1.0);
    p.experience = 1.0 - std::exp(-n / 6.0);
    p.score = 100.0 * p.experience * p.accuracy * (0.6 + 0.3 * p.unaided + 0.1 * p.pace);
    p.level = p.score >= 80 ? Master : p.score >= 60 ? Fluent : p.score >= 40 ? Practised : p.score >= 20 ? Learning : Novice;
    return p;
}

QHash<QString, double> Stats::weakness() const {
    QHash<QString, double> out;
    for (const auto &f : Commands::families()) {
        const auto it = m_commands.find(f.id);
        out.insert(f.id, it == m_commands.end() ? 1.0 : 1.0 - proficiency(*it).score / 100.0);
    }
    return out;
}

bool Stats::load() {
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024)
        return false;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value("version").toInt() != 1)
        return false;
    m_commands.clear();
    const QJsonObject commands = root.value("commands").toObject();
    for (auto it = commands.begin(); it != commands.end(); ++it) {
        if (!Commands::family(it.key()))
            continue;
        const QJsonObject o = it.value().toObject();
        Command c;
        auto count = [&](const char *key) { return std::max(0, o.value(key).toInt()); };
        c.uses = count("uses");
        c.good = count("good");
        c.neutral = count("neutral");
        c.detour = count("detour");
        c.aided = count("aided");
        for (const auto &r : o.value("reactions").toArray())
            if (r.toInt(-1) >= 0)
                c.reactions << r.toInt();
        while (c.reactions.size() > MaxReactions)
            c.reactions.removeFirst();
        c.last = o.value("last").toInteger();
        const QJsonObject actions = o.value("actions").toObject();
        for (auto a = actions.begin(); a != actions.end(); ++a)
            if (Commands::familyOf(a.key()) == it.key())
                c.actions.insert(a.key(), std::max(0, a.value().toInt()));
        m_commands.insert(it.key(), c);
    }
    m_runs.clear();
    for (const auto &value : root.value("runs").toArray()) {
        const QJsonObject o = value.toObject();
        Run r;
        r.level = o.value("level").toString().left(40);
        r.lesson = o.value("lesson").toBool();
        r.practice = o.value("practice").toBool();
        r.when = o.value("when").toInteger();
        r.ms = std::max<qint64>(0, o.value("ms").toInteger());
        r.moves = std::max(0, o.value("moves").toInt());
        r.par = std::max(0, o.value("par").toInt());
        r.looks = std::max(0, o.value("looks").toInt());
        if (!r.level.isEmpty())
            m_runs << r;
    }
    while (m_runs.size() > MaxRuns)
        m_runs.removeFirst();
    return true;
}

bool Stats::save(QString *error) const {
    QJsonObject commands;
    for (auto it = m_commands.begin(); it != m_commands.end(); ++it) {
        QJsonArray reactions;
        for (int r : it->reactions)
            reactions << r;
        QJsonObject actions;
        for (auto a = it->actions.begin(); a != it->actions.end(); ++a)
            actions.insert(a.key(), a.value());
        commands.insert(it.key(), QJsonObject{{"uses", it->uses}, {"good", it->good}, {"neutral", it->neutral},
                                              {"detour", it->detour}, {"aided", it->aided}, {"reactions", reactions},
                                              {"last", it->last}, {"actions", actions}});
    }
    QJsonArray runs;
    for (const Run &r : m_runs)
        runs << QJsonObject{{"level", r.level}, {"lesson", r.lesson}, {"practice", r.practice}, {"when", r.when},
                            {"ms", r.ms}, {"moves", r.moves}, {"par", r.par}, {"looks", r.looks}};
    const QJsonObject root{{"version", 1}, {"commands", commands}, {"runs", runs}};
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson()) < 0 || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}
