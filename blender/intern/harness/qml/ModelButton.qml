import QtQuick

// Shows the agent's model with its provider mark; opens the model picker.
Item {
    id: root
    signal openSettings(string section)
    implicitWidth: row.implicitWidth + 16
    implicitHeight: 28
    Rectangle { anchors.fill: parent; radius: Theme.radius; color: picker.opened ? Theme.selected : ma.containsMouse ? Theme.hover : "transparent" }
    Row {
        id: row
        x: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
        Icon { source: Theme.brand(studio.modelIcon(studio.agentModel, studio.agentProvider)); size: 13; tone: 0.85; anchors.verticalCenter: parent.verticalCenter }
        Label {
            text: studio.agentModelLabel.length ? studio.agentModelLabel : qsTr("Choose a model")
            color: Theme.textSecondary
            font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi
            width: Math.min(implicitWidth, 190)
            anchors.verticalCenter: parent.verticalCenter
        }
        Icon { source: Theme.icon("expand_more"); size: 15; tone: 0.5; anchors.verticalCenter: parent.verticalCenter }
    }
    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; onClicked: picker.opened ? picker.close() : picker.openFor(root) }
    ModelPicker {
        id: picker
        onOpenSettings: (s) => root.openSettings(s)
    }
}
