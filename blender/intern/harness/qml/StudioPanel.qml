import QtQuick

// Harness Studio: the agent, Create and the library, docked on the right.
Item {
    id: root
    signal openSettings(string section)
    property string tab: "agent"

    Item {
        id: header
        width: parent.width
        height: 56
        Row {
            x: 18
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Icon { source: Theme.icon("auto_awesome"); size: 16; anchors.verticalCenter: parent.verticalCenter }
            Label { text: qsTr("HARNESS STUDIO"); font.family: Theme.fontHero; font.pixelSize: 14; font.letterSpacing: 0.8; anchors.verticalCenter: parent.verticalCenter }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            IconButton {
                id: historyBtn
                visible: root.tab === "agent"
                icon: Theme.icon("history"); tooltip: qsTr("Conversations")
                onClicked: {
                    var items = [ { header: true, text: qsTr("Conversations") } ]
                    var ts = studio.threads
                    if (ts.length === 0) items.push({ text: qsTr("No saved conversations"), enabled: false })
                    for (var i = 0; i < Math.min(ts.length, 20); i++) {
                        items.push({ text: ts[i].title || qsTr("Untitled"), shortcut: ts[i].updated, checked: ts[i].id === studio.threadId,
                                     action: (function(id) { return function() { studio.openThread(id) } })(ts[i].id) })
                    }
                    if (ts.length) {
                        items.push({ separator: true })
                        items.push({ text: qsTr("Delete this conversation"), icon: Theme.icon("delete"), action: function() { studio.deleteThread(studio.threadId) } })
                    }
                    historyMenu.items = items
                    historyMenu.openFor(historyBtn, true)
                }
                PopupMenu { id: historyMenu; minWidth: 300 }
            }
            IconButton {
                visible: root.tab === "agent"
                icon: Theme.icon("add"); tooltip: qsTr("New conversation")
                onClicked: studio.newThread()
            }
            IconButton { icon: Theme.icon("tune"); tooltip: qsTr("Studio settings"); onClicked: root.openSettings("agent") }
        }
    }
    SegmentedBar {
        id: tabs
        anchors.top: header.bottom
        x: 16
        width: parent.width - 32
        barHeight: 40
        model: [ { key: "agent", text: qsTr("Agent"), icon: Theme.icon("chat") },
                 { key: "create", text: qsTr("Create"), icon: Theme.icon("auto_awesome") },
                 { key: "library", text: qsTr("Library"), icon: Theme.icon("grid_view") } ]
        current: root.tab
        onSelected: (k) => root.tab = k
    }

    Item {
        id: body
        anchors.top: tabs.bottom
        anchors.topMargin: 6
        anchors.bottom: parent.bottom
        width: parent.width
        AgentView {
            anchors.fill: parent
            visible: opacity > 0
            opacity: root.tab === "agent" ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.normal } }
            onOpenSettings: (s) => root.openSettings(s)
        }
        CreateView {
            anchors.fill: parent
            visible: opacity > 0
            opacity: root.tab === "create" ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.normal } }
            onOpenSettings: (s) => root.openSettings(s)
            onShowLibrary: root.tab = "library"
        }
        LibraryView {
            anchors.fill: parent
            visible: opacity > 0
            opacity: root.tab === "library" ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.normal } }
        }
    }
}
