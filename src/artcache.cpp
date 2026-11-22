#include "artcache.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>

// ── Credits and text ──────────────────────────────────────────────────────

QJsonObject Credit::toJson() const {
    return {{"title", title}, {"artist", artist}, {"date", date}, {"url", url},
            {"license", license}, {"origin", origin}, {"local", local}};
}

Credit Credit::fromJson(const QJsonObject &o) {
    Credit c;
    c.title = cleanText(o.value("title").toString());
    c.artist = cleanText(o.value("artist").toString());
    c.date = cleanText(o.value("date").toString(), 60);
    c.url = cleanText(o.value("url").toString(), 300);
    c.license = cleanText(o.value("license").toString(), 80);
    c.origin = cleanText(o.value("origin").toString(), 80);
    c.local = o.value("local").toBool();
    return c;
}

QString cleanText(const QString &text, int limit) {
    QString out;
    out.reserve(std::min<int>(text.size(), limit + 1));
    for (const QChar ch : text) {
        // Control characters and the invisible direction marks go; text from
        // outside must not be able to reorder or hide what's shown around it.
        const ushort u = ch.unicode();
        if (ch.category() == QChar::Other_Control || ch.category() == QChar::Other_Format || (u >= 0x202A && u <= 0x202E)
            || (u >= 0x2066 && u <= 0x2069))
            out += ' ';
        else
            out += ch;
    }
    out = out.simplified();
    if (out.size() > limit)
        out = out.left(limit - 1).trimmed() + QStringLiteral("…");
    return out;
}

QString shortName(const QString &title) {
    const QStringList words = cleanText(title, 80).split(' ', Qt::SkipEmptyParts);
    QString out;
    for (const QString &w : words) {
        static const QStringList filler{"The", "A", "An", "Le", "La", "Les", "Der", "Die", "Das"};
        if (out.isEmpty() && filler.contains(w) && words.size() > 1)
            continue;
        const QString next = out.isEmpty() ? w : out + ' ' + w;
        if (next.size() > 16 && !out.isEmpty())
            break;
        out = next;
        if (out.size() >= 10)
            break;
    }
    out.remove(QRegularExpression(QStringLiteral("[,.;:—–-]+$")));
    return out.isEmpty() ? QStringLiteral("Picture") : out.left(18);
}

// ── The cache ────────────────────────────────────────────────────────────

ArtCache::ArtCache(const QString &directory, qint64 capBytes) : m_dir(directory), m_cap(capBytes) {}

bool ArtCache::safeKey(const QString &key) {
    static const QRegularExpression ok(QStringLiteral("^[a-z0-9][a-z0-9:_-]{0,79}$"));
    return ok.match(key).hasMatch();
}

QString ArtCache::artPath(const QString &key) const {
    return m_dir + "/art/" + QString(key).replace(':', '-') + ".jpg";
}

QString ArtCache::picturePath(const QString &key) const {
    if (!enabled() || !safeKey(key))
        return {};
    return m_dir + "/pictures/" + QString(key).replace(':', '-') + ".jpg";
}

bool ArtCache::writeAtomically(const QString &path, const QByteArray &data, QString *error) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

static QByteArray jpeg(const QImage &image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG", 90);
    return bytes;
}

bool ArtCache::storeArt(const Source &source, const QImage &image, QString *error) {
    if (!enabled() || !safeKey(source.key) || image.isNull()) {
        if (error)
            *error = QStringLiteral("not cached");
        return false;
    }
    const QByteArray bytes = jpeg(image);
    if (bytes.isEmpty() || !writeAtomically(artPath(source.key), bytes, error))
        return false;
    Item item;
    item.source = source;
    item.source.kind = Source::Online;
    item.source.path = artPath(source.key);
    item.source.size = image.size();
    item.added = item.used = QDateTime::currentMSecsSinceEpoch();
    m_items.insert(source.key, item);
    if (!saveIndex(error))
        return false;
    enforceCap();
    return true;
}

void ArtCache::markUsed(const QString &key, qint64 when) {
    auto it = m_items.find(key);
    if (it == m_items.end())
        return;
    it->used = when;
    saveIndex();
}

bool ArtCache::storePicture(const QString &key, const QImage &image, QString *error) {
    const QString path = picturePath(key);
    if (path.isEmpty() || image.isNull())
        return false;
    const QByteArray bytes = jpeg(image);
    if (bytes.isEmpty() || !writeAtomically(path, bytes, error))
        return false;
    enforceCap();
    return true;
}

