import QtQuick
import QtQuick.Dialogs
import QtQuick.Controls.Basic as QC

// Create, laid out like a generation studio: the mode on top, the feed of what was made in the
// middle, and the composer docked at the bottom. The composer follows Higgsfield's anatomy
// (verified in its page source): an upload button and a two-line prompt, a row of setting chips
// (model, duration, aspect ratio, resolution, audio... as "5s", "16:9", "On"), and a large
// Create button beside them. Harness adds its scene links as chips too: the reference (viewport,
// camera or file, as image, depth, normal, edges or mask), the 3D camera path, an area of the
// viewport, @ references, presets and where 3D results go.
Item {
    id: root
    signal openSettings(string section)
    signal showLibrary()

    readonly property var modes: [ { k: "image", t: qsTr("Image"), i: "image" },
                                   { k: "video", t: qsTr("Video"), i: "movie" },
                                   { k: "3d", t: qsTr("3D model"), i: "view_in_ar" } ]
    readonly property var currentModel: studio.createModelInfo
    readonly property var opts: studio.createOptions
    TemplateData { id: templates }
    property var template: null
    Connections {
        target: studio
        function onCreateChanged() {
            if (root.template && root.template.cat) {
                var cats = templates.categories[studio.createKind], ok = false
                for (var i = 0; i < cats.length; i++) if (cats[i].k === root.template.cat) ok = true
                if (!ok) root.template = null
            }
        }
    }
    function create() {
        if (!studio.createModel.length || studio.creating) return
        studio.setCreateOption("refs", mention.activeRefs(prompt.text))
        var text = mention.plainText(prompt.text).trim()
        if (studio.createKind === "image" && opts.applyMaterial)
            text += (text.length ? ", " : "") + "seamless tileable PBR texture, flat even lighting, no shadows, straight-on view"
        if (root.template) text = (text.length ? text + ". " : "") + root.template.prompt
        studio.create(text)
    }
    // HARNESS_KIND=image|video|3d: start in that mode (for checks).
    Component.onCompleted: { var k = shell.env("HARNESS_KIND"); if (k.length) studio.createKind = k }
    // The preset window doesn't block the rest of the app; a press on its own button while it
    // is open closes it (the press that closed it must not open it again).
    property real presetsClosedAt: 0
    function togglePresets(anchor) {
        if (presets.opened) { presets.close(); return }
        if (Date.now() - presetsClosedAt < 300) return
        presets.openFor(anchor, composer.width)
    }
    function setReference(url) { if (shell.isImage(url)) studio.createFile = url }

    // Results of this mode, newest first.
    readonly property var feed: {
        var out = [], j = studio.jobs
        for (var i = 0; i < j.length; i++) if (j[i].kind === studio.createKind) out.push(j[i])
        return out
    }
    function jobMenu(m, anchor, x, y) {
        var items = []
        if (m.status === "done") {
            if (m.kind === "3d") {
                items.push({ text: qsTr("Place at the 3D cursor"), icon: Theme.icon("view_in_ar"), action: function() { studio.placeJob(m.id, "cursor") } })
                items.push({ text: qsTr("Place on the active object"), icon: Theme.icon("center_focus_strong"), enabled: blender.activeObject.length > 0,
                             action: function() { studio.placeJob(m.id, "object", blender.activeObject) } })
            }
            if (m.kind === "image" || m.kind === "video") {
                items.push({ text: qsTr("Add as a plane"), icon: Theme.icon("image"), action: function() { studio.useJob(m.id, "object") } })
                items.push({ text: qsTr("Use as texture on the selection"), icon: Theme.icon("texture"), action: function() { studio.useJob(m.id, "texture") } })
            }
            if (m.kind === "image") {
                items.push({ text: qsTr("Dress the selection as a PBR material"), icon: Theme.icon("palette"), enabled: blender.selectedCount > 0,
                             action: function() { studio.applyJobMaterial(m.id, []) } })
                items.push({ text: qsTr("Match the scene lighting"), icon: Theme.icon("bolt"), action: function() { studio.matchJobLighting(m.id) } })
                items.push({ text: qsTr("Use as the next reference"), icon: Theme.icon("upload_file"), action: function() { studio.createFile = m.url } })
            }
            items.unshift({ text: qsTr("Open"), icon: Theme.icon("open_in_new"), action: function() { Viewer.open(m) } })
            items.push({ text: qsTr("Reuse the prompt"), icon: Theme.icon("replay"), action: function() { prompt.text = m.prompt; prompt.forceActiveFocus() } })
            items.push({ text: qsTr("Show in folder"), icon: Theme.icon("folder_open"), action: function() { studio.revealJob(m.id) } })
            items.push({ separator: true })
        }
        if (m.status !== "done" && m.status !== "failed") items.push({ text: qsTr("Cancel"), icon: Theme.icon("stop"), action: function() { studio.cancelJob(m.id) } })
        else items.push({ text: qsTr("Remove from the list"), icon: Theme.icon("delete"), action: function() { studio.removeJob(m.id) } })
        jobMenuPopup.items = items
        jobMenuPopup.openAt(anchor, x, y)
    }

    // ---- Mode ----------------------------------------------------------------------------
    SegmentedBar {
        id: modeBar
        x: 16; y: 10
        width: parent.width - 32
        barHeight: 36
        model: [ { key: "image", text: qsTr("Image"), icon: Theme.icon("image") },
                 { key: "video", text: qsTr("Video"), icon: Theme.icon("movie") },
                 { key: "3d", text: qsTr("3D model"), icon: Theme.icon("view_in_ar") } ]
        current: studio.createKind
        onSelected: (k) => studio.createKind = k
    }

    // ---- Feed ----------------------------------------------------------------------------
    Flickable {
        id: feedView
        anchors.top: modeBar.bottom
        anchors.topMargin: 14
        anchors.bottom: composer.top
        anchors.bottomMargin: 14
        width: parent.width
        contentHeight: feedCol.implicitHeight + 8
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: feedCol
            x: 16
            width: parent.width - 32
            spacing: 22

            // Nothing made yet in this mode: what the studio does, and the presets to start from.
            Column {
                visible: root.feed.length === 0
                width: parent.width
                spacing: 14
                Item { width: 1; height: 6 }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: studio.createKind === "video" ? qsTr("TURN YOUR SCENE INTO A SHOT")
                        : studio.createKind === "3d" ? qsTr("MAKE A MODEL FOR YOUR SCENE") : qsTr("PAINT OVER YOUR SCENE")
                    color: Theme.text
                    font.family: Theme.fontHero; font.pixelSize: Theme.sizeHero; lineHeight: 1.1
                    renderType: Text.NativeRendering
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    lineHeight: Theme.lineHeight
                    text: studio.createKind === "video"
                          ? qsTr("Start from the viewport or the 3D camera path, pick a camera move, and describe the shot.")
                          : studio.createKind === "3d" ? qsTr("Describe an object, or start from the viewport. It lands at the 3D cursor or on an @ reference.")
                          : qsTr("Use the viewport as image, depth, normals, edges or a mask, @ reference objects, or repaint an area.")
                    color: Theme.textSecondary
                    font.family: Theme.font; font.pixelSize: Theme.sizeBody
                    renderType: Text.NativeRendering
                }
            }

            // Presets: a section that folds away; whether it's open is kept in the profile.
            Column {
                id: presetSection
                readonly property bool open: profiles.settings.create ? profiles.settings.create.show_presets !== false : true
                width: parent.width
                spacing: open ? 10 : 0
                Item {
                    width: parent.width
                    height: 28
                    Rectangle { anchors.fill: parent; anchors.margins: -4; radius: 8; color: hdr.containsMouse ? Theme.hover : "transparent" }
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        Icon { source: Theme.icon("expand_more"); size: 16; tone: 0.8; anchors.verticalCenter: parent.verticalCenter
                               rotation: presetSection.open ? 0 : -90
                               Behavior on rotation { NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } } }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Presets"); color: Theme.textSecondary; renderType: Text.NativeRendering
                               font.family: Theme.fontSecondary; font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi; font.capitalization: Font.AllUppercase; font.letterSpacing: 0.6 }
                        Text { anchors.verticalCenter: parent.verticalCenter; visible: root.template !== null; text: "·  " + (root.template ? root.template.name : "")
                               color: Theme.create; renderType: Text.NativeRendering; font.family: Theme.font; font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi }
                    }
                    MouseArea { id: hdr; anchors.fill: parent; anchors.rightMargin: allBtn.width + 8; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: profiles.set("create.show_presets", !presetSection.open) }
                    Chip {
                        id: allBtn
                        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                        height: 26
                        iconSource: Theme.icon("grid_view")
                        text: qsTr("Show all")
                        onClicked: root.togglePresets(allBtn)
                    }
                }
                ScrollRow {
                    width: parent.width
                    implicitHeight: presetSection.open ? 150 : 0
                    height: implicitHeight
                    opacity: presetSection.open ? 1 : 0
                    visible: height > 0
                    clip: true
                    Behavior on implicitHeight { NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
                    Behavior on opacity { NumberAnimation { duration: Theme.normal } }
                    spacing: 10
                    fadeColor: Theme.panel
                    Repeater {
                        model: {
                            var cats = templates.categories[studio.createKind], out = []
                            for (var i = 0; i < cats.length; i++) out = out.concat(templates.forCategory(cats[i].k).slice(0, 10))
                            return out
                        }
                        delegate: TemplateTile {
                            required property var modelData
                            template: modelData
                            width: 172
                            chosen: root.template !== null && root.template.id === modelData.id
                            onPicked: root.template = chosen ? null : modelData
                        }
                    }
                }
            }

            // What was made, newest first (kept across restarts).
            Text {
                visible: root.feed.length > 0
                text: qsTr("Results")
                color: Theme.textTertiary; renderType: Text.NativeRendering
                font.family: Theme.fontSecondary; font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi; font.capitalization: Font.AllUppercase; font.letterSpacing: 0.6
            }
            Grid {
                id: results
                visible: root.feed.length > 0
                width: parent.width
                columns: width > 420 ? 3 : 2
                columnSpacing: 10
                rowSpacing: 10
                Repeater {
                    model: root.feed
                    delegate: Rectangle {
                        id: card
                        required property var modelData
                        readonly property bool active: modelData.status !== "done" && modelData.status !== "failed"
                        width: (results.width - (results.columns - 1) * results.columnSpacing) / results.columns
                        height: width
                        radius: 12
                        color: Theme.raised
                        border.color: cm2.containsMouse ? Theme.borderStrong : Theme.border
                        clip: true
                        Image {
                            anchors.fill: parent; anchors.margins: 1
                            visible: card.modelData.kind === "image" && card.modelData.status === "done"
                            source: visible ? card.modelData.url : ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true; mipmap: true
                            sourceSize.width: Math.ceil(card.width * Theme.dpr * 1.5)
                            opacity: status === Image.Ready ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: Theme.normal } }
                        }
                        Icon {
                            anchors.centerIn: parent
                            visible: card.modelData.status === "done" && card.modelData.kind !== "image"
                            source: Theme.icon(card.modelData.kind === "video" ? "movie" : "view_in_ar")
                            size: 30; tone: 0.6
                        }
                        Column {
                            visible: card.active
                            anchors.centerIn: parent
                            width: parent.width - 24
                            spacing: 10
                            CubeLoader { anchors.horizontalCenter: parent.horizontalCenter; size: Math.min(56, card.width * 0.36) }
                            Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                                   text: card.modelData.message || qsTr("Queued"); color: Theme.textSecondary
                                   font.family: Theme.fontSecondary; font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi; renderType: Text.NativeRendering }
                            Rectangle {
                                width: parent.width; height: 3; radius: 1; color: Theme.border
                                Rectangle { width: parent.width * card.modelData.progress; height: parent.height; radius: 1; color: Theme.create
                                            Behavior on width { NumberAnimation { duration: Theme.slow } } }
                            }
                        }
                        Text {
                            visible: card.modelData.status === "failed"
                            anchors.centerIn: parent
                            width: parent.width - 20
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap; maximumLineCount: 5; elide: Text.ElideRight
                            text: card.modelData.message
                            color: Theme.danger
                            font.family: Theme.font; font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi; renderType: Text.NativeRendering
                        }
                        // Prompt over the bottom edge on hover.
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width; height: 34
                            visible: cm2.containsMouse && card.modelData.status === "done"
                            color: Qt.rgba(0.04, 0.04, 0.04, 0.78)
                            Text { x: 10; width: parent.width - 20; anchors.verticalCenter: parent.verticalCenter; elide: Text.ElideRight
                                   text: card.modelData.prompt || card.modelData.title; color: Theme.text
                                   font.family: Theme.font; font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi; renderType: Text.NativeRendering }
                        }
                        MouseArea {
                            id: cm2
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            cursorShape: Qt.PointingHandCursor
                            onClicked: (mouse) => {
                                if (mouse.button === Qt.LeftButton && card.modelData.status === "done") Viewer.open(card.modelData)
                                else root.jobMenu(card.modelData, card, mouse.x, mouse.y)
                            }
                        }
                    }
                }
            }

            // Fine settings that don't fit a chip.
            Column {
                readonly property var sliders: {
                    var out = [], ps = studio.createParams
                    for (var i = 0; i < ps.length; i++) if (ps[i].kind === "slider") out.push(ps[i])
                    return out
                }
                visible: sliders.length > 0 || studio.createKind === "3d"
                width: parent.width
                spacing: 10
                Text { text: qsTr("Fine settings"); color: Theme.textTertiary; renderType: Text.NativeRendering
                       font.family: Theme.fontSecondary; font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi; font.capitalization: Font.AllUppercase; font.letterSpacing: 0.6 }
                Repeater {
                    model: parent.sliders
                    delegate: Slider2 {
                        required property var modelData
                        width: feedCol.width
                        label: modelData.title
                        from: modelData.min || 0; to: modelData.max || 1
                        value: Number(modelData.value)
                        integer: modelData.integer === true
                        onMoved: (v) => studio.setCreateParam(modelData.key, v)
                    }
                }
                Toggle {
                    visible: studio.createKind === "3d"
                    width: parent.width
                    text: qsTr("Bring 3D results into the scene")
                    checked: profiles.settings.create ? profiles.settings.create.auto_import : true
                    onToggled: (v) => profiles.set("create.auto_import", v)
                }
            }
        }
    }

    // ---- Composer ------------------------------------------------------------------------
    Item {
        id: composer
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        x: 16
        width: parent.width - 32
        height: card.height
        readonly property bool wide: width >= 420

        Rectangle {   // soft halo while typing
            anchors.fill: card
            anchors.margins: -4
            radius: card.radius + 4
            color: "transparent"
            border.width: 4
            border.color: Qt.rgba(1, 1, 1, prompt.activeFocus ? 0.045 : 0)
            Behavior on border.color { ColorAnimation { duration: Theme.normal } }
        }
        Rectangle {
            id: card
            width: parent.width
            height: inner.implicitHeight + 32
            radius: 16
            color: Theme.raised
            border.color: prompt.activeFocus ? Theme.borderStrong : Theme.border
            Behavior on border.color { ColorAnimation { duration: Theme.normal } }
            // A cool sheen across the card, like a lit surface.
            gradient: Gradient {
                orientation: Gradient.Vertical
                GradientStop { position: 0.0; color: Qt.tint(Theme.raised, Qt.rgba(0.62, 0.8, 1.0, 0.05)) }
                GradientStop { position: 1.0; color: Theme.raised }
            }

            Column {
                id: inner
                x: 16; y: 16
                width: parent.width - 32
                spacing: 12

                AttachmentStrip {
                    width: parent.width
                    thumbHeight: 60
                    urls: {
                        var u = []
                        if (studio.createSource === "file" && studio.createFile.length) u.push(shell.fileUrl(studio.createFile))
                        if (studio.createKind === "video" && root.opts.endFile) u.push(root.opts.endFile)
                        return u
                    }
                    onRemoved: (i) => {
                        var hasStart = studio.createSource === "file" && studio.createFile.length > 0
                        if (i === 0 && hasStart) { studio.createFile = ""; studio.createSource = "viewport" }
                        else studio.setCreateOption("endFile", "")
                    }
                }
                RefChips { width: parent.width; mention: mention }

                Grid {
                    id: body
                    width: parent.width
                    columns: composer.wide ? 2 : 1
                    columnSpacing: 12
                    rowSpacing: 12

                    // Prompt over the chips.
                    Column {
                        width: composer.wide ? body.width - createButton.width - body.columnSpacing : body.width
                        spacing: 10
                        Row {
                            width: parent.width
                            spacing: 10
                            Rectangle {   // upload
                                id: upload
                                width: 32; height: 32
                                radius: 10
                                color: upm.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
                                border.color: Qt.rgba(0.85, 0.85, 0.85, 0.06)
                                Behavior on color { ColorAnimation { duration: Theme.fast } }
                                Icon { anchors.centerIn: parent; source: Theme.icon("add"); size: 16; tone: 1 }
                                MouseArea {
                                    id: upm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (studio.createKind === "video" && studio.createTakesEndFrame) {
                                            uploadMenu.items = [
                                                { text: qsTr("Start frame…"), icon: Theme.icon("image"), action: function() { fileDialog.open() } },
                                                { text: qsTr("End frame…"), icon: Theme.icon("image"), action: function() { endDialog.open() } } ]
                                            uploadMenu.openFor(upload, false)
                                        }
                                        else fileDialog.open()
                                    }
                                }
                                ToolTipLite { text: qsTr("Add a reference image (or paste / drop one)"); shown: upm.containsMouse }
                                PopupMenu { id: uploadMenu; minWidth: 200 }
                            }
                            Flickable {
                                id: promptScroll
                                width: parent.width - upload.width - parent.spacing
                                height: Math.min(140, Math.max(48, prompt.implicitHeight))
                                contentHeight: prompt.implicitHeight
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds
                                QC.TextArea {
                                    id: prompt
                                    width: promptScroll.width
                                    topPadding: 6; bottomPadding: 4; leftPadding: 0; rightPadding: 0
                                    wrapMode: TextEdit.Wrap
                                    color: Theme.text
                                    selectionColor: Theme.accentSoft
                                    selectedTextColor: Theme.text
                                    font.family: Theme.font
                                    font.pixelSize: 15
                                    renderType: Text.NativeRendering
                                    background: null
                                    placeholderText: studio.createKind === "video" ? qsTr("Describe the shot — @ to reference the scene")
                                        : studio.createKind === "3d" ? qsTr("Describe the object — @ to place it on one") : qsTr("Describe any visual idea — @ to reference the scene")
                                    placeholderTextColor: Theme.textTertiary
                                    Keys.onPressed: (e) => {
                                        if (mention.handleKey(e)) { e.accepted = true; return }
                                        if (e.matches(StandardKey.Paste)) {
                                            var img = shell.clipboardImage()
                                            if (img.length) { studio.createFile = img; e.accepted = true; return }
                                        }
                                        if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter) && (e.modifiers & Qt.ControlModifier)) { root.create(); e.accepted = true }
                                        else if (e.key === Qt.Key_Escape) { prompt.focus = false; e.accepted = true }
                                    }
                                }
                            }
                        }

                        // Setting chips: a single row that scrolls sideways.
                        ScrollRow {
                            id: chipScroll
                            width: parent.width
                            spacing: 6
                            fadeColor: Theme.raised
                                Chip {
                                    id: modelChip
                                    iconSource: Theme.brand(root.currentModel.icon || "")
                                    text: root.currentModel.name || qsTr("Model")
                                    opened: modelPicker.opened
                                    onClicked: modelPicker.openFor(modelChip, root)
                                }
                                Repeater {
                                    model: studio.createParams
                                    delegate: Chip {
                                        id: pc
                                        required property var modelData
                                        visible: modelData.kind === "choice" || modelData.kind === "toggle"
                                        width: visible ? implicitWidth : 0
                                        readonly property string k: modelData.key.toLowerCase()
                                        iconSource: Theme.icon(k.indexOf("duration") >= 0 || k.indexOf("length") >= 0 ? "schedule"
                                                    : k.indexOf("aspect") >= 0 || k.indexOf("ratio") >= 0 || k.indexOf("size") >= 0 ? "crop_square"
                                                    : k.indexOf("resolution") >= 0 || k.indexOf("quality") >= 0 ? "landscape"
                                                    : k.indexOf("audio") >= 0 || k.indexOf("sound") >= 0 ? "play_arrow"
                                                    : k.indexOf("num") >= 0 || k.indexOf("count") >= 0 || k.indexOf("batch") >= 0 ? "filter_none" : "tune")
                                        text: modelData.kind === "toggle" ? (modelData.value === true ? qsTr("On") : qsTr("Off"))
                                              : (k.indexOf("duration") >= 0 && /^\d+(\.\d+)?$/.test(String(modelData.value)) ? modelData.value + "s" : String(modelData.value))
                                        checked: modelData.kind === "toggle" && modelData.value === true
                                        tooltip: modelData.title
                                        opened: pmenu.opened
                                        onClicked: {
                                            if (modelData.kind === "toggle") { studio.setCreateParam(modelData.key, !(modelData.value === true)); return }
                                            var items = [ { header: true, text: modelData.title } ], opts = modelData.options || []
                                            for (var i = 0; i < opts.length; i++)
                                                items.push({ text: (k.indexOf("duration") >= 0 && /^\d+(\.\d+)?$/.test(opts[i]) ? opts[i] + "s" : opts[i]), checked: String(opts[i]) === String(modelData.value),
                                                             action: (function(v) { return function() { studio.setCreateParam(modelData.key, v) } })(opts[i]) })
                                            pmenu.items = items
                                            pmenu.toggleFor(pc, false)
                                        }
                                        PopupMenu { id: pmenu; minWidth: 160 }
                                    }
                                }
                                Chip {
                                    id: presetChip
                                    iconSource: Theme.icon("movie")
                                    text: root.template ? root.template.name : qsTr("Preset")
                                    checked: root.template !== null
                                    accent: Theme.create
                                    opened: presets.opened
                                    onClicked: root.togglePresets(presetChip)
                                }
                                Chip {
                                    id: refChip
                                    visible: studio.createAcceptsImage
                                    width: visible ? implicitWidth : 0
                                    readonly property var passNames: ({ beauty: "", depth: qsTr(" · Depth"), normal: qsTr(" · Normal"), edges: qsTr(" · Edges"), mask: qsTr(" · Mask") })
                                    iconSource: Theme.icon(studio.createSource === "camera" ? "photo_camera" : studio.createSource === "file" ? "upload_file"
                                                           : studio.createSource === "none" ? "close" : "center_focus_strong")
                                    text: (studio.createSource === "camera" ? qsTr("Camera") : studio.createSource === "file" ? qsTr("File")
                                           : studio.createSource === "none" ? qsTr("No reference") : qsTr("Viewport"))
                                          + ((studio.createSource === "viewport" || studio.createSource === "camera") ? (passNames[root.opts.pass || "beauty"] || "") : "")
                                    tooltip: qsTr("What of the scene goes with the prompt")
                                    opened: refMenu.opened
                                    onClicked: {
                                        var p = root.opts.pass || "beauty", s = studio.createSource
                                        var items = [ { header: true, text: qsTr("Reference") },
                                            { text: qsTr("Viewport"), icon: Theme.icon("center_focus_strong"), checked: s === "viewport", action: function() { studio.createSource = "viewport" } },
                                            { text: qsTr("Scene camera"), icon: Theme.icon("photo_camera"), checked: s === "camera", action: function() { studio.createSource = "camera" } },
                                            { text: qsTr("Image file…"), icon: Theme.icon("upload_file"), checked: s === "file", action: function() { fileDialog.open() } },
                                            { text: qsTr("None"), icon: Theme.icon("close"), checked: s === "none", action: function() { studio.createSource = "none" } } ]
                                        if (s === "viewport" || s === "camera") {
                                            items.push({ header: true, text: qsTr("Send it as") })
                                            var passes = [ ["beauty", qsTr("Image"), "image"], ["depth", qsTr("Depth map (near is white)"), "landscape"],
                                                           ["normal", qsTr("Normal map"), "texture"], ["edges", qsTr("Edge lines"), "edit"],
                                                           ["mask", qsTr("Mask of @ references / selection"), "crop_square"] ]
                                            for (var i = 0; i < passes.length; i++)
                                                items.push({ text: passes[i][1], icon: Theme.icon(passes[i][2]), checked: p === passes[i][0],
                                                             action: (function(v) { return function() { studio.setCreateOption("pass", v) } })(passes[i][0]) })
                                        }
                                        refMenu.items = items
                                        refMenu.toggleFor(refChip, false)
                                    }
                                }
                                Chip {   // video: follow the 3D camera path
                                    visible: studio.createKind === "video"
                                    width: visible ? implicitWidth : 0
                                    iconSource: Theme.icon("photo_camera")
                                    text: qsTr("Camera path")
                                    checked: root.opts.followCamera === true
                                    tooltip: qsTr("Start and end on the scene camera's first and last frames and follow its move")
                                    onClicked: studio.setCreateOption("followCamera", !(root.opts.followCamera === true))
                                }
                                Chip {   // image: repaint an area of the viewport
                                    visible: studio.createKind === "image"
                                    width: visible ? implicitWidth : 0
                                    iconSource: Theme.icon("crop_square")
                                    text: root.opts.region && blender.regionRect.length ? qsTr("Area picked") : qsTr("Area")
                                    checked: root.opts.region === true
                                    tooltip: root.opts.region ? qsTr("Click to stop editing an area") : qsTr("Drag a box over the viewport to repaint only that area")
                                    onClicked: root.opts.region ? studio.setCreateOption("region", false) : studio.pickRegion()
                                }
                                Chip {   // image: dress @ references
                                    visible: studio.createKind === "image" && mention.refs.length > 0
                                    width: visible ? implicitWidth : 0
                                    iconSource: Theme.icon("palette")
                                    text: qsTr("As material")
                                    checked: root.opts.applyMaterial === true
                                    tooltip: qsTr("Dress the @ references in the result as a PBR material")
                                    onClicked: studio.setCreateOption("applyMaterial", !(root.opts.applyMaterial === true))
                                }
                                Chip {   // 3D: where it lands
                                    id: placeChip
                                    visible: studio.createKind === "3d"
                                    width: visible ? implicitWidth : 0
                                    iconSource: Theme.icon("view_in_ar")
                                    text: root.opts.place === "object" ? qsTr("On @ reference") : root.opts.place === "origin" ? qsTr("At origin") : qsTr("At 3D cursor")
                                    tooltip: qsTr("Where the model is placed")
                                    opened: placeMenu.opened
                                    onClicked: {
                                        var pl = root.opts.place || "cursor"
                                        placeMenu.items = [ { header: true, text: qsTr("Place the model") },
                                            { text: qsTr("At the 3D cursor"), checked: pl === "cursor", action: function() { studio.setCreateOption("place", "cursor") } },
                                            { text: qsTr("On the @ reference"), checked: pl === "object", action: function() { studio.setCreateOption("place", "object") } },
                                            { text: qsTr("At the origin"), checked: pl === "origin", action: function() { studio.setCreateOption("place", "origin") } } ]
                                        placeMenu.toggleFor(placeChip, false)
                                    }
                                }
                        }
                    }

                    CreateButton {
                        id: createButton
                        width: composer.wide ? 120 : body.width
                        height: composer.wide ? Math.max(84, body.children[0].height) : 52
                        tall: composer.wide
                        running: studio.creating
                        progress: {
                            var j = studio.jobs
                            for (var i = 0; i < j.length; i++) if (j[i].status !== "done" && j[i].status !== "failed") return j[i].progress
                            return 0
                        }
                        enabled: studio.createModel.length > 0
                        onClicked: root.create()
                    }
                }
            }
        }
    }

    MentionPopup { id: mention; editor: prompt }
    CreateModelPicker {
        id: modelPicker
        models: studio.createModels
        current: studio.createModel
        currentFamily: root.currentModel.family || ""
        onPicked: (id) => studio.createModel = id
    }
    PopupMenu { id: refMenu; minWidth: 260 }
    PopupMenu { id: placeMenu; minWidth: 220 }
    PopupMenu { id: jobMenuPopup; minWidth: 250 }
    PresetPicker {
        id: presets
        templates: templates
        kind: studio.createKind
        chosen: root.template
        onPicked: (t) => root.template = t
        onClosed: root.presetsClosedAt = Date.now()
    }

    DropArea {
        anchors.fill: composer
        keys: ["text/uri-list"]
        onEntered: (d) => d.accept(Qt.CopyAction)
        onDropped: (d) => {
            for (var i = 0; i < d.urls.length; i++) if (shell.isImage(String(d.urls[i]))) { studio.createFile = String(d.urls[i]); break }
            d.acceptProposedAction()
        }
        Rectangle { anchors.fill: parent; radius: 16; visible: parent.containsDrag; color: Theme.accentSoft; border.color: Theme.accent }
    }
    FileDialog {
        id: fileDialog
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)")]
        onAccepted: studio.createFile = selectedFile
    }
    FileDialog {
        id: endDialog
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)")]
        onAccepted: studio.setCreateOption("endFile", String(selectedFile))
    }
}
