import QtQuick

// A top-level menu whose content is read from Blender's own menu class, so every entry
// (including those added by add-ons) is there and runs Blender's operator.
Item {
    id: root
    property string text
    property string menuName
    property int pending: -1
    implicitWidth: lbl.implicitWidth + 18
    implicitHeight: 28

    function convert(list) {
        var out = []
        for (var i = 0; i < list.length; i++) {
            var it = list[i]
            if (it.type === "separator") { out.push({ separator: true }); continue }
            if (it.type === "label") { out.push({ header: true, text: it.text }); continue }
            var icon = Theme.blenderIcon(it.icon)
            if (it.type === "operator") {
                out.push({ text: it.text, icon: icon, enabled: it.enabled,
                           action: (function(op, props, ctx) { return function() { blender.runOperator(op, props, ctx) } })(it.op, it.props, it.context) })
            } else if (it.type === "menu") {
                out.push({ text: it.text, icon: icon, enabled: it.enabled,
                           submenu: (function(children, name) { return function() { return children ? root.convert(children) : [] } })(it.children, it.menu) })
            } else if (it.type === "toggle") {
                out.push({ text: it.text, checked: it.value,
                           action: (function(path, prop) { return function() { blender.toggleProperty(path, prop) } })(it.path, it.prop) })
            }
        }
        return out
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: menu.opened ? Theme.selected : ma.containsMouse ? Theme.hover : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
    Label { id: lbl; anchors.centerIn: parent; text: root.text; color: menu.opened || ma.containsMouse ? Theme.text : Theme.textSecondary }
    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        onClicked: {
            if (menu.opened) { menu.close(); return }
            root.pending = blender.query("menu_tree", { menu: root.menuName, depth: 3 })
            menu.items = [ { header: true, text: qsTr("Loading…") } ]
            menu.openFor(root, false)
        }
    }
    Connections {
        target: blender
        function onReplied(id, result) {
            if (id !== root.pending) return
            root.pending = -1
            menu.items = result.items ? root.convert(result.items) : [ { header: true, text: result.error || qsTr("Unavailable") } ]
        }
    }
    PopupMenu { id: menu; minWidth: 250 }
}
