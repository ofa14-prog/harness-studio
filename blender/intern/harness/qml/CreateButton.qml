import QtQuick

// The Create button (the Openfield composer's button in the brand orange): a bevel edge, a
// soft glow on hover, a pressed state, and a progress fill while a generation runs.
Item {
    id: root
    property string text: qsTr("Create")
    property bool running: false
    property real progress: 0
    property bool tall: false          // the composer's square-ish button beside the prompt
    signal clicked()
    implicitHeight: 46
    implicitWidth: 200
    opacity: enabled ? 1 : 0.45

    // Glow
    Rectangle {
        anchors.fill: face
        anchors.margins: -5
        radius: Theme.radiusLarge + 4
        color: "transparent"
        border.width: 5
        border.color: Qt.rgba(0.984, 0.58, 0.043, ma.containsMouse && !root.running ? 0.24 : (root.running ? 0.14 : 0))
        Behavior on border.color { ColorAnimation { duration: Theme.normal } }
    }
    // Bevel
    Rectangle { anchors.fill: parent; anchors.topMargin: 2; radius: Theme.radiusLarge; color: Theme.createBevel }
    Rectangle {
        id: face
        anchors.fill: parent
        anchors.bottomMargin: ma.pressed ? 0 : 2
        anchors.topMargin: ma.pressed ? 2 : 0
        radius: Theme.radiusLarge
        color: root.running ? Qt.darker(Theme.create, 1.12) : ma.containsMouse ? Theme.createHover : Theme.create
        // A faint top light gives the face its depth.
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.30) }
                GradientStop { position: 0.45; color: Qt.rgba(1, 1, 1, 0) }
            }
        }
        clip: true
        Behavior on color { ColorAnimation { duration: Theme.fast } }
        // Progress
        Rectangle {
            visible: root.running
            width: parent.width * Math.max(0.04, root.progress)
            height: parent.height
            color: Qt.rgba(1, 1, 1, 0.22)
            Behavior on width { NumberAnimation { duration: Theme.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
        }
        // A light band sweeping while it works.
        Rectangle {
            visible: root.running && !Theme.reduceMotion
            width: 60; height: parent.height
            gradient: Gradient { orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0) }
                GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, 0.55) }
                GradientStop { position: 1; color: Qt.rgba(1, 1, 1, 0) } }
            NumberAnimation on x { from: -60; to: root.width; duration: 1300; loops: Animation.Infinite; running: root.running }
        }
        Grid {
            anchors.centerIn: parent
            columns: root.tall ? 1 : 2
            spacing: root.tall ? 4 : 8
            horizontalItemAlignment: Grid.AlignHCenter
            verticalItemAlignment: Grid.AlignVCenter
            CubeLoader { visible: root.running; size: root.tall ? 30 : 20; base: "#3a2205" }
            Text {
                text: root.running ? (root.tall ? qsTr("Creating") : qsTr("Creating…")) : root.text
                color: Theme.inkOnCreate
                font.family: Theme.font
                font.pixelSize: root.tall ? 15 : 14
                font.weight: Font.DemiBold
                renderType: Text.NativeRendering
            }
            Text {
                visible: root.tall && !root.running
                text: "Ctrl ⏎"
                color: Qt.rgba(0.03, 0.035, 0.045, 0.45)
                font.family: Theme.font
                font.pixelSize: 10
                font.weight: Font.Medium
                renderType: Text.NativeRendering
            }
        }
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
