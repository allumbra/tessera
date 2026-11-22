#pragma once

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <functional>
#include <optional>

#include "artwork.h"

class ArtCache;
class QNetworkReply;

// Public-domain art from two museums' open-access APIs, which label the
// rights of every object: the Art Institute of Chicago (is_public_domain,
// images over IIIF) and the Metropolitan Museum of Art (isPublicDomain,
// primaryImage). Only objects flagged public domain are ever used.
//
// The rules for the network:
// - HTTPS to a fixed list of hosts and nowhere else, redirects included.
// - Every request times out, and every response has a size cap; JSON and
//   images are read into memory and never run, opened or shown as markup.
// - Fields are checked before use. Only Tessera's own constants and checked
//   numbers and ids go into a URL; nothing a museum says is put into one.
// - Images are decoded with limits on format, size and pixels, then
//   re-encoded by Tessera, so the cache never holds a downloaded file as is.
// - The User-Agent says "Tessera" and the version, and nothing about the
//   player.
namespace Museum {

constexpr qint64 MaxJsonBytes = 2 * 1024 * 1024;
constexpr qint64 MaxImageBytes = 16 * 1024 * 1024;
constexpr int TimeoutMs = 15000;

const QStringList &hosts();
bool allowedUrl(const QUrl &url);
QByteArray userAgent();

struct Candidate {
    Source source; // key, credit and name; no path or size yet
    QUrl image;
    QSize hint;    // the full image's size, when the museum says
};

QUrl aicSearchUrl(const QString &theme, int page);
QUrl metSearchUrl(const QString &theme, int offset);
QUrl metObjectUrl(int id);
// The themes searched for, Tessera's own words.
const QStringList &themes();

// The Art Institute's search results, public domain ones only.
QList<Candidate> parseAicSearch(const QByteArray &json);
// The Met's search: object ids, at most `limit`.
QList<int> parseMetSearch(const QByteArray &json, int limit = 100);
// One Met object, if it's public domain with an image Tessera may fetch.
std::optional<Candidate> parseMetObject(const QByteArray &json);

// A downloaded image, decoded with limits: JPEG, PNG or WebP, at least
// 400 pixels on a side and 900 on the longer one, at most 12000, scaled
// down to `longest`.
QImage decodeImage(const QByteArray &bytes, QString *error = nullptr, int longest = 2400);

} // namespace Museum

// Fetches a few new public-domain images into the cache. One fetch makes a
// handful of requests: a search at one or both museums and an image per
// picture it adds.
class MuseumClient : public QObject {
    Q_OBJECT

public:
    explicit MuseumClient(ArtCache *cache, QObject *parent = nullptr);

    // Tests point it at a stand-in network.
    void setNetwork(QNetworkAccessManager *network) { m_net = network; }
    void fetch(int wanted);
    void cancel();
    bool busy() const { return m_busy; }

signals:
    // Which museum it's talking to now, for the window to say.
    void progress(const QString &museum);
    void finished(int added, const QString &error);

private:
    enum class Kind { Json, Image };
    using Done = std::function<void(const QByteArray &bytes, const QString &error)>;
    void get(const QUrl &url, Kind kind, Done done);
    void nextMuseum();
    void nextCandidate();
    void nextMetObject();
    void download(const Museum::Candidate &c, std::function<void()> next);
    void finish(const QString &error = {});

    ArtCache *m_cache;
    QNetworkAccessManager m_ownNet;
    QNetworkAccessManager *m_net;
    QList<QPointer<QNetworkReply>> m_replies;
    QTimer m_deadline;
    bool m_busy = false;
    int m_wanted = 0, m_added = 0, m_generation = 0;
    QStringList m_museums;
    QList<Museum::Candidate> m_queue;
    QList<int> m_metIds;
    bool m_metImagesRefused = false;
    QString m_lastError;
};
