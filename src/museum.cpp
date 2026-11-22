#include "museum.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFutureWatcher>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QtConcurrent>
#include <algorithm>

#include "artcache.h"

using namespace Museum;

static const QString Aic = QStringLiteral("Art Institute of Chicago");
static const QString Met = QStringLiteral("The Metropolitan Museum of Art");

const QStringList &Museum::hosts() {
    static const QStringList all{"api.artic.edu", "www.artic.edu", "collectionapi.metmuseum.org",
                                 "images.metmuseum.org"};
    return all;
}

bool Museum::allowedUrl(const QUrl &url) {
    return url.isValid() && url.scheme() == "https" && hosts().contains(url.host()) && url.userInfo().isEmpty()
           && (url.port() == -1 || url.port() == 443);
}

QByteArray Museum::userAgent() {
    // The app and its version only: nothing about who plays or where.
    const QString version = QCoreApplication::applicationVersion();
    return QStringLiteral("Tessera/%1 (jigsaw puzzle)").arg(version.isEmpty() ? QStringLiteral("0") : version).toUtf8();
}

const QStringList &Museum::themes() {
    static const QStringList all{"landscape", "sea", "garden", "river", "mountains", "flowers", "harbor",
                                 "forest", "snow", "sunset", "boats", "village", "coast", "birds"};
    return all;
}

QUrl Museum::aicSearchUrl(const QString &theme, int page) {
    QUrl url("https://api.artic.edu/api/v1/artworks/search");
    QUrlQuery q;
    q.addQueryItem("q", theme);
    q.addQueryItem("query[term][is_public_domain]", "true");
    q.addQueryItem("fields", "id,title,artist_title,artist_display,date_display,image_id,is_public_domain,thumbnail");
    q.addQueryItem("limit", "12");
    q.addQueryItem("page", QString::number(std::clamp(page, 1, 20)));
    url.setQuery(q);
    return url;
}

QUrl Museum::metSearchUrl(const QString &theme, int offset) {
    QUrl url("https://collectionapi.metmuseum.org/public/collection/v1.1/search");
    QUrlQuery q;
    q.addQueryItem("q", theme);
    q.addQueryItem("hasImages", "true");
    q.addQueryItem("isPublicDomain", "true");
    q.addQueryItem("limit", "20");
    q.addQueryItem("offset", QString::number(std::clamp(offset, 0, 400)));
    url.setQuery(q);
    return url;
}

QUrl Museum::metObjectUrl(int id) {
    return QUrl(QStringLiteral("https://collectionapi.metmuseum.org/public/collection/v1/objects/%1").arg(id));
}

// A positive whole number from JSON, or 0.
static int positiveId(const QJsonValue &v) {
    if (!v.isDouble())
        return 0;
    const double d = v.toDouble();
    if (d < 1 || d > 2e9 || d != std::floor(d))
        return 0;
    return int(d);
}

