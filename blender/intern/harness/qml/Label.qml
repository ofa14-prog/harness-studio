import QtQuick

Text {
    color: Theme.text
    font.family: Theme.font
    font.pixelSize: Theme.sizeBody
    font.letterSpacing: 0.15
    lineHeight: wrapMode === Text.NoWrap ? 1.0 : Theme.lineHeight
    renderType: Text.NativeRendering
    verticalAlignment: Text.AlignVCenter
    elide: Text.ElideRight
}
