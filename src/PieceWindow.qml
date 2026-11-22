import QtQuick
import QtQuick.Window

// One piece of a picture, in a window of its own, so Hyprland tiles it like
// any other window and the player's shortcuts act on it for real. It shows
// its slice of the picture stretched over the whole window: the picture only
// comes together when every piece has the right tile.
Window {
    id: win
    required property string pieceId
    required property string windowTitle
    required property string label
    required property string image
    required property rect cell
    required property bool inPlace
    // The hint strip shows on the focused piece until H hides it.
    property bool hintWanted: true
    property bool confirmQuit: false

    signal quitRequested()

    function clock(ms) {
        const lang = translator.language
        const total = Math.max(0, ms)
        const minutes = Math.floor(total / 60000)
        const seconds = (total % 60000) / 1000
        return minutes.toLocaleString(Qt.locale(), 'f', 0) + ":" + (seconds < 10 ? Number(0).toLocaleString(Qt.locale(), 'f', 0) : "")
               + seconds.toLocaleString(Qt.locale(), 'f', 1)
    }

    // A window nested in another becomes its dialog, and Hyprland floats
    // dialogs; pieces have to tile, so they have no parent window.
    transientParent: null
    title: windowTitle
    // The size a floating piece gets; tiled, Hyprland decides.
    width: 480
    height: 270
    visible: true
    color: "#000000"

    readonly property color accent: theme.accent
    readonly property bool focusedHere: win.active
    readonly property real unit: Math.max(0.65, Math.min(1.25, Math.min(width / 520, height / 300)))
    readonly property var firstHint: game.hints.length > 0 ? game.hints[0] : null
    // This piece's picture, from the board's list, so a piece knows when
    // its picture is whole.
    readonly property var artwork: {
        for (const p of game.pictures)
            if (p.image === win.image)
                return p
        return null
    }
    readonly property bool pictureDone: !!(artwork && artwork.complete)
    readonly property bool solved: game.state === "solved"
    // One piece speaks for the whole picture: its biggest, the first of
    // equals, which always has room for the banner.
    readonly property bool speaker: {
        if (!artwork || !artwork.cells)
            return false
        let best = null
        for (const c of artwork.cells)
            if (!best || c.w * c.h > best.w * best.h + 1e-6)
                best = c
        return best !== null && Math.abs(best.x - cell.x) < 1e-3 && Math.abs(best.y - cell.y) < 1e-3
    }
    onPictureDoneChanged: if (pictureDone) cheer.restart()
    onSolvedChanged: if (solved) cheer.restart()
    readonly property bool mine: firstHint !== null && (firstHint.piece === "" || firstHint.piece === win.pieceId
                                                        || game.focusedPiece === win.pieceId)

    // Super+W asks a piece to close; while a puzzle is on it stays.
    onClosing: (close) => {
        close.accepted = game.requestPieceClose(win.pieceId)
        if (!close.accepted)
            refusal.restart()
    }

    Shortcut { sequence: "H"; context: Qt.WindowShortcut
        onActivated: { if (!win.hintWanted) win.hintWanted = true; else game.hint() } }
    Shortcut { sequence: "Escape"; context: Qt.WindowShortcut
        onActivated: { if (win.confirmQuit) win.confirmQuit = false; else win.hintWanted = false } }
    Shortcut { sequence: "Q"; context: Qt.WindowShortcut
        onActivated: { if (game.midPuzzle) win.confirmQuit = true; else win.quitRequested() } }
    Shortcut { sequence: "Y"; context: Qt.WindowShortcut; enabled: win.confirmQuit; onActivated: win.quitRequested() }
    Shortcut { sequence: "Return"; context: Qt.WindowShortcut; enabled: win.confirmQuit; onActivated: win.quitRequested() }
    Shortcut { sequence: "N"; context: Qt.WindowShortcut; enabled: win.confirmQuit; onActivated: win.confirmQuit = false }
    // Solved, the board's keys work here too, so there's no need to go back.
    Shortcut { sequence: "Space"; context: Qt.WindowShortcut; enabled: win.solved; onActivated: game.primary() }
    Shortcut { sequence: "R"; context: Qt.WindowShortcut; enabled: win.solved; onActivated: game.restart() }

    Image {
        id: picture
        anchors.fill: parent
        source: win.image
        // The slice, in the picture's own pixels (every picture is 1600 by
        // 900), scaled to whatever tile the window has now.
        sourceClipRect: Qt.rect(win.cell.x * 1600, win.cell.y * 900, win.cell.width * 1600, win.cell.height * 900)
        fillMode: Image.Stretch
        smooth: true
        asynchronous: false
    }

    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        // The focused piece is ringed in the accent, inside Hyprland's own
        // border, so it's clear which piece the next shortcut moves.
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: win.accent
            border.width: win.focusedHere ? Math.max(2, Math.round(3 * win.unit)) : 0
        }

        // Its name, small, so hints that name a piece can be followed.
        Rectangle {
            id: tag
            anchors { left: parent.left; top: parent.top; margins: Math.round(8 * win.unit) }
            visible: win.width >= 90 && win.height >= 60
            width: tagText.implicitWidth + Math.round(14 * win.unit)
            height: tagText.implicitHeight + Math.round(6 * win.unit)
            radius: Math.min(4, theme.radius)
            color: Qt.rgba(0, 0, 0, 0.55)
            Text {
                id: tagText
                anchors.centerIn: parent
                text: (win.inPlace ? "✓ " : "") + win.label
                color: win.inPlace ? win.accent : "#f2f2f2"
                font.pixelSize: Math.round(13 * win.unit)
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                width: Math.min(implicitWidth, win.width - Math.round(40 * win.unit))
            }
        }

        // What to do next, on the piece in hand: the context hint, worked out
        // for this piece, with the player's own keys.
        Rectangle {
            id: strip
            visible: win.focusedHere && win.hintWanted && !win.confirmQuit && game.state === "playing" && !win.pictureDone
                     && win.height >= 70
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: Math.round(8 * win.unit) }
            height: stripColumn.implicitHeight + Math.round(14 * win.unit)
            radius: Math.min(6, theme.radius)
            color: Qt.rgba(0.05, 0.05, 0.06, 0.86)
            border.color: Qt.rgba(win.accent.r, win.accent.g, win.accent.b, 0.6)
            border.width: 1

            Column {
                id: stripColumn
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                          margins: Math.round(8 * win.unit) }
                spacing: Math.round(5 * win.unit)
                readonly property bool roomy: win.width >= 240

                Text {
                    width: parent.width
                    // Narrow but tall tiles have room to wrap the words.
                    visible: stripColumn.roomy || win.height >= 300 || !win.firstHint || win.firstHint.chord === ""
                    text: win.firstHint ? win.firstHint.text
                                        : (game.lesson ? qsTr("Press H for a hint.") : game.objective)
                    color: "#f2f2f2"
                    wrapMode: Text.Wrap
                    maximumLineCount: win.height >= 300 ? 5 : win.height >= 200 ? 3 : 1
                    elide: Text.ElideRight
                    font.pixelSize: Math.round(14 * win.unit)
                }
                Keycaps {
                    visible: win.firstHint !== null && win.firstHint.chord !== ""
                    chord: win.firstHint ? win.firstHint.chord : ""
                    keySize: Math.round(12 * win.unit)
                    width: Math.min(naturalWidth, parent.width)
                }
                Text {
                    width: parent.width
                    visible: win.firstHint !== null && win.firstHint.detail !== "" && win.height >= 260 && stripColumn.roomy
                    text: win.firstHint ? win.firstHint.detail : ""
                    color: "#a8a8b2"
                    wrapMode: Text.Wrap
                    font.pixelSize: Math.round(12 * win.unit)
                }
                Text {
                    width: parent.width
                    visible: stripColumn.roomy && win.height >= 150
                    text: qsTr("H more help · Esc hide")
                    color: "#7a7a84"
                    font.pixelSize: Math.round(11 * win.unit)
                    elide: Text.ElideRight
                }
            }
        }

        // A small H for a hidden strip, so the help is never more than a key
        // away.
        Rectangle {
            visible: win.focusedHere && !strip.visible && !win.confirmQuit && game.state === "playing" && !win.pictureDone
            anchors { right: parent.right; bottom: parent.bottom; margins: Math.round(8 * win.unit) }
            width: Math.round(24 * win.unit); height: width
            radius: Math.min(4, theme.radius)
            color: Qt.rgba(0, 0, 0, 0.6)
            border.color: win.accent
            Text { anchors.centerIn: parent; text: "H"; color: win.accent; font.pixelSize: Math.round(13 * win.unit); font.bold: true }
        }


        // A finished picture lights up on every piece, and the middle piece
        // says so: the board may be on another workspace.
        Rectangle {
            id: flash
            anchors.fill: parent
            color: win.accent
            opacity: 0
            SequentialAnimation on opacity {
                id: cheer
                running: false
                NumberAnimation { to: 0.45; duration: 140 }
                NumberAnimation { to: 0; duration: 700; easing.type: Easing.OutQuad }
            }
        }
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            visible: win.pictureDone
            border.color: win.accent
            border.width: Math.max(2, Math.round(3 * win.unit))
        }
        Rectangle {
            id: banner
            visible: win.pictureDone && win.speaker && !win.confirmQuit && win.width >= 120 && win.height >= 80
            anchors.centerIn: parent
            width: Math.min(parent.width - Math.round(16 * win.unit), bannerColumn.implicitWidth + Math.round(32 * win.unit))
            height: bannerColumn.implicitHeight + Math.round(20 * win.unit)
            radius: Math.min(8, theme.radius)
            color: Qt.rgba(0.05, 0.05, 0.06, 0.9)
            border.color: win.accent
            border.width: 2
            scale: visible ? 1 : 0.6
            Behavior on scale { NumberAnimation { duration: 260; easing.type: Easing.OutBack } }

            Column {
                id: bannerColumn
                anchors.centerIn: parent
                width: Math.min(implicitWidth, win.width - Math.round(48 * win.unit))
                spacing: Math.round(6 * win.unit)
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: !win.solved ? qsTr("Complete!")
                          : game.lesson ? "★".repeat(game.stars) + "☆".repeat(3 - game.stars)
                          : (game.newBest ? qsTr("New best: %1") : qsTr("Solved in %1")).arg(win.clock(game.elapsed))
                    color: win.accent
                    font.pixelSize: Math.round((win.solved && game.lesson ? 30 : 22) * win.unit)
                    font.weight: Font.Bold
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: !win.solved && win.artwork !== null
                    readonly property int done: game.pictures.filter(p => p.complete).length
                    text: win.artwork ? qsTr("%1 · %2 of %3 pictures done").arg(win.artwork.title).arg(done).arg(game.pictures.length) : ""
                    color: "#f2f2f2"
                    font.pixelSize: Math.round(13 * win.unit)
                    elide: Text.ElideRight
                    width: Math.min(implicitWidth, win.width - Math.round(48 * win.unit))
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: win.solved
                    text: qsTr("Space: next · R: again")
                    color: "#f2f2f2"
                    font.pixelSize: Math.round(13 * win.unit)
                }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: win.solved && game.boardChord !== ""
                    spacing: Math.round(6 * win.unit)
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Board:")
                        color: "#a8a8b2"
                        font.pixelSize: Math.round(12 * win.unit)
                    }
                    Keycaps { chord: game.boardChord; keySize: Math.round(11 * win.unit) }
                }
            }
        }

        // Super+W on a piece: it says why it stays.
        Rectangle {
            id: refusalBox
            anchors.centerIn: parent
            width: Math.min(parent.width - 16, refusalText.implicitWidth + 24)
            height: refusalText.implicitHeight + 16
            radius: Math.min(6, theme.radius)
            color: Qt.rgba(0, 0, 0, 0.8)
            opacity: 0
            Text {
                id: refusalText
                anchors.centerIn: parent
                width: Math.min(implicitWidth, win.width - 40)
                text: game.toast
                color: "#f2f2f2"
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Math.round(14 * win.unit)
            }
            SequentialAnimation on opacity {
                id: refusal
                running: false
                NumberAnimation { to: 1; duration: 120 }
                PauseAnimation { duration: 2600 }
                NumberAnimation { to: 0; duration: 400 }
            }
        }

        // Q mid-puzzle asks first, right here, since the board may be on
        // another workspace.
        Rectangle {
            anchors.fill: parent
            visible: win.confirmQuit
            color: Qt.rgba(0, 0, 0, 0.82)
            Column {
                anchors.centerIn: parent
                width: parent.width - 24
                spacing: 8
                Text {
                    width: parent.width
                    text: qsTr("Quit Tessera? The puzzle and all its pieces close.")
                    color: "#f2f2f2"
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: Math.round(15 * win.unit)
                }
                Text {
                    width: parent.width
                    text: qsTr("Y quits · N keeps playing")
                    color: win.accent
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: Math.round(13 * win.unit)
                }
            }
        }
    }
}
