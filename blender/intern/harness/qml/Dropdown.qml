import QtQuick

// Combo button: shows the current choice, opens a menu, closes on selection or outside click.
Item {
    id: root
    property var options: []          // strings or {value, text}
    property var value
    property string placeholder: ""
    signal picked(var value)
    implicitHeight: 34
    implicitWidth: 180
    function labelOf(v) {
        for (var i = 0; i < options.length; i++) {
            var o = options[i]
            if (typeof o === "object" && o.value === v) return o.text
            if (o === v) return String(o)
        }
        return v === undefined || v === null || v === "" ? placeholder : String(v)
    }
    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: ma.containsMouse || menu.opened ? Qt.lighter(Theme.raised, 1.15) : Theme.raised
        border.color: menu.opened ? Theme.borderStrong : Theme.border
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
    Label {
        x: 12; width: parent.width - 42
        anchors.verticalCenter: parent.verticalCenter
        text: root.labelOf(root.value)
        color: (root.value === undefined || root.value === "") ? Theme.textTertiary : Theme.text
    }
    Icon {
        anchors.right: parent.right; anchors.rightMargin: 7
        anchors.verticalCenter: parent.verticalCenter
        source: Theme.icon("expand_more"); size: 18; tone: 0.6
        rotation: menu.opened ? 180 : 0
        Behavior on rotation { NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        onClicked: {
            var list = []
            for (var i = 0; i < root.options.length; i++) {
                var o = root.options[i]
                var v = typeof o === "object" ? o.value : o
                var t = typeof o === "object" ? o.text : String(o)
                list.push({ text: t, checked: v === root.value, action: (function(val) { return function() { root.picked(val) } })(v) })
            }
            menu.items = list
            menu.minWidth = root.width
            menu.openFor(root, false)
        }
    }
    PopupMenu { id: menu }
}
