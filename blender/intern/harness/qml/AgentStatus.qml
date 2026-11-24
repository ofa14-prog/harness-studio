import QtQuick

// What the agent is doing, driven by its real steps (studio.activityPhase/Tool/Step): an
// icon for the phase, the step's own words, how long it has taken, and which step of the
// allowed number it is. A new step slides the old words up and out and the new ones in; a
// thin light runs along the bottom while work goes on.
Rectangle {
    id: root
    readonly property bool shown: studio.busy
    readonly property string phase: studio.activityPhase
    readonly property string tool: studio.activityTool
    property int elapsed: 0
    height: shown ? 44 : 0
    opacity: shown ? 1 : 0
    visible: opacity > 0
    radius: 10
    color: Qt.rgba(0.62, 0.796, 1.0, 0.06)
    border.color: Qt.rgba(0.62, 0.796, 1.0, 0.18)
    clip: true
    Behavior on height { NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
    Behavior on opacity { NumberAnimation { duration: Theme.normal } }

    function iconFor(phase, tool) {
        if (phase === "tool") {
            if (tool === "blender_execute_python") return "deployed_code"
            if (tool === "blender_capture_viewport" || tool === "blender_capture_pass") return "center_focus_strong"
            if (tool === "blender_render_image") return "photo_camera"
            if (tool === "blender_camera_path") return "movie"
            if (tool.indexOf("generate_") === 0) return "auto_awesome"
            if (tool === "blender_import_file" || tool === "blender_place_objects") return "view_in_ar"
            if (tool === "blender_apply_image_material") return "palette"
            if (tool === "blender_match_lighting") return "bolt"
            return "tune"
        }
        if (phase === "approval") return "lock"
        if (phase === "reading") return "search"
        if (phase === "writing") return "edit"
        return ""
    }

    Timer {
        interval: 1000; repeat: true; running: root.shown
        triggeredOnStart: true
        onTriggered: root.elapsed = Math.max(0, Math.round((Date.now() - studio.activitySince) / 1000))
    }

    // Phase mark: the cube field while thinking, the step's icon otherwise.
    Item {
        id: mark
        x: 12; anchors.verticalCenter: parent.verticalCenter
        width: 22; height: 22
        CubeLoader { anchors.centerIn: parent; size: 22; base: Theme.agent; visible: root.iconFor(root.phase, root.tool) === "" }
        Icon {
            anchors.centerIn: parent
            visible: root.iconFor(root.phase, root.tool) !== ""
            source: Theme.icon(root.iconFor(root.phase, root.tool) || "tune")
            size: 18; tone: 1
            SequentialAnimation on opacity {
                running: parent.visible && !Theme.reduceMotion; loops: Animation.Infinite
                NumberAnimation { to: 0.45; duration: 700; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1.0; duration: 700; easing.type: Easing.InOutSine }
            }
        }
    }

    // The words of the step: the old line slides up and fades, the new one comes in from below.
    Item {
        id: words
        anchors.left: mark.right; anchors.leftMargin: 10
        anchors.right: meta.left; anchors.rightMargin: 10
        height: parent.height
        clip: true
        property string shownText: studio.activity
        property string leaving: ""
        Connections {
            target: studio
            function onBusyChanged() {
                if (studio.activity === words.shownText) return
                words.leaving = words.shownText
                words.shownText = studio.activity
                swap.restart()
            }
        }
        Text {
            id: oldLine
            width: parent.width; elide: Text.ElideRight
            y: (parent.height - height) / 2
            text: words.leaving
            opacity: 0
            color: Theme.textSecondary
            font.family: Theme.font; font.pixelSize: Theme.sizeBody; font.weight: Theme.weightUi; renderType: Text.NativeRendering
        }
        Text {
            id: newLine
            width: parent.width; elide: Text.ElideRight
            y: (parent.height - height) / 2
            text: words.shownText
            color: Theme.text
            font.family: Theme.font; font.pixelSize: Theme.sizeBody; font.weight: Theme.weightUi; renderType: Text.NativeRendering
            SequentialAnimation on color {
                running: root.shown && !Theme.reduceMotion; loops: Animation.Infinite
                ColorAnimation { to: Theme.agent; duration: 900; easing.type: Easing.InOutSine }
                ColorAnimation { to: Theme.text; duration: 900; easing.type: Easing.InOutSine }
            }
        }
        ParallelAnimation {
            id: swap
            NumberAnimation { target: oldLine; property: "y"; from: (words.height - oldLine.height) / 2; to: -oldLine.height; duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve }
            NumberAnimation { target: oldLine; property: "opacity"; from: 1; to: 0; duration: Theme.normal }
            NumberAnimation { target: newLine; property: "y"; from: words.height; to: (words.height - newLine.height) / 2; duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve }
            NumberAnimation { target: newLine; property: "opacity"; from: 0; to: 1; duration: Theme.normal }
        }
    }

    // Time and step.
    Text {
        id: meta
        anchors.right: stopBtn.left; anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: (root.elapsed > 0 ? root.elapsed + "s" : "") + (studio.activityStep > 0 ? "  ·  " + qsTr("step %1/%2").arg(studio.activityStep).arg(studio.activityMaxSteps) : "")
        color: Theme.textTertiary
        font.family: Theme.fontSecondary; font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi; renderType: Text.NativeRendering
    }
    IconButton {
        id: stopBtn
        anchors.right: parent.right; anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        icon: Theme.icon("stop"); iconSize: 15; implicitWidth: 28; implicitHeight: 28
        tooltip: qsTr("Stop")
        onClicked: studio.stop()
    }

    // A light running along the bottom edge while it works.
    Rectangle {
        anchors.bottom: parent.bottom
        height: 2; width: parent.width * 0.28; radius: 1
        visible: root.shown && !Theme.reduceMotion
        gradient: Gradient { orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Qt.rgba(0.62, 0.796, 1.0, 0) }
            GradientStop { position: 0.5; color: Theme.agent }
            GradientStop { position: 1; color: Qt.rgba(0.62, 0.796, 1.0, 0) } }
        NumberAnimation on x { from: -root.width * 0.28; to: root.width; duration: 1600; loops: Animation.Infinite; running: root.shown && !Theme.reduceMotion }
    }
}
