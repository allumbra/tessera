#pragma once

#include <QJsonObject>
#include <QList>
#include <QSize>
#include <QString>

// Who made a picture, and on what terms Tessera shows it. Every picture on
// the board carries one of these per source image, so a collage credits each
// of its parts.
struct Credit {
    QString title;
    QString artist;
    QString date;
    QString url;     // where the work is described: the museum's page
    QString license; // "Public domain (CC0)"
    QString origin;  // "Art Institute of Chicago", "Your folder"
    bool local = false; // the player's own file, never shown anywhere else

    QJsonObject toJson() const;
    static Credit fromJson(const QJsonObject &o);
};

// One image a picture can be made from.
struct Source {
    enum Kind { Builtin, Online, Folder };
    QString key;  // "builtin:wave", "aic:14655", "met:10181", "folder:<hash>"
    Kind kind = Builtin;
    QString path; // ":/images/wave.jpg", a file in the cache, or the player's file
    QSize size;   // in pixels, from the file's header
    QString name; // short, for naming pieces: "Wave"
    Credit credit;
};

// Text from outside (a museum's JSON, a file name) as Tessera shows it: no
// control characters, no runs of spaces, and never longer than `limit`.
QString cleanText(const QString &text, int limit = 160);

// A short name for pieces from a full title: its first few words.
QString shortName(const QString &title);
