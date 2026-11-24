import QtQuick

// Generation in progress. From 28 px up: an isometric 3 × 3 field of cubes whose heights rise
// and fall in a diagonal wave while a band of light sweeps across their tops (each tile steps
// through its own phase, so the field reads as squares lighting up in turn). Below that: one
// cube that tumbles a quarter turn at a time, easing in and out with a short hold, squashing
// mid-turn while its shadow breathes. Faces are lit from the upper left. Brand orange by default.
Item {
    id: root
    property int size: 40
    property color base: Theme.create
    property bool running: visible && !Theme.reduceMotion
    readonly property bool field: size >= 28
    implicitWidth: size
    implicitHeight: size

    // ---- Field of cubes ----------------------------------------------------------------
    property real t: 0
    NumberAnimation on t { from: 0; to: 1; duration: 1800; loops: Animation.Infinite; running: root.running && root.field }
    onTChanged: if (field) fieldCanvas.requestPaint()
    Canvas {
        id: fieldCanvas
        visible: root.field
        anchors.fill: parent
        renderStrategy: Canvas.Immediate
        onPaint: {
            var g = getContext("2d")
            g.reset()
            var n = 3
            var s = width / (n * 1.9)                    // tile edge in pixels
            var ox = width / 2, oy = height * 0.30
            var c = root.base
            function P(x, y, z) { return [ox + (x - y) * s * 0.866, oy + (x + y) * s * 0.5 - z * s] }
            function fill(pts, k, a) {
                g.beginPath(); g.moveTo(pts[0][0], pts[0][1])
                for (var q = 1; q < pts.length; q++) g.lineTo(pts[q][0], pts[q][1])
                g.closePath()
                g.fillStyle = Qt.rgba(Math.min(1, c.r * k), Math.min(1, c.g * k), Math.min(1, c.b * k), a)
                g.fill()
            }
            // Back to front.
            var cells = []
            for (var i = 0; i < n; i++) for (var j = 0; j < n; j++) cells.push([i, j])
            cells.sort(function(a, b) { return (a[0] + a[1]) - (b[0] + b[1]) })
            var gap = 0.08
            for (var k = 0; k < cells.length; k++) {
                var x = cells[k][0], y = cells[k][1]
                var d = (x + y) / (2 * (n - 1))                     // 0 back corner .. 1 front
                var wave = 0.5 + 0.5 * Math.sin((root.t - d * 0.55) * Math.PI * 2)
                var h = 0.18 + 0.82 * wave * wave
                var band = Math.exp(-Math.pow(((root.t * 1.4) % 1.4) - 0.2 - d, 2) * 26)   // the light sweep
                var x0 = x + gap, x1 = x + 1 - gap, y0 = y + gap, y1 = y + 1 - gap
                fill([P(x0, y1, 0), P(x1, y1, 0), P(x1, y1, h), P(x0, y1, h)], 0.55 + 0.25 * band, 1)   // left face
                fill([P(x1, y0, 0), P(x1, y1, 0), P(x1, y1, h), P(x1, y0, h)], 0.38 + 0.2 * band, 1)    // right face
                fill([P(x0, y0, h), P(x1, y0, h), P(x1, y1, h), P(x0, y1, h)], 0.92 + 0.5 * band, 1)    // top
                if (band > 0.15) {   // glint on the lit tops
                    fill([P(x0, y0, h), P(x1, y0, h), P(x1, y1, h), P(x0, y1, h)], 2.2, 0.35 * band)
                }
            }
        }
    }

    // ---- Single tumbling cube ----------------------------------------------------------

    property real ay: 0          // quarter turn about the vertical axis (degrees)
    property real ax: 0          // quarter roll about the horizontal axis (degrees)
    readonly property real phase: Math.sin((ay % 90) / 90 * Math.PI) + Math.sin((ax % 90) / 90 * Math.PI)
    readonly property real squash: 1 - 0.07 * phase

    SequentialAnimation {
        running: root.running && !root.field
        loops: Animation.Infinite
        NumberAnimation { target: root; property: "ay"; from: 0; to: 90; duration: 640; easing.type: Easing.InOutCubic }
        PauseAnimation { duration: 170 }
        NumberAnimation { target: root; property: "ax"; from: 0; to: 90; duration: 640; easing.type: Easing.InOutCubic }
        PauseAnimation { duration: 170 }
        // A cube a quarter turn on looks the same: start over without a visible jump.
        ScriptAction { script: { root.ax = 0; root.ay = 0 } }
    }
    onAyChanged: canvas.requestPaint()
    onAxChanged: canvas.requestPaint()
    onBaseChanged: { canvas.requestPaint(); fieldCanvas.requestPaint() }

    // Soft shadow under the cube.
    Rectangle {
        visible: !root.field
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height * 0.86
        width: parent.width * (0.62 - 0.08 * root.phase)
        height: parent.height * 0.09
        radius: height / 2
        color: Qt.rgba(0, 0, 0, 0.45 - 0.12 * root.phase)
    }

    Canvas {
        id: canvas
        visible: !root.field
        anchors.fill: parent
        renderStrategy: Canvas.Immediate
        transform: Scale { origin.x: canvas.width / 2; origin.y: canvas.height * 0.8; yScale: root.squash; xScale: 2 - root.squash }
        onPaint: {
            var g = getContext("2d")
            g.reset()
            var s = root.size * 0.25
            var c = root.base
            var cy = Math.cos(root.ay * Math.PI / 180), sy = Math.sin(root.ay * Math.PI / 180)
            var cx = Math.cos(root.ax * Math.PI / 180), sx = Math.sin(root.ax * Math.PI / 180)
            // Isometric view: 45° about vertical, then 35.26° tilt.
            var vy = Math.PI / 4, vx = Math.atan(1 / Math.SQRT2)
            var cvy = Math.cos(vy), svy = Math.sin(vy), cvx = Math.cos(vx), svx = Math.sin(vx)
            function xf(p) {
                // object rotation: about Y, then about X
                var x = p[0] * cy + p[2] * sy, y = p[1], z = -p[0] * sy + p[2] * cy
                var y2 = y * cx - z * sx, z2 = y * sx + z * cx
                // view
                var x3 = x * cvy + z2 * svy, z3 = -x * svy + z2 * cvy
                var y4 = y2 * cvx - z3 * svx, z4 = y2 * svx + z3 * cvx
                return [x3, y4, z4]
            }
            var faces = [
                { n: [0, 1, 0], v: [[-1, 1, -1], [1, 1, -1], [1, 1, 1], [-1, 1, 1]] },
                { n: [0, -1, 0], v: [[-1, -1, -1], [-1, -1, 1], [1, -1, 1], [1, -1, -1]] },
                { n: [1, 0, 0], v: [[1, -1, -1], [1, -1, 1], [1, 1, 1], [1, 1, -1]] },
                { n: [-1, 0, 0], v: [[-1, -1, -1], [-1, 1, -1], [-1, 1, 1], [-1, -1, 1]] },
                { n: [0, 0, 1], v: [[-1, -1, 1], [-1, 1, 1], [1, 1, 1], [1, -1, 1]] },
                { n: [0, 0, -1], v: [[-1, -1, -1], [1, -1, -1], [1, 1, -1], [-1, 1, -1]] }
            ]
            var L = [-0.45, 0.78, 0.43]   // light from the upper left, towards the viewer
            var ox = width / 2, oy = height * 0.5
            var drawn = []
            for (var i = 0; i < faces.length; i++) {
                var n = xf(faces[i].n)
                if (n[2] <= 0.001) continue
                var pts = []
                var depth = 0
                for (var k = 0; k < 4; k++) {
                    var v = faces[i].v[k]
                    var p = xf([v[0] * s, v[1] * s, v[2] * s])
                    pts.push([ox + p[0], oy - p[1]])
                    depth += p[2]
                }
                var lit = Math.max(0, n[0] * L[0] + n[1] * L[1] + n[2] * L[2])
                drawn.push({ pts: pts, depth: depth, k: 0.42 + 0.68 * lit })
            }
            drawn.sort(function(a, b) { return a.depth - b.depth })
            for (var j = 0; j < drawn.length; j++) {
                var f = drawn[j]
                var r = Math.min(1, c.r * f.k), gg = Math.min(1, c.g * f.k), b = Math.min(1, c.b * f.k)
                g.beginPath()
                g.moveTo(f.pts[0][0], f.pts[0][1])
                for (var m = 1; m < 4; m++) g.lineTo(f.pts[m][0], f.pts[m][1])
                g.closePath()
                g.fillStyle = Qt.rgba(r, gg, b, 1)
                g.fill()
                g.lineJoin = "round"
                g.lineWidth = Math.max(1, root.size / 40)
                g.strokeStyle = Qt.rgba(1, 1, 1, 0.18 * f.k)
                g.stroke()
            }
        }
    }
}
