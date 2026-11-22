QT += core gui network qml quick quickcontrols2 concurrent dbus

CONFIG += c++17 release
TARGET = tessera
TEMPLATE = app

HEADERS += \
    src/artcache.h \
    src/artwork.h \
    src/bindings.h \
    src/collage.h \
    src/commands.h \
    src/filepicker.h \
    src/folder.h \
    src/gallery.h \
    src/museum.h \
    src/portalfilepicker.h \
    src/settings.h \
    src/stats.h \
    src/cut.h \
    src/game.h \
    src/hyprland.h \
    src/levels.h \
    src/progress.h \
    src/puzzle.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/artcache.cpp \
    src/bindings.cpp \
    src/collage.cpp \
    src/commands.cpp \
    src/folder.cpp \
    src/gallery.cpp \
    src/museum.cpp \
    src/portalfilepicker.cpp \
    src/settings.cpp \
    src/stats.cpp \
    src/cut.cpp \
    src/game.cpp \
    src/hyprland.cpp \
    src/levels.cpp \
    src/progress.cpp \
    src/puzzle.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
