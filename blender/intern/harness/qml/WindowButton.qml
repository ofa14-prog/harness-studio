import QtQuick

Item {
    id: root
    property url icon
    property bool danger: false
    signal clicked()
    width: 44
    height: parent ? parent.height : 42
    Rectangle {
        anchors.fill: parent
        color: root.danger && ma.containsMouse ? "#d9443c" : ma.pressed ? Theme.pressed : ma.containsMouse ? Theme.hover : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
    Icon { anchors.centerIn: parent; source: root.icon; size: 16; tone: ma.containsMouse ? 1 : 0.7 }
    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; onClicked: root.clicked() }
}
