#include "gallery.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFutureWatcher>
#include <QImageReader>
#include <QMutexLocker>
#include <QQuickImageProvider>
#include <QRandomGenerator>
#include <QtConcurrent>
#include <algorithm>

#include "collage.h"
#include "filepicker.h"
#include "folder.h"
#include "levels.h"
#include "museum.h"

namespace {

class Provider : public QQuickImageProvider {
public:
    explicit Provider(Gallery *gallery) : QQuickImageProvider(QQuickImageProvider::Image), m_gallery(gallery) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override {
        QImage image = m_gallery ? m_gallery->image(id) : QImage();
        if (image.isNull()) {
            image = QImage(16, 9, QImage::Format_RGB32);
            image.fill(QColor("#16161a"));
        }
        if (size)
            *size = image.size();
        // Thumbnails ask for a smaller copy; pieces take the full picture.
        if (requested.width() > 0 && requested.width() < image.width())
            return image.scaledToWidth(requested.width(), Qt::SmoothTransformation);
        return image;
    }

private:
    QPointer<Gallery> m_gallery;
};

// One source image's pixels, read with limits and only as large as the
// picture needs. Reading never writes: the player's own files are opened
// read-only by QImageReader.
QImage load(const Source &s, int longest) {
    QImageReader reader(s.path);
    reader.setDecideFormatFromContent(true);
    reader.setAllocationLimit(512);
    const QSize size = reader.size();
    if (size.isValid() && std::max(size.width(), size.height()) > longest * 2)
        reader.setScaledSize(size.scaled(longest * 2, longest * 2, Qt::KeepAspectRatio));
    reader.setAutoTransform(true);
    QImage image = reader.read();
    return image.isNull() ? image : image.convertToFormat(QImage::Format_RGB32);
}

struct Job {
    QList<Source> sources;
    Collage::Layout layout; // in render pixels
    QSize pixels;
    QString key, cachePath; // cachePath empty: kept in memory only
};

QImage renderJob(const Job &job) {
    if (!job.cachePath.isEmpty()) {
        QImageReader cached(job.cachePath);
        cached.setAllocationLimit(512);
        if (cached.size() == job.pixels) {
            const QImage image = cached.read();
            if (!image.isNull())
                return image.convertToFormat(QImage::Format_RGB32);
        }
    }
    QList<QImage> images;
    const int longest = std::max(job.pixels.width(), job.pixels.height());
    for (const Source &s : job.sources)
        images << load(s, longest);
    return Collage::render(images, job.layout, job.pixels);
}

} // namespace

Gallery::Gallery(const Options &options, QObject *parent)
    : QObject(parent), m_settings(options.settingsPath), m_persist(!options.settingsPath.isEmpty()),
      m_network(options.network), m_cache(options.cacheDir) {
    if (m_persist)
        m_settings.load();
    m_cache.load();
    m_museums = std::make_unique<MuseumClient>(&m_cache);
    connect(m_museums.get(), &MuseumClient::progress, this, [this](const QString &museum) {
        setStatus(tr("Fetching public-domain art from %1…").arg(museum));
    });
    connect(m_museums.get(), &MuseumClient::finished, this, [this](int added, const QString &error) {
        if (added == 0)
            m_fetchFailedAt = QDateTime::currentMSecsSinceEpoch();
        setStatus({});
        auto after = std::move(m_afterFetch);
        m_afterFetch = nullptr;
        if (after)
            after(added, error);
        emit changed();
    });
    if (!m_settings.folder.isEmpty())
        rescan();
}

Gallery::~Gallery() {
    ++m_generation;
    ++m_scanGeneration;
}

void Gallery::setPicker(FilePicker *picker) {
    if (m_picker)
        disconnect(m_picker, nullptr, this, nullptr);
    m_picker = picker;
    if (!picker)
        return;
    connect(picker, &FilePicker::folderSelected, this, [this](const QString &path) {
        m_settings.folder = path;
        // Choosing a folder means wanting its pictures.
        if (m_settings.source == "builtin" || m_settings.source == "online")
            m_settings.source = "folder";
        saveSettings();
        rescan();
    });
    connect(picker, &FilePicker::failed, this, [this](const QString &message) {
        m_folderProblem = message;
        emit changed();
    });
}

bool Gallery::fetching() const { return m_museums && m_museums->busy(); }

QStringList Gallery::hosts() const { return Museum::hosts(); }

