import QtQuick
import QtQuick.Layouts

// One picture on the board: what it should look like, how it is cut, which
// pieces are already in place, the workspace it belongs on, and who made it.
// Titles and names come from museums and file names, so they are shown as
// plain text, never as markup.
Rectangle {
    id: card
    property var picture: ({})
    readonly property color accent: theme.accent
    readonly property real aspect: picture.aspect > 0 ? picture.aspect : 16 / 9

    implicitHeight: column.implicitHeight + 20
    radius: Math.min(8, theme.radius)
    color: "#16161a"
    border.color: picture.here ? accent : "#26262c"
    border.width: picture.here ? 2 : 1

    ColumnLayout {
        id: column
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
        spacing: 8

        // The picture at the monitor's own shape; a tall one is kept from
        // taking the whole board.
        Item {
            id: frame
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(width / card.aspect, 380)

            Image {
                id: thumb
                anchors.centerIn: parent
                width: Math.min(parent.width, parent.height * card.aspect)
                height: width / card.aspect
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
                    x: thumb.x + modelData.x * thumb.width
                    y: thumb.y + modelData.y * thumb.height
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
                anchors { right: thumb.right; bottom: thumb.bottom; margins: 6 }
                width: doneText.implicitWidth + 14; height: doneText.implicitHeight + 6
                radius: Math.min(4, theme.radius)
                color: card.accent
                Text { id: doneText; anchors.centerIn: parent; text: qsTr("Complete"); color: theme.accentForeground; font.pixelSize: 12; font.bold: true }
            }
        }

        Text {
            Layout.fillWidth: true
            text: card.picture.title || ""
            textFormat: Text.PlainText
            color: "#f2f2f2"
            font.pixelSize: 15
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            visible: text !== ""
            text: (card.picture.artist || "") + (card.picture.artist && card.picture.year ? " · " : "") + (card.picture.year || "")
            textFormat: Text.PlainText
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

        // Every work in it, with where it's from and on what terms.
        Rectangle { Layout.fillWidth: true; height: 1; color: "#26262c"; visible: (card.picture.credits || []).length > 0 }
        Repeater {
            model: card.picture.credits || []
            delegate: ColumnLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 1
                Text {
                    Layout.fillWidth: true
                    visible: card.picture.collage === true
                    text: modelData.title + (modelData.artist ? " — " + modelData.artist : "") + (modelData.date ? ", " + modelData.date : "")
                    textFormat: Text.PlainText
                    color: "#c8c8d0"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: [modelData.license, modelData.origin].filter(t => t !== "").join(" · ")
                    textFormat: Text.PlainText
                    color: "#7a7a84"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: modelData.url !== ""
                    text: modelData.url.replace(/^https:\/\//, "")
                    textFormat: Text.PlainText
                    color: "#5e5e68"
                    font.pixelSize: 10
                    elide: Text.ElideMiddle
                }
            }
        }
    }
}
