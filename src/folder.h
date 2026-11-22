#pragma once

#include <QList>
#include <QString>

#include "artwork.h"

// The player's own pictures: the JPEG, PNG and WebP files directly in one
// folder, read and never written. Only each file's header is read here, for
// its size; the pixels are read when a picture is made from it. Nothing from
// the folder ever leaves the machine.
namespace Folder {

struct Scan {
    QList<Source> sources;
    int skipped = 0;        // image-named files that couldn't be read
    bool truncated = false; // more images than the cap
    QString error;          // the folder itself couldn't be read
};

constexpr int DefaultCap = 200;
constexpr qint64 MaxFileBytes = 80ll * 1024 * 1024;

Scan scan(const QString &directory, int cap = DefaultCap, qint64 maxFileBytes = MaxFileBytes);

// Whether a file name is one Tessera reads.
bool isImageName(const QString &fileName);

} // namespace Folder
