import QtQuick

// Value slider with the value printed on the right. Drag or click to set.
Item {
    id: root
    property real from: 0
    property real to: 1
    property real value: 0
    property bool integer: false
    property string label: ""
    signal moved(real value)
    implicitHeight: 30
    implicitWidth: 200
    readonly property real frac: to > from ? Math.max(0, Math.min(1, (value - from) / (to - from))) : 0
    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: Theme.raised
        border.color: ma.containsMouse || ma.pressed ? Theme.borderStrong : Theme.border
        clip: true
        Rectangle {
            width: parent.width * root.frac
            height: parent.height
            radius: Theme.radius
            color: Theme.accentSoft
            Behavior on width { enabled: !ma.pressed; NumberAnimation { duration: Theme.fast } }
        }
    }
    Label { x: 10; anchors.verticalCenter: parent.verticalCenter; text: root.label; color: Theme.textSecondary }
    Label {
        anchors.right: parent.right; anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        text: root.integer ? Math.round(root.value) : Number(root.value).toFixed(2)
        font.features: { "tnum": 1 }
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        function setFrom(x) {
            var f = Math.max(0, Math.min(1, x / width))
            var v = root.from + f * (root.to - root.from)
            root.moved(root.integer ? Math.round(v) : Math.round(v * 100) / 100)
        }
        onPressed: (m) => setFrom(m.x)
        onPositionChanged: (m) => { if (pressed) setFrom(m.x) }
    }
}
