import QtQuick

// One template: a small animated storyboard of what it does (camera move or effect) drawn
// with shapes in the brand palette, and its name. Plays while hovered or chosen.
Item {
    id: root
    required property var template
    property bool chosen: false
    signal picked()
    implicitHeight: frame.height + 24

    readonly property var a: template.anim || ({})
    readonly property bool playing: !Theme.reduceMotion && (ma.containsMouse || chosen)
    property real t: 0.4                     // 0..1 progress of the move
    readonly property real e: t * t * (3 - 2 * t)   // eased
    // A real picture of the preset: one made with fal.ai when the person refreshed the
    // previews, else the one rendered with Blender that ships with the app.
    readonly property string previewUrl: {
        var rev = studio.presetsRevision      // re-read when new previews arrive
        var c = studio.presetPreview(template.id)
        return c.length ? c : "qrc:/harness/presets/" + template.id + ".jpg"
    }
    readonly property bool hasShot: shot.status === Image.Ready

    SequentialAnimation {
        running: root.playing
        loops: Animation.Infinite
        onRunningChanged: if (!running) root.t = 0.4
        NumberAnimation { target: root; property: "t"; from: 0; to: 1; duration: root.a.ms || 2000; easing.type: Easing.Linear }
        NumberAnimation { target: root; property: "t"; to: root.a.yoyo ? 0 : 1; duration: root.a.yoyo ? (root.a.ms || 2000) : 380 }
    }

    // ---- Frame ----------------------------------------------------------------------------
    Rectangle {
        id: frame
        width: parent.width
        height: Math.round(width * 0.64)
        radius: Theme.radius + 1
        color: "#0d0f13"
        border.width: root.chosen ? 2 : 1
        border.color: root.chosen ? Theme.create : ma.containsMouse ? Theme.borderStrong : Theme.border
        Behavior on border.color { ColorAnimation { duration: Theme.fast } }
        clip: true

        readonly property real fw: width
        readonly property real fh: height
        readonly property real orbitA: (root.a.orbit || 0) * 2 * Math.PI * root.e
        readonly property real spinA: (root.a.spin || 0) * 2 * Math.PI * root.e
        readonly property real zoomS: 1 + ((root.a.zoom || 1) - 1) * root.e
        readonly property real depth: root.a.depth === undefined ? 0.7 : root.a.depth
        readonly property real shx: (root.a.shake || 0) * (Math.sin(root.t * 47) + 0.5 * Math.sin(root.t * 83))
        readonly property real shy: (root.a.shake || 0) * (Math.cos(root.t * 39) + 0.5 * Math.sin(root.t * 71))

        // The picture, moved the way the preset moves the camera.
        Image {
            id: shot
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: ((root.a.dx || 0) * frame.depth + Math.sin(frame.orbitA) * 0.12) * frame.fw * 0.32 + frame.shx
            anchors.verticalCenterOffset: (root.a.dy || 0) * frame.fh * 0.3 + frame.shy
            width: frame.fw * 1.3
            height: frame.fh * 1.3
            source: root.previewUrl
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            smooth: true
            mipmap: true
            sourceSize.width: Math.ceil(frame.fw * 1.3 * Theme.dpr)
            scale: (1 + ((root.a.zoom || 1) - 1) * root.e * 0.55) * (1 + (root.a.dollyZoom || 0) * root.e * 0.45)
            rotation: (root.a.roll || 0) * root.e
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.normal } }
        }
        // While a picture loads, or if there is none: the drawn storyboard.
        Item {
            id: cam
            visible: !root.hasShot
            anchors.fill: parent
            rotation: (root.a.roll || 0) * root.e
            transformOrigin: Item.Center
            x: frame.shx; y: frame.shy

            // Background world: sky, horizon, pillars (moves less than the subject: parallax).
            Item {
                id: world
                width: frame.fw * 2.2; height: frame.fh * 2.2
                x: (frame.fw - width) / 2 + ((root.a.dx || 0) * frame.depth + Math.sin(frame.orbitA) * -0.45) * frame.fw
                y: (frame.fh - height) / 2 + (root.a.dy || 0) * frame.depth * frame.fh
                scale: (1 + (frame.zoomS - 1) * frame.depth) * (1 + (root.a.dollyZoom || 0) * root.e)
                visible: !root.a.overhead
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: root.a.sky ? Qt.rgba(0.12 + 0.2 * root.e, 0.16 + 0.1 * root.e, 0.28, 1) : "#11151d" }
                        GradientStop { position: 0.5; color: "#0d0f13" }
                        GradientStop { position: 0.5001; color: "#14161a" }
                        GradientStop { position: 1.0; color: "#0b0b0c" }
                    }
                }
                Repeater {
                    model: 9
                    Rectangle {
                        required property int index
                        x: world.width * (0.08 + index * 0.105)
                        width: Math.max(3, world.width * 0.028)
                        height: world.height * (0.12 + (index % 3) * 0.05)
                        y: world.height / 2 - height
                        radius: 1
                        color: Qt.rgba(0.835, 0.851, 0.886, 0.12 + (index % 2) * 0.06)
                    }
                }
                Rectangle { y: world.height / 2; width: world.width; height: 1; color: Qt.rgba(0.835, 0.851, 0.886, 0.25) }
                // Clouds for time-lapse.
                Repeater {
                    model: root.a.sky ? 4 : 0
                    Rectangle {
                        required property int index
                        width: world.width * 0.16; height: world.height * 0.035; radius: height / 2
                        y: world.height * (0.3 + index * 0.04)
                        x: ((index * 0.27 + root.t * (1.2 + index * 0.3)) % 1) * world.width
                        color: Qt.rgba(1, 1, 1, 0.16)
                    }
                }
                // Road lines for chases.
                Repeater {
                    model: root.a.road ? 5 : 0
                    Rectangle {
                        required property int index
                        width: world.width * 0.012; height: world.height * 0.05
                        x: world.width / 2 - width / 2
                        y: world.height * 0.5 + ((index * 0.2 + root.t * 2) % 1) * world.height * 0.45
                        color: Theme.brandMist; opacity: 0.5
                    }
                }
            }

            // Top-down world.
            Item {
                anchors.fill: parent
                visible: !!root.a.overhead
                Grid {
                    anchors.centerIn: parent
                    columns: 8
                    scale: 1 + 0.12 * root.e
                    Repeater { model: 48; Rectangle { width: frame.fw / 7; height: width; color: "transparent"; border.color: Qt.rgba(0.835, 0.851, 0.886, 0.08) } }
                }
            }

            // ---- Subject ------------------------------------------------------------------
            Item {
                id: subject
                width: frame.fh * 0.36; height: frame.fh * 0.62
                x: (frame.fw - width) / 2 + ((root.a.dx || 0) + Math.sin(frame.orbitA) * 0.1) * frame.fw
                y: frame.fh * 0.5 - height * 0.78 + (root.a.dy || 0) * frame.fh
                   - (root.a.fx === "float" ? frame.fh * 0.16 * root.e : 0)
                transformOrigin: Item.Bottom
                scale: root.a.dollyZoom ? 1 : frame.zoomS
                visible: !root.a.overhead
                opacity: root.a.fx === "dust" ? 1 - root.e : 1
                transform: Scale {
                    origin.x: subject.width / 2; origin.y: subject.height
                    xScale: (root.a.spin ? Math.max(0.25, Math.abs(Math.cos(frame.spinA))) : 1)
                            * (root.a.orbit ? Math.max(0.55, Math.abs(Math.cos(frame.orbitA))) : 1)
                            * (root.a.fx === "melt" ? 1 + 0.5 * root.e : 1)
                            * (root.a.fx === "squash" ? 1 + 0.12 * Math.sin(root.t * Math.PI * 4) : 1)
                    yScale: (root.a.fx === "melt" ? 1 - 0.7 * root.e : 1)
                            * (root.a.fx === "squash" ? 1 - 0.12 * Math.sin(root.t * Math.PI * 4) : 1)
                }
                readonly property color tint: root.a.fx === "frost" ? Qt.tint(Theme.brandMist, Qt.rgba(0.62, 0.8, 1, root.e))
                                            : root.a.fx === "swap" ? (root.e < 0.5 ? Theme.brandMist : Theme.brandSky)
                                            : root.a.fx === "metal" ? Qt.tint(Theme.brandMist, Qt.rgba(0.75, 0.78, 0.85, 0.6 * root.e))
                                            : Theme.brandMist
                // Shadow
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: parent.height - height / 2 + (root.a.fx === "float" ? frame.fh * 0.16 * root.e / Math.max(0.01, subject.scale) : 0)
                    width: parent.width * (root.a.fx === "float" ? 1.1 - 0.5 * root.e : 1.1); height: parent.height * 0.06
                    radius: height / 2; color: Qt.rgba(0, 0, 0, 0.55)
                }
                // Plinth
                Rectangle {
                    visible: root.a.fx === "plinth"
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: parent.height - height * 0.4
                    width: parent.width * 1.5; height: parent.height * 0.12; radius: height / 2
                    color: "#1d2027"; border.color: Qt.rgba(0.835, 0.851, 0.886, 0.25)
                }
                // Body and head
                Rectangle {
                    id: body
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: parent.height * 0.3
                    width: parent.width * 0.74; height: parent.height * 0.7
                    radius: width * 0.36
                    color: subject.tint
                    x: root.a.fx === "glitch" ? (Math.sin(root.t * 90) > 0.6 ? 4 : 0) : (parent.width - width) / 2
                    Rectangle {   // facing marker: shows turning
                        visible: !!(root.a.spin || root.a.orbit)
                        width: parent.width * 0.16; height: width; radius: width / 2
                        color: Theme.create
                        y: parent.height * 0.22
                        x: parent.width / 2 - width / 2 + Math.sin(root.a.spin ? frame.spinA : frame.orbitA) * parent.width * 0.32
                        opacity: Math.cos(root.a.spin ? frame.spinA : frame.orbitA) > 0 ? 1 : 0.15
                    }
                }
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width * 0.5; height: width; radius: width / 2
                    color: subject.tint
                }
                // Glitch copies
                Rectangle { visible: root.a.fx === "glitch" && Math.sin(root.t * 60) > 0.3; x: body.x - 3; y: body.y; width: body.width; height: body.height; radius: body.radius; color: Theme.brandBlue; opacity: 0.6 }
                Rectangle { visible: root.a.fx === "glitch" && Math.sin(root.t * 60) > 0.3; x: body.x + 3; y: body.y; width: body.width; height: body.height; radius: body.radius; color: Theme.brandOrange; opacity: 0.6 }
                // Metal sheen / light sweep
                Rectangle {
                    visible: root.a.fx === "metal" || root.a.fx === "sweep"
                    width: parent.width * 0.22; height: parent.height * 1.2
                    rotation: 18
                    x: -parent.width * 0.4 + root.e * parent.width * 1.6
                    y: -parent.height * 0.1
                    color: Qt.rgba(1, 1, 1, 0.55)
                    opacity: Math.sin(root.e * Math.PI)
                }
                // Sticker border
                Rectangle {
                    visible: root.a.fx === "peel"
                    anchors.fill: parent; anchors.margins: -4
                    radius: width * 0.3; color: "transparent"; border.width: 3; border.color: "#ffffff"
                    rotation: -6 * root.e
                }
            }

            // Overhead subject
            Rectangle {
                visible: !!root.a.overhead
                anchors.centerIn: parent
                width: frame.fh * 0.34 * (1 + 0.15 * root.e); height: width; radius: width / 2
                color: Theme.brandMist
                Rectangle { anchors.centerIn: parent; width: parent.width * 0.5; height: width; radius: width / 2; color: "#c4c8d0" }
                Rectangle { visible: root.a.fx === "reveal"; anchors.fill: parent; anchors.margins: -6; color: "transparent"
                            border.width: 6; border.color: Theme.brandOrange; opacity: 1 - root.e; radius: 4 }
            }

            // Through object: a foreground frame the camera passes.
            Rectangle {
                visible: !!root.a.through
                anchors.centerIn: parent
                width: frame.fw * 0.5; height: frame.fh * 0.55
                scale: (root.a.zoom || 1) > 1 ? 1 + root.e * 3.5 : 4.5 - root.e * 3.5
                color: "transparent"
                border.width: frame.fw * 0.06
                border.color: "#22252c"
                radius: 4
            }
        }

        Item {   // drawn effects, only over the drawn storyboard
            anchors.fill: parent
            visible: !root.hasShot
        // ---- Effects over the scene ---------------------------------------------------------
        Rectangle {   // explosion ring
            visible: root.a.fx === "burst"
            anchors.centerIn: parent
            width: frame.fh * (0.2 + 1.6 * root.e); height: width; radius: width / 2
            color: "transparent"; border.width: 4 + 10 * (1 - root.e)
            border.color: Theme.brandOrange; opacity: 1 - root.e
        }
        Rectangle {
            visible: root.a.fx === "burst"
            anchors.centerIn: parent
            width: frame.fh * 0.5 * Math.sin(root.e * Math.PI); height: width; radius: width / 2
            color: Qt.rgba(1, 0.75, 0.3, 0.55)
        }
        Repeater {   // particles (dust, sparkle, shatter, frost, bolt flashes)
            model: (root.a.fx === "dust" || root.a.fx === "sparkle" || root.a.fx === "shatter") ? 14 : 0
            Rectangle {
                required property int index
                readonly property real seed: (index * 37 % 14) / 14
                width: root.a.fx === "shatter" ? frame.fh * 0.08 : 3; height: width
                radius: root.a.fx === "sparkle" ? width / 2 : 0
                rotation: root.a.fx === "shatter" ? 120 * root.e * (seed - 0.5) : 45
                color: root.a.fx === "sparkle" ? "#ffffff" : Theme.brandMist
                x: frame.fw * 0.5 + (seed - 0.5) * frame.fh * 0.3 + (root.a.fx === "sparkle" ? 0 : root.e * frame.fw * (0.15 + seed * 0.4) * (index % 2 ? 1 : -0.6))
                y: frame.fh * (0.3 + 0.4 * ((index * 53 % 14) / 14)) - (root.a.fx === "sparkle" ? 0 : root.e * frame.fh * 0.25 * seed)
                opacity: root.a.fx === "sparkle" ? Math.abs(Math.sin(root.t * Math.PI * 3 + seed * 6)) : (root.a.fx === "dust" ? root.e : 1 - root.e * 0.6)
            }
        }
        Repeater {   // flames
            model: root.a.fx === "fire" ? 5 : 0
            Rectangle {
                required property int index
                width: frame.fh * 0.11; height: frame.fh * (0.12 + 0.3 * root.e) * (0.7 + 0.3 * Math.abs(Math.sin(root.t * 23 + index)))
                radius: width / 2
                x: frame.fw / 2 - frame.fh * 0.2 + index * frame.fh * 0.09
                y: frame.fh * 0.5 - height + frame.fh * 0.02
                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.rgba(1, 0.85, 0.4, 0.2) }
                    GradientStop { position: 1; color: Theme.brandOrange }
                }
                opacity: 0.85
            }
        }
        Rectangle {   // lightning flash
            visible: root.a.fx === "bolt"
            anchors.fill: parent
            color: Theme.brandSky
            opacity: (Math.sin(root.t * 31) > 0.85) ? 0.35 : 0
        }
        Rectangle {   // pixel grid (detail boost / low poly)
            visible: root.a.fx === "pixels"
            anchors.fill: parent
            color: "transparent"
            opacity: 1 - root.e
            Grid {
                anchors.fill: parent
                columns: 12
                Repeater { model: 96; Rectangle { width: frame.fw / 12; height: frame.fh / 8; color: "transparent"; border.color: Qt.rgba(0.04, 0.04, 0.04, 0.9) } }
            }
        }
        Rectangle {   // expand: the original frame grows outwards
            visible: root.a.fx === "expand"
            anchors.centerIn: parent
            width: frame.fw * (0.45 + 0.55 * root.e); height: frame.fh * (0.45 + 0.55 * root.e)
            color: "transparent"; border.width: 2; border.color: Theme.brandBlue
        }
        Rectangle {   // rack focus: a soft foreground blob that fades
            visible: !!root.a.focus
            x: frame.fw * 0.08; y: frame.fh * 0.35
            width: frame.fh * 0.5; height: width; radius: width / 2
            color: Qt.rgba(0.835, 0.851, 0.886, 0.5 * (1 - root.e))
        }
        Rectangle {   // motion streaks
            visible: !!root.a.blur
            anchors.fill: parent
            color: "transparent"
            opacity: Math.sin(root.e * Math.PI) * 0.9
            Repeater {
                model: 6
                Rectangle { required property int index; y: frame.fh * (0.15 + index * 0.13); width: frame.fw; height: 2; color: Qt.rgba(1, 1, 1, 0.18) }
            }
        }
        Rectangle {   // fisheye vignette
            visible: !!root.a.fisheye
            anchors.fill: parent; anchors.margins: -frame.fw * 0.2
            radius: width / 2; color: "transparent"
            border.width: frame.fw * 0.24; border.color: Qt.rgba(0, 0, 0, 0.6 + 0.2 * root.e)
        }
        }
    }

    Text {
        anchors.top: frame.bottom
        anchors.topMargin: 6
        width: parent.width
        text: root.template.name
        color: root.chosen ? Theme.text : ma.containsMouse ? Theme.text : Theme.textSecondary
        font.family: Theme.fontSecondary
        font.pixelSize: Theme.sizeSmall
        font.weight: root.chosen ? Theme.weightStrong : Theme.weightUi
        elide: Text.ElideRight
        renderType: Text.NativeRendering
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.picked()
    }
}
