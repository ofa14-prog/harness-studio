import QtQuick

// "Label + description  ........  control" row.
Item {
    id: root
    property string text
    property string description: ""
    default property alias control: holder.data
    width: parent ? parent.width : 400
    implicitHeight: Math.max(36, col.implicitHeight + 12)
    Column {
        id: col
        width: parent.width * 0.5
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Label { text: root.text; width: parent.width }
        Label { visible: root.description.length > 0; text: root.description; width: parent.width; color: Theme.textTertiary; font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi; wrapMode: Text.WordWrap; elide: Text.ElideNone }
    }
    Item {
        id: holder
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width * 0.45
        height: childrenRect.height
    }
}
