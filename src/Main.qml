import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQml

// The board: the pictures to put together, where each one goes, the clock,
// and what to do next. The pieces live in windows of their own
// (PieceWindow.qml), tiled by Hyprland like any other window.
ApplicationWindow {
    id: win
    width: 1120
    height: 720
    minimumWidth: 720
    minimumHeight: 500
    visible: true
    title: "Tessera"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    property bool confirmQuit: false
    property bool confirmLeave: false
    property bool statsVisible: false
    property bool picturesVisible: false
    property int menuIndex: 0
    property int sourceIndex: 0

    readonly property color accent: theme.accent
    readonly property bool onBoard: ["preparing", "waiting", "dealing", "playing", "solved"].indexOf(game.state) >= 0
    readonly property int lessonCount: game.lessons.length
    readonly property int practiceIndex: lessonCount + game.tiers.length
    readonly property int menuCount: practiceIndex + 1
    readonly property bool overlay: helpVisible || statsVisible || picturesVisible
    // Where pictures come from, in the order P's list shows them.
    readonly property var sources: [
        { id: "mixed", name: qsTr("Mixed"), about: qsTr("The built-in works, museum art and your folder, together.") },
        { id: "builtin", name: qsTr("Built-in"), about: qsTr("The five public-domain works that come with Tessera. Works offline.") },
        { id: "online", name: qsTr("Museums online"), about: qsTr("Public-domain art from the Art Institute of Chicago and the Met, fetched and kept in a cache.") },
        { id: "folder", name: qsTr("My folder"), about: qsTr("JPEG, PNG and WebP pictures from a folder of yours. They never leave this computer.") }
    ]
    function sourceName(id) {
        for (const s of sources) if (s.id === id) return s.name
        return id
    }
    function seconds(ms) {
        const lang = translator.language
        return (ms / 1000).toLocaleString(Qt.locale(), 'f', 1) + " s"
    }
    function openPictures() {
        for (let i = 0; i < sources.length; ++i) if (sources[i].id === gallery.source) sourceIndex = i
        picturesVisible = true
    }

    function quit() {
        game.shutdown()
        Qt.quit()
    }
    function askQuit() {
        if (game.midPuzzle) confirmQuit = true
        else quit()
    }
    function clock(ms) {
        const lang = translator.language
        const total = Math.max(0, ms)
        const minutes = Math.floor(total / 60000)
        const seconds = (total % 60000) / 1000
        return minutes.toLocaleString(Qt.locale(), 'f', 0) + ":" + (seconds < 10 ? Number(0).toLocaleString(Qt.locale(), 'f', 0) : "")
               + seconds.toLocaleString(Qt.locale(), 'f', 1)
    }
    function activateMenu(i) {
        if (i < lessonCount) game.playLesson(i)
        else if (i < practiceIndex) game.playTier(i - lessonCount)
        else game.playPractice()
    }

    // Closing the board mid-puzzle is almost always a Super+W meant for a
    // piece, so it asks first; closing it otherwise quits, pieces and all.
    onClosing: (close) => {
        if (game.midPuzzle && !confirmQuit) {
            close.accepted = false
            confirmQuit = true
            return
        }
        game.shutdown()
        Qt.quit()
    }

    Instantiator {
        model: game.pieces
        delegate: PieceWindow {
            onQuitRequested: win.quit()
        }
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.WindowShortcut; onActivated: win.askQuit() }
    Shortcut { sequence: "Y"; context: Qt.WindowShortcut; enabled: win.confirmQuit || win.confirmLeave
        onActivated: { if (win.confirmQuit) win.quit(); else { win.confirmLeave = false; game.toMenu() } } }
    Shortcut { sequence: "N"; context: Qt.WindowShortcut; enabled: win.confirmQuit || win.confirmLeave
        onActivated: { win.confirmQuit = false; win.confirmLeave = false } }
    Shortcut { sequence: "Space"; context: Qt.WindowShortcut
        onActivated: {
            if (win.picturesVisible) gallery.setSource(win.sources[win.sourceIndex].id)
            else if (win.statsVisible) { if (game.puzzlesUnlocked) { win.statsVisible = false; game.playPractice() } }
            else if (game.state === "menu") win.activateMenu(win.menuIndex)
            else game.primary()
        } }
    Shortcut { sequences: ["Return", "Enter"]; context: Qt.WindowShortcut
        onActivated: {
            if (win.confirmQuit) win.quit()
            else if (win.picturesVisible) gallery.setSource(win.sources[win.sourceIndex].id)
            else if (win.statsVisible) { if (game.puzzlesUnlocked) { win.statsVisible = false; game.playPractice() } }
            else if (game.state === "menu") win.activateMenu(win.menuIndex)
            else game.primary()
        } }
    Shortcut { sequence: "R"; context: Qt.WindowShortcut; enabled: !win.overlay; onActivated: game.restart() }
    Shortcut { sequence: "H"; context: Qt.WindowShortcut; enabled: !win.overlay; onActivated: game.hint() }
    // The stats and where pictures come from open from the levels.
    Shortcut { sequence: "S"; context: Qt.WindowShortcut; enabled: game.state === "menu" && !win.picturesVisible && !win.helpVisible
        onActivated: win.statsVisible = !win.statsVisible }
    Shortcut { sequence: "P"; context: Qt.WindowShortcut; enabled: game.state === "menu" && !win.statsVisible && !win.helpVisible
        onActivated: { if (win.picturesVisible) win.picturesVisible = false; else win.openPictures() } }
    Shortcut { sequence: "O"; context: Qt.WindowShortcut; enabled: win.picturesVisible; onActivated: gallery.setOnline(!gallery.online) }
    Shortcut { sequence: "F"; context: Qt.WindowShortcut; enabled: win.picturesVisible; onActivated: gallery.chooseFolder() }
    Shortcut { sequence: "X"; context: Qt.WindowShortcut; enabled: win.picturesVisible && gallery.folder !== ""; onActivated: gallery.forgetFolder() }
    Shortcut { sequence: "Escape"; context: Qt.WindowShortcut
        onActivated: {
            if (win.helpVisible) win.helpVisible = false
            else if (win.statsVisible) win.statsVisible = false
            else if (win.picturesVisible) win.picturesVisible = false
            else if (win.confirmQuit || win.confirmLeave) { win.confirmQuit = false; win.confirmLeave = false }
            else if (game.midPuzzle) win.confirmLeave = true
            else game.toMenu()
        } }
    Shortcut { sequence: "Down"; context: Qt.WindowShortcut; enabled: game.state === "menu" && !win.helpVisible
        onActivated: {
            if (win.picturesVisible) win.sourceIndex = Math.min(win.sources.length - 1, win.sourceIndex + 1)
            else if (win.statsVisible) statsList.flick(0, -900)
            else win.menuIndex = Math.min(win.menuCount - 1, win.menuIndex + 1)
        } }
    Shortcut { sequence: "Up"; context: Qt.WindowShortcut; enabled: game.state === "menu" && !win.helpVisible
        onActivated: {
            if (win.picturesVisible) win.sourceIndex = Math.max(0, win.sourceIndex - 1)
            else if (win.statsVisible) statsList.flick(0, 900)
            else win.menuIndex = Math.max(0, win.menuIndex - 1)
        } }

    Connections {
        target: game
        function onChanged() {
            if (game.state === "menu" && win.menuIndex >= win.menuCount) win.menuIndex = 0
        }
    }

    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 16

            // ── Header ───────────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                spacing: 14
                Rectangle {
                    width: 30; height: 30
                    radius: Math.min(6, theme.radius)
                    color: win.accent
                    Grid {
                        anchors.centerIn: parent
                        columns: 2; spacing: 2
                        Repeater { model: 4; Rectangle { width: 9; height: 9; color: index === 1 ? win.accent : "#0e0e10"
                                                        border.color: "#0e0e10"; border.width: index === 1 ? 2 : 0 } }
                    }
                }
                Text {
                    text: "Tessera"
                    color: "#f2f2f2"
                    font.pixelSize: 22
                    font.weight: Font.Bold
                }
                Text {
                    visible: game.state !== "menu" && game.state !== "connecting" && game.state !== "error"
                    text: "· " + game.levelTitle
                    color: "#c8c8d0"
                    font.pixelSize: 18
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Item { Layout.fillWidth: true; visible: !(game.state !== "menu" && game.state !== "connecting" && game.state !== "error") }
                // Going online is always said, while it happens.
                Rectangle {
                    visible: gallery.fetching
                    Layout.maximumWidth: 360
                    Layout.preferredWidth: onlineText.implicitWidth + 20
                    Layout.preferredHeight: onlineText.implicitHeight + 8
                    radius: Math.min(4, theme.radius)
                    color: "#1d2430"
                    border.color: "#3a5070"
                    Text {
                        id: onlineText
                        anchors.fill: parent
                        anchors.leftMargin: 10; anchors.rightMargin: 10
                        verticalAlignment: Text.AlignVCenter
                        text: "⇅ " + (gallery.status !== "" ? gallery.status : qsTr("Online"))
                        color: "#a8c8f0"
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }
                Text {
                    visible: !game.lesson && (game.state === "playing" || game.state === "solved")
                    text: win.clock(game.elapsed)
                    color: game.state === "solved" ? win.accent : "#f2f2f2"
                    font.pixelSize: 22
                    font.family: "monospace"
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "★ " + game.totalStars.toLocaleString(Qt.locale(), 'f', 0)
                    color: win.accent
                    font.pixelSize: 16
                }
                Text {
                    text: qsTr("? shortcuts")
                    color: "#7a7a84"
                    font.pixelSize: 13
                }
            }

            Text {
                Layout.fillWidth: true
                readonly property var lines: [game.notice, game.saveError, gallery.saveError,
                                              win.onBoard ? game.pictureNote : ""].filter(t => t !== "")
                visible: lines.length > 0
                text: lines.join("  ")
                color: "#e0a060"
                wrapMode: Text.Wrap
                font.pixelSize: 13
            }

            // ── Body ─────────────────────────────────────────────────
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                // Connecting or failed: what's wrong, in words.
                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width, 620)
                    visible: game.state === "connecting" || game.state === "error"
                    spacing: 14
                    Text {
                        Layout.fillWidth: true
                        text: game.state === "connecting" ? qsTr("Connecting to Hyprland…") : qsTr("Something's in the way")
                        color: "#f2f2f2"
                        font.pixelSize: 24
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Text {
                        Layout.fillWidth: true
                        text: game.error
                        color: "#c8c8d0"
                        wrapMode: Text.Wrap
                        font.pixelSize: 16
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: game.state === "error"
                        text: qsTr("R tries again · Esc goes to the levels · Q quits")
                        color: win.accent
                        font.pixelSize: 14
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                // The levels: lessons first, then the timed puzzles.
                RowLayout {
                    anchors.fill: parent
                    visible: game.state === "menu"
                    spacing: 20

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 3
                        spacing: 8
                        Text { text: qsTr("Lessons"); color: win.accent; font.pixelSize: 13; font.weight: Font.DemiBold
                               font.capitalization: Font.AllUppercase; font.letterSpacing: 1 }
                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 6
                            model: game.lessons
                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                width: ListView.view.width
                                height: 54
                                radius: Math.min(6, theme.radius)
                                readonly property bool selected: win.menuIndex === index
                                color: selected ? "#202026" : "#16161a"
                                border.color: selected ? win.accent : "#26262c"
                                opacity: modelData.unlocked ? 1 : 0.45
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 14; anchors.rightMargin: 14
                                    spacing: 12
                                    Text { text: (index + 1).toLocaleString(Qt.locale(), 'f', 0); color: "#7a7a84"; font.pixelSize: 15
                                           Layout.preferredWidth: 22 }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text { Layout.fillWidth: true; text: modelData.title; color: "#f2f2f2"; font.pixelSize: 15
                                               font.weight: Font.DemiBold; elide: Text.ElideRight }
                                        Text { Layout.fillWidth: true; text: modelData.teaches; color: "#8a8a94"; font.pixelSize: 12
                                               elide: Text.ElideRight }
                                    }
                                    Text {
                                        text: modelData.unlocked ? "★".repeat(modelData.stars) + "☆".repeat(3 - modelData.stars) : "🔒"
                                        color: win.accent
                                        font.pixelSize: 16
                                    }
                                }
                                MouseArea { anchors.fill: parent; onClicked: { win.menuIndex = index; win.activateMenu(index) } }
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 2
                        spacing: 8
                        Text { text: qsTr("Puzzles"); color: win.accent; font.pixelSize: 13; font.weight: Font.DemiBold
                               font.capitalization: Font.AllUppercase; font.letterSpacing: 1 }
                        Text {
                            Layout.fillWidth: true
                            visible: !game.puzzlesUnlocked
                            text: qsTr("Finish every lesson to open the timed puzzles.")
                            color: "#8a8a94"
                            wrapMode: Text.Wrap
                            font.pixelSize: 13
                        }
                        Repeater {
                            model: game.tiers
                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                Layout.preferredHeight: tierColumn.implicitHeight + 20
                                radius: Math.min(6, theme.radius)
                                readonly property bool selected: win.menuIndex === win.lessonCount + index
                                color: selected ? "#202026" : "#16161a"
                                border.color: selected ? win.accent : "#26262c"
                                opacity: modelData.unlocked ? 1 : 0.45
                                ColumnLayout {
                                    id: tierColumn
                                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 14 }
                                    spacing: 4
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { Layout.fillWidth: true; text: modelData.title; color: "#f2f2f2"; font.pixelSize: 16
                                               font.weight: Font.DemiBold; elide: Text.ElideRight }
                                        Text {
                                            text: !modelData.unlocked ? "🔒"
                                                  : modelData.best > 0 ? qsTr("Best %1").arg(win.clock(modelData.best)) : ""
                                            color: win.accent
                                            font.pixelSize: 14
                                        }
                                    }
                                    Text { Layout.fillWidth: true; text: modelData.about; color: "#8a8a94"; font.pixelSize: 12
                                           wrapMode: Text.Wrap }
                                }
                                MouseArea { anchors.fill: parent
                                    onClicked: { win.menuIndex = win.lessonCount + index; win.activateMenu(win.lessonCount + index) } }
                            }
                        }
                        // Practice: a puzzle made of the player's weakest moves.
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: practiceColumn.implicitHeight + 20
                            radius: Math.min(6, theme.radius)
                            readonly property bool selected: win.menuIndex === win.practiceIndex
                            color: selected ? "#202026" : "#16161a"
                            border.color: selected ? win.accent : "#26262c"
                            opacity: game.puzzlesUnlocked ? 1 : 0.45
                            ColumnLayout {
                                id: practiceColumn
                                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 14 }
                                spacing: 4
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { Layout.fillWidth: true; text: qsTr("Practice weak spots"); color: "#f2f2f2"; font.pixelSize: 16
                                           font.weight: Font.DemiBold; elide: Text.ElideRight }
                                    Text { visible: !game.puzzlesUnlocked; text: "🔒"; color: win.accent; font.pixelSize: 14 }
                                }
                                Text { Layout.fillWidth: true; text: qsTr("A puzzle made of the moves you're least sure of. S shows how each one is going.")
                                       color: "#8a8a94"; font.pixelSize: 12; wrapMode: Text.Wrap }
                            }
                            MouseArea { anchors.fill: parent
                                onClicked: { win.menuIndex = win.practiceIndex; win.activateMenu(win.practiceIndex) } }
                        }

                        Item { Layout.fillHeight: true }

                        // Where the pictures come from, and whether Tessera goes online.
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: sourceColumn.implicitHeight + 20
                            radius: Math.min(6, theme.radius)
                            color: "#121216"
                            border.color: "#26262c"
                            ColumnLayout {
                                id: sourceColumn
                                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 12 }
                                spacing: 3
                                Text { Layout.fillWidth: true; text: qsTr("Pictures: %1").arg(win.sourceName(gallery.source))
                                       color: "#c8c8d0"; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight }
                                Text {
                                    Layout.fillWidth: true
                                    text: gallery.online ? qsTr("Online art is on: Tessera may fetch from the Art Institute of Chicago and the Met.")
                                                         : qsTr("Online art is off: Tessera stays offline.")
                                    color: gallery.online ? "#a8c8f0" : "#8a8a94"
                                    font.pixelSize: 12
                                    wrapMode: Text.Wrap
                                }
                            }
                            MouseArea { anchors.fill: parent; onClicked: win.openPictures() }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("↑ ↓ choose · Space starts · S stats · P pictures · ? shortcuts · Q quits")
                            color: "#7a7a84"
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }
                    }
                }

                // The card before a level: the move it teaches, with the keys.
                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width, 640)
                    visible: game.state === "intro"
                    spacing: 16
                    Text {
                        Layout.fillWidth: true
                        text: game.teaches
                        color: win.accent
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 1
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Text {
                        Layout.fillWidth: true
                        text: game.levelTitle
                        color: "#f2f2f2"
                        font.pixelSize: 30
                        font.weight: Font.Bold
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: game.intro
                        color: "#c8c8d0"
                        font.pixelSize: 17
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Repeater {
                        model: game.introChords
                        delegate: Item {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: caps.height
                            Keycaps {
                                id: caps
                                anchors.horizontalCenter: parent.horizontalCenter
                                chord: modelData
                                keySize: 16
                                width: Math.min(naturalWidth, parent.width)
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: !game.lesson && game.best > 0
                        text: qsTr("Best time %1").arg(win.clock(game.best))
                        color: win.accent
                        font.pixelSize: 15
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Tessera deals the pieces on empty workspaces and never touches your other windows.")
                        color: "#8a8a94"
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Space deals · Esc goes back")
                        color: win.accent
                        font.pixelSize: 15
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                // The board while a level is on.
                RowLayout {
                    anchors.fill: parent
                    visible: win.onBoard
                    spacing: 20

                    Flickable {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentHeight: cards.implicitHeight
                        clip: true
                        GridLayout {
                            id: cards
                            width: parent.width
                            columns: game.pictures.length >= 3 ? (width > 900 ? 3 : 2) : (width > 520 ? Math.max(1, game.pictures.length) : 1)
                            columnSpacing: 14
                            rowSpacing: 14
                            Repeater {
                                model: game.pictures
                                delegate: PictureCard {
                                    required property var modelData
                                    picture: modelData
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignTop
                                    Layout.maximumWidth: game.pictures.length === 1 ? 680 : 100000
                                }
                            }
                        }
                    }

                    HintPanel {
                        Layout.preferredWidth: Math.max(280, Math.min(380, win.width * 0.32))
                        Layout.alignment: Qt.AlignTop
                    }
                }

                // Done: stars for a lesson, the time for a puzzle.
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    visible: game.state === "solved"
                    width: Math.min(parent.width, solvedColumn.implicitWidth + 60)
                    height: solvedColumn.implicitHeight + 30
                    radius: Math.min(8, theme.radius)
                    color: "#1d1d22"
                    border.color: win.accent
                    border.width: 2
                    ColumnLayout {
                        id: solvedColumn
                        anchors.centerIn: parent
                        spacing: 6
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: game.lesson ? "★".repeat(game.stars) + "☆".repeat(3 - game.stars)
                                              : (game.newBest ? qsTr("New best time: %1") : qsTr("Solved in %1")).arg(win.clock(game.elapsed))
                            color: win.accent
                            font.pixelSize: game.lesson ? 34 : 24
                            font.weight: Font.Bold
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: game.lesson ? (game.stars === 3 ? qsTr("Solved without a hint!")
                                                                  : qsTr("Solved. Fewer hints earn more stars."))
                                              : (!game.newBest && game.best > 0 ? qsTr("Best time %1").arg(win.clock(game.best)) : qsTr("Every picture is complete."))
                            color: "#f2f2f2"
                            font.pixelSize: 15
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            visible: game.summary.moves !== undefined && game.summary.par > 0
                            text: game.summary.moves !== undefined
                                  ? qsTr("%n move(s) · par %1", "", game.summary.moves).arg(Number(game.summary.par).toLocaleString(Qt.locale(), 'f', 0))
                                    + ((game.summary.best || "") !== "" ? "  ·  " + qsTr("Best: %1").arg(game.summary.best) : "")
                                    + ((game.summary.worst || "") !== "" ? "  ·  " + qsTr("Needs practice: %1").arg(game.summary.worst) : "")
                                  : ""
                            color: "#c8c8d0"
                            font.pixelSize: 13
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Space: next · R: again · Esc: levels")
                            color: "#8a8a94"
                            font.pixelSize: 13
                        }
                    }
                }
            }
        }

        // A passing word from the game.
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 24
            visible: toastText.text !== "" && toastTimer.running
            width: Math.min(parent.width - 40, toastText.implicitWidth + 32)
            height: toastText.implicitHeight + 16
            radius: Math.min(6, theme.radius)
            color: "#26262c"
            Text {
                id: toastText
                anchors.centerIn: parent
                width: Math.min(implicitWidth, win.width - 72)
                text: game.toast
                color: "#f2f2f2"
                wrapMode: Text.Wrap
                font.pixelSize: 14
            }
            Timer { id: toastTimer; interval: 4000 }
            Connections { target: game; function onToastChanged() { if (game.toast !== "") toastTimer.restart() } }
        }

        // How each kind of move is going: its level first, since focus and
        // swaps naturally come up far more often than the rest.
        Rectangle {
            anchors.fill: parent
            visible: win.statsVisible
            color: "#0e0e10"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: qsTr("Your shortcuts"); color: "#f2f2f2"; font.pixelSize: 22; font.weight: Font.Bold }
                    Item { Layout.fillWidth: true }
                    Text { text: qsTr("↑ ↓ scroll · Space practises weak spots · Esc closes"); color: "#7a7a84"; font.pixelSize: 12
                           elide: Text.ElideRight; Layout.maximumWidth: win.width * 0.55 }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("A level weighs how often the move was the right one, how often you made it without its keys on screen, and how quickly. Focus and swaps come up far more often than the rest, so compare levels, not counts.")
                    color: "#8a8a94"
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                }
                // Narrow, the runs column doesn't fit: the latest run says it.
                Text {
                    Layout.fillWidth: true
                    visible: win.width < 940 && game.recentRuns.length > 0
                    readonly property var last: game.recentRuns.length > 0 ? game.recentRuns[0] : null
                    text: last ? qsTr("Last run: %1 · %2").arg(last.title)
                                     .arg(qsTr("%n move(s) · par %1", "", last.moves).arg(Number(last.par).toLocaleString(Qt.locale(), 'f', 0)))
                               : ""
                    color: "#c8c8d0"
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 24

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 3
                        spacing: 6
                        // Column heads, sized like the rows below.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            Text { Layout.fillWidth: true; text: qsTr("Move"); color: win.accent; font.pixelSize: 11; font.weight: Font.DemiBold
                                   font.capitalization: Font.AllUppercase; font.letterSpacing: 1 }
                            Text { Layout.preferredWidth: 96; text: qsTr("Level"); color: win.accent; font.pixelSize: 11; font.weight: Font.DemiBold
                                   font.capitalization: Font.AllUppercase; font.letterSpacing: 1; elide: Text.ElideRight }
                            Text { Layout.preferredWidth: 64; text: qsTr("Uses"); color: win.accent; font.pixelSize: 11; font.weight: Font.DemiBold
                                   font.capitalization: Font.AllUppercase; font.letterSpacing: 1; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight }
                            Text { Layout.preferredWidth: 112; text: qsTr("Accuracy"); color: win.accent; font.pixelSize: 11; font.weight: Font.DemiBold
                                   font.capitalization: Font.AllUppercase; font.letterSpacing: 1; elide: Text.ElideRight }
                            Text { Layout.preferredWidth: 84; text: qsTr("Reaction"); color: win.accent; font.pixelSize: 11; font.weight: Font.DemiBold
                                   font.capitalization: Font.AllUppercase; font.letterSpacing: 1; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight }
                        }
                        ListView {
                            id: statsList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 4
                            boundsBehavior: Flickable.StopAtBounds
                            model: win.statsVisible ? game.commandStats : []
                            delegate: Rectangle {
                                required property var modelData
                                width: ListView.view.width
                                height: statRow.implicitHeight + 12
                                radius: Math.min(6, theme.radius)
                                color: "#16161a"
                                border.color: "#26262c"
                                RowLayout {
                                    id: statRow
                                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                                              leftMargin: 12; rightMargin: 12 }
                                    spacing: 12
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 4
                                        Text { Layout.fillWidth: true; text: modelData.name; color: "#f2f2f2"; font.pixelSize: 14
                                               font.weight: Font.DemiBold; elide: Text.ElideRight }
                                        Keycaps { Layout.fillWidth: true; chord: modelData.chord; keySize: 10 }
                                    }
                                    // The headline: the level, not the count.
                                    Rectangle {
                                        Layout.preferredWidth: 96
                                        Layout.preferredHeight: 24
                                        radius: Math.min(4, theme.radius)
                                        readonly property int lv: modelData.level
                                        color: lv >= 5 ? win.accent : lv >= 4 ? Qt.rgba(win.accent.r, win.accent.g, win.accent.b, 0.35)
                                             : lv >= 3 ? Qt.rgba(win.accent.r, win.accent.g, win.accent.b, 0.18) : "#202026"
                                        border.color: lv >= 3 ? win.accent : "#3a3a42"
                                        Text {
                                            anchors.fill: parent
                                            anchors.leftMargin: 4; anchors.rightMargin: 4
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            text: modelData.levelName
                                            color: parent.lv >= 5 ? theme.accentForeground : parent.lv >= 1 ? "#f2f2f2" : "#7a7a84"
                                            font.pixelSize: 12
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }
                                    }
                                    Text {
                                        Layout.preferredWidth: 64
                                        horizontalAlignment: Text.AlignRight
                                        text: Number(modelData.uses).toLocaleString(Qt.locale(), 'f', 0)
                                        color: "#8a8a94"
                                        font.pixelSize: 13
                                    }
                                    // Right, so-so and detours, as one bar.
                                    Item {
                                        Layout.preferredWidth: 112
                                        Layout.preferredHeight: 10
                                        Rectangle { anchors.fill: parent; radius: Math.min(3, theme.radius); color: "#202026" }
                                        Row {
                                            anchors.fill: parent
                                            visible: modelData.uses > 0
                                            Rectangle { width: parent.width * modelData.good; height: parent.height; color: win.accent }
                                            Rectangle { width: parent.width * modelData.neutral; height: parent.height; color: "#6a6a74" }
                                            Rectangle { width: parent.width * modelData.detour; height: parent.height; color: "#c05a4a" }
                                        }
                                    }
                                    Text {
                                        Layout.preferredWidth: 84
                                        horizontalAlignment: Text.AlignRight
                                        text: modelData.reaction >= 0 ? win.seconds(modelData.reaction) : "–"
                                        color: "#c8c8d0"
                                        font.pixelSize: 13
                                    }
                                }
                            }
                        }
                        Row {
                            spacing: 14
                            Repeater {
                                model: [{ c: win.accent, t: qsTr("Right") }, { c: "#6a6a74", t: qsTr("Looking around") },
                                        { c: "#c05a4a", t: qsTr("Detour") }]
                                delegate: Row {
                                    required property var modelData
                                    spacing: 6
                                    Rectangle { width: 10; height: 10; anchors.verticalCenter: parent.verticalCenter; color: modelData.c }
                                    Text { text: modelData.t; color: "#8a8a94"; font.pixelSize: 12 }
                                }
                            }
                        }
                    }

                    // Recent runs: moves against the deal's par.
                    ColumnLayout {
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1.4
                        Layout.maximumWidth: 340
                        visible: win.width >= 940
                        spacing: 8
                        Text { text: qsTr("Recent runs"); color: win.accent; font.pixelSize: 11; font.weight: Font.DemiBold
                               font.capitalization: Font.AllUppercase; font.letterSpacing: 1 }
                        Text {
                            Layout.fillWidth: true
                            visible: game.recentRuns.length === 0
                            text: qsTr("Finish a lesson or a puzzle and it shows up here.")
                            color: "#8a8a94"
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                        }
                        Repeater {
                            model: win.statsVisible ? game.recentRuns : []
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: runColumn.implicitHeight + 14
                                radius: Math.min(6, theme.radius)
                                color: "#16161a"
                                border.color: "#26262c"
                                ColumnLayout {
                                    id: runColumn
                                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
                                    spacing: 2
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { Layout.fillWidth: true; text: modelData.title; color: "#f2f2f2"; font.pixelSize: 13
                                               font.weight: Font.DemiBold; elide: Text.ElideRight }
                                        Text { text: win.clock(modelData.ms); color: "#c8c8d0"; font.pixelSize: 12; font.family: "monospace" }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: qsTr("%n move(s) · par %1", "", modelData.moves).arg(Number(modelData.par).toLocaleString(Qt.locale(), 'f', 0))
                                        color: modelData.moves <= modelData.par ? win.accent : "#8a8a94"
                                        font.pixelSize: 12
                                    }
                                }
                            }
                        }
                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }

        // Where the pictures come from: the choice, going online, the folder.
        Rectangle {
            anchors.fill: parent
            visible: win.picturesVisible
            color: "#0e0e10"
            Flickable {
                anchors.fill: parent
                contentHeight: picturesColumn.implicitHeight + 40
                clip: true
                ColumnLayout {
                    id: picturesColumn
                    x: Math.max(20, (parent.width - width) / 2)
                    y: 20
                    width: Math.min(win.width - 40, 780)
                    spacing: 10
                    Text { text: qsTr("Where pictures come from"); color: "#f2f2f2"; font.pixelSize: 22; font.weight: Font.Bold }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Every picture is made to the shape of your monitor: cropped when it's close, or a collage of a few.")
                        color: "#8a8a94"
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }
                    Repeater {
                        model: win.sources
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            Layout.preferredHeight: sourceRow.implicitHeight + 18
                            radius: Math.min(6, theme.radius)
                            readonly property bool selected: win.sourceIndex === index
                            readonly property bool current: gallery.source === modelData.id
                            color: selected ? "#202026" : "#16161a"
                            border.color: selected ? win.accent : "#26262c"
                            RowLayout {
                                id: sourceRow
                                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 14 }
                                spacing: 12
                                Text { Layout.preferredWidth: 18; text: parent.parent.current ? "●" : "○"; color: win.accent; font.pixelSize: 15 }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text { Layout.fillWidth: true; text: modelData.name; color: "#f2f2f2"; font.pixelSize: 15
                                           font.weight: Font.DemiBold; elide: Text.ElideRight }
                                    Text { Layout.fillWidth: true; text: modelData.about; color: "#8a8a94"; font.pixelSize: 12
                                           wrapMode: Text.Wrap }
                                }
                            }
                            MouseArea { anchors.fill: parent; onClicked: { win.sourceIndex = index; gallery.setSource(modelData.id) } }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: "#26262c" }

                    // Going online, said plainly: to whom, what for, and the key to stop it.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Keycaps { chord: "O"; keySize: 12; width: naturalWidth; Layout.alignment: Qt.AlignTop }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text {
                                Layout.fillWidth: true
                                text: gallery.online ? qsTr("Online art: on") : qsTr("Online art: off")
                                color: gallery.online ? "#a8c8f0" : "#c8c8d0"
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                            }
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Tessera asks %1 for works marked public domain, and keeps what it fetches in ~/.cache/tessera. It sends nothing about you or your pictures.")
                                          .arg(gallery.hosts.join(", "))
                                color: "#8a8a94"
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                            }
                            Text {
                                Layout.fillWidth: true
                                text: gallery.fetching ? gallery.status
                                      : qsTr("Museum pictures cached: %1").arg(Number(gallery.onlineCount).toLocaleString(Qt.locale(), 'f', 0))
                                color: gallery.fetching ? "#a8c8f0" : "#7a7a84"
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Keycaps { chord: "F"; keySize: 12; width: naturalWidth; Layout.alignment: Qt.AlignTop }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text {
                                Layout.fillWidth: true
                                text: gallery.folder !== "" ? qsTr("Your folder") : qsTr("Choose a folder…")
                                color: "#c8c8d0"
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: gallery.folder !== ""
                                text: gallery.folder
                                textFormat: Text.PlainText
                                color: "#c8c8d0"
                                font.pixelSize: 12
                                elide: Text.ElideMiddle
                            }
                            Text {
                                Layout.fillWidth: true
                                text: gallery.folder !== ""
                                      ? qsTr("Pictures found: %1").arg(Number(gallery.folderCount).toLocaleString(Qt.locale(), 'f', 0))
                                        + " · " + qsTr("F picks another, X forgets it")
                                      : qsTr("Tessera reads the JPEG, PNG and WebP files in it, never changes them, and never sends them anywhere.")
                                color: "#8a8a94"
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: gallery.folderProblem !== ""
                                text: gallery.folderProblem
                                color: "#e0a060"
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        text: qsTr("↑ ↓ choose · Space uses it · O online on or off · F folder · Esc closes")
                        color: "#7a7a84"
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }
                }
            }
        }

        // Every shortcut, the player's own chords.
        Rectangle {
            anchors.fill: parent
            visible: win.helpVisible
            color: "#0e0e10"
            MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 1060)
                spacing: 12
                Text { text: qsTr("Shortcuts"); color: "#f2f2f2"; font.pixelSize: 22; font.weight: Font.Bold }
                GridLayout {
                    Layout.fillWidth: true
                    columns: win.width > 980 ? 2 : 1
                    columnSpacing: 36
                    rowSpacing: 8
                    uniformCellWidths: true
                    Repeater {
                        model: game.cheatSheet
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 10
                            Text { Layout.fillWidth: true; text: modelData.name; color: "#c8c8d0"; font.pixelSize: 14; wrapMode: Text.Wrap }
                            Keycaps { chord: modelData.chord; keySize: 11; width: naturalWidth }
                        }
                    }
                }
                Text { text: qsTr("Esc or ? closes this"); color: "#7a7a84"; font.pixelSize: 12 }
            }
        }

        // Asks before throwing a puzzle away.
        Rectangle {
            anchors.fill: parent
            visible: win.confirmQuit || win.confirmLeave
            color: Qt.rgba(0, 0, 0, 0.7)
            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 460)
                height: confirmColumn.implicitHeight + 40
                radius: Math.min(8, theme.radius)
                color: "#1d1d22"
                border.color: win.accent
                ColumnLayout {
                    id: confirmColumn
                    anchors.centerIn: parent
                    width: parent.width - 40
                    spacing: 12
                    Text {
                        Layout.fillWidth: true
                        text: win.confirmQuit ? qsTr("Quit Tessera? The puzzle and all its pieces close.")
                                              : qsTr("Leave this puzzle? Its pieces close.")
                        color: "#f2f2f2"
                        font.pixelSize: 17
                        wrapMode: Text.Wrap
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        spacing: 10
                        Button { text: qsTr("Keep playing (N)"); flat: true
                                 onClicked: { win.confirmQuit = false; win.confirmLeave = false } }
                        Button { text: win.confirmQuit ? qsTr("Quit (Y)") : qsTr("Leave (Y)"); highlighted: true
                                 onClicked: { if (win.confirmQuit) win.quit(); else { win.confirmLeave = false; game.toMenu() } } }
                    }
                }
            }
        }
    }
}
