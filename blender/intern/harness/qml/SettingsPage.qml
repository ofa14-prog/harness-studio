import QtQuick

Page {
    id: page
    title: qsTr("Settings")
    sections: [
        { key: "general", text: qsTr("General"), icon: Theme.icon("tune") },
        { key: "providers", text: qsTr("AI providers"), icon: Theme.icon("key") },
        { key: "agent", text: qsTr("Agent"), icon: Theme.icon("psychology") },
        { key: "create", text: qsTr("Create"), icon: Theme.icon("auto_awesome") },
        { key: "blender", text: qsTr("Blender"), icon: Theme.icon("view_in_ar") },
        { key: "about", text: qsTr("About"), icon: Theme.icon("info") } ]

    // General
    Column {
        visible: page.section === "general"
        width: parent.width
        spacing: 14
        Label { text: qsTr("General"); font.pixelSize: Theme.sizeTitle; font.weight: Font.DemiBold }
        Toggle { width: parent.width; text: qsTr("Reduce motion"); description: qsTr("Turn interface animations off."); checked: profiles.settings.interface.reduce_motion; onToggled: (v) => profiles.set("interface.reduce_motion", v) }
        Toggle { width: parent.width; text: qsTr("Carry on where I left off"); description: qsTr("At start, open the last project with every tab, view and editor as it was saved. Each project keeps its own agent conversation."); checked: profiles.settings.interface.reopen_last !== false; onToggled: (v) => { profiles.set("interface.reopen_last", v); blender.query("config", { reopen_last: v }) } }
        Toggle { width: parent.width; text: qsTr("Ask before quitting with unsaved changes"); description: qsTr("Blender asks to save when the window closes."); checked: profiles.settings.interface.confirm_quit; onToggled: (v) => profiles.set("interface.confirm_quit", v) }
        SettingRow {
            text: qsTr("Language of the studio")
            Dropdown { width: parent.width; options: [ { value: "en", text: "English" }, { value: "tr", text: "Türkçe" } ]; value: profiles.settings.interface.language; onPicked: (v) => profiles.set("interface.language", v) }
        }
    }

    // Providers
    Column {
        visible: page.section === "providers"
        width: parent.width
        spacing: 12
        Label { text: qsTr("AI providers"); font.pixelSize: Theme.sizeTitle; font.weight: Font.DemiBold }
        Label {
            width: parent.width
            text: qsTr("Keys are stored only on this computer, in a private file for this profile, never in project files. An environment variable, when set, takes precedence.")
            color: Theme.textTertiary; wrapMode: Text.WordWrap; elide: Text.ElideNone
        }
        Repeater {
            model: studio.providers
            delegate: ProviderCard { width: parent.width }
        }
    }

    // Agent
    Column {
        visible: page.section === "agent"
        width: parent.width
        spacing: 14
        Label { text: qsTr("Agent"); font.pixelSize: Theme.sizeTitle; font.weight: Font.DemiBold }
        SettingRow {
            text: qsTr("Model"); description: qsTr("Used for new messages.")
            ModelButton { }
        }
        SettingRow {
            text: qsTr("Creativity"); description: qsTr("Temperature, where the model supports it.")
            Slider2 { width: parent.width; from: 0; to: 1; value: profiles.settings.agent.temperature; onMoved: (v) => profiles.set("agent.temperature", v) }
        }
        SettingRow {
            text: qsTr("Most steps per request"); description: qsTr("The agent stops and asks before going further.")
            Slider2 { width: parent.width; from: 5; to: 120; integer: true; value: profiles.settings.agent.max_steps; onMoved: (v) => profiles.set("agent.max_steps", v) }
        }
        Toggle { width: parent.width; text: qsTr("Ask before paid generations"); description: qsTr("Image, video and 3D generations bill your provider account."); checked: profiles.settings.agent.approve_paid; onToggled: (v) => profiles.set("agent.approve_paid", v) }
        Toggle { width: parent.width; text: qsTr("Ask before running Python"); description: qsTr("Show the code and wait for your OK each time."); checked: profiles.settings.agent.approve_python; onToggled: (v) => profiles.set("agent.approve_python", v) }
        Toggle { width: parent.width; text: qsTr("Attach the viewport by default"); checked: profiles.settings.agent.attach_viewport; onToggled: (v) => profiles.set("agent.attach_viewport", v) }
    }

    // Create
    Column {
        visible: page.section === "create"
        width: parent.width
        spacing: 14
        Label { text: qsTr("Create"); font.pixelSize: Theme.sizeTitle; font.weight: Font.DemiBold }
        Toggle { width: parent.width; text: qsTr("Bring 3D results into the scene"); checked: profiles.settings.create.auto_import; onToggled: (v) => profiles.set("create.auto_import", v) }
        Label { width: parent.width; text: qsTr("Models are chosen in the Create tab; the list includes every active fal.ai model and refreshes with the provider."); color: Theme.textTertiary; wrapMode: Text.WordWrap; elide: Text.ElideNone }
        Button { text: qsTr("Refresh model lists"); icon: Theme.icon("refresh"); busy: studio.modelsLoading; onClicked: studio.refreshModels() }
    }

    // Blender
    Column {
        visible: page.section === "blender"
        width: parent.width
        spacing: 14
        Label { text: qsTr("Blender"); font.pixelSize: Theme.sizeTitle; font.weight: Font.DemiBold }
        Label { width: parent.width; text: qsTr("Blender's own preferences (input, keymap, themes, add-ons, system, file paths) open in their own window."); color: Theme.textTertiary; wrapMode: Text.WordWrap; elide: Text.ElideNone }
        Row {
            spacing: 8
            Button { text: qsTr("Open Blender preferences"); icon: Theme.icon("settings"); onClicked: blender.runOperator("screen.userpref_show") }
            Button { text: qsTr("Keymap"); icon: Theme.icon("keyboard"); onClicked: blender.runOperator("screen.userpref_show", { section: "KEYMAP" }) }
            Button { text: qsTr("Add-ons"); icon: Theme.icon("add_circle"); onClicked: blender.runOperator("screen.userpref_show", { section: "ADDONS" }) }
        }
    }

    // About
    Column {
        visible: page.section === "about"
        width: parent.width
        spacing: 10
        Row { spacing: 10
            Icon { source: "qrc:/harness/icons/logo.svg"; size: 28; anchors.verticalCenter: parent.verticalCenter }
            Label { text: "HARNESS STUDIO"; font.family: Theme.fontHero; font.pixelSize: Theme.sizeHeading; font.letterSpacing: 1.2; anchors.verticalCenter: parent.verticalCenter } }
        Label { text: qsTr("Built on Blender %1").arg(blender.version); color: Theme.textSecondary }
        Label { text: qsTr("Interface: Qt %1, Qt Quick, OpenGL; window system: GHOST Qt back-end").arg(Qt.application.version); color: Theme.textSecondary }
        Label { width: parent.width; text: qsTr("Blender is free software (GPL). Icons: Material Symbols (Apache 2.0), Simple Icons (CC0). Fonts: Instrument Sans, Cabin, Notable (OFL)."); color: Theme.textTertiary; wrapMode: Text.WordWrap; elide: Text.ElideNone }
    }
}
