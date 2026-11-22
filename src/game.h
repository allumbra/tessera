#pragma once

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QRectF>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <functional>

#include "bindings.h"
#include "hyprland.h"
#include "levels.h"
#include "progress.h"
#include "puzzle.h"

// The pieces on the desktop. Each one is a window of Tessera's own, made by
// QML from a row here, so adding or removing a row opens or closes exactly
// one window and leaves the others alone.
class PieceModel : public QAbstractListModel {
    Q_OBJECT

public:
    struct Row {
        QString id, title, image, label, address;
        QRectF cell;
        bool focused = false, inPlace = false;
    };
    enum Role { IdRole = Qt::UserRole + 1, TitleRole, ImageRole, LabelRole, CellRole, FocusedRole, InPlaceRole };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_rows.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    const QList<Row> &rows() const { return m_rows; }
    Row *find(const QString &id);
    Row *findAddress(const QString &address);
    void add(const Row &row);
    void clear();
    void update(const QString &id, bool focused, bool inPlace);
    void relabel(const std::function<QString(const QString &)> &label);

private:
    QList<Row> m_rows;
};

class Game : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString notice READ notice NOTIFY changed)
    Q_PROPERTY(QString saveError READ saveError NOTIFY changed)
    Q_PROPERTY(bool lesson READ lesson NOTIFY changed)
    Q_PROPERTY(int levelIndex READ levelIndex NOTIFY changed)
    Q_PROPERTY(QString levelTitle READ levelTitle NOTIFY changed)
    Q_PROPERTY(QString teaches READ teaches NOTIFY changed)
    Q_PROPERTY(QString intro READ intro NOTIFY changed)
    Q_PROPERTY(QVariantList introChords READ introChords NOTIFY changed)
    Q_PROPERTY(QVariantList lessons READ lessons NOTIFY changed)
    Q_PROPERTY(QVariantList tiers READ tiers NOTIFY changed)
    Q_PROPERTY(bool puzzlesUnlocked READ puzzlesUnlocked NOTIFY changed)
    Q_PROPERTY(QVariantList pictures READ pictures NOTIFY changed)
    Q_PROPERTY(QVariantList hints READ hints NOTIFY changed)
    Q_PROPERTY(int hintTier READ hintTier NOTIFY changed)
    Q_PROPERTY(QString objective READ objective NOTIFY changed)
    Q_PROPERTY(QString objectiveChord READ objectiveChord NOTIFY changed)
    Q_PROPERTY(QString focusedPiece READ focusedPiece NOTIFY changed)
    Q_PROPERTY(qint64 elapsed READ elapsed NOTIFY tick)
    Q_PROPERTY(qint64 best READ best NOTIFY changed)
    Q_PROPERTY(bool newBest READ newBest NOTIFY changed)
    Q_PROPERTY(int stars READ stars NOTIFY changed)
    Q_PROPERTY(int totalStars READ totalStars NOTIFY changed)
    Q_PROPERTY(bool midPuzzle READ midPuzzle NOTIFY changed)
    Q_PROPERTY(QString toast READ toast NOTIFY toastChanged)
    Q_PROPERTY(QVariantList cheatSheet READ cheatSheet NOTIFY changed)
    Q_PROPERTY(QObject *pieces READ pieces CONSTANT)

