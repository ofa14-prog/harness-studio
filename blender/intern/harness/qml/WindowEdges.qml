import QtQuick

// Resize handles for the frameless window.
Item {
    id: root
    readonly property int t: 5
    component Edge: MouseArea {
        property int edges: 0
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        onPressed: shell.startResize(edges)
    }
    Edge { x: 0; y: root.t; width: root.t; height: parent.height - 2 * root.t; edges: Qt.LeftEdge; cursorShape: Qt.SizeHorCursor }
    Edge { x: parent.width - root.t; y: root.t; width: root.t; height: parent.height - 2 * root.t; edges: Qt.RightEdge; cursorShape: Qt.SizeHorCursor }
    Edge { x: root.t; y: 0; width: parent.width - 2 * root.t; height: root.t; edges: Qt.TopEdge; cursorShape: Qt.SizeVerCursor }
    Edge { x: root.t; y: parent.height - root.t; width: parent.width - 2 * root.t; height: root.t; edges: Qt.BottomEdge; cursorShape: Qt.SizeVerCursor }
    Edge { x: 0; y: 0; width: root.t * 2; height: root.t * 2; edges: Qt.LeftEdge | Qt.TopEdge; cursorShape: Qt.SizeFDiagCursor }
    Edge { x: parent.width - root.t * 2; y: parent.height - root.t * 2; width: root.t * 2; height: root.t * 2; edges: Qt.RightEdge | Qt.BottomEdge; cursorShape: Qt.SizeFDiagCursor }
    Edge { x: parent.width - root.t * 2; y: 0; width: root.t * 2; height: root.t * 2; edges: Qt.RightEdge | Qt.TopEdge; cursorShape: Qt.SizeBDiagCursor }
    Edge { x: 0; y: parent.height - root.t * 2; width: root.t * 2; height: root.t * 2; edges: Qt.LeftEdge | Qt.BottomEdge; cursorShape: Qt.SizeBDiagCursor }
}
