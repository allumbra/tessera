// tessera — a jigsaw puzzle you solve with Omarchy's window shortcuts.

#include <QGuiApplication>
#include <QDir>
#include <QIcon>
#include <QStandardPaths>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QQuickStyle>
#include <QUrl>

#include "game.h"
#include "gallery.h"
#include "hyprland.h"
#include "portalfilepicker.h"
#include "theme.h"
#include "translator.h"

// The user's own chords, as Omarchy's keybindings menu prints them, so the
// hints show the keys this desktop really uses. (`hyprctl binds` only shows
// "__lua" under Hyprland's Lua config.)
static QString readBindings(QString *problem) {
    const QString command = qEnvironmentVariableIsEmpty("TESSERA_BINDINGS_COMMAND")
        ? QStringLiteral("omarchy-menu-keybindings") : qEnvironmentVariable("TESSERA_BINDINGS_COMMAND");
    QProcess process;
    process.start(command, {"--print"});
    if (!process.waitForFinished(5000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        process.kill();
        *problem = QCoreApplication::translate("Game", "Couldn't read your key bindings with %1, so the hints show "
                                                       "Omarchy's defaults.").arg(command);
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput());
}

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("tessera");
    app.setApplicationVersion("0.2.1");

    // Matches every window, the board and each piece, to tessera.desktop:
    // the compositor sees the Wayland app id "tessera", and the launcher
    // picks up the installed icon.
    app.setDesktopFileName("tessera");
    app.setWindowIcon(QIcon::fromTheme("tessera"));
    // Closing the board quits, pieces and all; Main.qml asks first mid-puzzle.
    app.setQuitOnLastWindowClosed(false);

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    HyprlandEvents events;
    Hyprctl hyprctl;
    Game game(&events, &hyprctl, qEnvironmentVariable("TESSERA_PROGRESS"));
    // Settings beside the progress; downloads and rendered pictures in the
    // cache. TESSERA_OFFLINE=1 keeps Tessera off the network whatever the
    // setting says.
    Gallery::Options galleryOptions;
    galleryOptions.settingsPath = QDir::homePath() + "/.local/state/tessera/settings.json";
    galleryOptions.cacheDir = qEnvironmentVariableIsEmpty("TESSERA_CACHE")
        ? QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/tessera"
        : qEnvironmentVariable("TESSERA_CACHE");
    galleryOptions.network = qEnvironmentVariable("TESSERA_OFFLINE") != "1";
    Gallery gallery(galleryOptions);
    PortalFilePicker picker;
    gallery.setPicker(&picker);
    game.setGallery(&gallery);
    QString problem;
    const QString bindings = readBindings(&problem);
    game.setBindings(bindings, problem);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    QObject::connect(&translator, &Translator::changed, &game, &Game::retranslate);
    // Quitting any way at all saves and takes every piece window with it. A
    // crash takes them too: they are this process's windows.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &game, &Game::shutdown);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("game", &game);
    engine.rootContext()->setContextProperty("gallery", &gallery);
    engine.addImageProvider("tessera", gallery.createImageProvider());
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    game.start();

    return app.exec();
}
