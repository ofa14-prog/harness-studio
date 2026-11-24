import QtQuick

Row {
    property string text: ""
    spacing: 8
    Spinner { size: 15; anchors.verticalCenter: parent.verticalCenter }
    Label { visible: parent.text.length > 0; text: parent.text; color: Theme.textTertiary; anchors.verticalCenter: parent.verticalCenter }
}