QList<Source> ArtCache::online() const {
    QList<Source> out;
    for (const Item &item : m_items)
        if (QFileInfo::exists(item.source.path))
            out << item.source;
    std::sort(out.begin(), out.end(), [this](const Source &a, const Source &b) {
        const qint64 ua = m_items.value(a.key).used, ub = m_items.value(b.key).used;
        return ua != ub ? ua < ub : a.key < b.key;
    });
    return out;
}

// Only files that match Tessera's own naming count, and only they are ever
// deleted: anything else someone put in the directory is left alone.
static bool ours(const QFileInfo &info) {
    static const QRegularExpression name(QStringLiteral("^[a-z0-9][a-z0-9_-]{0,79}\\.jpg$"));
    return info.isFile() && !info.isSymLink() && name.match(info.fileName()).hasMatch();
}

qint64 ArtCache::size() const {
    if (!enabled())
        return 0;
    qint64 total = 0;
    for (const QString &sub : {QStringLiteral("/art"), QStringLiteral("/pictures")})
        for (const QFileInfo &info : QDir(m_dir + sub).entryInfoList(QDir::Files))
            if (ours(info))
                total += info.size();
    return total;
}

void ArtCache::enforceCap() {
    if (!enabled())
        return;
    struct Entry {
        QString path, key;
        qint64 size, used;
    };
    QList<Entry> entries;
    qint64 total = 0;
    QHash<QString, QString> keyByPath;
    for (auto it = m_items.begin(); it != m_items.end(); ++it)
        keyByPath.insert(QFileInfo(it->source.path).absoluteFilePath(), it.key());
    for (const QString &sub : {QStringLiteral("/art"), QStringLiteral("/pictures")})
        for (const QFileInfo &info : QDir(m_dir + sub).entryInfoList(QDir::Files)) {
            if (!ours(info))
                continue;
            const QString key = keyByPath.value(info.absoluteFilePath());
            const qint64 used = key.isEmpty() ? info.lastModified().toMSecsSinceEpoch() : m_items.value(key).used;
            entries << Entry{info.absoluteFilePath(), key, info.size(), used};
            total += info.size();
        }
    if (total <= m_cap)
        return;
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        return a.used != b.used ? a.used < b.used : a.path < b.path;
    });
    bool changed = false;
    for (const Entry &e : entries) {
        if (total <= m_cap)
            break;
        if (QFile::remove(e.path)) {
            total -= e.size;
            if (!e.key.isEmpty()) {
                m_items.remove(e.key);
                changed = true;
            }
        }
    }
    if (changed)
        saveIndex();
}

bool ArtCache::load() {
    m_items.clear();
    if (!enabled())
        return false;
    QFile file(m_dir + "/credits.json");
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024)
        return false;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value("version").toInt() != 1)
        return false;
    const QJsonObject items = root.value("items").toObject();
    for (auto it = items.begin(); it != items.end(); ++it) {
        if (!safeKey(it.key()))
            continue;
        const QJsonObject o = it.value().toObject();
        Item item;
        item.source.key = it.key();
        item.source.kind = Source::Online;
        item.source.path = artPath(it.key());
        const QJsonArray size = o.value("size").toArray();
        item.source.size = QSize(size.at(0).toInt(), size.at(1).toInt());
        item.source.credit = Credit::fromJson(o.value("credit").toObject());
        item.source.name = shortName(item.source.credit.title);
        item.added = o.value("added").toInteger();
        item.used = o.value("used").toInteger();
        if (item.source.size.isEmpty() || item.source.credit.title.isEmpty())
            continue;
        m_items.insert(it.key(), item);
    }
    return true;
}

bool ArtCache::saveIndex(QString *error) const {
    if (!enabled())
        return false;
    QJsonObject items;
    for (auto it = m_items.begin(); it != m_items.end(); ++it)
        items.insert(it.key(), QJsonObject{{"credit", it->source.credit.toJson()},
                                           {"size", QJsonArray{it->source.size.width(), it->source.size.height()}},
                                           {"added", it->added},
                                           {"used", it->used}});
    const QJsonObject root{{"version", 1}, {"items", items}};
    return writeAtomically(m_dir + "/credits.json", QJsonDocument(root).toJson(), error);
}
