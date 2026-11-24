pragma Singleton
import QtQuick

// Surface hierarchy and type scale. The palette is one place so the final brand palette can
// replace it without touching components.
QtObject {
    // The brand palette (attachments/picture-2.png):
    //   #0A0A0A  dark mode base       #FFFFFF  text
    //   #D5D9E2  in-between tone (secondary text, lines, quiet controls)
    //   #4B93FF  blue  — action: selection, focus, toggles, active tabs, links, progress
    //   #9ECBFF  light blue — the agent: its replies, thinking, models, tools
    //   #FB940B  orange — making: Create, generation jobs, the active object
    readonly property color brandBase: "#0a0a0a"
    readonly property color brandMist: "#d5d9e2"
    readonly property color brandBlue: "#4b93ff"
    readonly property color brandSky: "#9ecbff"
    readonly property color brandOrange: "#fb940b"

    // Surfaces, darkest to lightest — all neutral, lifted from #0A0A0A.
    readonly property color base: brandBase           // window behind the panels
    readonly property color chrome: brandBase         // title bar, workspace row
    readonly property color panel: "#111111"          // panels
    readonly property color raised: "#171717"         // cards, inputs
    readonly property color overlay: "#1c1c1d"        // menus, popovers
    readonly property color hover: Qt.rgba(0.835, 0.851, 0.886, 0.07)
    readonly property color pressed: Qt.rgba(0.835, 0.851, 0.886, 0.11)
    readonly property color selected: Qt.rgba(0.835, 0.851, 0.886, 0.14)
    readonly property color border: Qt.rgba(0.835, 0.851, 0.886, 0.12)
    readonly property color borderSubtle: Qt.rgba(0.835, 0.851, 0.886, 0.07)
    readonly property color borderStrong: Qt.rgba(0.835, 0.851, 0.886, 0.26)

    // Ink.
    readonly property color text: "#ffffff"
    readonly property color textSecondary: brandMist
    readonly property color textTertiary: Qt.rgba(0.835, 0.851, 0.886, 0.58)
    readonly property color textDisabled: Qt.rgba(0.835, 0.851, 0.886, 0.32)

    // Accents with one meaning each.
    readonly property color accent: brandBlue
    readonly property color accentSoft: Qt.rgba(0.294, 0.576, 1.0, 0.22)
    readonly property color agent: brandSky
    readonly property color agentSoft: Qt.rgba(0.62, 0.796, 1.0, 0.12)
    readonly property color create: brandOrange      // the Create button
    readonly property color createHover: "#ffa52e"
    readonly property color createBevel: "#b96500"
    readonly property color createSoft: Qt.rgba(0.984, 0.58, 0.043, 0.16)
    readonly property color inkOnCreate: brandBase
    readonly property color success: "#3dd68c"
    readonly property color warning: brandOrange
    readonly property color danger: "#ff5a52"
    readonly property color info: brandSky

    // Type: Instrument Sans for the interface, Cabin for secondary text (captions, section
    // labels, meta), Notable for hero moments (the wordmark, empty-state titles, page titles).
    readonly property string font: "Instrument Sans"
    readonly property string fontSecondary: "Cabin"
    readonly property string fontHero: "Notable"
    readonly property string mono: "DejaVu Sans Mono"
    readonly property int sizeCaption: 12
    readonly property int sizeSmall: 13
    readonly property int sizeBody: 14
    readonly property int sizeTitle: 16
    readonly property int sizeHeading: 20
    readonly property int sizeHero: 24
    // Breathing room: wrapped text gets 1.3 line height, small text a touch of tracking.
    readonly property real lineHeight: 1.3
    function tracking(px) { return px <= 12 ? 0.25 : px <= 14 ? 0.12 : 0 }

    // Shape and rhythm.
    readonly property int radiusSmall: 4
    readonly property int radius: 6
    readonly property int radiusLarge: 9
    readonly property int gap: 8
    readonly property int pad: 16

    // Motion (zeroed when the profile asks for reduced motion).
    property bool reduceMotion: false
    readonly property int fast: reduceMotion ? 0 : 110
    readonly property int normal: reduceMotion ? 0 : 180
    readonly property int slow: reduceMotion ? 0 : 280

    // Standard easing, cubic-bezier(.4, 0, .2, 1): quick to start, gentle to settle.
    readonly property var curve: [0.4, 0.0, 0.2, 1.0, 1.0, 1.0]

    // The window's real device pixel ratio (fractional scales included).
    property real dpr: 1.0

    // Weights: body text Regular; small text, labels and controls Medium (Regular is too thin
    // at 12-13 px); headings and the primary button SemiBold. Bold only for single glyphs.
    readonly property int weightBody: Font.Normal
    readonly property int weightUi: Font.Medium
    readonly property int weightStrong: Font.DemiBold

    function icon(name) { return "qrc:/harness/icons/ui/" + name + ".svg" }
    // A maker's mark; makers without one in Simple Icons get a neutral spark.
    // Marks in their own colours (Simple Icons, CC0; fal.ai's from its site). fal ships as PNG.
    function brand(name) {
        if (!name) return icon("auto_awesome")
        return "qrc:/harness/icons/brands/" + name + (name === "fal" ? ".png" : ".svg")
    }
    function blenderIcon(name) {
        if (!name || name === "NONE") return ""
        return "qrc:/harness/blender-icons/" + name.toLowerCase() + ".svg"
    }
}
