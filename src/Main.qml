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
    property int menuIndex: 0

    readonly property color accent: theme.accent
    readonly property bool onBoard: ["waiting", "dealing", "playing", "solved"].indexOf(game.state) >= 0
    readonly property int lessonCount: game.lessons.length
    readonly property int menuCount: lessonCount + game.tiers.length

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
        else game.playTier(i - lessonCount)
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
        onActivated: { if (game.state === "menu") win.activateMenu(win.menuIndex); else game.primary() } }
    Shortcut { sequences: ["Return", "Enter"]; context: Qt.WindowShortcut
        onActivated: { if (win.confirmQuit) win.quit(); else if (game.state === "menu") win.activateMenu(win.menuIndex); else game.primary() } }
    Shortcut { sequence: "R"; context: Qt.WindowShortcut; onActivated: game.restart() }
    Shortcut { sequence: "H"; context: Qt.WindowShortcut; onActivated: game.hint() }
    Shortcut { sequence: "Escape"; context: Qt.WindowShortcut
        onActivated: {
            if (win.helpVisible) win.helpVisible = false
            else if (win.confirmQuit || win.confirmLeave) { win.confirmQuit = false; win.confirmLeave = false }
            else if (game.midPuzzle) win.confirmLeave = true
            else game.toMenu()
        } }
    Shortcut { sequence: "Down"; context: Qt.WindowShortcut; enabled: game.state === "menu"
        onActivated: win.menuIndex = Math.min(win.menuCount - 1, win.menuIndex + 1) }
    Shortcut { sequence: "Up"; context: Qt.WindowShortcut; enabled: game.state === "menu"
        onActivated: win.menuIndex = Math.max(0, win.menuIndex - 1) }

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
                visible: game.notice !== "" || game.saveError !== ""
                text: [game.notice, game.saveError].filter(t => t !== "").join("  ")
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
                        Item { Layout.fillHeight: true }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("↑ ↓ choose · Space starts · ? shortcuts · Q quits")
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
