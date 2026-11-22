#pragma once

#include <QHash>
#include <QImage>
#include <QString>

#include "artwork.h"

// Downloaded art and rendered pictures, kept in ~/.cache/tessera/:
//
//   art/<key>.jpg      museum images, re-encoded by Tessera after decoding
//   pictures/<key>.jpg rendered pictures (cover-crops and collages)
//   credits.json       who made each image in art/, and when it was used
//
// Every file is written beside its target and renamed over it, so a crash
// never leaves half an image or half an index. The whole directory is kept
// under a size cap by deleting what was used longest ago. Only files Tessera
// named itself are ever deleted.
class ArtCache {
public:
    // An empty directory keeps nothing on disk (the tests and the built-in
    // only mode).
    explicit ArtCache(const QString &directory = {}, qint64 capBytes = 200ll * 1024 * 1024);

    QString directory() const { return m_dir; }
    bool enabled() const { return !m_dir.isEmpty(); }
    qint64 cap() const { return m_cap; }
    void setCap(qint64 bytes) { m_cap = bytes; }

    // The museum images known to the index whose files are still there.
    QList<Source> online() const;
    bool has(const QString &key) const { return m_items.contains(key); }

    // Stores a decoded museum image under `key` with its credit. False with
    // an error when it couldn't be written.
    bool storeArt(const Source &source, const QImage &image, QString *error = nullptr);
    void markUsed(const QString &key, qint64 when);

    // Rendered pictures, by a key naming what went into them.
    QString picturePath(const QString &key) const;
    bool storePicture(const QString &key, const QImage &image, QString *error = nullptr);

    // Total bytes of Tessera's files in the cache.
    qint64 size() const;
    // Deletes the least recently used files until the cache fits its cap.
    void enforceCap();

    bool load();
    bool saveIndex(QString *error = nullptr) const;

    // A whole file written atomically, for anything in the cache.
    static bool writeAtomically(const QString &path, const QByteArray &data, QString *error = nullptr);
    // Keys become file names, so only a safe alphabet is allowed in them.
    static bool safeKey(const QString &key);

private:
    struct Item {
        Source source;
        qint64 added = 0, used = 0;
    };
    QString artPath(const QString &key) const;

    QString m_dir;
    qint64 m_cap;
    QHash<QString, Item> m_items;
};
