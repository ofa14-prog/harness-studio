import QtQuick
import QtQuick.Controls.Basic as QC

// Searchable list of the chat models every configured provider offers right now.
QC.Popup {
    id: root
    signal openSettings(string section)
    width: 400
    height: 480
    /** Open above or below `anchor`, whichever has room, inside the window. */
    function openFor(anchor) {
        var ov = QC.Overlay.overlay
        var g = anchor.mapToItem(ov, 0, 0)
        var below = ov.height - (g.y + anchor.height) - 14
        var above = g.y - 14
        var natural = 480
        var down = natural <= below || (natural > above && below >= above)
        height = Math.max(220, Math.min(natural, down ? below : above))
        var wx = Math.max(8, Math.min(g.x, ov.width - width - 8))
        var wy = down ? g.y + anchor.height + 6 : g.y - 6 - height
        var p = parent.mapFromItem(ov, wx, wy)
        x = p.x
        y = p.y
        open()
    }
    padding: 0
    focus: true
    // Modal without dimming: a press outside closes it and goes no further, so its button
    // toggles it and nothing underneath reacts to the closing click.
    modal: true
    dim: false
    QC.Overlay.modal: Item {}
    closePolicy: QC.Popup.CloseOnEscape | QC.Popup.CloseOnPressOutside
    onOpened: { search.text = ""; search.forceActiveFocus() }
    background: Rectangle { color: Theme.overlay; radius: Theme.radiusLarge; border.color: Theme.border }
    Connections {
        target: Qt.application
        function onStateChanged() { if (Qt.application.state !== Qt.ApplicationActive && root.opened) root.close() }
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.fast } }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.fast } }

    readonly property var results: studio.searchModels(search.text)

    Column {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8
        Row {
            width: parent.width
            spacing: 6
            TextInput2 { id: search; width: parent.width - 34; placeholderText: qsTr("Search models") }
            Item {
                width: 28; height: 32
                IconButton {
                    anchors.centerIn: parent
                    icon: Theme.icon("refresh")
                    tooltip: qsTr("Refresh model lists")
                    onClicked: studio.refreshModels()
                    RotationAnimator on rotation { from: 0; to: 360; duration: 800; loops: Animation.Infinite; running: studio.modelsLoading }
                }
            }
        }
        ListView {
            id: list
            width: parent.width
            height: parent.height - 32 - 8 - footer.height - 8
            clip: true
            model: root.results
            boundsBehavior: Flickable.StopAtBounds
            section.property: "providerName"
            section.delegate: Item {
                required property string section
                width: list.width; height: 36
                // The API the models come from: its mark and name.
                readonly property string providerIcon: {
                    var ps = studio.providers
                    for (var i = 0; i < ps.length; i++) if (ps[i].name === section) return ps[i].icon
                    return ""
                }
                Row {
                    x: 8; anchors.bottom: parent.bottom; anchors.bottomMargin: 6
                    spacing: 7
                    Icon { source: Theme.brand(parent.parent.providerIcon); size: 14; tone: 0.9; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: section + qsTr(" API"); color: Theme.textSecondary; anchors.verticalCenter: parent.verticalCenter
                           font.family: Theme.fontSecondary; font.pixelSize: Theme.sizeCaption; font.weight: Theme.weightStrong
                           font.capitalization: Font.AllUppercase; font.letterSpacing: 0.6; renderType: Text.NativeRendering }
                }
            }
            delegate: Item {
                id: row
                required property var modelData
                readonly property bool current: modelData.provider === studio.agentProvider && modelData.id === studio.agentModel
                width: list.width
                height: 50
                Rectangle { anchors.fill: parent; radius: 8; color: row.current ? Theme.selected : rm.containsMouse ? Theme.hover : "transparent"
                            Behavior on color { ColorAnimation { duration: Theme.fast } } }
                // The maker's mark (Anthropic, OpenAI, Google, Meta...), whatever API serves it.
                Icon { x: 10; anchors.verticalCenter: parent.verticalCenter; source: Theme.brand(row.modelData.icon); size: 20; tone: row.modelData.usable ? 0.95 : 0.35 }
                Column {
                    x: 40; width: parent.width - 40 - meta.width - 16
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text { width: parent.width; elide: Text.ElideRight; text: row.modelData.name || row.modelData.id
                           color: row.modelData.usable ? Theme.text : Theme.textTertiary
                           font.family: Theme.font; font.pixelSize: Theme.sizeBody; font.weight: Theme.weightUi; renderType: Text.NativeRendering }
                    Text { width: parent.width; elide: Text.ElideMiddle; text: row.modelData.id + "  ·  " + row.modelData.providerName
                           color: Theme.textTertiary
                           font.family: Theme.fontSecondary; font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi; renderType: Text.NativeRendering }
                }
                Text {
                    id: meta
                    anchors.right: parent.right; anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: !row.modelData.usable ? qsTr("Add key") : row.modelData.context ? Math.round(row.modelData.context / 1000) + "k" : ""
                    color: row.modelData.usable ? Theme.textTertiary : Theme.accent
                    font.family: Theme.font; font.pixelSize: Theme.sizeCaption; font.weight: Theme.weightUi; renderType: Text.NativeRendering
                }
                MouseArea {
                    id: rm
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if (!row.modelData.usable) { root.close(); root.openSettings("providers"); return }
                        studio.setAgentModel(row.modelData.provider, row.modelData.id)
                        root.close()
                    }
                }
            }
            Label {
                visible: list.count === 0
                anchors.centerIn: parent
                width: parent.width - 30
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
                color: Theme.textTertiary
                text: studio.modelsLoading ? qsTr("Loading model lists…") : search.text.length ? qsTr("No model matches") : qsTr("Add a provider key to see its models.")
            }
        }
        Item {
            id: footer
            width: parent.width
            height: 30
            Rectangle { width: parent.width; height: 1; color: Theme.border }
            Button {
                anchors.bottom: parent.bottom
                variant: "ghost"
                icon: Theme.icon("key")
                text: qsTr("Manage providers")
                onClicked: { root.close(); root.openSettings("providers") }
            }
        }
    }
}
