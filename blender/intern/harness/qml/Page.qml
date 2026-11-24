import QtQuick

// A full-window page over the workspace (settings, profile) with a section list on the left.
Rectangle {
    id: root
    signal closed()
    property string title: ""
    property var sections: []          // [{key, text, icon}]
    property string section: ""
    default property alias content: body.data
    color: Qt.rgba(0.03, 0.035, 0.045, 0.72)
    opacity: 0
    Component.onCompleted: opacity = 1
    Behavior on opacity { NumberAnimation { duration: Theme.normal } }
    MouseArea { anchors.fill: parent; onClicked: root.closed() }
    Keys.onEscapePressed: root.closed()
    focus: true

    Rectangle {
        id: sheet
        anchors.centerIn: parent
        anchors.verticalCenterOffset: root.opacity < 1 ? 14 * (1 - root.opacity) : 0
        width: Math.min(parent.width - 80, 980)
        height: Math.min(parent.height - 80, 720)
        radius: 12
        color: Theme.panel
        border.color: Theme.border
        MouseArea { anchors.fill: parent }   // keep clicks inside

        Rectangle {
            id: side
            width: 220
            height: parent.height
            radius: 12
            color: Theme.chrome
            Rectangle { anchors.right: parent.right; width: 12; height: parent.height; color: Theme.chrome }
            Column {
                x: 12; y: 18
                width: parent.width - 24
                spacing: 2
                Label { x: 8; text: root.title; font.pixelSize: Theme.sizeHeading; font.weight: Font.DemiBold; height: 40 }
                Repeater {
                    model: root.sections
                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool on: root.section === modelData.key
                        width: parent.width; height: 34
                        radius: Theme.radius
                        color: on ? Theme.selected : sm.containsMouse ? Theme.hover : "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.fast } }
                        Icon { x: 10; anchors.verticalCenter: parent.verticalCenter; source: modelData.icon; size: 16; tone: parent.on ? 1 : 0.6 }
                        Label { x: 36; anchors.verticalCenter: parent.verticalCenter; text: modelData.text; color: parent.on ? Theme.text : Theme.textSecondary }
                        MouseArea { id: sm; anchors.fill: parent; hoverEnabled: true; onClicked: root.section = modelData.key }
                    }
                }
            }
        }
        IconButton {
            anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12
            icon: Theme.icon("close")
            tooltip: qsTr("Close (Esc)")
            onClicked: root.closed()
        }
        Flickable {
            id: flick
            anchors.left: side.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.margins: 28
            anchors.topMargin: 56
            contentHeight: body.childrenRect.height + 20
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Item { id: body; width: flick.width }
        }
    }
}
