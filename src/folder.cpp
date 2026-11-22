#include "folder.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>

bool Folder::isImageName(const QString &fileName) {
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    return suffix == "jpg" || suffix == "jpeg" || suffix == "png" || suffix == "webp";
}

Folder::Scan Folder::scan(const QString &directory, int cap, qint64 maxFileBytes) {
    Scan out;
    const QDir dir(directory);
    if (directory.isEmpty() || !dir.exists()) {
        out.error = QStringLiteral("missing");
        return out;
    }
    if (!QFileInfo(directory).isReadable()) {
        out.error = QStringLiteral("unreadable");
        return out;
    }
    // Only the folder itself, not what's below it, in name order so the same
    // folder always gives the same pictures.
    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &info : entries) {
        if (!isImageName(info.fileName()) || info.fileName().startsWith('.'))
            continue;
        if (out.sources.size() >= cap) {
            out.truncated = true;
            break;
        }
        if (info.size() <= 0 || info.size() > maxFileBytes) {
            ++out.skipped;
            continue;
        }
        // The header says the size without decoding the image.
        QImageReader reader(info.absoluteFilePath());
        reader.setDecideFormatFromContent(true);
        const QSize size = reader.size();
        const QByteArray format = reader.format();
        if (!size.isValid() || size.width() < 64 || size.height() < 64 || size.width() > 20000 || size.height() > 20000
            || !(format == "jpeg" || format == "png" || format == "webp")) {
            ++out.skipped;
            continue;
        }
        Source s;
        s.kind = Source::Folder;
        s.key = "folder:" + QString::fromLatin1(QCryptographicHash::hash(info.absoluteFilePath().toUtf8(),
                                                                          QCryptographicHash::Sha1).toHex().left(16));
        s.path = info.absoluteFilePath();
        s.size = size;
        QString title = info.completeBaseName();
        title.replace('_', ' ');
        s.credit.title = cleanText(title, 80);
        s.credit.local = true;
        s.name = shortName(s.credit.title);
        out.sources << s;
    }
    return out;
}
