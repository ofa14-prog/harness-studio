import QtQuick

// Image thumbnails at their own aspect ratio. Each fades in once decoded, shows a spinner while
// it loads and a clear mark when the file can't be read; removable ones get a close button.
Flow {
    id: root
    property var urls: []
    property int thumbHeight: 72
    property int maxWidth: thumbHeight * 2.4
    property bool removable: true
    signal removed(int index)
    spacing: 8
    visible: urls.length > 0
    height: visible ? implicitHeight : 0

    Repeater {
        model: root.urls
        delegate: Rectangle {
            id: t
            required property string modelData
            required property int index
            readonly property real aspect: img.status === Image.Ready && img.implicitHeight > 0
                                           ? img.implicitWidth / img.implicitHeight : 1.4
            height: root.thumbHeight
            width: Math.max(44, Math.min(root.maxWidth, Math.round(height * aspect)))
            radius: 8
            color: Theme.base
            border.color: hover.hovered ? Theme.borderStrong : Theme.border
            clip: true
            Behavior on width { NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }

            Image {
                id: img
                anchors.fill: parent
                anchors.margins: 1
                source: t.modelData
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
                smooth: true
                mipmap: true
                // Decode at the size it is shown on this screen, so it is neither blurry nor wasteful.
                sourceSize.height: Math.ceil(root.thumbHeight * Screen.devicePixelRatio * 1.5)
                opacity: status === Image.Ready ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.normal } }
            }
            Spinner { anchors.centerIn: parent; size: 16; visible: img.status === Image.Loading }
            Column {
                anchors.centerIn: parent
                visible: img.status === Image.Error
                spacing: 2
                Icon { anchors.horizontalCenter: parent.horizontalCenter; source: Theme.icon("error"); size: 16; tone: 0.7 }
                Text { text: qsTr("Can't read"); color: Theme.textTertiary; font.family: Theme.fontSecondary; font.pixelSize: 10; renderType: Text.NativeRendering }
            }
            HoverHandler { id: hover }
            TapHandler { onTapped: shell.openExternally(t.modelData) }
            Rectangle {
                visible: root.removable && hover.hovered
                anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 4
                width: 20; height: 20; radius: 10
                color: Qt.rgba(0.04, 0.04, 0.04, 0.82)
                border.color: Theme.border
                Icon { anchors.centerIn: parent; source: Theme.icon("close"); size: 12; tone: 1 }
                TapHandler { onTapped: root.removed(t.index) }
            }
        }
    }
}
