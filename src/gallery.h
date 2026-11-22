#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QSizeF>
#include <QStringList>
#include <functional>
#include <memory>

#include "artcache.h"
#include "artwork.h"
#include "settings.h"

class FilePicker;
class MuseumClient;
class QQuickImageProvider;

// Where the pictures come from and how they're made to fit the screen: the
// five built-in works, public-domain art from the museums, the player's own
// folder, or a mix. A picture for a puzzle is one source image cover-cropped
// to the shape of the monitor, or a collage when no single image is close
// enough; either is rendered off the UI thread and handed to the windows
// through the "image://tessera/" provider.
class Gallery : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString source READ source NOTIFY changed)
    Q_PROPERTY(bool online READ online NOTIFY changed)
    Q_PROPERTY(QString folder READ folder NOTIFY changed)
    Q_PROPERTY(int folderCount READ folderCount NOTIFY changed)
    Q_PROPERTY(QString folderProblem READ folderProblem NOTIFY changed)
    Q_PROPERTY(int onlineCount READ onlineCount NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool fetching READ fetching NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString note READ note NOTIFY changed)
    Q_PROPERTY(QStringList hosts READ hosts CONSTANT)
    Q_PROPERTY(QString saveError READ saveError NOTIFY changed)

public:
    struct Options {
        QString settingsPath; // empty: nothing is remembered (tests, defaults)
        QString cacheDir;     // empty: nothing is cached on disk
        bool network = false; // whether the museums may be asked at all
    };

    Gallery() : Gallery(Options()) {}
    explicit Gallery(const Options &options, QObject *parent = nullptr);
    ~Gallery() override;

    void setPicker(FilePicker *picker);
    MuseumClient *museums() const { return m_museums.get(); }
    ArtCache &cache() { return m_cache; }

    QString source() const { return m_settings.source; }
    bool online() const { return m_settings.online; }
    QString folder() const { return m_settings.folder; }
    int folderCount() const { return m_folderSources.size(); }
    QString folderProblem() const { return m_folderProblem; }
    int onlineCount() const { return m_cache.online().size(); }
    bool busy() const { return m_preparing || fetching(); }
    bool fetching() const;
    QString status() const { return m_status; }
    QString note() const { return m_note; }
    QStringList hosts() const;
    QString saveError() const { return m_saveError; }

    Q_INVOKABLE void setSource(const QString &source);
    Q_INVOKABLE void cycleSource(int step);
    Q_INVOKABLE void setOnline(bool online);
    Q_INVOKABLE void chooseFolder();
    Q_INVOKABLE void forgetFolder();
    // Reads the folder again (after it was chosen, or at start).
    void rescan();
    // Runs the folder scan right here, for tests.
    void rescanNow();

    // The built-in pictures as sources.
    static QList<Source> builtins();

    // One picture made for a level.
    struct Prepared {
        QString key;           // "p-…", names its image
        QStringList sources;   // the source keys, in cell order
        QList<Credit> credits; // one per source
        QString name;          // short, for piece names (untranslated)
        QString builtinName;   // the built-in work's key when the first source is one
        QSize size;
        bool collage = false;
        QString url() const { return "image://tessera/" + key; }
    };
    struct Request {
        QStringList musts;     // per picture: a built-in key it must show, or empty
        int strays = 0;        // extra single pictures for stray pieces
        QSizeF area;           // the monitor's work area, logical pixels
        QSize pixels;          // the size to render at
        quint32 seed = 1;
        bool lesson = false;   // lessons always use the built-in works
    };
    struct Result {
        QList<Prepared> pictures, strays;
        QString note;          // why fewer or other pictures were used
        bool ok() const { return !pictures.isEmpty(); }
    };
    using Ready = std::function<void(const Result &)>;

    // Works out the pictures and renders them off the UI thread, fetching
    // from the museums first when the source needs it and none are cached.
    // `ready` runs on the UI thread. A newer request replaces an older one.
    void prepare(const Request &request, Ready ready);
    void cancel();

    // The rendered image for a key, for the image provider (any thread).
    QImage image(const QString &key) const;
    QQuickImageProvider *createImageProvider();

    // Which sources a request picks from, before shuffling.
    QList<Source> pool(bool lesson) const;

signals:
    void changed();

private:
    void continuePrepare(const Request &request, int generation, Ready ready);
    void topUp();
    void setStatus(const QString &status);
    void saveSettings();

    Settings m_settings;
    bool m_persist;
    bool m_network;
    ArtCache m_cache;
    std::unique_ptr<MuseumClient> m_museums;
    QPointer<FilePicker> m_picker;
    QList<Source> m_folderSources;
    QString m_folderProblem, m_status, m_note, m_saveError;
    bool m_preparing = false;
    int m_generation = 0, m_scanGeneration = 0;
    qint64 m_fetchFailedAt = 0;
    std::function<void(int, const QString &)> m_afterFetch;

    mutable QMutex m_mutex;
    QHash<QString, QImage> m_images;
};
