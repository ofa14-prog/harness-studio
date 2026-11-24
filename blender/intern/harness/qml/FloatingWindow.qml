import QtQuick
import Harness.Blender 1.0

// A Blender window that opens on its own (Preferences, Render, file browser, dialogs).
Rectangle {
    id: root
    property string slot: ""
    color: Theme.panel
    function requestClose() { surface.requestClose() }
    BlenderSurface {
        id: surface
        slot: root.slot
        anchors.fill: parent
    }
    Loading { anchors.centerIn: parent; visible: !surface.attached }
}
