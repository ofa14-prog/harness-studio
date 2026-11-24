import QtQuick

// One row of the conversation: the person's message, the agent's answer, a tool step, an
// approval request or a notice.
Item {
    id: root
    required property var modelData
    required property int index
    readonly property string role: modelData.role
    implicitHeight: loader.implicitHeight
    height: implicitHeight

    Loader {
        id: loader
        width: parent.width
        sourceComponent: root.role === "user" ? userC : root.role === "assistant" ? assistantC
                       : root.role === "tool" ? toolC : root.role === "approval" ? approvalC : noticeC
    }

    Component {
        id: userC
        Item {
            implicitHeight: bubble.height + (imgs.visible ? imgs.height + 8 : 0)
            Rectangle {
                id: bubble
                anchors.right: parent.right
                // The text wraps at a fixed width; the bubble hugs the longest line. (Sizing the
                // text by its own implicit width fed back into its wrapping and made it jitter.)
                width: Math.min(parent.width * 0.86, ut.contentWidth + 26)
                height: ut.contentHeight + 20
                radius: Theme.radiusLarge
                color: Theme.raised
                border.color: Theme.borderSubtle
                TextEdit {
                    id: ut
                    x: 13; y: 10
                    width: root.width * 0.86 - 26
                    text: root.modelData.text
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    color: Theme.text
                    selectionColor: Theme.accentSoft
                    font.family: Theme.font
                    font.pixelSize: Theme.sizeBody
                    renderType: TextEdit.NativeRendering
                }
            }
            Thumbs { id: imgs; anchors.top: bubble.bottom; anchors.topMargin: 8; anchors.right: parent.right; width: parent.width * 0.86
                     layoutDirection: Qt.RightToLeft; urls: root.modelData.images || [] }
        }
    }

    Component {
        id: assistantC
        Item {
            // Room for the actions is always kept, so showing them on hover moves nothing.
            implicitHeight: at.implicitHeight + (root.modelData.streaming === true ? 0 : 30)
            TextEdit {
                id: at
                width: parent.width
                text: root.modelData.text
                textFormat: TextEdit.MarkdownText
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                color: Theme.text
                selectionColor: Theme.accentSoft
                font.family: Theme.font
                font.pixelSize: Theme.sizeBody
                renderType: TextEdit.NativeRendering
                onLinkActivated: (link) => shell.openExternally(link)
            }
            // A caret while the answer is still arriving.
            Rectangle {
                visible: root.modelData.streaming === true
                width: 7; height: 14; radius: 1
                color: Theme.agent
                x: Math.min(parent.width - 8, at.positionToRectangle(at.length).x + 3)
                y: at.positionToRectangle(at.length).y + 2
                SequentialAnimation on opacity { running: parent.visible; loops: Animation.Infinite
                    NumberAnimation { to: 0.15; duration: 420 } NumberAnimation { to: 1; duration: 420 } }
            }
            Row {
                id: actions
                anchors.top: at.bottom
                anchors.topMargin: 4
                visible: root.modelData.streaming !== true
                opacity: hover.hovered ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.fast } }
                spacing: 2
                IconButton { icon: Theme.icon("content_copy"); iconSize: 14; implicitWidth: 24; implicitHeight: 24; tooltip: qsTr("Copy"); onClicked: shell.copyText(root.modelData.text) }
            }
            HoverHandler { id: hover }
        }
    }

    Component {
        id: toolC
        Rectangle {
            implicitHeight: col.implicitHeight + 16
            radius: Theme.radius
            color: Qt.rgba(0.62, 0.796, 1.0, 0.04)
            border.color: Qt.rgba(0.62, 0.796, 1.0, 0.16)
            property bool expanded: false
            Column {
                id: col
                x: 10; y: 8
                width: parent.width - 20
                spacing: 6
                Item {
                    width: parent.width
                    height: 20
                    Item {
                        id: state
                        width: 16; height: 16
                        anchors.verticalCenter: parent.verticalCenter
                        Spinner { anchors.centerIn: parent; size: 13; visible: root.modelData.status === "running" }
                        Icon { anchors.centerIn: parent; size: 15; visible: root.modelData.status === "done"; source: Theme.icon("check_circle"); tone: 0.55 }
                        Icon { anchors.centerIn: parent; size: 15; visible: root.modelData.status === "error"; source: Theme.icon("error") }
                    }
                    Label {
                        x: 24; width: parent.width - 50
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.modelData.text
                        color: root.modelData.status === "error" ? Theme.danger : Theme.textSecondary
                        font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi
                    }
                    IconButton {
                        visible: (root.modelData.detail || "").length > 0 || (root.modelData.error || "").length > 0
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        implicitWidth: 22; implicitHeight: 22; iconSize: 16
                        icon: Theme.icon("expand_more")
                        rotation: parent.parent.parent.expanded ? 180 : 0
                        Behavior on rotation { NumberAnimation { duration: Theme.normal } }
                        onClicked: parent.parent.parent.expanded = !parent.parent.parent.expanded
                    }
                }
                Rectangle {
                    visible: parent.parent.expanded
                    width: parent.width
                    height: code.implicitHeight + 14
                    radius: Theme.radiusSmall
                    color: Theme.base
                    TextEdit {
                        id: code
                        x: 8; y: 7
                        width: parent.width - 16
                        text: (root.modelData.error || "").length ? root.modelData.error : root.modelData.detail
                        readOnly: true; selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        color: (root.modelData.error || "").length ? Theme.danger : Theme.textSecondary
                        font.family: Theme.mono
                        font.pixelSize: 11
                        renderType: TextEdit.NativeRendering
                    }
                }
                Thumbs { width: parent.width; urls: root.modelData.images || [] }
            }
        }
    }

    Component {
        id: approvalC
        Rectangle {
            implicitHeight: ac.implicitHeight + 24
            radius: Theme.radiusLarge
            color: Theme.raised
            border.color: root.modelData.state === "pending" ? Theme.warning : Theme.border
            Column {
                id: ac
                x: 12; y: 12
                width: parent.width - 24
                spacing: 10
                Label { width: parent.width; text: root.modelData.text; wrapMode: Text.WordWrap; elide: Text.ElideNone }
                Rectangle {
                    visible: (root.modelData.detail || "").length > 0
                    width: parent.width; height: Math.min(180, dcode.implicitHeight + 14)
                    radius: Theme.radiusSmall; color: Theme.base; clip: true
                    TextEdit { id: dcode; x: 8; y: 7; width: parent.width - 16; text: root.modelData.detail || ""; readOnly: true; wrapMode: TextEdit.WrapAnywhere; color: Theme.textSecondary; font.family: Theme.mono; font.pixelSize: 11; renderType: TextEdit.NativeRendering }
                }
                Row {
                    visible: root.modelData.state === "pending"
                    spacing: 8
                    Button { text: qsTr("Allow"); variant: "primary"; onClicked: studio.answerApproval(root.index, true) }
                    Button { text: qsTr("Decline"); onClicked: studio.answerApproval(root.index, false) }
                }
                Label {
                    visible: root.modelData.state !== "pending"
                    text: root.modelData.state === "approved" ? qsTr("Allowed") : root.modelData.state === "declined" ? qsTr("Declined") : qsTr("No longer pending")
                    color: Theme.textTertiary
                    font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi
                }
            }
        }
    }

    Component {
        id: noticeC
        Item {
            implicitHeight: nt.implicitHeight + (nimgs.visible ? nimgs.height + 6 : 0) + 4
            Row {
                id: nrow
                spacing: 8
                width: parent.width
                Rectangle { width: 6; height: 6; radius: 1; anchors.verticalCenter: nt.verticalCenter
                    color: root.modelData.kind === "error" ? Theme.danger : root.modelData.kind === "done" ? Theme.success : Theme.textTertiary }
                Label { id: nt; width: parent.width - 20; text: root.modelData.text; wrapMode: Text.WordWrap; elide: Text.ElideNone
                    color: root.modelData.kind === "error" ? Theme.danger : Theme.textSecondary; font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi }
            }
            Thumbs { id: nimgs; anchors.top: nrow.bottom; anchors.topMargin: 8; x: 14; width: parent.width - 28; urls: root.modelData.images || [] }
            Row {
                visible: root.modelData.kind === "error" && root.index === studio.messages.length - 1 && !studio.busy
                anchors.right: parent.right
                anchors.verticalCenter: nrow.verticalCenter
                IconButton { icon: Theme.icon("replay"); iconSize: 15; implicitWidth: 24; implicitHeight: 24; tooltip: qsTr("Try again"); onClicked: studio.retry() }
            }
        }
    }
}
