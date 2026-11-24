import QtQuick

// Title bar: mark and name, Blender's menus, the open file, profile and settings, window
// buttons. Empty space drags the window; double-click maximises.
Rectangle {
    id: bar
    color: Theme.chrome
    signal settingsRequested()
    signal profileRequested()
    signal providersRequested()

    // Drag area under everything.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onPressed: shell.startMove()
        onDoubleClicked: shell.toggleMaximize()
    }

    Row {
        id: left
        x: 14
        height: parent.height
        spacing: 10
        Icon { source: "qrc:/harness/icons/logo.svg"; size: 20; anchors.verticalCenter: parent.verticalCenter }
        Label {
            text: "HARNESS STUDIO"
            font.family: Theme.fontHero
            font.pixelSize: 13
            font.letterSpacing: 1.2
            anchors.verticalCenter: parent.verticalCenter
        }
        Item { width: 10; height: 1 }
        Repeater {
            model: [ { t: qsTr("File"), m: "TOPBAR_MT_file" }, { t: qsTr("Edit"), m: "TOPBAR_MT_edit" },
                     { t: qsTr("Render"), m: "TOPBAR_MT_render" }, { t: qsTr("Window"), m: "TOPBAR_MT_window" },
                     { t: qsTr("Help"), m: "TOPBAR_MT_help" } ]
            delegate: MenuTitle {
                required property var modelData
                text: modelData.t
                menuName: modelData.m
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    // The open file (or session) and its state.
    Row {
        id: project
        anchors.left: left.right
        anchors.leftMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8
        Rectangle { width: 1; height: 16; color: Theme.border; anchors.verticalCenter: parent.verticalCenter }
        Label {
            text: blender.projectName.length ? blender.projectName : qsTr("Untitled")
            color: Theme.textSecondary
            font.weight: Font.Medium
            anchors.verticalCenter: parent.verticalCenter
            ToolTipLite { text: blender.projectPath; shown: pma.containsMouse && blender.projectPath.length > 0 }
            MouseArea { id: pma; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
        }
        Rectangle {
            visible: blender.modified
            width: 6; height: 6; radius: 3
            color: Theme.warning
            anchors.verticalCenter: parent.verticalCenter
            ToolTipLite { text: qsTr("Unsaved changes"); shown: dma.containsMouse }
            MouseArea { id: dma; anchors.fill: parent; anchors.margins: -4; hoverEnabled: true; acceptedButtons: Qt.NoButton }
        }
        Label {
            visible: blender.jobText.length > 0
            text: blender.jobText
            color: Theme.info
            font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Row {
        anchors.right: parent.right
        height: parent.height
        spacing: 4
        // Profile
        Item {
            width: 30; height: parent.height
            Rectangle {
                id: avatar
                anchors.centerIn: parent
                width: 24; height: 24; radius: 12
                color: profiles.current.color || Theme.accent
                border.width: ama.containsMouse || profileMenu.opened ? 2 : 0
                border.color: Theme.text
                Label {
                    anchors.centerIn: parent
                    text: (profiles.current.name || "?").charAt(0).toUpperCase()
                    color: "#0b0c0e"
                    font.pixelSize: 11
                    font.weight: Font.Bold
                }
            }
            MouseArea {
                id: ama
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    var items = [ { header: true, text: profiles.current.name || "" } ]
                    for (var i = 0; i < profiles.profiles.length; i++) {
                        var p = profiles.profiles[i]
                        if (p.id === profiles.currentId) continue
                        items.push({ text: qsTr("Switch to %1").arg(p.name), icon: Theme.icon("swap_horiz"),
                                     action: (function(id) { return function() { profiles.switchTo(id) } })(p.id) })
                    }
                    items.push({ separator: true })
                    items.push({ text: qsTr("Profile"), icon: Theme.icon("account_circle"), action: function() { bar.profileRequested() } })
                    items.push({ text: qsTr("AI providers"), icon: Theme.icon("key"), action: function() { bar.providersRequested() } })
                    items.push({ text: qsTr("Settings"), icon: Theme.icon("settings"), shortcut: "Ctrl ,", action: function() { bar.settingsRequested() } })
                    profileMenu.items = items
                    profileMenu.openFor(avatar, true)
                }
            }
            PopupMenu { id: profileMenu; minWidth: 230 }
        }
        IconButton {
            icon: Theme.icon("settings")
            tooltip: qsTr("Settings")
            anchors.verticalCenter: parent.verticalCenter
            onClicked: bar.settingsRequested()
        }
        Item { width: 10; height: 1 }
        WindowButton { icon: Theme.icon("remove"); onClicked: shell.minimize() }
        WindowButton { icon: shell.maximized ? Theme.icon("filter_none") : Theme.icon("crop_square"); onClicked: shell.toggleMaximize() }
        WindowButton { icon: Theme.icon("close"); danger: true; onClicked: shell.close() }
    }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }

    Shortcut { sequence: "Ctrl+,"; context: Qt.ApplicationShortcut; onActivated: bar.settingsRequested() }
}