public:
    struct Timings {
        int poll = 250;       // checking geometry, which has no event of its own
        int settle = 250;     // a new window or dispatch settles before it's checked
        int spawn = 3000;     // a piece window must appear by then
        int wait = 1000;      // looking again for empty workspaces
        int tier1 = 12000;    // lessons: the hint says what to do
        int tier2 = 30000;    // and shows the keys
        int tier3 = 60000;    // and explains
    };

    Game(EventSource *events, Hyprctl *hyprctl, const QString &progressPath = {}, QObject *parent = nullptr);

    void setBindings(const QString &menuText, const QString &problem = {});
    void setTimings(const Timings &timings) { m_timings = timings; }
    // Tessera's own windows are the ones with its process id; tests pick one.
    void setPid(qint64 pid) { m_pid = pid; }
    // The clock the timer reads, in milliseconds; tests drive their own.
    void setClock(std::function<qint64()> now) { m_now = std::move(now); }
    // Puzzles shuffle with this; tests fix it.
    void setSeed(quint32 seed) { m_seed = seed; m_fixedSeed = true; }

    QString state() const { return m_state; }
    QString error() const { return m_error; }
    QString notice() const { return m_notice; }
    QString saveError() const { return m_saveError; }
    bool lesson() const { return m_level.lesson; }
    int levelIndex() const { return m_levelIndex; }
    QString levelTitle() const;
    QString teaches() const;
    QString intro() const;
    QVariantList introChords() const;
    QVariantList lessons() const;
    QVariantList tiers() const;
    bool puzzlesUnlocked() const;
    QVariantList pictures() const;
    QVariantList hints() const;
    int hintTier() const { return m_hintTier; }
    QString objective() const { return m_objective; }
    QString objectiveChord() const { return m_objectiveChord; }
    QString focusedPiece() const { return m_focusedPiece; }
    qint64 elapsed() const;
    qint64 best() const;
    bool newBest() const { return m_newBest; }
    int stars() const { return m_stars; }
    int totalStars() const { return m_progress.totalStars(); }
    bool midPuzzle() const { return m_state == "dealing" || m_state == "playing"; }
    QString toast() const { return m_toast; }
    QVariantList cheatSheet() const;
    QObject *pieces() { return &m_pieces; }
    PieceModel &pieceModel() { return m_pieces; }
    const Puzzle::Desk &desk() const { return m_desk; }
    const Puzzle::Evaluation &evaluation() const { return m_eval; }
    const QList<Hints::Hint> &currentHints() const { return m_hints; }
    QList<int> workspaces() const; // the pictures' workspaces, then spares
    const Progress &progress() const { return m_progress; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void primary();
    Q_INVOKABLE void playLesson(int index);
    Q_INVOKABLE void playTier(int index);
    Q_INVOKABLE void begin();
    Q_INVOKABLE void restart();
    Q_INVOKABLE void toMenu();
    Q_INVOKABLE void hint();
    // A piece window was asked to close (Super+W). Pieces stay while a
    // puzzle is on; the window says why.
    Q_INVOKABLE bool requestPieceClose(const QString &id);
    // Saves and closes every piece window. Called on quit.
    Q_INVOKABLE void shutdown();
    Q_INVOKABLE void retranslate();

    void handleEvent(const QString &name, const QString &data);

signals:
    void changed();
    void tick();
    void toastChanged();
    void solved();

private:
    struct Action {
        enum Kind { Spawn, Float, Scratchpad, Spare, PileStart, PileJoin } kind;
        QString piece;
    };

    void setState(const QString &state);
    void say(const QString &text);
    void fail(const QString &text);
    void save();
    void sourceChanged();

    // The desktop
    void refresh();
    QJsonObject client(const QString &address) const;
    QString pieceAt(const QString &address) const;
    bool ownPiece(const QString &id, QString *address); // the only gate to a dispatch
    bool focusOwnPiece(const QString &id, QString *error);
    QString chord(const QString &action) const;
    QString pieceName(const QString &id) const;
    QString pictureTitle(int picture) const;

    // Setting up
    int monitorBlock() const;
    void allocate();
    void dealPicture();
    void queueDeal(int picture);
    void runNext();
    void spawn(const QString &id);
    void bind(const QString &id, const QString &address);
    void bindFromClients();
    void afterSettle(std::function<void()> next);
    void verify(const QString &id, const std::function<bool(const QJsonObject &)> &ok, const QString &what,
                std::function<void()> retry = {});
    void startPlaying();
    void clearPieces();

    // Playing
    void evaluate();
    void updateHints();
    void finish();

    EventSource *m_events;
    Hyprctl *m_hyprctl;
    Progress m_progress;
    QList<Binding> m_bindings;
    Timings m_timings;
    qint64 m_pid;
    std::function<qint64()> m_now;
    quint32 m_seed = 1;
    bool m_fixedSeed = false;

    QString m_state = "connecting";
    QString m_error, m_notice, m_saveError, m_toast, m_objective, m_objectiveChord, m_focusedPiece;
    Levels::Level m_level;
    int m_levelIndex = 0;
    int m_hintTier = 0, m_askedTier = 0, m_stars = 0;
    bool m_newBest = false;

    PieceModel m_pieces;
    Puzzle::Desk m_desk;
    Puzzle::Evaluation m_eval;
    QList<Hints::Hint> m_hints;
    QHash<QString, QString> m_titles;  // piece id -> window title
    QHash<QString, QString> m_art;     // piece id -> artwork
    QList<int> m_spares;
    QSet<QString> m_dead;
    QJsonArray m_clients;
    QString m_board;                   // the board window's address
    int m_boardWs = 0, m_activeWs = 0;
    bool m_scratchpadShown = false;
    // The last window that had focus, never empty: Hyprland follows each
    // switch inside a group with an empty activewindowv2.
    QString m_lastWindow;

    QList<Action> m_queue;
    int m_dealing = -1;                // the picture being dealt
    QString m_pending;                 // the piece whose window is awaited
    QString m_pileLeader;
    bool m_busy = false;
    bool m_sourceFailed = false;
    int m_generation = 0;
    qint64 m_started = 0, m_finished = 0, m_playStart = 0;

    QTimer m_poll, m_tick, m_spawnTimeout, m_soon;
    int m_spawnTries = 0;
};
