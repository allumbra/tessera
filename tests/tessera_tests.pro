QT += core gui network quick quickcontrols2 testlib
CONFIG += c++17 testcase
TARGET = tessera_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += fakehyprland.h dwindle.h player.h ../src/bindings.h ../src/cut.h ../src/game.h ../src/hyprland.h \
    ../src/levels.h ../src/progress.h ../src/puzzle.h ../src/theme.h ../src/translator.h
RESOURCES += ../src/resources.qrc
SOURCES += tessera_tests.cpp ../src/bindings.cpp ../src/cut.cpp ../src/game.cpp ../src/hyprland.cpp \
    ../src/levels.cpp ../src/progress.cpp ../src/puzzle.cpp ../src/theme.cpp ../src/translator.cpp
DEFINES += SRCDIR=\\\"$$PWD/../src\\\"
