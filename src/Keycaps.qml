import QtQuick

// Draws a chord as keycaps: "SUPER SHIFT + RETURN" becomes [Super][Shift][Return].
// Alternatives ("SUPER + W / SUPER + Q") are joined by "or".
Flow {
    id: root
    property string chord: ""
    property int keySize: 22
    property color capColor: "#1d1d22"
    property color edgeColor: "#3a3a42"
    property color textColor: "#f2f2f2"

    spacing: Math.round(keySize * 0.3)

    // The width the keys take on one line, so a caller can size the Flow to
    // it and center it; a Flow's own implicit width doesn't say.
    readonly property real naturalWidth: {
        let w = 0, n = 0
        for (let i = 0; i < children.length; ++i) {
            const c = children[i]
            if (c.width > 0) { w += c.width; ++n }
        }
        // Rounded up, so a Flow given exactly this width never wraps over a
        // fraction of a pixel.
        return Math.ceil(w + Math.max(0, n - 1) * spacing) + 1
    }
    layoutDirection: Qt.LeftToRight

    readonly property var names: ({
        "SUPER": "Super", "SHIFT": "Shift", "CTRL": "Ctrl", "ALT": "Alt",
        "RETURN": "Return", "SPACE": "Space", "space": "Space", "ESCAPE": "Esc", "Escape": "Esc",
        "BACKSPACE": "⌫", "TAB": "Tab", "COMMA": ",", "PERIOD": ".", "SLASH": "/",
        "MINUS": "−", "EQUAL": "=", "LEFT": "←", "RIGHT": "→", "UP": "↑", "DOWN": "↓",
        "PRINT": "Print", "DELETE": "Del", "Delete": "Del", "Home": "Home",
        "BRACKETLEFT": "[", "BRACKETRIGHT": "]", "ARROWS": "← → ↑ ↓"
    })

    function keysOf(single) {
        const plus = single.lastIndexOf(" + ")
        if (plus < 0)
            return [single.trim()]
        const keys = single.substring(0, plus).split(" ").filter(k => k.length > 0)
        keys.push(single.substring(plus + 3).trim())
        return keys
    }

    // Flattened into one list of keys and "or" separators, so Flow can wrap
    // a long pair of chords onto two lines.
    readonly property var parts: {
        const out = []
        const alternatives = chord.split(" / ")
        for (let i = 0; i < alternatives.length; ++i) {
            if (i > 0)
                out.push({ separator: true, label: "" })
            for (const k of keysOf(alternatives[i]))
                out.push({ separator: false, label: names[k] !== undefined ? names[k] : k })
        }
        return out
    }

    Repeater {
        model: root.parts
        delegate: Item {
            required property var modelData
            width: modelData.separator ? orText.implicitWidth + root.keySize * 0.4 : cap.width
            height: cap.height

            Text {
                id: orText
                visible: modelData.separator
                anchors.centerIn: parent
                text: qsTr("or")
                color: "#8a8a94"
                font.pixelSize: Math.round(root.keySize * 0.7)
            }

            Rectangle {
                id: cap
                visible: !modelData.separator
                width: Math.max(height, label.implicitWidth + root.keySize * 0.9)
                height: Math.round(root.keySize * 1.9)
                radius: Math.min(Math.round(root.keySize * 0.3), Math.max(2, theme.radius))
                color: root.capColor
                border.color: root.edgeColor
                border.width: 1

                // The darker lip along the bottom makes it read as a key.
                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: Math.max(2, Math.round(root.keySize * 0.14))
                    radius: parent.radius
                    color: root.edgeColor
                }

                Text {
                    id: label
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: -Math.round(root.keySize * 0.06)
                    text: modelData.label
                    color: root.textColor
                    font.pixelSize: root.keySize
                    font.weight: Font.DemiBold
                }
            }
        }
    }
}
