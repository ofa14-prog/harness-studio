import QtQuick
import QtQuick.Controls.Basic as QC

// Panel layout of the current workspace: templates, saving one's own, swapping two panels and
// going back to the factory layout. Blender tiles the panels; borders drag to resize.
IconButton {
    id: root
    icon: Theme.icon("dashboard")
    iconSize: 17
    tooltip: qsTr("Panel layout")
    checked: menu.opened || namer.opened

    onClicked: {
        var items = [ { header: true, text: qsTr("Layout · %1").arg(blender.activeWorkspace) } ]
        var ts = blender.layoutTemplates
        var own = []
        for (var i = 0; i < ts.length; i++) {
            items.push({ text: ts[i].name, icon: Theme.icon(ts[i].builtin ? "dashboard" : "grid_view"),
                         action: (function(id) { return function() { blender.layoutAction("apply_template", id) } })(ts[i].id) })
            if (!ts[i].builtin) own.push(ts[i])
        }
        items.push({ separator: true })
        items.push({ text: qsTr("Swap two panels"), icon: Theme.icon("swap_horiz"),
                     action: function() { blender.runOperator("harness.swap_panels") } })
        items.push({ text: qsTr("Save as template…"), icon: Theme.icon("save"),
                     action: function() { nameField.text = ""; namer.open(); nameField.forceActiveFocus() } })
        if (own.length) {
            items.push({ text: qsTr("Delete a template"), icon: Theme.icon("delete"), submenu: function() {
                var sub = []
                for (var j = 0; j < own.length; j++)
                    sub.push({ text: own[j].name, action: (function(id) { return function() { blender.layoutAction("delete_template", id) } })(own[j].id) })
                return sub
            } })
        }
        items.push({ separator: true })
        items.push({ text: qsTr("Reset this workspace"), icon: Theme.icon("replay"), enabled: blender.layoutCustomized,
                     action: function() { blender.layoutAction("reset") } })
        items.push({ text: qsTr("Reset all workspaces"), icon: Theme.icon("refresh"),
                     action: function() { blender.layoutAction("reset_all") } })
        menu.items = items
        menu.openFor(root, true)
    }

    PopupMenu { id: menu; minWidth: 250 }

    QC.Popup {
        id: namer
        x: root.width - width
        y: root.height + 8
        width: 280
        padding: 12
        focus: true
        modal: true
        dim: false
        QC.Overlay.modal: Item {}
        closePolicy: QC.Popup.CloseOnEscape | QC.Popup.CloseOnPressOutside
        background: Rectangle { color: Theme.overlay; radius: Theme.radiusLarge; border.color: Theme.border }
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.fast } }
        exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.fast } }
        function save() {
            var n = nameField.text.trim()
            if (!n.length) return
            blender.layoutAction("save_template", n)
            close()
        }
        contentItem: Column {
            spacing: 10
            Label { text: qsTr("Template name"); color: Theme.textSecondary; font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi }
            TextInput2 {
                id: nameField
                width: parent.width
                placeholderText: blender.activeWorkspace
                onAccepted: namer.save()
            }
            Row {
                anchors.right: parent.right
                spacing: 6
                Button { text: qsTr("Cancel"); variant: "ghost"; onClicked: namer.close() }
                Button { text: qsTr("Save"); variant: "primary"; enabled: nameField.text.trim().length > 0; onClicked: namer.save() }
            }
        }
    }
}
