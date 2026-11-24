import QtQuick
import QtQuick.Effects

// Workspace switcher in the manner of a photo app's scrolling tab strip / a horizontal time
// picker: the current workspace sits in focus at the centre, its neighbours drift out of focus
// with distance, and a scroll blurs with speed until the next tab settles sharp at the centre.
Item {
    id: root
    clip: true
    readonly property var names: blender.workspaces
    readonly property int itemWidth: 136
    property bool userMoving: false

    function indexOfActive() {
        var i = names.indexOf(blender.activeWorkspace)
        return i < 0 ? 0 : i
    }

    ListView {
        id: list
        anchors.fill: parent
        orientation: ListView.Horizontal
        model: root.names
        spacing: 0
        boundsBehavior: Flickable.StopAtBounds
        snapMode: ListView.SnapToItem
        highlightRangeMode: ListView.StrictlyEnforceRange
        preferredHighlightBegin: width / 2 - root.itemWidth / 2
        preferredHighlightEnd: width / 2 + root.itemWidth / 2
        highlightMoveDuration: Theme.slow
        highlightMoveVelocity: -1
        flickDeceleration: 2400
        maximumFlickVelocity: 3200
        currentIndex: root.indexOfActive()
        // Speed of the strip, 0..1, used for motion blur.
        readonly property real speed: Math.min(1, Math.abs(horizontalVelocity) / 1400)

        onMovementStarted: root.userMoving = true
        onMovementEnded: {
            root.userMoving = false
            if (currentIndex >= 0 && root.names[currentIndex] !== blender.activeWorkspace)
                blender.setWorkspace(root.names[currentIndex])
        }

        delegate: Item {
            id: tab
            required property string modelData
            required property int index
            width: root.itemWidth
            height: list.height
            // Distance from the centre in tab widths.
            readonly property real distance: (x - list.contentX + width / 2 - list.width / 2) / root.itemWidth
            readonly property real d: Math.abs(distance)
            readonly property bool focused: d < 0.5

            Item {
                id: content
                anchors.fill: parent
                opacity: Math.max(0.28, 1 - tab.d * 0.34)
                Label {
                    id: lbl
                    anchors.centerIn: parent
                    text: tab.modelData
                    font.pixelSize: Theme.sizeBody
                    font.weight: tab.focused ? Font.DemiBold : Font.Medium
                    color: tab.focused ? Theme.text : Theme.textSecondary
                }
                Rectangle {
                    visible: tab.focused && !list.moving
                    anchors.top: lbl.bottom
                    anchors.topMargin: 5
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 4; height: 4; radius: 2
                    color: Theme.text
                }
                // Out-of-focus tabs (and every tab while the strip moves fast) are blurred.
                // The tab in focus is never put through a layer, so its text stays native.
                layer.enabled: !Theme.reduceMotion && (tab.d > 0.35 || list.speed > 0.08)
                layer.effect: MultiEffect {
                    blurEnabled: true
                    blurMax: 18
                    blur: Math.min(1, Math.max(0, (tab.d - 0.35) * 0.55) + list.speed * 0.6)
                }
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    list.currentIndex = tab.index
                    blender.setWorkspace(tab.modelData)
                }
            }
        }

        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (e) => {
                var step = (e.angleDelta.y !== 0 ? -e.angleDelta.y : e.angleDelta.x) > 0 ? 1 : -1
                var next = Math.max(0, Math.min(root.names.length - 1, list.currentIndex + step))
                if (next !== list.currentIndex) {
                    list.currentIndex = next
                    switchTimer.restart()
                }
            }
        }
        Timer { id: switchTimer; interval: 260; onTriggered: blender.setWorkspace(root.names[list.currentIndex]) }
    }

    // Follow Blender when the workspace changes elsewhere.
    Connections {
        target: blender
        function onStateChanged() {
            if (!root.userMoving) list.currentIndex = root.indexOfActive()
        }
    }

    // Fade the edges into the bar.
    Rectangle {
        anchors.left: parent.left; width: 90; height: parent.height
        gradient: Gradient { orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Theme.chrome }
            GradientStop { position: 1; color: Qt.rgba(Theme.chrome.r, Theme.chrome.g, Theme.chrome.b, 0) } }
    }
    Rectangle {
        anchors.right: parent.right; width: 90; height: parent.height
        gradient: Gradient { orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Qt.rgba(Theme.chrome.r, Theme.chrome.g, Theme.chrome.b, 0) }
            GradientStop { position: 1; color: Theme.chrome } }
    }
    Keys.onLeftPressed: { if (list.currentIndex > 0) { list.currentIndex--; switchTimer.restart() } }
    Keys.onRightPressed: { if (list.currentIndex < names.length - 1) { list.currentIndex++; switchTimer.restart() } }
}
