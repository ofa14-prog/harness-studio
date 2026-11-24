import QtQuick

// Everything generated in this session: progress while it runs, then ways to use it.
Item {
    id: root
    GridView {
        id: grid
        anchors.fill: parent
        anchors.margins: 12
        cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 150)))
        cellHeight: cellWidth + 44
        clip: true
        model: studio.jobs
        boundsBehavior: Flickable.StopAtBounds
        add: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.normal } }
        delegate: Item {
            id: cell
            required property var modelData
            width: grid.cellWidth
            height: grid.cellHeight
            readonly property bool active: modelData.status !== "done" && modelData.status !== "failed"
            Rectangle {
                id: tile
                x: 4; y: 4
                width: parent.width - 8
                height: width
                radius: Theme.radiusLarge
                color: Theme.raised
                border.color: tm.containsMouse ? Theme.borderStrong : Theme.border
                clip: true
                Image {
                    anchors.fill: parent
                    anchors.margins: 1
                    visible: cell.modelData.kind === "image" && cell.modelData.status === "done"
                    source: visible ? cell.modelData.url : ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize.width: 320
                }
                Icon {
                    anchors.centerIn: parent
                    visible: cell.modelData.status === "done" && cell.modelData.kind !== "image"
                    source: Theme.icon(cell.modelData.kind === "video" ? "movie" : "view_in_ar")
                    size: 30; tone: 0.5
                }
                Column {
                    visible: cell.active
                    anchors.centerIn: parent
                    width: parent.width - 24
                    spacing: 8
                    CubeLoader { anchors.horizontalCenter: parent.horizontalCenter; size: 48 }
                    Label { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: cell.modelData.message || qsTr("Queued"); color: Theme.textSecondary; font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi }
                    Rectangle {
                        width: parent.width; height: 3; radius: 1; color: Theme.border
                        Rectangle { width: parent.width * cell.modelData.progress; height: parent.height; radius: 1; color: Theme.accent
                            Behavior on width { NumberAnimation { duration: Theme.slow } } }
                    }
                }
                Label {
                    visible: cell.modelData.status === "failed"
                    anchors.centerIn: parent
                    width: parent.width - 18
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                    maximumLineCount: 5
                    text: cell.modelData.message
                    color: Theme.danger
                    font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi
                }
                MouseArea {
                    id: tm
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: (mouse) => {
                        var m = cell.modelData
                        if (mouse.button === Qt.LeftButton && m.status === "done") { Viewer.open(m); return }
                        var items = []
                        if (m.status === "done") {
                            items.push({ text: qsTr("Open"), icon: Theme.icon("open_in_new"), action: function() { Viewer.open(m) } })
                            if (m.kind === "3d") {
                                items.push({ text: qsTr("Place at the 3D cursor"), icon: Theme.icon("view_in_ar"), action: function() { studio.placeJob(m.id, "cursor") } })
                                items.push({ text: qsTr("Place on the active object"), icon: Theme.icon("center_focus_strong"), enabled: blender.activeObject.length > 0,
                                             action: function() { studio.placeJob(m.id, "object", blender.activeObject) } })
                                items.push({ text: qsTr("Add at the origin"), icon: Theme.icon("add"), action: function() { studio.placeJob(m.id, "origin") } })
                            }
                            if (m.kind === "image" || m.kind === "video") {
                                items.push({ text: qsTr("Add as a plane"), icon: Theme.icon("image"), action: function() { studio.useJob(m.id, "object") } })
                                items.push({ text: qsTr("Use as texture on the selection"), icon: Theme.icon("texture"), action: function() { studio.useJob(m.id, "texture") } })
                            }
                            if (m.kind === "image") {
                                items.push({ text: qsTr("Dress the selection as a PBR material"), icon: Theme.icon("palette"), enabled: blender.selectedCount > 0,
                                             action: function() { studio.applyJobMaterial(m.id, []) } })
                                items.push({ text: qsTr("Match the scene lighting"), icon: Theme.icon("bolt"), action: function() { studio.matchJobLighting(m.id) } })
                                items.push({ text: qsTr("Use as world background"), icon: Theme.icon("landscape"), action: function() { studio.useJob(m.id, "world") } })
                            }
                            items.push({ text: qsTr("Show in folder"), icon: Theme.icon("folder_open"), action: function() { studio.revealJob(m.id) } })
                            items.push({ separator: true })
                        }
                        if (cell.active) items.push({ text: qsTr("Cancel"), icon: Theme.icon("stop"), action: function() { studio.cancelJob(m.id) } })
                        else items.push({ text: qsTr("Remove from the list"), icon: Theme.icon("delete"), action: function() { studio.removeJob(m.id) } })
                        menu.items = items
                        menu.openAt(tm, mouse.x, mouse.y)
                    }
                }
                PopupMenu { id: menu; minWidth: 230 }
            }
            Label {
                x: 6; y: tile.y + tile.height + 6
                width: parent.width - 12
                text: cell.modelData.title
                font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi
            }
            Label {
                x: 6; y: tile.y + tile.height + 22
                width: parent.width - 12
                text: cell.modelData.time + (cell.modelData.prompt ? "  ·  " + cell.modelData.prompt : "")
                color: Theme.textTertiary
                font.pixelSize: Theme.sizeCaption; font.weight: Theme.weightUi
            }
        }
    }
    Column {
        visible: grid.count === 0
        anchors.centerIn: parent
        width: parent.width - 60
        spacing: 8
        Icon { anchors.horizontalCenter: parent.horizontalCenter; source: Theme.icon("grid_view"); size: 26; tone: 0.35 }
        Label { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: qsTr("Images, videos and models you create appear here."); color: Theme.textTertiary; wrapMode: Text.WordWrap; elide: Text.ElideNone }
    }
}
