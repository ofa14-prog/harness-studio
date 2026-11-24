import QtQuick
import QtQuick3D
import QtQuick3D.Helpers
import Harness.Gltf 1.0

// Inspect a generated 3D model before it goes into the scene: orbit (drag), pan (right drag),
// zoom (wheel); lighting presets; shaded, wireframe or one material channel at a time (base
// colour, roughness, metalness, normals, occlusion); ground grid; turntable; the model's size.
Item {
    id: root
    property url source
    property string lighting: "studio"
    property int channel: DebugSettings.None
    property bool wire: false
    property bool grid: true
    property bool turntable: false
    readonly property vector3d bmin: gltf.minimum
    readonly property vector3d bmax: gltf.maximum
    readonly property vector3d size: Qt.vector3d(bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z)
    readonly property real radius: Math.max(0.001, Math.sqrt(size.x * size.x + size.y * size.y + size.z * size.z) / 2)

    function frame() {
        // Fit the model: look at its centre from a distance that shows all of it. Read the
        // bounds from the loader itself (bindings derived from them may not have updated yet).
        var lo = gltf.minimum, hi = gltf.maximum
        var dx = hi.x - lo.x, dy = hi.y - lo.y, dz = hi.z - lo.z
        var r = Math.max(0.001, Math.sqrt(dx * dx + dy * dy + dz * dz) / 2)
        origin.position = Qt.vector3d((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2)
        origin.eulerRotation = Qt.vector3d(-18, 32, 0)
        camera.position = Qt.vector3d(0, 0, r * 3.2)
        camera.clipNear = r * 0.01
        camera.clipFar = r * 200
    }

    GltfModel {
        id: gltf
        source: root.source
        onLoaded: {
            if (ready) root.frame()
        }
    }

    View3D {
        id: view
        anchors.fill: parent
        environment: SceneEnvironment {
            clearColor: "#101114"
            backgroundMode: SceneEnvironment.Color
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
            tonemapMode: SceneEnvironment.TonemapModeFilmic
            aoEnabled: true
            aoStrength: 60
            aoDistance: root.radius * 0.3
            debugSettings: DebugSettings { materialOverride: root.channel; wireframeEnabled: root.wire }
            // A procedural sky for reflections, so metals and glossy surfaces read correctly.
            lightProbe: Texture {
                textureData: ProceduralSkyTextureData {
                    skyTopColor: root.lighting === "night" ? "#0b1830" : root.lighting === "sunset" ? "#3a4f86" : "#5d6b80"
                    skyHorizonColor: root.lighting === "night" ? "#1a2238" : root.lighting === "sunset" ? "#ff9a55" : "#c9d0dc"
                    groundBottomColor: "#16171b"
                    groundHorizonColor: root.lighting === "sunset" ? "#5b3a2a" : "#3a3d44"
                    sunColor: root.lighting === "sunset" ? "#ffb066" : "#ffffff"
                }
            }
            probeExposure: root.lighting === "night" ? 0.35 : 0.9
        }

        Node {
            id: origin
            PerspectiveCamera { id: camera; fieldOfView: 35 }
        }

        // Lighting presets: studio (key, fill, rim), sunset (warm low sun, cool sky), night.
        DirectionalLight {
            eulerRotation: root.lighting === "sunset" ? Qt.vector3d(-12, 60, 0) : root.lighting === "night" ? Qt.vector3d(-50, -30, 0) : Qt.vector3d(-38, 35, 0)
            color: root.lighting === "sunset" ? "#ffb066" : root.lighting === "night" ? "#9ecbff" : "#ffffff"
            brightness: root.lighting === "night" ? 0.6 : 1.6
            castsShadow: true
            shadowFactor: 60
            shadowMapQuality: Light.ShadowMapQualityHigh
        }
        DirectionalLight {
            eulerRotation: Qt.vector3d(-20, -120, 0)
            color: root.lighting === "sunset" ? "#5a7cc8" : root.lighting === "night" ? "#2a3f6e" : "#d5d9e2"
            brightness: root.lighting === "night" ? 0.25 : 0.55
        }
        DirectionalLight {
            eulerRotation: Qt.vector3d(-10, 170, 0)
            color: root.lighting === "night" ? "#4b93ff" : "#fb940b"
            brightness: root.lighting === "studio" ? 0.7 : 0.9
        }
        PointLight {
            position: Qt.vector3d(0, root.radius * 3, root.radius * 2)
            brightness: root.lighting === "night" ? 0.1 : 0.25
        }

        // The model: one Model per glTF primitive, with its PBR material and textures.
        // Turns about the model's own vertical axis (an outer node at its centre).
        Node {
            id: loader
            position: Qt.vector3d((root.bmin.x + root.bmax.x) / 2, 0, (root.bmin.z + root.bmax.z) / 2)
            NumberAnimation on eulerRotation.y { from: 0; to: 360; duration: 14000; loops: Animation.Infinite; running: root.turntable }
          Node {
            position: Qt.vector3d(-(root.bmin.x + root.bmax.x) / 2, 0, -(root.bmin.z + root.bmax.z) / 2)
            Repeater3D {
                model: gltf.parts
                delegate: Model {
                    required property var modelData
                    geometry: modelData.geometry
                    castsShadows: true
                    receivesShadows: true
                    Texture { id: baseTex; source: modelData.baseColorMap || "" }
                    Texture { id: ormTex; source: modelData.ormMap || "" }
                    Texture { id: normalTex; source: modelData.normalMap || "" }
                    Texture { id: emissiveTex; source: modelData.emissiveMap || "" }
                    materials: PrincipledMaterial {
                        baseColor: modelData.baseColor
                        baseColorMap: (modelData.baseColorMap || "").length ? baseTex : null
                        metalness: modelData.metalness
                        roughness: modelData.roughness
                        metalnessMap: (modelData.ormMap || "").length ? ormTex : null
                        metalnessChannel: Material.B
                        roughnessMap: (modelData.ormMap || "").length ? ormTex : null
                        roughnessChannel: Material.G
                        normalMap: (modelData.normalMap || "").length ? normalTex : null
                        emissiveFactor: Qt.vector3d(modelData.emissive.r, modelData.emissive.g, modelData.emissive.b)
                        emissiveMap: (modelData.emissiveMap || "").length ? emissiveTex : null
                        opacity: modelData.opacity
                        cullMode: modelData.doubleSided ? Material.NoCulling : Material.BackFaceCulling
                    }
                }
            }
          }
        }

        // Ground: a grid at the model's base.
        Model {
            visible: root.grid
            source: "#Rectangle"
            y: root.bmin.y
            eulerRotation.x: -90
            scale: Qt.vector3d(root.radius * 0.12, root.radius * 0.12, 1)
            receivesShadows: true
            materials: PrincipledMaterial { baseColor: "#17181c"; roughness: 0.95 }
        }
    }
    OrbitCameraController {
        anchors.fill: parent
        origin: origin
        camera: camera
        panEnabled: true
    }

    // Loading and errors.
    Text {
        anchors.centerIn: parent
        width: parent.width * 0.6
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        visible: !gltf.ready && gltf.error.length > 0
        text: gltf.error
        color: Theme.danger; font.family: Theme.font; font.pixelSize: Theme.sizeBody; renderType: Text.NativeRendering
    }

    // ---- Controls --------------------------------------------------------------------
    Column {
        x: 20; y: 20
        spacing: 10
        Rectangle {
            width: info.implicitWidth + 24; height: info.implicitHeight + 18
            radius: 10; color: Qt.rgba(0.07, 0.07, 0.08, 0.88); border.color: Theme.border
            Column {
                id: info
                x: 12; y: 9
                spacing: 3
                Text { text: qsTr("Size"); color: Theme.textTertiary; font.family: Theme.fontSecondary; font.pixelSize: Theme.sizeCaption; font.weight: Theme.weightUi
                       font.capitalization: Font.AllUppercase; font.letterSpacing: 0.6; renderType: Text.NativeRendering }
                Text { text: root.size.x.toFixed(2) + " × " + root.size.y.toFixed(2) + " × " + root.size.z.toFixed(2) + " m"
                       color: Theme.text; font.family: Theme.font; font.pixelSize: Theme.sizeBody; font.weight: Theme.weightUi; renderType: Text.NativeRendering }
                Item { width: 1; height: 6 }
                Text { text: qsTr("Geometry"); color: Theme.textTertiary; font.family: Theme.fontSecondary; font.pixelSize: Theme.sizeCaption; font.weight: Theme.weightUi
                       font.capitalization: Font.AllUppercase; font.letterSpacing: 0.6; renderType: Text.NativeRendering }
                Text { text: qsTr("%1 triangles · %2 vertices").arg(gltf.triangleCount.toLocaleString(Qt.locale(), "f", 0)).arg(gltf.vertexCount.toLocaleString(Qt.locale(), "f", 0))
                       color: Theme.text; font.family: Theme.font; font.pixelSize: Theme.sizeBody; font.weight: Theme.weightUi; renderType: Text.NativeRendering }
                Text { text: qsTr("%1 parts · %2 textures").arg(gltf.parts.length).arg(gltf.textureCount)
                       color: Theme.textSecondary; font.family: Theme.font; font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi; renderType: Text.NativeRendering }
            }
        }
    }
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom; anchors.bottomMargin: 22
        width: tools.implicitWidth + 16; height: 48
        radius: 12; color: Qt.rgba(0.07, 0.07, 0.08, 0.92); border.color: Theme.border
        Row {
            id: tools
            anchors.centerIn: parent
            spacing: 6
            Chip { iconSource: Theme.icon("bolt"); text: root.lighting === "sunset" ? qsTr("Sunset") : root.lighting === "night" ? qsTr("Night") : qsTr("Studio")
                   tooltip: qsTr("Lighting"); onClicked: root.lighting = root.lighting === "studio" ? "sunset" : root.lighting === "sunset" ? "night" : "studio" }
            Chip {
                iconSource: Theme.icon("palette")
                readonly property var names: [qsTr("Shaded"), qsTr("Base colour"), qsTr("Roughness"), qsTr("Metalness"), qsTr("Normals"), qsTr("Occlusion")]
                readonly property var modes: [DebugSettings.None, DebugSettings.BaseColor, DebugSettings.Roughness, DebugSettings.Metalness, DebugSettings.Normals, DebugSettings.AmbientOcclusion]
                property int i: 0
                text: names[i]
                checked: i > 0
                tooltip: qsTr("Material channel")
                onClicked: { i = (i + 1) % modes.length; root.channel = modes[i] }
            }
            Chip { iconSource: Theme.icon("grid_view"); text: qsTr("Wireframe"); checked: root.wire; onClicked: root.wire = !root.wire }
            Chip { iconSource: Theme.icon("dashboard"); text: qsTr("Ground"); checked: root.grid; onClicked: root.grid = !root.grid }
            Chip { iconSource: Theme.icon("refresh"); text: qsTr("Turntable"); checked: root.turntable; onClicked: root.turntable = !root.turntable }
            Chip { iconSource: Theme.icon("center_focus_strong"); text: qsTr("Frame"); onClicked: root.frame() }
        }
    }
}
