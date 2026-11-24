import QtQuick

// Underline tabs (no pills). model: [{key, text, icon}]
Item {
    id: root
    property var model: []
    property string current: ""
    signal selected(string key)
    implicitHeight: 42
    Row {
        id: row
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        height: parent.height
        spacing: 24
        Repeater {
            model: root.model
            delegate: Item {
                id: tab
                required property var modelData
                readonly property bool active: root.current === modelData.key
                width: content.implicitWidth
                height: row.height
                Row {
                    id: content
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    Icon { visible: !!tab.modelData.icon; source: tab.modelData.icon || ""; size: 15; tone: tab.active ? 1 : 0.55; anchors.verticalCenter: parent.verticalCenter }
                    Label {
                        text: tab.modelData.text
                        color: tab.active ? Theme.text : (ma.containsMouse ? Theme.textSecondary : Theme.textTertiary)
                        font.weight: Font.Medium
                        Behavior on color { ColorAnimation { duration: Theme.fast } }
                    }
                }
                MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.selected(tab.modelData.key) }
            }
        }
    }
    // The indicator slides between tabs.
    Rectangle {
        id: bar
        height: 2
        radius: 1
        color: Theme.text
        anchors.bottom: parent.bottom
        property Item target: {
            for (var i = 0; i < row.children.length; i++) {
                var c = row.children[i]
                if (c.modelData && c.modelData.key === root.current) return c
            }
            return null
        }
        x: target ? target.x : 0
        width: target ? target.width : 0
        Behavior on x { NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
        Behavior on width { NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
    }
}
