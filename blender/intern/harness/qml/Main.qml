import QtQuick
import Harness.Blender 1.0

// The Harness Studio window: title bar, workspace carousel, then three panels — Blender's
// outliner/properties window, Blender's main window, and Harness Studio.
Rectangle {
    id: root
    color: Theme.base
    focus: false

    // Harness Studio's share of the width is kept as a ratio so the layout scales with the
    // window. Everything left of it is one Blender window, tiled by Blender itself.
    property real rightRatio: 0.22
    readonly property real minSide: 300
    readonly property real minCenter: 560
    property string page: ""          // "", "settings", "profile"
    property string settingsSection: "general"

    Binding { target: Theme; property: "dpr"; value: shell.devicePixelRatio }
    Binding { target: Theme; property: "reduceMotion"; value: profiles.settings.interface ? profiles.settings.interface.reduce_motion : false }

    function openSettings(section) { settingsSection = section || "general"; page = "settings" }
    function openProfile() { page = "profile" }

    TitleBar {
        id: titleBar
        width: parent.width
        height: 42
        onSettingsRequested: root.openSettings("general")
        onProfileRequested: root.openProfile()
        onProvidersRequested: root.openSettings("providers")
    }

    Item {
        id: carouselRow
        anchors.top: titleBar.bottom
        width: parent.width
        height: 38
        Rectangle { anchors.fill: parent; color: Theme.chrome }
        WorkspaceCarousel {
            anchors.centerIn: parent
            width: Math.min(760, parent.width * 0.5)
            height: parent.height
        }
        LayoutButton {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
    }

    Item {
        id: panels
        anchors.top: carouselRow.bottom
        anchors.bottom: parent.bottom
        width: parent.width
        readonly property real inner: width - 2 * Theme.gap - 6
        readonly property real rightW: Math.max(root.minSide, Math.min(inner - root.minCenter, Math.round(inner * root.rightRatio)))

        Panel {
            id: centerPanel
            x: Theme.gap
            y: Theme.gap
            width: panels.width - rightPanel.width - 2 * Theme.gap - 6
            height: panels.height - 2 * Theme.gap
            BlenderSurface {
                id: centerSurface
                slot: "center"
                anchors.fill: parent
                anchors.margins: 1
            }
            // Cover while Blender starts and lays out its workspaces once, so the first tab
            // switches never show panels being rebuilt.
            Rectangle {
                id: cover
                anchors.fill: parent
                anchors.margins: 1
                radius: Theme.radiusLarge
                color: Theme.panel
                readonly property bool needed: !centerSurface.attached || !blender.layoutsReady
                opacity: needed ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: Theme.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
                MouseArea { anchors.fill: parent; enabled: cover.needed; hoverEnabled: true }
                Column {
                    anchors.centerIn: parent
                    spacing: 18
                    CubeLoader { anchors.horizontalCenter: parent.horizontalCenter; size: 64; running: cover.visible && !Theme.reduceMotion }
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: !centerSurface.attached ? qsTr("Starting Blender") : qsTr("Preparing workspaces")
                        color: Theme.textSecondary
                        font.weight: Theme.weightUi
                    }
                }
            }
        }
        Splitter {
            x: centerPanel.x + centerPanel.width
            width: 6
            height: panels.height
            onDragged: (dx) => {
                var w = Math.max(root.minSide, Math.min(panels.inner - root.minCenter, rightPanel.width - dx))
                root.rightRatio = w / panels.inner
            }
            onReleased: profiles.set("interface.studio_width", Math.round(rightPanel.width))
        }
        Panel {
            id: rightPanel
            x: panels.width - width - Theme.gap
            y: Theme.gap
            width: panels.rightW
            height: panels.height - 2 * Theme.gap
            StudioPanel {
                anchors.fill: parent
                onOpenSettings: (section) => root.openSettings(section)
            }
        }
    }

    Toasts { anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 18 }

    // Pages over the workspace.
    Loader {
        anchors.fill: parent
        active: root.page === "settings"
        sourceComponent: SettingsPage { section: root.settingsSection; onClosed: root.page = "" }
    }
    Loader {
        anchors.fill: parent
        active: root.page === "profile"
        sourceComponent: ProfilePage { onClosed: root.page = ""; onOpenSettings: (s) => root.openSettings(s) }
    }

    // The full-window viewer for a result (image, video, 3D model).
    Loader {
        anchors.fill: parent
        active: Viewer.job !== null
        sourceComponent: MediaViewer {}
    }

    WindowEdges { anchors.fill: parent; visible: !shell.maximized }

    // HARNESS_AGENT_TEST="<task>" with HARNESS_AGENT_MODEL="<provider>:<model>": run one agent
    // task end to end, write the transcript to HARNESS_AGENT_DUMP, take the picture, quit.
    property bool agentTestStarted: false
    property bool agentTestSawBusy: false
    Connections {
        target: blender
        function onStateChanged() {
            if (root.agentTestStarted || !blender.layoutsReady || !shell.env("HARNESS_AGENT_TEST").length) return
            root.agentTestStarted = true
            var pm = shell.env("HARNESS_AGENT_MODEL"), cut = pm.indexOf(":")
            if (cut > 0) studio.setAgentModel(pm.substring(0, cut), pm.substring(cut + 1))
            agentTestTimer.start()
        }
    }
    Timer { id: agentTestTimer; interval: 1500; onTriggered: studio.send(shell.env("HARNESS_AGENT_TEST"), shell.env("HARNESS_AGENT_VIEWPORT") === "1", []) }
    Connections {
        target: studio
        function onBusyChanged() {
            if (!root.agentTestStarted) return
            if (studio.busy) { root.agentTestSawBusy = true; return }
            if (!root.agentTestSawBusy) return
            studio.dumpThread(shell.env("HARNESS_AGENT_DUMP") || "/tmp/harness-agent-test.json")
            agentTestEnd.start()
        }
    }
    Timer { id: agentTestEnd; interval: 2500; onTriggered: shell.captureAndExit() }

    // The bridge learns the person's start-up choice as soon as it runs.
    Connections {
        target: blender
        function onReadyChanged() {
            if (blender.ready) {
                var ui = profiles.settings.interface
                blender.query("config", { reopen_last: !ui || ui.reopen_last !== false })
            }
        }
    }
    Component.onCompleted: {
        // HARNESS_VIEW=<file>: open the viewer on a file (checks the viewer without generating).
        var v = shell.env("HARNESS_VIEW")
        if (v.length) {
            var ext = v.split(".").pop().toLowerCase()
            Viewer.open({ id: -1, kind: ["glb", "gltf", "obj", "fbx"].indexOf(ext) >= 0 ? "3d" : ["mp4", "webm", "mov"].indexOf(ext) >= 0 ? "video" : "image",
                          url: shell.fileUrl(v), file: v, title: v.split("/").pop(), prompt: "", status: "done" })
        }
        if (blender.ready) blender.query("config", { reopen_last: profiles.settings.interface.reopen_last !== false })
        var ui = profiles.settings.interface
        var total = Math.max(1, width - 18)
        if (ui && ui.studio_width > 0) rightRatio = Math.min(0.4, ui.studio_width / Math.max(1200, total))
    }
}
