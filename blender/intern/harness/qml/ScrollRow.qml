import QtQuick

// A single row that scrolls sideways when it doesn't fit: mouse wheel (either direction),
// touchpad, dragging, and arrow buttons at the edges that appear only when there is more to
// see. The edges fade into `fadeColor` so a cut-off item reads as "more this way".
Item {
    id: root
    default property alias content: row.data
    property int spacing: 6
    property color fadeColor: Theme.raised
    implicitHeight: 32
    height: implicitHeight
    readonly property bool overflow: flick.contentWidth > flick.width + 1
    readonly property bool atStart: flick.contentX <= 1
    readonly property bool atEnd: flick.contentX >= flick.contentWidth - flick.width - 1

    function scrollBy(dx) {
        var target = Math.max(0, Math.min(flick.contentWidth - flick.width, flick.contentX + dx))
        glide.stop()
        glide.to = target
        glide.start()
    }
    /** Bring a child into view (used after picking a value that widened a chip). */
    function ensureVisible(item) {
        var p = item.mapToItem(row, 0, 0)
        if (p.x < flick.contentX) scrollBy(p.x - flick.contentX - 8)
        else if (p.x + item.width > flick.contentX + flick.width) scrollBy(p.x + item.width - flick.contentX - flick.width + 8)
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: row.width
        contentHeight: height
        clip: true
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        interactive: root.overflow
        NumberAnimation { id: glide; target: flick; property: "contentX"; duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve }
        Row {
            id: row
            spacing: root.spacing
            height: parent.height
        }
    }
    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        enabled: root.overflow
        onWheel: (e) => {
            if (e.pixelDelta.x !== 0 || e.pixelDelta.y !== 0) {        // touchpad: follow the fingers
                glide.stop()
                var d = e.pixelDelta.x !== 0 ? e.pixelDelta.x : e.pixelDelta.y
                flick.contentX = Math.max(0, Math.min(flick.contentWidth - flick.width, flick.contentX - d))
            } else {                                                   // wheel notches
                var a = e.angleDelta.x !== 0 ? e.angleDelta.x : e.angleDelta.y
                root.scrollBy(-a / 120 * 90)
            }
            e.accepted = true
        }
    }

    // Edge fades and arrows.
    Rectangle {
        anchors.left: parent.left; width: 40; height: parent.height
        visible: root.overflow && !root.atStart
        gradient: Gradient { orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: root.fadeColor }
            GradientStop { position: 0.55; color: root.fadeColor }
            GradientStop { position: 1.0; color: Qt.rgba(root.fadeColor.r, root.fadeColor.g, root.fadeColor.b, 0) } }
        ArrowButton { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; iconName: "chevron_left"; onClicked: root.scrollBy(-root.width * 0.6) }
    }
    Rectangle {
        anchors.right: parent.right; width: 40; height: parent.height
        visible: root.overflow && !root.atEnd
        gradient: Gradient { orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Qt.rgba(root.fadeColor.r, root.fadeColor.g, root.fadeColor.b, 0) }
            GradientStop { position: 0.45; color: root.fadeColor }
            GradientStop { position: 1.0; color: root.fadeColor } }
        ArrowButton { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; iconName: "chevron_right"; onClicked: root.scrollBy(root.width * 0.6) }
    }

    component ArrowButton: Rectangle {
        property string iconName
        signal clicked()
        width: 24; height: 24; radius: 7
        color: am.containsMouse ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(1, 1, 1, 0.07)
        border.color: Theme.border
        Icon { anchors.centerIn: parent; source: Theme.icon(parent.iconName); size: 16; tone: 1 }
        MouseArea { id: am; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked() }
    }
}
