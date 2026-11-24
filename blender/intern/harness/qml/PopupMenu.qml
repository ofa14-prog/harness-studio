import QtQuick
import QtQuick.Controls.Basic as QC

// Menu popup. Items: [{text, subtitle, icon (url), shortcut, action (fn), enabled, separator,
// checked, submenu: fn returning items, header}].
//
// Placement: the menu's size is computed from its items (not read back from the layout, which
// is only settled a frame later), then it opens below its anchor when it fits there, otherwise
// above; it never crosses a window edge and scrolls when even the larger side is too small.
//
// Closing: a top-level menu is modal (without dimming), so a press anywhere outside closes it
// and is not passed on: pressing the button that opened it closes it instead of opening it
// again, and a press on Blender's viewport doesn't also select something there. Escape, picking
// an item and the window losing focus close it too.
QC.Popup {
    id: menu
    property var items: []
    property int minWidth: 220
    property var parentMenu: null
    readonly property int edge: 8
    readonly property int rowHeight: 34
    readonly property int subtitleRowHeight: 50
    property real maxListHeight: 560
    property Item _anchor: null
    property bool _alignRight: false

    padding: 6
    modal: parentMenu === null
    dim: false
    focus: true
    closePolicy: QC.Popup.CloseOnEscape | QC.Popup.CloseOnPressOutside
    background: Rectangle {
        color: Theme.overlay
        radius: 12
        border.color: Theme.border
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.fast } }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.fast } }
    // Modal popups normally grey the window; menus shouldn't.
    QC.Overlay.modal: Item {}

    onClosed: {
        _anchor = null
        if (subLoader.item && subLoader.item.opened) subLoader.item.close()
    }
    Connections {
        target: Qt.application
        function onStateChanged() { if (Qt.application.state !== Qt.ApplicationActive && menu.opened) menu.close() }
    }

    function closeAll() { close(); if (parentMenu) parentMenu.closeAll() }

    // ---- Size from the items -----------------------------------------------------------
    TextMetrics { id: tm; font.family: Theme.font; font.pixelSize: Theme.sizeBody; font.weight: Theme.weightUi }
    function _rowH(it) { return it.separator ? 11 : it.header ? 30 : it.subtitle ? subtitleRowHeight : rowHeight }
    function naturalHeight() {
        var h = 0
        for (var i = 0; i < items.length; i++) h += _rowH(items[i])
        return h + topPadding + bottomPadding
    }
    function naturalWidth() {
        var w = 0
        for (var i = 0; i < items.length; i++) {
            var it = items[i]
            if (it.separator) continue
            tm.text = it.text || ""
            var line = tm.advanceWidth
            if (it.subtitle) { tm.text = it.subtitle; line = Math.max(line, tm.advanceWidth * 0.9) }
            if (it.shortcut) { tm.text = it.shortcut; line += tm.advanceWidth + 24 }
            w = Math.max(w, line + 64)
        }
        return Math.min(560, Math.max(minWidth, w)) + leftPadding + rightPadding
    }

    function _overlay() { return QC.Overlay.overlay }
    function _place(wx, wyTop, anchorH, gap) {
        var ov = _overlay()
        if (!ov) return
        var w = naturalWidth()
        width = w
        var natural = naturalHeight()
        var below = ov.height - (wyTop + anchorH) - gap - edge
        var above = wyTop - gap - edge
        var down = natural <= below || (natural > above && below >= above)
        var room = down ? below : above
        maxListHeight = Math.max(90, room - topPadding - bottomPadding)
        var h = Math.min(natural, room)
        height = h
        var wy = down ? wyTop + anchorH + gap : wyTop - gap - h
        wx = Math.max(edge, Math.min(wx, ov.width - w - edge))
        var p = parent.mapFromItem(ov, wx, wy)
        x = p.x
        y = p.y
    }
    /** Open below `anchor` when it fits there, else above it. */
    function openFor(anchor, alignRight) {
        _anchor = anchor
        _alignRight = !!alignRight
        var g = anchor.mapToItem(_overlay(), 0, 0)
        _place(alignRight ? g.x + anchor.width - naturalWidth() : g.x, g.y, anchor.height, 6)
        open()
    }
    /** Open, or close when already open (for buttons that toggle their menu). */
    function toggleFor(anchor, alignRight) {
        if (opened) { close(); return }
        openFor(anchor, alignRight)
    }
    /** Open at a point (a right click) inside `item`. */
    function openAt(item, px, py) {
        _anchor = null
        var g = item.mapToItem(_overlay(), px, py)
        _place(g.x, g.y, 0, 2)
        open()
    }
    /** A submenu beside `row`: right of it, or left when the window ends there. */
    function openBeside(row) {
        var ov = _overlay()
        var g = row.mapToItem(ov, 0, 0)
        var w = naturalWidth()
        width = w
        var natural = naturalHeight()
        maxListHeight = ov.height - 2 * edge - topPadding - bottomPadding
        var h = Math.min(natural, ov.height - 2 * edge)
        height = h
        var wx = g.x + row.width + 4
        if (wx + w > ov.width - edge) wx = g.x - w - 4
        var wy = Math.max(edge, Math.min(g.y - topPadding, ov.height - h - edge))
        var p = parent.mapFromItem(ov, wx, wy)
        x = p.x
        y = p.y
        open()
    }
    // Items that arrive after opening (menus recorded from Blender) re-measure the room.
    onItemsChanged: if (opened && _anchor) Qt.callLater(function() {
        if (!_anchor) return
        var g = _anchor.mapToItem(_overlay(), 0, 0)
        _place(_alignRight ? g.x + _anchor.width - naturalWidth() : g.x, g.y, _anchor.height, 6)
    })

    contentItem: Flickable {
        implicitWidth: menu.width - menu.leftPadding - menu.rightPadding
        implicitHeight: Math.min(col.height, menu.maxListHeight)
        contentHeight: col.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        QC.ScrollIndicator.vertical: QC.ScrollIndicator { }
        Column {
            id: col
            width: parent.width
            Repeater {
                model: menu.items
                delegate: Item {
                    id: row
                    required property var modelData
                    required property int index
                    readonly property bool sep: modelData.separator === true
                    readonly property bool header: modelData.header === true
                    readonly property bool on: modelData.enabled === undefined || modelData.enabled
                    readonly property bool two: !!modelData.subtitle
                    width: col.width
                    height: menu._rowH(modelData)
                    Rectangle { visible: row.sep; anchors.centerIn: parent; width: parent.width - 12; height: 1; color: Theme.border }
                    Rectangle {
                        visible: !row.sep && !row.header
                        anchors.fill: parent
                        radius: 8
                        color: row.modelData.checked === true ? Theme.selected : hover.containsMouse && row.on ? Theme.hover : "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.fast } }
                    }
                    Icon {
                        visible: !row.sep && !row.header && (row.modelData.icon || "").toString().length > 0
                        source: row.modelData.icon || ""
                        size: row.two ? 20 : 16
                        tone: row.on ? 0.95 : 0.35
                        x: row.two ? 10 : 11; anchors.verticalCenter: parent.verticalCenter
                    }
                    Column {
                        visible: !row.sep
                        x: row.header ? 12 : 40
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - x - sc.width - 30
                        spacing: 2
                        Text {
                            width: parent.width
                            elide: Text.ElideRight
                            text: row.modelData.text || ""
                            color: row.header ? Theme.textTertiary : row.on ? Theme.text : Theme.textDisabled
                            font.family: row.header ? Theme.fontSecondary : Theme.font
                            font.pixelSize: row.header ? Theme.sizeCaption : Theme.sizeBody
                            font.weight: Theme.weightUi
                            font.capitalization: row.header ? Font.AllUppercase : Font.MixedCase
                            font.letterSpacing: row.header ? 0.6 : 0.1
                            renderType: Text.NativeRendering
                        }
                        Text {
                            visible: row.two
                            width: parent.width
                            elide: Text.ElideRight
                            text: row.modelData.subtitle || ""
                            color: Theme.textTertiary
                            font.family: Theme.fontSecondary
                            font.pixelSize: Theme.sizeSmall
                            font.weight: Theme.weightUi
                            renderType: Text.NativeRendering
                        }
                    }
                    Icon {
                        visible: row.modelData.checked === true
                        source: Theme.icon("check")
                        size: 15
                        anchors.right: parent.right; anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        id: sc
                        visible: !row.sep && !row.header && text.length > 0
                        anchors.right: parent.right
                        anchors.rightMargin: row.modelData.checked === true ? 32 : 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData.submenu ? "" : (row.modelData.shortcut || "")
                        color: Theme.textTertiary
                        font.family: Theme.font; font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi
                        renderType: Text.NativeRendering
                    }
                    Icon {
                        visible: !!row.modelData.submenu
                        source: Theme.icon("chevron_right")
                        size: 16; tone: 0.5
                        anchors.right: parent.right; anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    MouseArea {
                        id: hover
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: !row.sep && !row.header
                        cursorShape: row.on ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onContainsMouseChanged: {
                            if (containsMouse && row.modelData.submenu && row.on) {
                                subLoader.active = true
                                var s = subLoader.item
                                s.parentMenu = menu
                                s.items = row.modelData.submenu()
                                s.openBeside(row)
                            }
                            else if (containsMouse && subLoader.item && subLoader.item.opened) {
                                subLoader.item.close()
                            }
                        }
                        onClicked: {
                            if (!row.on || row.modelData.submenu) return
                            var act = row.modelData.action
                            menu.closeAll()
                            if (act) act()
                        }
                    }
                }
            }
        }
    }
    Loader { id: subLoader; active: false; source: "PopupMenu.qml" }
}
