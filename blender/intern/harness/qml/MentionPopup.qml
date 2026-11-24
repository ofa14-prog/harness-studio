import QtQuick
import QtQuick.Controls.Basic as QC

// @ references: typing @ in `editor` lists the scene's objects, cameras, lights, materials and
// collections (live from Blender); the chosen one is written as @Name and kept in `refs`, which
// goes with the prompt as live context and as the target of scene actions.
QC.Popup {
    id: root
    required property Item editor          // a TextArea / TextEdit
    property var refs: []                  // [{kind, name, icon}]
    property var items: []
    property string query: ""
    property int start: -1
    property int current: 0
    property int pending: -1
    signal refsEdited()

    padding: 6
    modal: false
    focus: false
    closePolicy: QC.Popup.CloseOnEscape | QC.Popup.CloseOnPressOutsideParent
    background: Rectangle { color: Theme.overlay; radius: Theme.radiusLarge; border.color: Theme.border }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.fast } }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.fast } }

    readonly property var kinds: ({ object: qsTr("Object"), camera: qsTr("Camera"), light: qsTr("Light"),
                                    material: qsTr("Material"), collection: qsTr("Collection") })
    readonly property var filtered: {
        var q = query.toLowerCase()
        var out = []
        for (var i = 0; i < items.length && out.length < 60; i++) {
            var it = items[i]
            if (!q.length || it.name.toLowerCase().indexOf(q) >= 0 || it.kind.indexOf(q) === 0) out.push(it)
        }
        return out
    }
    onFilteredChanged: current = Math.min(current, Math.max(0, filtered.length - 1))

    /** The references still written in `text`. */
    function activeRefs(text) {
        var out = []
        for (var i = 0; i < refs.length; i++) if (text.indexOf("@" + refs[i].name) >= 0) out.push({ kind: refs[i].kind, name: refs[i].name })
        return out
    }
    /** `text` with each @Name written as plain Name (for image and video models). */
    function plainText(text) {
        var t = text
        for (var i = 0; i < refs.length; i++) t = t.split("@" + refs[i].name).join(refs[i].name)
        return t
    }
    function removeRef(name) {
        var out = []
        for (var i = 0; i < refs.length; i++) if (refs[i].name !== name) out.push(refs[i])
        refs = out
        editor.text = editor.text.split("@" + name + " ").join("").split("@" + name).join("")
        refsEdited()
    }
    function clear() { refs = []; refsEdited() }

    // Follow the caret: an @ word right before it opens the list.
    function update() {
        var before = editor.text.substring(0, editor.cursorPosition)
        var m = before.match(/(^|\s)@([^\s@]*)$/)
        if (!m) { if (opened) close(); return }
        start = editor.cursorPosition - m[2].length - 1
        query = m[2]
        if (!opened) {
            current = 0
            pending = blender.query("scene_items", {})
            place()
            open()
        }
    }
    function place() {
        var ov = QC.Overlay.overlay
        var g = editor.mapToItem(ov, 0, 0)
        var h = Math.min(340, Math.max(60, list.contentHeight) + 46)
        var below = ov.height - (g.y + editor.height) - 12
        var down = below >= h
        var wx = Math.max(8, Math.min(g.x - 6, ov.width - width - 8))
        var wy = down ? g.y + editor.height + 6 : g.y - h - 8
        var p = parent.mapFromItem(ov, wx, wy)
        x = p.x
        y = p.y
    }
    function pick(it) {
        if (!it) return
        var before = editor.text.substring(0, start)
        var after = editor.text.substring(editor.cursorPosition)
        var token = "@" + it.name + " "
        editor.text = before + token + after
        editor.cursorPosition = before.length + token.length
        var have = false
        for (var i = 0; i < refs.length; i++) if (refs[i].name === it.name && refs[i].kind === it.kind) have = true
        if (!have) { var r = refs.slice(); r.push({ kind: it.kind, name: it.name, icon: it.icon }); refs = r }
        close()
        refsEdited()
        editor.forceActiveFocus()
    }
    /** Keys while the list is open; returns true when it used the key. */
    function handleKey(e) {
        if (!opened) return false
        if (e.key === Qt.Key_Down) { current = Math.min(filtered.length - 1, current + 1); list.positionViewAtIndex(current, ListView.Contain); return true }
        if (e.key === Qt.Key_Up) { current = Math.max(0, current - 1); list.positionViewAtIndex(current, ListView.Contain); return true }
        if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Tab) { pick(filtered[current]); return true }
        if (e.key === Qt.Key_Escape) { close(); return true }
        return false
    }

    Connections {
        target: blender
        function onReplied(id, result) {
            if (id !== root.pending) return
            root.items = result.items || []
            root.place()
        }
    }
    Connections {
        target: root.editor
        function onTextChanged() { root.update() }
        function onCursorPositionChanged() { root.update() }
    }

    width: 340
    contentItem: Column {
        spacing: 4
        Text {
            x: 8
            height: 26
            verticalAlignment: Text.AlignVCenter
            text: root.items.length ? qsTr("Reference from the scene") : qsTr("Reading the scene…")
            color: Theme.textTertiary
            font.family: Theme.fontSecondary
            font.pixelSize: Theme.sizeCaption
            font.weight: Theme.weightUi
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0.6
            renderType: Text.NativeRendering
        }
        ListView {
            id: list
            width: parent.width
            height: Math.min(300, contentHeight)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.filtered
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                width: list.width
                height: 40
                radius: Theme.radius
                color: index === root.current ? Theme.selected : rm.containsMouse ? Theme.hover : "transparent"
                Icon {
                    x: 10; anchors.verticalCenter: parent.verticalCenter
                    source: Theme.blenderIcon(row.modelData.icon); size: 16; tone: 0.9
                }
                Column {
                    x: 36; anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 46
                    spacing: 1
                    Text { width: parent.width; text: row.modelData.name; color: Theme.text; elide: Text.ElideRight
                           font.family: Theme.font; font.pixelSize: Theme.sizeBody; font.weight: Font.Medium; renderType: Text.NativeRendering }
                    Text { width: parent.width; text: row.modelData.detail || root.kinds[row.modelData.kind]; color: Theme.textTertiary; elide: Text.ElideRight
                           font.family: Theme.fontSecondary; font.pixelSize: Theme.sizeCaption; font.weight: Theme.weightUi; renderType: Text.NativeRendering }
                }
                MouseArea { id: rm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onEntered: root.current = row.index; onClicked: root.pick(row.modelData) }
            }
        }
        Text {
            visible: root.items.length > 0 && root.filtered.length === 0
            x: 8; height: 30
            verticalAlignment: Text.AlignVCenter
            text: qsTr("Nothing named “%1”").arg(root.query)
            color: Theme.textTertiary
            font.family: Theme.font; font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi
            renderType: Text.NativeRendering
        }
    }
}
