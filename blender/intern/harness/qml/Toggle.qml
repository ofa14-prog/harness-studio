import QtQuick

// On/off switch with a squared track (no capsule).
Item {
    id: root
    property bool checked: false
    property string text: ""
    property string description: ""
    signal toggled(bool value)
    implicitHeight: description.length ? 40 : 28
    implicitWidth: 260
    Column {
        anchors.left: parent.left
        anchors.right: track.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Label { text: root.text; width: parent.width }
        Label { visible: root.description.length > 0; text: root.description; color: Theme.textTertiary; font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi; width: parent.width; wrapMode: Text.WordWrap; elide: Text.ElideNone }
    }
    Rectangle {
        id: track
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 34; height: 18; radius: 5
        color: root.checked ? Theme.accent : Theme.raised
        border.color: root.checked ? Theme.accent : Theme.border
        Behavior on color { ColorAnimation { duration: Theme.normal } }
        Rectangle {
            width: 14; height: 14; radius: 4
            y: 2
            x: root.checked ? parent.width - width - 2 : 2
            color: root.checked ? "#ffffff" : Theme.textSecondary
            Behavior on x { NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
        }
    }
    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.toggled(!root.checked) }
}
