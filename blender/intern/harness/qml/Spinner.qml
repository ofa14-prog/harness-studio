import QtQuick

// Small activity indicator: an arc that turns.
Item {
    id: root
    property int size: 14
    property color color: Theme.textSecondary
    implicitWidth: size
    implicitHeight: size
    Canvas {
        id: c
        anchors.fill: parent
        onPaint: {
            var g = getContext("2d"); g.reset()
            g.lineWidth = Math.max(1.5, root.size / 9)
            g.strokeStyle = root.color
            g.lineCap = "round"
            g.beginPath(); g.arc(width / 2, height / 2, width / 2 - g.lineWidth, 0, Math.PI * 1.4); g.stroke()
        }
        RotationAnimator on rotation { from: 0; to: 360; duration: 900; loops: Animation.Infinite; running: root.visible }
    }
}
