import QtQuick
import QtQuick.Controls.Basic as QC

// The Create model picker: a fixed-size window (at most 440 × 520, never wider than the panel)
// that scrolls inside, with search across names, makers, abilities (image, video, 3D, from
// image / text, edit...) and the API, models grouped by maker. Each row: the maker's mark in
// its own colour, the full model name, and below it the API that serves it (fal.ai, with its
// mark) and what the model does.
QC.Popup {
    id: root
    signal picked(string id)
    property var models: []
    property string current: ""
    property string currentFamily: ""
    readonly property var makerNames: ({ bytedance: "ByteDance", google: "Google", googlegemini: "Google", openai: "OpenAI",
        kuaishou: "Kuaishou · Kling", minimax: "MiniMax · Hailuo", flux: "Black Forest Labs · FLUX", qwen: "Alibaba · Qwen",
        alibabacloud: "Alibaba · Wan", tencenthy: "Tencent · Hunyuan", meta: "Meta", mistralai: "Mistral AI", x: "xAI",
        deepseek: "DeepSeek", nvidia: "NVIDIA", moonshotai: "Moonshot AI", baidu: "Baidu", xiaomi: "Xiaomi", elevenlabs: "ElevenLabs" })
    function makerOf(m) { return makerNames[m.icon] || (m.provider && m.provider !== "fal.ai" ? m.provider : qsTr("Other makers")) }

    // Rows for the list: a header per maker, then its models (filtered by the search).
    readonly property var rows: {
        var q = search.text.trim().toLowerCase()
        var groups = {}, order = []
        for (var i = 0; i < models.length; i++) {
            var m = models[i]
            var maker = makerOf(m)
            var hay = (m.name + " " + m.id + " " + (m.detail || "") + " " + maker + " fal.ai " + studio.createKind).toLowerCase()
            if (q.length && q.split(/\s+/).some(function(w) { return hay.indexOf(w) < 0 })) continue
            if (!groups[maker]) { groups[maker] = []; order.push(maker) }
            groups[maker].push(m)
        }
        var out = []
        for (var j = 0; j < order.length; j++) {
            out.push({ header: true, text: order[j], icon: groups[order[j]][0].icon })
            for (var k = 0; k < groups[order[j]].length; k++) out.push(groups[order[j]][k])
        }
        return out
    }

    padding: 8
    modal: true
    dim: false
    focus: true
    QC.Overlay.modal: Item {}
    closePolicy: QC.Popup.CloseOnEscape | QC.Popup.CloseOnPressOutside
    background: Rectangle { color: Theme.overlay; radius: 14; border.color: Theme.border }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.fast } }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.fast } }
    onOpened: { search.text = ""; search.forceActiveFocus(); Qt.callLater(function() { list.positionViewAtIndex(Math.max(0, root.currentRow()), ListView.Center) }) }
    Connections {
        target: Qt.application
        function onStateChanged() { if (Qt.application.state !== Qt.ApplicationActive && root.opened) root.close() }
    }
    function currentRow() {
        for (var i = 0; i < rows.length; i++) if (!rows[i].header && (rows[i].id === current || rows[i].family === currentFamily)) return i
        return 0
    }

    /** Open above or below `anchor`, within the panel `bounds` (its width) and the window. */
    function openFor(anchor, bounds) {
        var ov = QC.Overlay.overlay
        var g = anchor.mapToItem(ov, 0, 0)
        var b = bounds ? bounds.mapToItem(ov, 0, 0) : Qt.point(8, 0)
        width = Math.min(440, bounds ? bounds.width : 440)
        var below = ov.height - (g.y + anchor.height) - 14
        var above = g.y - 14
        var natural = 520
        var down = natural <= below || (natural > above && below >= above)
        height = Math.max(240, Math.min(natural, down ? below : above))
        var wx = Math.max(b.x, Math.min(g.x, b.x + (bounds ? bounds.width : ov.width) - width))
        var wy = down ? g.y + anchor.height + 8 : g.y - 8 - height
        var p = parent.mapFromItem(ov, wx, wy)
        x = p.x
        y = p.y
        open()
    }

    contentItem: Column {
        spacing: 8
        TextInput2 {
            id: search
            width: parent.width
            placeholderText: qsTr("Search models, makers or abilities (e.g. \"from image\", \"4k\")")
            Keys.onDownPressed: list.forceActiveFocus()
        }
        ListView {
            id: list
            width: parent.width
            height: root.height - root.topPadding - root.bottomPadding - search.height - 8
            clip: true
            model: root.rows
            boundsBehavior: Flickable.StopAtBounds
            QC.ScrollIndicator.vertical: QC.ScrollIndicator { }
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool header: modelData.header === true
                readonly property bool chosen: !header && (modelData.id === root.current || modelData.family === root.currentFamily)
                width: list.width
                height: header ? 34 : 52
                // Maker heading with its mark.
                Row {
                    visible: row.header
                    x: 8; anchors.bottom: parent.bottom; anchors.bottomMargin: 6
                    spacing: 7
                    Icon { source: Theme.brand(row.modelData.icon || ""); size: 14; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: row.modelData.text || ""; color: Theme.textSecondary; anchors.verticalCenter: parent.verticalCenter; renderType: Text.NativeRendering
                           font.family: Theme.fontSecondary; font.pixelSize: Theme.sizeCaption; font.weight: Theme.weightStrong
                           font.capitalization: Font.AllUppercase; font.letterSpacing: 0.6 }
                }
                Rectangle {
                    visible: !row.header
                    anchors.fill: parent
                    radius: 9
                    color: row.chosen ? Theme.selected : rm.containsMouse ? Theme.hover : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.fast } }
                }
                Icon { visible: !row.header; x: 12; anchors.verticalCenter: parent.verticalCenter; source: Theme.brand(row.modelData.icon || ""); size: 22 }
                Column {
                    visible: !row.header
                    x: 46; width: parent.width - 46 - 34
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3
                    Text { width: parent.width; elide: Text.ElideRight; text: row.modelData.name || ""; color: Theme.text; renderType: Text.NativeRendering
                           font.family: Theme.font; font.pixelSize: Theme.sizeBody; font.weight: Theme.weightUi }
                    Row {
                        spacing: 6
                        Image { source: Theme.brand("fal"); width: 13; height: 13; sourceSize: Qt.size(Math.round(13 * Theme.dpr), Math.round(13 * Theme.dpr)); smooth: true; anchors.verticalCenter: parent.verticalCenter }
                        Text { width: Math.min(implicitWidth, list.width - 120); elide: Text.ElideRight; anchors.verticalCenter: parent.verticalCenter
                               text: "fal.ai" + (row.modelData.detail ? "  ·  " + row.modelData.detail : "") + (row.modelData.curated ? "" : "  ·  " + qsTr("live list"))
                               color: Theme.textTertiary; renderType: Text.NativeRendering
                               font.family: Theme.fontSecondary; font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi }
                    }
                }
                Icon { visible: row.chosen; anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter; source: Theme.icon("check"); size: 16 }
                MouseArea {
                    id: rm
                    anchors.fill: parent
                    enabled: !row.header
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { root.picked(row.modelData.id); root.close() }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: list.count === 0
                text: qsTr("No model matches “%1”").arg(search.text)
                color: Theme.textTertiary; renderType: Text.NativeRendering
                font.family: Theme.font; font.pixelSize: Theme.sizeBody
            }
        }
    }
}
