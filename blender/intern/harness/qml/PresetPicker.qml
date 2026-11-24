import QtQuick
import QtQuick.Controls.Basic as QC

// The preset picker of the composer (camera moves, effects, apps, product, 3D): category tabs
// over a grid of animated storyboard tiles. Opens above or below its chip, whichever has room.
QC.Popup {
    id: root
    required property var templates          // TemplateData
    property string kind: "image"
    property var chosen: null
    property string category: templates.categories[kind][0].k
    signal picked(var template)
    padding: 12
    focus: true
    // Modal without dimming: a press outside closes it and goes no further, so its button
    // toggles it and nothing underneath reacts to the closing click.
    // Doesn't lock the app: the viewport and panels stay usable; a press outside closes it.
    modal: false
    closePolicy: QC.Popup.CloseOnEscape | QC.Popup.CloseOnPressOutside
    background: Rectangle { color: Theme.overlay; radius: 14; border.color: Theme.border }
    Connections {
        target: Qt.application
        function onStateChanged() { if (Qt.application.state !== Qt.ApplicationActive && root.opened) root.close() }
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.fast } }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.fast } }
    onKindChanged: category = templates.categories[kind][0].k

    function openFor(anchor, w) {
        width = w
        var ov = QC.Overlay.overlay
        var g = anchor.mapToItem(ov, 0, 0)
        var below = ov.height - (g.y + anchor.height) - 14
        var above = g.y - 14
        var down = below >= 460 || (above < 460 && below >= above)
        height = Math.max(260, Math.min(520, down ? below : above))
        var wx = Math.max(8, Math.min(g.x, ov.width - width - 8))
        var wy = down ? g.y + anchor.height + 8 : g.y - 8 - height
        var p = parent.mapFromItem(ov, wx, wy)
        x = p.x
        y = p.y
        open()
    }

    contentItem: Column {
        spacing: 12
        Item {
            width: parent.width
            height: 30
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Presets")
                color: Theme.text
                font.family: Theme.font; font.pixelSize: Theme.sizeTitle; font.weight: Theme.weightStrong
                renderType: Text.NativeRendering
            }
            Chip {
                id: refresh
                anchors.left: parent.left; anchors.leftMargin: 84
                anchors.verticalCenter: parent.verticalCenter
                visible: studio.hasKey("fal")
                iconSource: Theme.icon("refresh")
                text: qsTr("New previews")
                tooltip: qsTr("Make a picture for each preset in this tab with fal.ai's fastest model (about $0.003 each)")
                onClicked: {
                    var list = root.templates.forCategory(root.category), out = []
                    for (var i = 0; i < list.length; i++) out.push({ id: list[i].id, prompt: list[i].prompt, cat: list[i].cat })
                    studio.generatePresetPreviews(out)
                }
            }
            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                Repeater {
                    model: root.templates.categories[root.kind]
                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool on: root.category === modelData.k
                        width: ct.implicitWidth + 20; height: 28
                        radius: 8
                        color: on ? Theme.selected : cm.containsMouse ? Theme.hover : "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.fast } }
                        Text { id: ct; anchors.centerIn: parent; text: modelData.t; renderType: Text.NativeRendering
                               color: parent.on ? Theme.text : Theme.textSecondary
                               font.family: Theme.font; font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi }
                        MouseArea { id: cm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.category = modelData.k }
                    }
                }
            }
        }
        Flickable {
            width: parent.width
            height: root.height - root.topPadding - root.bottomPadding - 42
            contentHeight: grid.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Grid {
                id: grid
                width: parent.width
                columns: width > 520 ? 4 : 3
                columnSpacing: 10
                rowSpacing: 12
                Repeater {
                    model: root.templates.forCategory(root.category)
                    delegate: TemplateTile {
                        required property var modelData
                        template: modelData
                        width: (grid.width - (grid.columns - 1) * grid.columnSpacing) / grid.columns
                        chosen: root.chosen !== null && root.chosen.id === modelData.id
                        onPicked: { root.picked(chosen ? null : modelData); root.close() }
                    }
                }
            }
        }
    }
}
