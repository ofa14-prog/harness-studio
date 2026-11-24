import QtQuick

Page {
    id: page
    signal openSettings(string section)
    title: qsTr("Profile")
    section: "profile"
    sections: [ { key: "profile", text: qsTr("Profile"), icon: Theme.icon("account_circle") },
                { key: "usage", text: qsTr("Usage"), icon: Theme.icon("bar_chart") },
                { key: "profiles", text: qsTr("All profiles"), icon: Theme.icon("swap_horiz") } ]

    Column {
        visible: page.section === "profile"
        width: parent.width
        spacing: 16
        Row {
            spacing: 14
            Rectangle {
                width: 56; height: 56; radius: 28
                color: profiles.current.color || Theme.accent
                Label { anchors.centerIn: parent; text: (profiles.current.name || "?").charAt(0).toUpperCase(); color: "#0b0c0e"; font.pixelSize: 22; font.weight: Font.Bold }
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                Label { text: profiles.current.name || ""; font.pixelSize: Theme.sizeHeading; font.weight: Font.DemiBold }
                Label { text: profiles.current.email || qsTr("No email"); color: Theme.textTertiary }
            }
        }
        SettingRow { text: qsTr("Name")
            TextInput2 { width: parent.width; text: profiles.current.name || ""; onEditingFinished: profiles.updateProfile("name", text) } }
        SettingRow { text: qsTr("Email"); description: qsTr("Optional, only shown here.")
            TextInput2 { width: parent.width; text: profiles.current.email || ""; onEditingFinished: profiles.updateProfile("email", text) } }
        SettingRow { text: qsTr("Colour")
            Row { spacing: 8
                Repeater { model: ["#7C8CFF", "#3DD6B0", "#FFB547", "#FF6B8A", "#5EC2FF", "#B28CFF"]
                    delegate: Rectangle { required property string modelData; width: 22; height: 22; radius: 11; color: modelData
                        border.width: (profiles.current.color || "").toUpperCase() === modelData ? 2 : 0; border.color: Theme.text
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: profiles.updateProfile("color", modelData) } } } } }
        Row { spacing: 8
            Button { text: qsTr("AI providers"); icon: Theme.icon("key"); onClicked: page.openSettings("providers") }
            Button { text: qsTr("Settings"); icon: Theme.icon("settings"); onClicked: page.openSettings("general") } }
    }

    Column {
        visible: page.section === "usage"
        width: parent.width
        spacing: 10
        Label { text: qsTr("Usage on this computer"); font.pixelSize: Theme.sizeTitle; font.weight: Font.DemiBold }
        Label { width: parent.width; text: qsTr("Counted from the providers' answers; your provider's dashboard is the bill."); color: Theme.textTertiary; wrapMode: Text.WordWrap; elide: Text.ElideNone }
        Repeater {
            model: Object.keys(profiles.usage)
            delegate: Rectangle {
                required property string modelData
                readonly property var u: profiles.usage[modelData]
                width: parent.width; height: 50
                radius: Theme.radius; color: Theme.raised; border.color: Theme.border
                Icon { x: 12; anchors.verticalCenter: parent.verticalCenter; source: Theme.brand(studio.providerIcon(modelData)); size: 18 }
                Label { x: 40; anchors.verticalCenter: parent.verticalCenter; text: studio.providerName(modelData); font.weight: Font.Medium }
                Label { anchors.right: parent.right; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("%1 requests · %2 in · %3 out tokens").arg(Math.round(u.requests || 0)).arg(Math.round(u.input_tokens || 0)).arg(Math.round(u.output_tokens || 0))
                    color: Theme.textSecondary; font.features: { "tnum": 1 } }
            }
        }
        Label { visible: Object.keys(profiles.usage).length === 0; text: qsTr("Nothing used yet."); color: Theme.textTertiary }
    }

    Column {
        visible: page.section === "profiles"
        width: parent.width
        spacing: 10
        Label { text: qsTr("Profiles"); font.pixelSize: Theme.sizeTitle; font.weight: Font.DemiBold }
        Label { width: parent.width; text: qsTr("Each profile has its own keys, models, settings and conversations."); color: Theme.textTertiary; wrapMode: Text.WordWrap; elide: Text.ElideNone }
        Repeater {
            model: profiles.profiles
            delegate: Rectangle {
                required property var modelData
                readonly property bool current: modelData.id === profiles.currentId
                width: parent.width; height: 52
                radius: Theme.radius
                color: current ? Theme.selected : Theme.raised
                border.color: Theme.border
                Rectangle { x: 12; anchors.verticalCenter: parent.verticalCenter; width: 28; height: 28; radius: 14; color: modelData.color
                    Label { anchors.centerIn: parent; text: modelData.name.charAt(0).toUpperCase(); color: "#0b0c0e"; font.weight: Font.Bold; font.pixelSize: 12 } }
                Column { x: 52; anchors.verticalCenter: parent.verticalCenter
                    Label { text: modelData.name; font.weight: Font.Medium }
                    Label { text: modelData.email || ""; visible: text.length > 0; color: Theme.textTertiary; font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi } }
                Row { anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter; spacing: 6
                    Label { visible: parent.parent.current; text: qsTr("In use"); color: Theme.success; font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi; anchors.verticalCenter: parent.verticalCenter }
                    Button { visible: !parent.parent.current; text: qsTr("Switch"); onClicked: profiles.switchTo(modelData.id) }
                    Button { visible: profiles.profiles.length > 1; text: qsTr("Delete"); variant: "danger"; onClicked: profiles.remove(modelData.id) } }
            }
        }
        Row {
            spacing: 8
            TextInput2 { id: newName; width: 240; placeholderText: qsTr("New profile name") }
            Button { text: qsTr("Create profile"); variant: "primary"; enabled: newName.text.trim().length > 0; onClicked: { profiles.create(newName.text); newName.text = "" } }
        }
    }
}