void Gallery::setStatus(const QString &status) {
    if (m_status == status)
        return;
    m_status = status;
    emit changed();
}

void Gallery::saveSettings() {
    m_saveError.clear();
    if (m_persist) {
        QString error;
        if (!m_settings.save(&error))
            m_saveError = tr("Can't save settings: %1").arg(error);
    }
    emit changed();
}

void Gallery::setSource(const QString &source) {
    if (!Settings::validSource(source) || source == m_settings.source)
        return;
    m_settings.source = source;
    saveSettings();
}

void Gallery::cycleSource(int step) {
    static const QStringList order{"mixed", "builtin", "online", "folder"};
    const int i = std::max(0, int(order.indexOf(m_settings.source)));
    setSource(order.at((i + step % 4 + 4) % 4));
}

void Gallery::setOnline(bool online) {
    if (online == m_settings.online)
        return;
    m_settings.online = online;
    if (!online && m_museums)
        m_museums->cancel();
    saveSettings();
}

void Gallery::chooseFolder() {
    if (!m_picker) {
        m_folderProblem = tr("The desktop's file chooser (xdg-desktop-portal) isn't available.");
        emit changed();
        return;
    }
    m_folderProblem.clear();
    emit changed();
    m_picker->chooseFolder(m_settings.folder);
}

void Gallery::forgetFolder() {
    m_settings.folder.clear();
    m_folderSources.clear();
    m_folderProblem.clear();
    if (m_settings.source == "folder")
        m_settings.source = "mixed";
    saveSettings();
}

static QString folderProblemText(const Folder::Scan &scan) {
    if (scan.error == "missing")
        return QCoreApplication::translate("Gallery", "The folder isn't there any more.");
    if (scan.error == "unreadable")
        return QCoreApplication::translate("Gallery", "Tessera isn't allowed to read that folder.");
    if (scan.sources.isEmpty())
        return QCoreApplication::translate("Gallery", "No JPEG, PNG or WebP pictures in that folder.");
    if (scan.truncated)
        return QCoreApplication::translate("Gallery", "Only the first %1 pictures are used.").arg(Folder::DefaultCap);
    return {};
}

void Gallery::rescan() {
    const QString dir = m_settings.folder;
    const int generation = ++m_scanGeneration;
    if (dir.isEmpty()) {
        m_folderSources.clear();
        emit changed();
        return;
    }
    auto *watcher = new QFutureWatcher<Folder::Scan>(this);
    connect(watcher, &QFutureWatcher<Folder::Scan>::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation != m_scanGeneration)
            return;
        const Folder::Scan scan = watcher->result();
        m_folderSources = scan.sources;
        m_folderProblem = folderProblemText(scan);
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run([dir] { return Folder::scan(dir); }));
}

void Gallery::rescanNow() {
    ++m_scanGeneration;
    const Folder::Scan scan = Folder::scan(m_settings.folder);
    m_folderSources = scan.sources;
    m_folderProblem = m_settings.folder.isEmpty() ? QString() : folderProblemText(scan);
    emit changed();
}

QList<Source> Gallery::builtins() {
    QList<Source> out;
    for (const auto &w : Art::all()) {
        Source s;
        s.key = "builtin:" + w.key;
        s.kind = Source::Builtin;
        s.path = ":/images/" + w.key + ".jpg";
        s.size = QSize(1600, 900);
        s.name = QString::fromUtf8(w.name);
        s.credit.title = QString::fromUtf8(w.title);
        s.credit.artist = QString::fromUtf8(w.artist);
        s.credit.date = QString::fromUtf8(w.year);
        s.credit.url = QString::fromUtf8(w.url);
        s.credit.license = QString::fromUtf8(w.license);
        s.credit.origin = QStringLiteral("Wikimedia Commons");
        out << s;
    }
    return out;
}

QList<Source> Gallery::pool(bool lesson) const {
    const QString mode = lesson ? QStringLiteral("builtin") : m_settings.source;
    QList<Source> online = m_settings.online ? m_cache.online() : QList<Source>();
    if (mode == "online")
        return online.isEmpty() ? builtins() : online;
    if (mode == "folder")
        return m_folderSources.isEmpty() ? builtins() : m_folderSources;
    if (mode == "mixed")
        return builtins() + m_folderSources + online;
    return builtins();
}

void Gallery::cancel() {
    ++m_generation;
    m_preparing = false;
    m_afterFetch = nullptr;
    emit changed();
}

