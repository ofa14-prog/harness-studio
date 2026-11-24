import QtQuick

Item {
    id: root
    property url icon
    property int iconSize: 16
    property string tooltip: ""
    property bool checked: false
    property color hoverColor: Theme.hover
    property real tone: 0.72
    signal clicked()
    implicitWidth: 30
    implicitHeight: 30
    opacity: enabled ? 1 : 0.4
    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: root.checked ? Theme.selected : ma.pressed ? Theme.pressed : ma.containsMouse ? root.hoverColor : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
    Icon {
        anchors.centerIn: parent
        source: root.icon
        size: root.iconSize
        tone: root.checked || ma.containsMouse ? 1.0 : root.tone
        Behavior on tone { NumberAnimation { duration: Theme.fast } }
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabled
        onClicked: root.clicked()
    }
    ToolTipLite { text: root.tooltip; shown: ma.containsMouse && root.tooltip.length > 0 }
}
