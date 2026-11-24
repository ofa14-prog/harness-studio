import QtQuick

// One provider: its mark, key state, a key field, test and refresh.
Rectangle {
    id: card
    required property var modelData
    property string testMessage: ""
    property bool testOk: false
    property bool testing: false
    height: col.implicitHeight + 24
    radius: Theme.radiusLarge
    color: Theme.raised
    border.color: Theme.border
    Connections {
        target: studio
        function onProviderTested(provider, ok, message) {
            if (provider !== card.modelData.id) return
            card.testing = false; card.testOk = ok; card.testMessage = message
        }
    }
    Column {
        id: col
        x: 14; y: 12
        width: parent.width - 28
        spacing: 10
        Item {
            width: parent.width; height: 26
            Icon { id: mark; source: Theme.brand(card.modelData.icon); size: 18; anchors.verticalCenter: parent.verticalCenter }
            Label { x: 28; anchors.verticalCenter: parent.verticalCenter; text: card.modelData.name; font.weight: Theme.weightUi }
            Label {
                anchors.right: toggle.left; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter
                text: !card.modelData.needsKey ? qsTr("No key needed")
                    : card.modelData.hasKey ? (card.modelData.keySource === "env" ? qsTr("Key from %1").arg(card.modelData.env) : qsTr("Key saved"))
                    : qsTr("No key")
                color: card.modelData.hasKey || !card.modelData.needsKey ? Theme.success : Theme.textTertiary
                font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi
            }
            Toggle {
                id: toggle
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                width: 40
                checked: card.modelData.enabled
                onToggled: (v) => profiles.set("providers." + card.modelData.id + ".enabled", v)
            }
        }
        Row {
            visible: card.modelData.needsKey
            width: parent.width
            spacing: 8
            TextInput2 {
                id: keyField
                width: parent.width - saveBtn.width - 8
                echoMode: TextInput.Password
                placeholderText: card.modelData.maskedKey.length ? card.modelData.maskedKey : qsTr("Paste your %1 key").arg(card.modelData.name)
                onAccepted: saveBtn.clicked()
            }
            Button {
                id: saveBtn
                text: qsTr("Save")
                variant: "primary"
                enabled: keyField.text.trim().length > 0
                onClicked: { profiles.setApiKey(card.modelData.id, keyField.text); keyField.text = ""; card.testing = true; studio.testProvider(card.modelData.id) }
            }
        }
        Row {
            visible: card.modelData.id === "ollama"
            width: parent.width
            spacing: 8
            TextInput2 { width: parent.width; text: profiles.settings.providers.ollama.url; onEditingFinished: profiles.set("providers.ollama.url", text) }
        }
        Item {
            width: parent.width; height: 30
            Row {
                spacing: 6
                anchors.verticalCenter: parent.verticalCenter
                Button { text: qsTr("Test"); busy: card.testing; enabled: card.modelData.hasKey || !card.modelData.needsKey; onClicked: { card.testing = true; studio.testProvider(card.modelData.id) } }
                Button { text: qsTr("Refresh models"); variant: "ghost"; icon: Theme.icon("refresh"); enabled: card.modelData.hasKey || !card.modelData.needsKey; onClicked: studio.refreshModels(card.modelData.id) }
                Button { visible: card.modelData.keySource === "file"; text: qsTr("Remove key"); variant: "danger"; onClicked: profiles.clearApiKey(card.modelData.id) }
            }
            Label {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: card.testMessage.length ? card.testMessage : card.modelData.error.length ? card.modelData.error : card.modelData.modelCount > 0 ? qsTr("%1 models").arg(card.modelData.modelCount) : ""
                color: card.testMessage.length ? (card.testOk ? Theme.success : Theme.danger) : card.modelData.error.length ? Theme.danger : Theme.textTertiary
                font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi
                width: Math.min(implicitWidth, parent.width * 0.42)
            }
        }
        Label {
            text: "<a href=\"" + card.modelData.docs + "\">" + qsTr("API documentation") + "</a>"
            textFormat: Text.StyledText
            linkColor: Theme.textTertiary
            font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi
            onLinkActivated: (l) => shell.openExternally(l)
            MouseArea { anchors.fill: parent; acceptedButtons: Qt.NoButton; cursorShape: Qt.PointingHandCursor }
        }
    }
}
