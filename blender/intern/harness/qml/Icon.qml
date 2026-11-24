import QtQuick

// A single-colour SVG drawn at its device pixel size (no resampling).
Item {
    id: root
    property url source
    property int size: 16
    property real tone: 1.0          // 1 primary, ~0.62 secondary, ~0.4 tertiary
    implicitWidth: size
    implicitHeight: size
    Image {
        anchors.centerIn: parent
        width: root.size
        height: root.size
        source: root.source
        // Rasterise at the window's own scale (on a 1.25 output the screen reports 2.0; drawing
        // at 2x and shrinking blurred every icon), then show the pixels as they are.
        sourceSize.width: Math.round(root.size * Theme.dpr)
        sourceSize.height: Math.round(root.size * Theme.dpr)
        smooth: false
        mipmap: false
        opacity: root.tone
        visible: root.source.toString().length > 0
    }
}
