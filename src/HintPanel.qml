import QtQuick
import QtQuick.Layouts

// The context hint: what the focused piece, or the workspace in view, needs
// next, with the player's own keys.
Rectangle {
    id: panel
    readonly property color accent: theme.accent
    radius: Math.min(8, theme.radius)
    color: "#16161a"
    border.color: "#26262c"
    implicitHeight: column.implicitHeight + 28

    ColumnLayout {
        id: column
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 14 }
        spacing: 12

        Text {
            Layout.fillWidth: true
            text: qsTr("Now")
            color: panel.accent
            font.pixelSize: 12
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1
        }
        Text {
            Layout.fillWidth: true
            text: game.objective
            color: "#f2f2f2"
            wrapMode: Text.Wrap
            font.pixelSize: 15
        }
        Keycaps {
            Layout.fillWidth: true
            visible: game.objectiveChord !== ""
            chord: game.objectiveChord
            keySize: 14
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#26262c"; visible: game.state === "playing" }

        Text {
            Layout.fillWidth: true
            visible: game.state === "playing"
            text: game.focusedPiece !== "" ? qsTr("For the focused piece") : qsTr("On this workspace")
            color: panel.accent
            font.pixelSize: 12
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1
        }
        Text {
            Layout.fillWidth: true
            visible: game.state === "playing" && game.hints.length === 0
            text: game.lesson ? qsTr("Try it yourself first. H shows a hint.") : qsTr("Focus a piece to see what it needs.")
            color: "#a8a8b2"
            wrapMode: Text.Wrap
            font.pixelSize: 14
        }
        Repeater {
            model: game.state === "playing" ? game.hints : []
            delegate: ColumnLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 6
                Text {
                    Layout.fillWidth: true
                    text: modelData.text
                    color: "#f2f2f2"
                    wrapMode: Text.Wrap
                    font.pixelSize: 15
                }
                Keycaps {
                    Layout.fillWidth: true
                    visible: modelData.chord !== ""
                    chord: modelData.chord
                    keySize: 14
                }
                Text {
                    Layout.fillWidth: true
                    visible: modelData.detail !== ""
                    text: modelData.detail
                    color: "#a8a8b2"
                    wrapMode: Text.Wrap
                    font.pixelSize: 13
                }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: game.state === "playing" && game.hintTier < 3
            text: qsTr("H shows more help, here and on the focused piece.")
            color: "#7a7a84"
            wrapMode: Text.Wrap
            font.pixelSize: 12
        }
    }
}
