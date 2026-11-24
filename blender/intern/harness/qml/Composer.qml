import QtQuick
import QtQuick.Controls.Basic as QC
import QtQuick.Dialogs

// Message box: grows with the text, Enter sends (Shift+Enter for a new line), @ references
// scene items (their live data goes with the message), the viewport can be attached, the
// model is chosen below, Stop interrupts the agent.
Rectangle {
    id: root
    signal openSettings(string section)
    property bool attachViewport: profiles.settings.agent ? profiles.settings.agent.attach_viewport : true
    function setText(t) { input.text = t; input.forceActiveFocus(); input.cursorPosition = input.length }
    property var attachments: []          // image URLs the person added
    function addImages(list) {
        var a = attachments.slice()
        for (var i = 0; i < list.length; i++) {
            var u = String(list[i])
            if (shell.isImage(u) && a.indexOf(u) < 0) a.push(u)
        }
        attachments = a.slice(0, 8)
    }
    function submit() {
        if (studio.busy || (input.text.trim().length === 0 && attachments.length === 0)) return
        studio.send(input.text, root.attachViewport, mention.activeRefs(input.text), attachments)
        input.text = ""
        attachments = []
        mention.clear()
    }
    implicitHeight: col.implicitHeight + 24
    MentionPopup { id: mention; editor: input }
    FileDialog {
        id: imageDialog
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff *.gif)")]
        onAccepted: root.addImages(selectedFiles)
    }
    // Drop images onto the box.
    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onEntered: (d) => d.accept(Qt.CopyAction)
        onDropped: (d) => { root.addImages(d.urls); d.acceptProposedAction() }
        Rectangle {
            anchors.fill: parent
            radius: root.radius
            visible: parent.containsDrag
            color: Theme.accentSoft
            border.color: Theme.accent
        }
    }
    radius: 12
    color: Theme.raised
    border.color: input.activeFocus ? Theme.borderStrong : Theme.border
    Behavior on border.color { ColorAnimation { duration: Theme.normal } }
    // Soft halo while typing: the box lifts instead of drawing a focus ring.
    Rectangle {
        anchors.fill: parent
        anchors.margins: -4
        radius: parent.radius + 4
        color: "transparent"
        border.width: 4
        border.color: Qt.rgba(1, 1, 1, input.activeFocus ? 0.045 : 0)
        Behavior on border.color { ColorAnimation { duration: Theme.normal } }
    }

    Column {
        id: col
        x: 14; y: 12
        width: parent.width - 28
        spacing: 10
        AttachmentStrip {
            width: parent.width
            urls: root.attachments
            onRemoved: (i) => { var a = root.attachments.slice(); a.splice(i, 1); root.attachments = a }
        }
        RefChips { width: parent.width; mention: mention }
        Flickable {
            width: parent.width
            height: Math.min(180, Math.max(64, input.implicitHeight))
            contentHeight: input.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            QC.TextArea {
                id: input
                width: parent.width
                padding: 2
                wrapMode: TextEdit.Wrap
                placeholderText: qsTr("Ask the agent — type @ to point at scene items")
                placeholderTextColor: Theme.textTertiary
                color: Theme.text
                selectionColor: Theme.accentSoft
                font.family: Theme.font
                font.pixelSize: 14
                renderType: Text.NativeRendering
                background: null
                Keys.onPressed: (e) => {
                    if (mention.handleKey(e)) { e.accepted = true; return }
                    if (e.matches(StandardKey.Paste)) {
                        var img = shell.clipboardImage()
                        if (img.length) { root.addImages([img]); e.accepted = true; return }
                    }
                    if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter) && !(e.modifiers & Qt.ShiftModifier)) { root.submit(); e.accepted = true }
                    else if (e.key === Qt.Key_Escape) { input.focus = false; e.accepted = true }
                }
            }
        }
        Item {
            width: parent.width
            height: 34
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                IconButton {
                    icon: Theme.icon("attach_file")
                    tooltip: qsTr("Add images (or paste / drop them here)")
                    onClicked: imageDialog.open()
                }
                IconButton {
                    icon: Theme.icon("center_focus_strong")
                    checked: root.attachViewport
                    tooltip: root.attachViewport ? qsTr("The viewport is attached to your message") : qsTr("Attach the viewport to your message")
                    onClicked: { root.attachViewport = !root.attachViewport; profiles.set("agent.attach_viewport", root.attachViewport) }
                }
                ModelButton { onOpenSettings: (s) => root.openSettings(s) }
            }
            Rectangle {
                id: send
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 34; height: 34
                radius: Theme.radius + 2
                readonly property bool can: studio.busy || input.text.trim().length > 0 || root.attachments.length > 0
                color: studio.busy ? Theme.overlay : can ? (sendMa.containsMouse ? Qt.lighter(Theme.accent, 1.1) : Theme.accent) : Theme.overlay
                Behavior on color { ColorAnimation { duration: Theme.normal } }
                Icon {
                    anchors.centerIn: parent
                    source: studio.busy ? Theme.icon("stop") : Theme.icon("arrow_upward")
                    size: 17
                    visible: studio.busy || !send.can
                    tone: 0.7
                }
                // Dark arrow on the light button.
                Text {
                    visible: !studio.busy && send.can
                    anchors.centerIn: parent
                    text: "↑"; color: "#ffffff"
                    font.pixelSize: 17; font.weight: Font.Bold
                    renderType: Text.NativeRendering
                }
                MouseArea {
                    id: sendMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: studio.busy ? studio.stop() : root.submit()
                }
                ToolTipLite { text: studio.busy ? qsTr("Stop") : qsTr("Send (Enter)"); shown: false }
            }
        }
    }
}
