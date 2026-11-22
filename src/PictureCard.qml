import QtQuick
import QtQuick.Layouts

// One picture on the board: what it should look like, how it is cut, which
// pieces are already in place, and the workspace it belongs on.
Rectangle {
    id: card
    property var picture: ({})
    readonly property color accent: theme.accent

    implicitHeight: column.implicitHeight + 20
    radius: Math.min(8, theme.radius)
    color: "#16161a"
    border.color: picture.here ? accent : "#26262c"
    border.width: picture.here ? 2 : 1

    ColumnLayout {
        id: column
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
        spacing: 8

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: width * 9 / 16

            Image {
                id: thumb
                anchors.fill: parent
                source: card.picture.image || ""
                fillMode: Image.Stretch
                sourceSize.width: 640
                opacity: card.picture.complete ? 1 : 0.55
            }
            // The cut: each piece's tile, lit once the piece is in place.
            Repeater {
                model: card.picture.cells || []
                delegate: Rectangle {
                    required property var modelData
                    x: modelData.x * thumb.width
                    y: modelData.y * thumb.height
                    width: modelData.w * thumb.width
                    height: modelData.h * thumb.height
                    color: modelData.placed ? Qt.rgba(card.accent.r, card.accent.g, card.accent.b, 0.18) : "transparent"
                    border.color: modelData.placed ? card.accent : Qt.rgba(1, 1, 1, 0.55)
                    border.width: modelData.placed ? 2 : 1
                    Text {
                        anchors { left: parent.left; top: parent.top; margins: 4 }
                        text: (modelData.placed ? "✓ " : "") + modelData.number
                        color: modelData.placed ? card.accent : "#f2f2f2"
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        style: Text.Outline
                        styleColor: "#000000"
                    }
                }
            }
            Rectangle {
                visible: card.picture.complete === true
                anchors { right: parent.right; bottom: parent.bottom; margins: 6 }
                width: doneText.implicitWidth + 14; height: doneText.implicitHeight + 6
                radius: Math.min(4, theme.radius)
                color: card.accent
                Text { id: doneText; anchors.centerIn: parent; text: qsTr("Complete"); color: theme.accentForeground; font.pixelSize: 12; font.bold: true }
            }
        }

        Text {
            Layout.fillWidth: true
            text: card.picture.title || ""
            color: "#f2f2f2"
            font.pixelSize: 15
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: (card.picture.artist || "") + (card.picture.year ? " · " + card.picture.year : "")
            color: "#8a8a94"
            font.pixelSize: 12
            elide: Text.ElideRight
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                text: card.picture.slot > 0 ? qsTr("Workspace %1").arg(card.picture.slot) : qsTr("Workspace to come")
                color: card.picture.here ? card.accent : "#c8c8d0"
                font.pixelSize: 13
            }
            Keycaps {
                visible: (card.picture.chord || "") !== ""
                chord: card.picture.chord || ""
                keySize: 10
                width: naturalWidth
            }
            Item { Layout.fillWidth: true }
            Text {
                text: qsTr("%1 of %2").arg(Number(card.picture.placed || 0).toLocaleString(Qt.locale(), 'f', 0))
                                      .arg(Number(card.picture.total || 0).toLocaleString(Qt.locale(), 'f', 0))
                color: "#c8c8d0"
                font.pixelSize: 13
            }
        }
    }
}