QList<Candidate> Museum::parseAicSearch(const QByteArray &json) {
    QList<Candidate> out;
    if (json.size() > MaxJsonBytes)
        return out;
    const QJsonArray data = QJsonDocument::fromJson(json).object().value("data").toArray();
    static const QRegularExpression uuid(
        QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
    for (const QJsonValue &value : data) {
        const QJsonObject o = value.toObject();
        // Only true itself: a missing or odd value is not public domain.
        if (o.value("is_public_domain") != QJsonValue(true))
            continue;
        const int id = positiveId(o.value("id"));
        const QString imageId = o.value("image_id").toString();
        const QString title = cleanText(o.value("title").toString());
        if (!id || !uuid.match(imageId).hasMatch() || title.isEmpty())
            continue;
        Candidate c;
        c.source.key = QStringLiteral("aic:%1").arg(id);
        c.source.kind = Source::Online;
        c.source.name = shortName(title);
        c.source.credit.title = title;
        QString artist = o.value("artist_title").toString();
        if (artist.isEmpty())
            artist = o.value("artist_display").toString().section('\n', 0, 0);
        c.source.credit.artist = cleanText(artist, 100);
        c.source.credit.date = cleanText(o.value("date_display").toString(), 60);
        c.source.credit.url = QStringLiteral("https://www.artic.edu/artworks/%1").arg(id);
        c.source.credit.license = QStringLiteral("Public domain (CC0)");
        c.source.credit.origin = Aic;
        // The image id has been checked to be a plain UUID, so it can't add a
        // path or a query to the URL.
        c.image = QUrl(QStringLiteral("https://www.artic.edu/iiif/2/%1/full/1686,/0/default.jpg").arg(imageId));
        const QJsonObject thumb = o.value("thumbnail").toObject();
        const int w = positiveId(thumb.value("width")), h = positiveId(thumb.value("height"));
        if (w && h)
            c.hint = QSize(w, h);
        out << c;
    }
    return out;
}

QList<int> Museum::parseMetSearch(const QByteArray &json, int limit) {
    QList<int> out;
    if (json.size() > MaxJsonBytes)
        return out;
    for (const QJsonValue &v : QJsonDocument::fromJson(json).object().value("objectIDs").toArray()) {
        if (out.size() >= limit)
            break;
        if (const int id = positiveId(v))
            out << id;
    }
    return out;
}

std::optional<Candidate> Museum::parseMetObject(const QByteArray &json) {
    if (json.size() > MaxJsonBytes)
        return std::nullopt;
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    if (o.value("isPublicDomain") != QJsonValue(true))
        return std::nullopt;
    const int id = positiveId(o.value("objectID"));
    const QString title = cleanText(o.value("title").toString());
    if (!id || title.isEmpty())
        return std::nullopt;
    // The original, since the "small" web image is too small to cut up; the
    // small one only when there's no original. Either must be on the Met's
    // own image host, and the size cap still applies.
    QUrl image(o.value("primaryImage").toString());
    if (!allowedUrl(image) || image.host() != "images.metmuseum.org")
        image = QUrl(o.value("primaryImageSmall").toString());
    if (!allowedUrl(image) || image.host() != "images.metmuseum.org" || image.hasQuery() || image.hasFragment())
        return std::nullopt;
    Candidate c;
    c.source.key = QStringLiteral("met:%1").arg(id);
    c.source.kind = Source::Online;
    c.source.name = shortName(title);
    c.source.credit.title = title;
    c.source.credit.artist = cleanText(o.value("artistDisplayName").toString(), 100);
    c.source.credit.date = cleanText(o.value("objectDate").toString(), 60);
    const QUrl page(o.value("objectURL").toString());
    c.source.credit.url = page.isValid() && page.scheme() == "https" && page.host() == "www.metmuseum.org"
                              ? cleanText(page.toString(), 300)
                              : QStringLiteral("https://www.metmuseum.org/art/collection/search/%1").arg(id);
    c.source.credit.license = QStringLiteral("Public domain (CC0)");
    c.source.credit.origin = Met;
    c.image = image;
    return c;
}

QImage Museum::decodeImage(const QByteArray &bytes, QString *error, int longest) {
    auto fail = [&](const char *why) {
        if (error)
            *error = QString::fromLatin1(why);
        return QImage();
    };
    if (bytes.isEmpty() || bytes.size() > MaxImageBytes)
        return fail("image too large or empty");
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    reader.setAllocationLimit(512);
    const QByteArray format = reader.format();
    if (format != "jpeg" && format != "png" && format != "webp")
        return fail("not a JPEG, PNG or WebP image");
    const QSize size = reader.size();
    if (!size.isValid() || size.width() < 400 || size.height() < 400 || std::max(size.width(), size.height()) < 900
        || size.width() > 12000 || size.height() > 12000
        || qint64(size.width()) * size.height() > 80'000'000)
        return fail("image dimensions out of range");
    if (std::max(size.width(), size.height()) > longest)
        reader.setScaledSize(size.scaled(longest, longest, Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (image.isNull())
        return fail("image didn't decode");
    return image.convertToFormat(QImage::Format_RGB32);
}

// ── The client ───────────────────────────────────────────────────────────

MuseumClient::MuseumClient(ArtCache *cache, QObject *parent) : QObject(parent), m_cache(cache), m_net(&m_ownNet) {
    m_deadline.setSingleShot(true);
    // One whole fetch, every request in it, is given up after this.
    m_deadline.setInterval(60000);
    connect(&m_deadline, &QTimer::timeout, this, [this] { finish(tr("The museums took too long to answer.")); });
}

void MuseumClient::fetch(int wanted) {
    if (m_busy)
        return;
    m_busy = true;
    ++m_generation;
    m_wanted = std::clamp(wanted, 1, 6);
    m_added = 0;
    m_lastError.clear();
    m_queue.clear();
    m_metIds.clear();
    m_museums = QStringList{"aic", "met"};
    if (QRandomGenerator::global()->bounded(2))
        std::swap(m_museums[0], m_museums[1]);
    m_deadline.start();
    nextMuseum();
}

void MuseumClient::cancel() {
    if (m_busy)
        finish(tr("Cancelled."));
}

void MuseumClient::finish(const QString &error) {
    if (!m_busy)
        return;
    m_busy = false;
    ++m_generation;
    m_deadline.stop();
    for (const auto &reply : m_replies)
        if (reply)
            reply->abort();
    m_replies.clear();
    const QString e = m_added > 0 ? QString() : (error.isEmpty() ? m_lastError : error);
    emit finished(m_added, e);
}

void MuseumClient::get(const QUrl &url, Kind kind, Done done) {
    if (!allowedUrl(url)) {
        done({}, tr("Refused a link to %1, which isn't one of the museums.").arg(url.host()));
        return;
    }
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    // The Art Institute asks API clients to name themselves in this header.
    if (url.host().endsWith("artic.edu"))
        request.setRawHeader("AIC-User-Agent", userAgent());
    request.setRawHeader("Accept", kind == Kind::Json ? "application/json" : "image/jpeg, image/png, image/webp");
    request.setTransferTimeout(TimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    request.setMaximumRedirectsAllowed(3);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    const qint64 cap = kind == Kind::Json ? MaxJsonBytes : MaxImageBytes;

    QNetworkReply *reply = m_net->get(request);
    m_replies << reply;
    auto tooLarge = std::make_shared<bool>(false);
    connect(reply, &QNetworkReply::redirected, reply, [reply](const QUrl &to) {
        if (allowedUrl(to))
            emit reply->redirectAllowed();
        else
            reply->abort();
    });
    connect(reply, &QNetworkReply::metaDataChanged, reply, [reply, cap, tooLarge] {
        if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > cap) {
            *tooLarge = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply, cap, tooLarge](qint64 received, qint64) {
        if (received > cap) {
            *tooLarge = true;
            reply->abort();
        }
    });
    const int generation = m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, kind, done, tooLarge, generation, cap] {
        reply->deleteLater();
        m_replies.removeAll(reply);
        if (generation != m_generation)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString type = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
        if (*tooLarge)
            return done({}, tr("A museum sent more than Tessera accepts."));
        if (reply->error() != QNetworkReply::NoError)
            return done({}, reply->errorString());
        if (status != 200)
            return done({}, tr("A museum answered with HTTP %1.").arg(status));
        const bool typeOk = kind == Kind::Json ? type.startsWith("application/json") : type.startsWith("image/");
        if (!typeOk)
            return done({}, kind == Kind::Json ? tr("A museum sent a web page instead of data.")
                                               : tr("A museum's image server turned the download away."));
        const QByteArray bytes = reply->read(cap + 1);
        if (bytes.size() > cap)
            return done({}, tr("A museum sent more than Tessera accepts."));
        done(bytes, {});
    });
}

void MuseumClient::nextMuseum() {
    if (!m_busy)
        return;
    if (m_added >= m_wanted || m_museums.isEmpty())
        return finish();
    const QString museum = m_museums.takeFirst();
    auto *random = QRandomGenerator::global();
    const QString theme = themes().at(random->bounded(int(themes().size())));
    if (museum == "aic") {
        emit progress(Aic);
        get(aicSearchUrl(theme, 1 + random->bounded(8)), Kind::Json, [this](const QByteArray &bytes, const QString &error) {
            if (!error.isEmpty()) {
                m_lastError = error;
                return nextMuseum();
            }
            m_queue.clear();
            for (const Candidate &c : parseAicSearch(bytes)) {
                // Landscape-ish or portrait-ish, any shape will do: odd ones
                // go into collages. Already cached ones are skipped.
                if (!m_cache->has(c.source.key))
                    m_queue << c;
            }
            std::shuffle(m_queue.begin(), m_queue.end(), *QRandomGenerator::global());
            nextCandidate();
        });
    } else {
        if (m_metImagesRefused)
            return nextMuseum();
        emit progress(Met);
        get(metSearchUrl(theme, random->bounded(10) * 20), Kind::Json, [this](const QByteArray &bytes, const QString &error) {
            if (!error.isEmpty()) {
                m_lastError = error;
                return nextMuseum();
            }
            m_metIds = parseMetSearch(bytes, 20);
            std::shuffle(m_metIds.begin(), m_metIds.end(), *QRandomGenerator::global());
            // A few objects at most per fetch.
            m_metIds = m_metIds.mid(0, std::min<int>(m_metIds.size(), m_wanted - m_added + 3));
            nextMetObject();
        });
    }
}

void MuseumClient::nextCandidate() {
    if (!m_busy)
        return;
    if (m_added >= m_wanted || m_queue.isEmpty())
        return nextMuseum();
    const Candidate c = m_queue.takeFirst();
    download(c, [this] { nextCandidate(); });
}

void MuseumClient::nextMetObject() {
    if (!m_busy)
        return;
    if (m_added >= m_wanted || m_metIds.isEmpty() || m_metImagesRefused)
        return nextMuseum();
    const int id = m_metIds.takeFirst();
    if (m_cache->has(QStringLiteral("met:%1").arg(id)))
        return nextMetObject();
    get(metObjectUrl(id), Kind::Json, [this](const QByteArray &bytes, const QString &error) {
        if (!error.isEmpty()) {
            m_lastError = error;
            return nextMetObject();
        }
        const auto c = parseMetObject(bytes);
        if (!c)
            return nextMetObject();
        download(*c, [this] { nextMetObject(); });
    });
}

void MuseumClient::download(const Candidate &c, std::function<void()> next) {
    get(c.image, Kind::Image, [this, c, next](const QByteArray &bytes, const QString &error) {
        if (!error.isEmpty()) {
            m_lastError = error;
            // The Met's images sit behind a bot filter that sometimes answers
            // with a page instead; then the rest of its images are skipped.
            if (c.source.key.startsWith("met:") && error == tr("A museum's image server turned the download away."))
                m_metImagesRefused = true;
            return next();
        }
        // Decoding is the slow part, so it runs off the UI thread.
        auto *watcher = new QFutureWatcher<QImage>(this);
        const int generation = m_generation;
        connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, c, next, generation] {
            watcher->deleteLater();
            if (generation != m_generation)
                return;
            const QImage image = watcher->result();
            QString error;
            if (image.isNull()) {
                m_lastError = tr("A museum image couldn't be read.");
                return next();
            }
            if (m_cache->storeArt(c.source, image, &error))
                ++m_added;
            else
                m_lastError = tr("Couldn't save to the cache: %1").arg(error);
            next();
        });
        watcher->setFuture(QtConcurrent::run([bytes] { return decodeImage(bytes); }));
    });
}