void Gallery::prepare(const Request &request, Ready ready) {
    const int generation = ++m_generation;
    m_preparing = true;
    m_note.clear();
    emit changed();
    const QString mode = request.lesson ? QStringLiteral("builtin") : m_settings.source;
    const int wanted = request.musts.size() + request.strays;
    const bool wantsOnline = mode == "online" || mode == "mixed";
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool failedLately = m_fetchFailedAt > 0 && now - m_fetchFailedAt < 120000;
    const int cached = m_cache.online().size();
    const bool fetchFirst = wantsOnline && m_settings.online && m_network && !failedLately && !fetching()
                            && (mode == "online" ? cached < wanted : cached == 0);
    if (fetchFirst) {
        m_afterFetch = [this, request, generation, ready](int, const QString &error) {
            if (generation != m_generation)
                return;
            if (!error.isEmpty() && m_cache.online().isEmpty())
                m_note = tr("Couldn't reach the museums (%1), so this puzzle uses the built-in pictures.").arg(error);
            continuePrepare(request, generation, ready);
        };
        m_museums->fetch(std::max(2, wanted));
        return;
    }
    continuePrepare(request, generation, ready);
}

void Gallery::continuePrepare(const Request &request, int generation, Ready ready) {
    const QString mode = request.lesson ? QStringLiteral("builtin") : m_settings.source;
    QList<Source> pool = this->pool(request.lesson);
    if (m_note.isEmpty() && !request.lesson) {
        if ((mode == "online" || mode == "mixed") && !m_settings.online && mode == "online")
            m_note = tr("Online pictures are turned off, so this puzzle uses the built-in ones.");
        else if (mode == "online" && m_cache.online().isEmpty())
            m_note = m_network ? tr("No museum art is cached yet and the museums couldn't be reached, so this puzzle "
                                    "uses the built-in pictures.")
                               : tr("Tessera is offline, so this puzzle uses the built-in pictures.");
        else if (mode == "folder" && m_folderSources.isEmpty())
            m_note = m_settings.folder.isEmpty()
                         ? tr("No folder is chosen yet, so this puzzle uses the built-in pictures. Press P to choose one.")
                         : tr("Your folder has no pictures Tessera can read, so this puzzle uses the built-in ones.");
    }

    QRandomGenerator random(request.seed);
    std::shuffle(pool.begin(), pool.end(), random);
    const QList<Source> all = builtins();
    auto byKey = [&](const QString &key) -> std::optional<Source> {
        for (const Source &s : all + pool)
            if (s.key == key)
                return s;
        return std::nullopt;
    };

    const QSizeF area = request.area.isEmpty() ? QSizeF(1920, 1054) : request.area;
    const QSize pixels = request.pixels.isEmpty() ? Collage::renderSize(area, 1) : request.pixels;
    const double gutter = Collage::gutterFor(area);
    QSet<QString> used;
    for (const QString &m : request.musts)
        if (!m.isEmpty())
            used.insert("builtin:" + m);

    QList<Job> jobs;
    Result result;
    result.note = m_note;
    auto makeJob = [&](const QList<Source> &chosen, const Collage::Layout &areaLayout) {
        Job job;
        job.sources = chosen;
        job.pixels = pixels;
        job.layout = areaLayout;
        const double sx = pixels.width() / area.width(), sy = pixels.height() / area.height();
        for (auto &cell : job.layout.cells)
            cell.rect = QRectF(cell.rect.x() * sx, cell.rect.y() * sy, cell.rect.width() * sx, cell.rect.height() * sy);
        QStringList keys;
        bool local = false;
        for (const Source &s : chosen) {
            keys << s.key;
            local = local || s.kind == Source::Folder;
        }
        QByteArray id = keys.join('|').toUtf8() + '|' + QByteArray::number(pixels.width()) + 'x'
                        + QByteArray::number(pixels.height()) + "|1";
        for (const auto &cell : job.layout.cells)
            id += '|' + QByteArray::number(cell.rect.x(), 'f', 1) + ',' + QByteArray::number(cell.rect.y(), 'f', 1);
        job.key = "p-" + QString::fromLatin1(QCryptographicHash::hash(id, QCryptographicHash::Sha1).toHex().left(20));
        // Pictures made from the player's own files stay in memory: no copy
        // of them is written anywhere.
        if (!local)
            job.cachePath = m_cache.picturePath(job.key);
        Prepared p;
        p.key = job.key;
        p.sources = keys;
        for (const Source &s : chosen)
            p.credits << s.credit;
        p.name = chosen.first().name;
        if (chosen.first().kind == Source::Builtin)
            p.builtinName = chosen.first().key.mid(8);
        p.size = pixels;
        p.collage = chosen.size() > 1;
        jobs << job;
        return p;
    };

    for (int i = 0; i < request.musts.size(); ++i) {
        QList<Source> candidates;
        if (!request.musts.at(i).isEmpty()) {
            if (auto s = byKey("builtin:" + request.musts.at(i)))
                candidates << *s;
        }
        for (const Source &s : pool)
            if (!used.contains(s.key) && (candidates.isEmpty() || s.key != candidates.first().key))
                candidates << s;
        // Fewer sources than pictures: the rest may be used again, but only
        // from the source the player chose.
        if (candidates.size() < 3)
            for (const Source &s : pool)
                if (std::none_of(candidates.begin(), candidates.end(), [&](const Source &c) { return c.key == s.key; }))
                    candidates << s;
        if (candidates.isEmpty())
            continue;
        QList<QSizeF> sizes;
        for (const Source &s : candidates)
            sizes << QSizeF(s.size);
        // A collage leaves enough unused images for the pictures and strays
        // still to come, so the same work doesn't turn up twice.
        int unused = 0;
        for (const Source &s : pool)
            unused += !used.contains(s.key);
        const int still = int(request.musts.size()) - i - 1 + request.strays;
        const int cells = std::clamp((unused + (request.musts.at(i).isEmpty() ? 0 : 1)) / (still + 1), 2, 4);
        const Collage::Plan plan = Collage::plan(sizes, area, gutter, 0.8, cells);
        if (plan.sources.isEmpty())
            continue;
        QList<Source> chosen;
        for (int k : plan.sources) {
            chosen << candidates.at(k);
            used.insert(candidates.at(k).key);
        }
        result.pictures << makeJob(chosen, plan.layout);
    }
    for (int k = 0; k < request.strays; ++k) {
        // A stray is from no picture here: a single image, preferably one
        // not used yet.
        std::optional<Source> pick;
        for (const Source &s : pool + all)
            if (!used.contains(s.key)) {
                pick = s;
                break;
            }
        if (!pick)
            pick = all.at(k % all.size());
        used.insert(pick->key);
        const Collage::Layout whole{{Collage::Cell{0, QRectF(QPointF(0, 0), area)}}, 0};
        result.strays << makeJob({*pick}, whole);
    }

    // Museum art just used goes to the back of the queue for next time.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const Job &job : jobs)
        for (const Source &s : job.sources)
            if (s.kind == Source::Online)
                m_cache.markUsed(s.key, now);

    auto *watcher = new QFutureWatcher<QList<QImage>>(this);
    connect(watcher, &QFutureWatcher<QList<QImage>>::finished, this, [this, watcher, jobs, result, generation, ready] {
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        const QList<QImage> images = watcher->result();
        {
            QMutexLocker lock(&m_mutex);
            // Only the pictures of the level at hand are kept.
            m_images.clear();
            for (int i = 0; i < jobs.size() && i < images.size(); ++i)
                m_images.insert(jobs.at(i).key, images.at(i));
        }
        for (int i = 0; i < jobs.size() && i < images.size(); ++i)
            if (!jobs.at(i).cachePath.isEmpty() && !QFileInfo::exists(jobs.at(i).cachePath))
                m_cache.storePicture(jobs.at(i).key, images.at(i));
        m_preparing = false;
        emit changed();
        ready(result);
        topUp();
    });
    watcher->setFuture(QtConcurrent::run([jobs] {
        QList<QImage> out;
        for (const Job &job : jobs)
            out << renderJob(job);
        return out;
    }));
}

// A few more museum pictures in the background for next time, while the
// cache holds few.
void Gallery::topUp() {
    const QString mode = m_settings.source;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!m_network || !m_settings.online || !(mode == "online" || mode == "mixed") || fetching()
        || (m_fetchFailedAt > 0 && now - m_fetchFailedAt < 120000) || m_cache.online().size() >= 16)
        return;
    m_museums->fetch(3);
}

QImage Gallery::image(const QString &key) const {
    QMutexLocker lock(&m_mutex);
    return m_images.value(key);
}

QQuickImageProvider *Gallery::createImageProvider() { return new Provider(this); }
