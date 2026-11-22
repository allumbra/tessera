#pragma once

// After Omacut's src/filepicker.h by David Heinemeier Hansson (MIT,
// https://github.com/omacom-io/omacut): the file dialog as an abstract
// picker, so the app asks xdg-desktop-portal and the tests hand in a fake.

#include <QObject>
#include <QString>

class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~FilePicker() = default;

    // Asks for a folder, starting in `current` when there is one.
    virtual void chooseFolder(const QString &current) = 0;

signals:
    void folderSelected(const QString &path);
    void cancelled();
    void failed(const QString &message);
};
