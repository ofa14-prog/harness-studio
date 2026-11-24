import QtQuick

Item {
    id: root
    signal openSettings(string section)

    ListView {
        id: list
        anchors.top: parent.top
        anchors.bottom: status.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 16
        anchors.bottomMargin: 8
        clip: true
        spacing: 18
        model: studio.messages
        boundsBehavior: Flickable.StopAtBounds
        delegate: MessageItem { width: list.width }
        onCountChanged: Qt.callLater(function() { list.positionViewAtEnd() })
        add: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.normal }
                NumberAnimation { property: "y"; from: list.contentHeight + 12; duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve }
            }
        }
        // Keep following the stream while the answer grows.
        onContentHeightChanged: if (studio.busy && atYEnd === false && contentHeight - contentY - height < 160) positionViewAtEnd()
    }

    // Empty conversation: what the agent can do, with one-tap starting points.
    Column {
        visible: studio.messages.length === 0
        anchors.centerIn: list
        width: list.width - 24
        spacing: 18
        Label {
            width: parent.width
            text: qsTr("ASK THE AGENT TO WORK ON YOUR SCENE")
            font.family: Theme.fontHero
            font.pixelSize: Theme.sizeHero
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            lineHeight: 1.1
            horizontalAlignment: Text.AlignHCenter
        }
        Label {
            width: parent.width
            text: qsTr("It reads the scene, runs Blender operations, looks at the viewport and can generate images, video and 3D with your keys.")
            color: Theme.textSecondary
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            horizontalAlignment: Text.AlignHCenter
        }
        Column {
            width: parent.width
            spacing: 8
            Repeater {
                model: [ qsTr("Set up three-point studio lighting for the selected object"),
                         qsTr("Make a turntable animation of the scene over 120 frames"),
                         qsTr("Give every mesh a clean PBR material and render a preview"),
                         qsTr("Use the viewport's depth pass to generate a cinematic still of this scene") ]
                delegate: Rectangle {
                    required property string modelData
                    width: parent.width
                    height: sl.implicitHeight + 22
                    radius: Theme.radius
                    color: sm.containsMouse ? Theme.hover : "transparent"
                    border.color: Theme.border
                    Label { id: sl; x: 14; width: parent.width - 28; anchors.verticalCenter: parent.verticalCenter; text: modelData; color: Theme.textSecondary; wrapMode: Text.WordWrap; elide: Text.ElideNone }
                    MouseArea { id: sm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: composer.setText(modelData) }
                }
            }
        }
    }

    AgentStatus {
        id: status
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: 16; anchors.rightMargin: 16
        anchors.bottom: composer.top; anchors.bottomMargin: shown ? 10 : 0
    }
    Composer {
        id: composer
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 16
        onOpenSettings: (s) => root.openSettings(s)
    }
}
