import QtQuick

// The @ references of a prompt as removable chips with their Blender icons.
Flow {
    id: root
    required property var mention          // a MentionPopup
    spacing: 6
    visible: mention.refs.length > 0
    height: visible ? implicitHeight : 0
    Repeater {
        model: root.mention.refs
        delegate: Rectangle {
            id: chip
            required property var modelData
            height: 26
            width: cr.implicitWidth + 30
            radius: Theme.radius
            color: Theme.agentSoft
            border.color: Qt.rgba(0.62, 0.796, 1.0, 0.32)
            Row {
                id: cr
                x: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                Icon { source: Theme.blenderIcon(chip.modelData.icon || ""); size: 13; tone: 0.95; anchors.verticalCenter: parent.verticalCenter }
                Text { text: chip.modelData.name; color: Theme.text; font.family: Theme.font; font.pixelSize: Theme.sizeSmall; font.weight: Font.Medium
                       renderType: Text.NativeRendering; anchors.verticalCenter: parent.verticalCenter }
            }
            IconButton {
                anchors.right: parent.right; anchors.rightMargin: 2
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: 20; implicitHeight: 20; iconSize: 12
                icon: Theme.icon("close"); tooltip: qsTr("Remove reference")
                onClicked: root.mention.removeRef(chip.modelData.name)
            }
        }
    }
}
