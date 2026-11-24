import QtQuick

// Text button. variant: "primary" | "secondary" | "ghost" | "danger".
Item {
    id: root
    property string text: ""
    property url icon: ""
    property string variant: "secondary"
    property bool busy: false
    signal clicked()
    implicitHeight: 34
    implicitWidth: Math.max(72, row.implicitWidth + 30)
    opacity: enabled ? 1 : 0.45

    Rectangle {
        id: bg
        anchors.fill: parent
        radius: Theme.radius
        color: root.variant === "primary" ? (ma.pressed ? Qt.darker(Theme.accent, 1.15) : ma.containsMouse ? Qt.lighter(Theme.accent, 1.08) : Theme.accent)
             : root.variant === "danger" ? (ma.containsMouse ? Qt.rgba(1, 0.35, 0.32, 0.22) : Qt.rgba(1, 0.35, 0.32, 0.14))
             : root.variant === "ghost" ? (ma.pressed ? Theme.pressed : ma.containsMouse ? Theme.hover : "transparent")
             : (ma.pressed ? Theme.pressed : ma.containsMouse ? Qt.lighter(Theme.raised, 1.18) : Theme.raised)
        border.width: root.variant === "secondary" ? 1 : 0
        border.color: Theme.border
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 8
        Icon { source: root.icon; size: 15; visible: root.icon.toString().length > 0 && !root.busy; anchors.verticalCenter: parent.verticalCenter }
        Spinner { visible: root.busy; size: 14; anchors.verticalCenter: parent.verticalCenter }
        Label {
            text: root.text
            font.pixelSize: Theme.sizeBody
            font.weight: Font.Medium
            color: root.variant === "primary" ? "#ffffff" : root.variant === "danger" ? Theme.danger : Theme.text
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabled && !root.busy
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
