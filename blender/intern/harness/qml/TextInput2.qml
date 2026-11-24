import QtQuick
import QtQuick.Controls.Basic as QC

// Single-line field. Focus shows as a slightly brighter border, never an outline ring.
QC.TextField {
    id: f
    implicitHeight: 36
    color: Theme.text
    font.family: Theme.font
    font.pixelSize: Theme.sizeBody
    placeholderTextColor: Theme.textTertiary
    selectionColor: Theme.accentSoft
    selectedTextColor: Theme.text
    leftPadding: 12
    rightPadding: 12
    verticalAlignment: Text.AlignVCenter
    renderType: Text.NativeRendering
    selectByMouse: true
    background: Rectangle {
        radius: Theme.radius
        color: Theme.raised
        border.width: 1
        border.color: f.activeFocus ? Theme.borderStrong : (hover.hovered ? Theme.border : Theme.borderSubtle)
        Behavior on border.color { ColorAnimation { duration: Theme.fast } }
        HoverHandler { id: hover }
    }
    Keys.onEscapePressed: focus = false
}
