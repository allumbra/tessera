QT += core gui network quick quickcontrols2 concurrent testlib
CONFIG += c++17 testcase
TARGET = tessera_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += fakehyprland.h dwindle.h player.h fakenetwork.h ../src/artcache.h ../src/artwork.h ../src/collage.h \
    ../src/commands.h ../src/filepicker.h ../src/folder.h ../src/gallery.h ../src/museum.h ../src/settings.h ../src/stats.h \
    ../src/bindings.h ../src/cut.h ../src/game.h ../src/hyprland.h \
    ../src/levels.h ../src/progress.h ../src/puzzle.h ../src/theme.h ../src/translator.h
RESOURCES += ../src/resources.qrc
SOURCES += tessera_tests.cpp ../src/artcache.cpp ../src/collage.cpp ../src/commands.cpp ../src/folder.cpp \
    ../src/gallery.cpp ../src/museum.cpp ../src/settings.cpp ../src/stats.cpp ../src/bindings.cpp ../src/cut.cpp ../src/game.cpp ../src/hyprland.cpp \
    ../src/levels.cpp ../src/progress.cpp ../src/puzzle.cpp ../src/theme.cpp ../src/translator.cpp
DEFINES += SRCDIR=\\\"$$PWD/../src\\\"
