import QtQuick
import QtQuick.Controls.Basic as QC

QC.ToolTip {
    id: tip
    property bool shown: false
    visible: shown && text.length > 0
    delay: 600
    timeout: 6000
    padding: 6
    contentItem: Label { text: tip.text; font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi; color: Theme.text }
    background: Rectangle { color: Theme.overlay; radius: Theme.radiusSmall; border.color: Theme.border }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.fast } }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.fast } }
}
