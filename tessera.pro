QT += core gui network qml quick quickcontrols2

CONFIG += c++17 release
TARGET = tessera
TEMPLATE = app

HEADERS += \
    src/bindings.h \
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
    src/bindings.cpp \
    src/cut.cpp \
    src/game.cpp \
    src/hyprland.cpp \
    src/levels.cpp \
    src/progress.cpp \
    src/puzzle.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
