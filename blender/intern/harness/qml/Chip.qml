import QtQuick

// A setting chip of the composer: icon and value ("5s", "16:9", "On"). Quiet by default,
// tinted when switched on, lit while its menu is open.
Rectangle {
    id: root
    property url iconSource
    property string text: ""
    property bool checked: false
    property bool opened: false
    property string tooltip: ""
    property color accent: Theme.accent
    signal clicked()
    implicitWidth: row.implicitWidth + 18
    implicitHeight: 32
    height: 32
    radius: 8
    color: checked ? Qt.rgba(accent.r, accent.g, accent.b, 0.16)
         : opened ? Qt.rgba(1, 1, 1, 0.12) : ma.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
    border.color: checked ? Qt.rgba(accent.r, accent.g, accent.b, 0.5) : Qt.rgba(0.85, 0.85, 0.85, 0.06)
    Behavior on color { ColorAnimation { duration: Theme.fast } }
    Row {
        id: row
        x: 9
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
        Icon {
            visible: root.iconSource.toString().length > 0
            source: root.iconSource
            size: 16
            tone: root.checked || root.opened || ma.containsMouse ? 1 : 0.7
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: root.text
            color: Theme.text
            font.family: Theme.font
            font.pixelSize: Theme.sizeSmall
            font.weight: Theme.weightUi
            renderType: Text.NativeRendering
            anchors.verticalCenter: parent.verticalCenter
            rightPadding: 1
        }
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
    ToolTipLite { text: root.tooltip; shown: ma.containsMouse && root.tooltip.length > 0 }
}
