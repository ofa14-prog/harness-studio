import QtQuick

// The cut between two panels: drag to resize both, proportions are kept.
Item {
    id: root
    signal dragged(real dx)
    signal released()
    Rectangle {
        anchors.centerIn: parent
        width: 2
        height: Math.min(parent.height * 0.12, 64)
        radius: 1
        color: ma.pressed ? Theme.accent : Theme.borderStrong
        opacity: ma.containsMouse || ma.pressed ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.normal } }
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        anchors.leftMargin: -2
        anchors.rightMargin: -2
        hoverEnabled: true
        cursorShape: Qt.SplitHCursor
        property real last: 0
        onPressed: (m) => last = mapToItem(null, m.x, 0).x
        onPositionChanged: (m) => {
            if (!pressed) return
            var p = mapToItem(null, m.x, 0).x
            root.dragged(p - last)
            last = p
        }
        onReleased: root.released()
    }
}
