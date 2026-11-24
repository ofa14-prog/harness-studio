import QtQuick
import QtQuick.Controls.Basic as QC

// The full-window viewer for a result: images zoom and pan, videos play, 3D models can be
// turned, lit and taken apart (wireframe, material channels) before they go into the scene.
Rectangle {
    id: root
    property var job: Viewer.job
    color: Qt.rgba(0.039, 0.039, 0.039, 0.97)
    focus: true
    Keys.onEscapePressed: Viewer.close()
    MouseArea { anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.AllButtons; onWheel: (w) => w.accepted = true }
    Component.onCompleted: forceActiveFocus()

    // ---- Top bar ---------------------------------------------------------------------
    Item {
        id: bar
        width: parent.width
        height: 56
        Row {
            x: 20
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12
            Icon { source: Theme.icon(root.job && root.job.kind === "video" ? "movie" : root.job && root.job.kind === "3d" ? "view_in_ar" : "image"); size: 18; anchors.verticalCenter: parent.verticalCenter }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text { text: root.job ? (root.job.title || "") : ""; color: Theme.text; font.family: Theme.font; font.pixelSize: Theme.sizeTitle; font.weight: Theme.weightStrong; renderType: Text.NativeRendering }
                Text { width: Math.min(implicitWidth, root.width * 0.45); elide: Text.ElideRight
                       text: root.job ? (root.job.prompt || root.job.file || "") : ""; color: Theme.textTertiary
                       font.family: Theme.fontSecondary; font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi; renderType: Text.NativeRendering }
            }
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Button {
                visible: !!root.job && root.job.kind === "3d"
                icon: Theme.icon("view_in_ar"); text: qsTr("Place at the 3D cursor"); variant: "primary"
                onClicked: { studio.placeJob(root.job.id, "cursor"); Viewer.close() }
            }
            Button {
                visible: !!root.job && root.job.kind === "3d" && blender.activeObject.length > 0
                icon: Theme.icon("center_focus_strong"); text: qsTr("On the active object")
                onClicked: { studio.placeJob(root.job.id, "object", blender.activeObject); Viewer.close() }
            }
            Button {
                visible: !!root.job && root.job.kind === "image"
                icon: Theme.icon("palette"); text: qsTr("Material on the selection"); enabled: blender.selectedCount > 0
                onClicked: studio.applyJobMaterial(root.job.id, [])
            }
            Button {
                visible: !!root.job && root.job.kind === "image"
                icon: Theme.icon("bolt"); text: qsTr("Match lighting")
                onClicked: studio.matchJobLighting(root.job.id)
            }
            Button {
                visible: !!root.job && (root.job.kind === "image" || root.job.kind === "video")
                icon: Theme.icon("image"); text: qsTr("Add as a plane")
                onClicked: studio.useJob(root.job.id, "object")
            }
            IconButton { icon: Theme.icon("folder_open"); tooltip: qsTr("Show in folder"); onClicked: studio.revealJob(root.job.id) }
            IconButton { icon: Theme.icon("close"); tooltip: qsTr("Close (Esc)"); onClicked: Viewer.close() }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
    }

    // ---- Stage -----------------------------------------------------------------------
    Item {
        id: stage
        anchors.top: bar.bottom
        anchors.bottom: parent.bottom
        width: parent.width

        // Image: fit, then wheel to zoom around the pointer, drag to pan, double-click to toggle 1:1.
        Flickable {
            id: imageView
            visible: !!root.job && root.job.kind === "image"
            anchors.fill: parent
            anchors.margins: 24
            clip: true
            contentWidth: Math.max(width, img.width * zoom)
            contentHeight: Math.max(height, img.height * zoom)
            property real zoom: 1
            boundsBehavior: Flickable.StopAtBounds
            Image {
                id: img
                source: imageView.visible ? root.job.url : ""
                asynchronous: true
                fillMode: Image.PreserveAspectFit
                width: imageView.width; height: imageView.height
                scale: imageView.zoom
                transformOrigin: Item.TopLeft
                x: Math.max(0, (imageView.contentWidth - width * imageView.zoom) / 2)
                y: Math.max(0, (imageView.contentHeight - height * imageView.zoom) / 2)
                smooth: true; mipmap: true
            }
            CubeLoader { anchors.centerIn: parent; size: 56; visible: img.status === Image.Loading }
            WheelHandler {
                onWheel: (e) => {
                    var z = Math.max(1, Math.min(8, imageView.zoom * (e.angleDelta.y > 0 ? 1.15 : 1 / 1.15)))
                    imageView.zoom = z
                }
            }
            TapHandler { onDoubleTapped: imageView.zoom = imageView.zoom > 1 ? 1 : 2.5 }
        }

        // Video and 3D load their modules only when needed.
        Loader {
            anchors.fill: parent
            active: !!root.job && root.job.kind === "video"
            source: "VideoViewer.qml"
            onLoaded: item.source = root.job.url
            onStatusChanged: if (status === Loader.Error) failed.text = qsTr("Video playback isn't available on this computer (Qt Multimedia).")
        }
        Loader {
            anchors.fill: parent
            active: !!root.job && root.job.kind === "3d"
            source: "ModelViewer3D.qml"
            onLoaded: item.source = root.job.url
            onStatusChanged: if (status === Loader.Error) failed.text = qsTr("The 3D viewer isn't available on this computer (Qt Quick 3D).")
        }
        Text {
            id: failed
            anchors.centerIn: parent
            color: Theme.textSecondary
            font.family: Theme.font; font.pixelSize: Theme.sizeBody
            renderType: Text.NativeRendering
        }
    }
}
