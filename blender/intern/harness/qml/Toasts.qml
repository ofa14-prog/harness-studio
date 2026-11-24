import QtQuick

// Feedback when something finishes or fails: slides in, stays a few seconds, slides out.
Column {
    id: root
    spacing: 8
    width: 340
    ListModel { id: toasts }
    function push(kind, text) {
        toasts.append({ kind: kind, text: text, stamp: Date.now() })
        if (toasts.count > 4) toasts.remove(0)
    }
    Connections { target: studio; function onNotify(kind, text) { root.push(kind, text) } }
    Connections {
        target: blender
        function onReportsChanged() {
            var r = blender.reports
            if (r.length) {
                var last = r[r.length - 1]
                if (last.type === "ERROR" || last.type === "WARNING") root.push(last.type === "ERROR" ? "error" : "warning", last.message)
            }
        }
    }
    move: Transition { NumberAnimation { properties: "y"; duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
    Repeater {
        model: toasts
        delegate: Rectangle {
            id: toast
            required property string kind
            required property string text
            required property int index
            width: root.width
            height: Math.max(44, msg.implicitHeight + 24)
            radius: Theme.radiusLarge
            color: Theme.overlay
            border.color: Theme.border
            opacity: 0
            x: 24
            Component.onCompleted: { opacity = 1; x = 0; life.start() }
            Behavior on opacity { NumberAnimation { duration: Theme.normal } }
            Behavior on x { NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
            Rectangle {
                x: 12; anchors.verticalCenter: parent.verticalCenter
                width: 8; height: 8; radius: 2
                color: toast.kind === "error" ? Theme.danger : toast.kind === "warning" ? Theme.warning : Theme.success
            }
            Label {
                id: msg
                x: 30; width: parent.width - 60
                anchors.verticalCenter: parent.verticalCenter
                text: toast.text
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
                maximumLineCount: 4
            }
            IconButton {
                anchors.right: parent.right; anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                icon: Theme.icon("close"); iconSize: 14; implicitWidth: 22; implicitHeight: 22
                onClicked: dismiss()
            }
            function dismiss() { opacity = 0; x = 24; removeTimer.start() }
            Timer { id: life; interval: toast.kind === "error" ? 9000 : 4500; onTriggered: toast.dismiss() }
            Timer { id: removeTimer; interval: Theme.normal + 20; onTriggered: if (toast.index >= 0 && toast.index < toasts.count) toasts.remove(toast.index) }
        }
    }
}
