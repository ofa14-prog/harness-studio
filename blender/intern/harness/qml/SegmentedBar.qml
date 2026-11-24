import QtQuick

// A row of equal segments with a lit plate that slides to the chosen one. Corners are modest
// (10 outside, 8 inside) — a panel control, not a pill. model: [{key, text, icon}].
Item {
    id: root
    property var model: []
    property string current: ""
    property int barHeight: 38
    signal selected(string key)
    implicitHeight: barHeight
    height: barHeight

    readonly property int index: {
        for (var i = 0; i < model.length; i++) if (model[i].key === current) return i
        return 0
    }
    Rectangle { anchors.fill: parent; radius: 10; color: Qt.rgba(0, 0, 0, 0.30); border.color: Theme.borderSubtle }
    Rectangle {
        y: 3; height: parent.height - 6
        width: (parent.width - 6) / Math.max(1, root.model.length)
        x: 3 + root.index * width
        radius: 8
        color: Theme.overlay
        border.color: Theme.borderStrong
        Behavior on x { NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
        // A thin light along the top edge of the plate.
        Rectangle { x: 8; width: parent.width - 16; height: 1; y: 1; color: Qt.rgba(1, 1, 1, 0.08) }
    }
    Row {
        x: 3; y: 3
        Repeater {
            model: root.model
            delegate: Item {
                required property var modelData
                readonly property bool on: root.current === modelData.key
                width: (root.width - 6) / Math.max(1, root.model.length)
                height: root.height - 6
                Row {
                    anchors.centerIn: parent
                    spacing: 7
                    Icon { visible: !!modelData.icon; source: modelData.icon || ""; size: 16; anchors.verticalCenter: parent.verticalCenter
                           tone: parent.parent.on ? 1 : (sm.containsMouse ? 0.85 : 0.55) }
                    Text { text: modelData.text; anchors.verticalCenter: parent.verticalCenter
                           color: parent.parent.on ? Theme.text : (sm.containsMouse ? Theme.textSecondary : Theme.textTertiary)
                           font.family: Theme.font; font.pixelSize: Theme.sizeBody; font.weight: Theme.weightUi; renderType: Text.NativeRendering
                           Behavior on color { ColorAnimation { duration: Theme.fast } } }
                }
                MouseArea { id: sm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: if (!parent.on) root.selected(modelData.key) }
            }
        }
    }
}
