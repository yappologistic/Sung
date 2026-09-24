import QtQuick
import QtQuick.Shapes

// The visualizer's ring: the sound drawn as bars standing out from the edge of
// the Material shape the cover is cut to.
//
// Material's shape guidance keeps abstract shapes for imagery and decorative
// moments ("Emphasize aesthetic moments with shape"), and a visualizer is the
// decorative moment of a music player. The bars start a gap outside the
// cover's own outline rather than on a circle, so the shape's lobes carry out
// into the ring, and follow it while it morphs.
//
// Bass is at the top and the treble runs down both sides to meet at the
// bottom, so the ring is symmetrical and the band that moves most sits where
// the eye lands first.
//
// Each bar follows its band on Material's fast spatial spring, integrated
// here frame by frame because the band it follows changes thirty times a
// second: a fitted curve only runs from one rest to the next. The spring
// rings, so a kick overshoots and settles like the rest of the expressive
// motion scheme. With motion off the ring holds still at rest.
Item {
    id: ring
    objectName: "spectrumRing"
    Accessible.ignored: true

    property string shape: "cookie12Sided"
    // The shape the cover is morphing to and how far it has got, so the ring
    // moves with the cover's edge.
    property string toShape: ""
    property real morph: 0
    // The diameter of the cover inside the ring.
    property real coverSize: 0
    // Playing and allowed to move. The ring only asks the backend to measure
    // while it is also on screen.
    property bool running: false
    readonly property bool listening: running && visible && app.motion && !!Window.window &&
                                      Window.window.visible && Window.window.visibility!==Window.Minimized
    onListeningChanged: {app.spectrumActive=listening;settling=true;}
    Component.onCompleted: app.spectrumActive=listening
    Component.onDestruction: app.spectrumActive=false

    readonly property int barCount: 72
    // MWavyProgress's 4dp stroke, the width Material gives its indicators'
    // active track, with round caps so a silent bar is a dot.
    readonly property real barWidth: 4
    // One step of Material's 4dp grid past the bar's own width, 8dp, between
    // the cover's edge and the first bar.
    readonly property real gap: Theme.space
    readonly property real reach: Math.max(0,(Math.min(width,height)-coverSize)/2-gap-barWidth)
    // Where each bar's round cap is centred: the cover's outline pushed out by
    // the gap and half a bar, measured square to the edge rather than along
    // the bar, so a bar down a clover's notch clears both leaves by the same
    // 8dp a bar on a lobe does.
    readonly property var starts: coverSize>0 ? app.shapeOffset(shape, toShape, morph, (gap+barWidth/2)/(coverSize/2), barCount) : []
    readonly property var physics: Theme.springs.fastSpatial
    property var heights: []
    property var speeds: []
    property var segments: []

    function bandFor(index) {
        const bands = app.audioSpectrum.length
        // Angle 0 points right and angles grow clockwise on screen, so the top
        // is three quarters of the way round.
        const turn = ((index/barCount - 0.75) % 1 + 1) % 1
        const fromTop = Math.min(turn, 1-turn)*2
        return Math.min(bands-1, Math.floor(fromTop*bands))
    }
    function rebuild() {
        const cx = width/2, cy = height/2, inner = coverSize/2
        const lines = []
        for (let i = 0; i < barCount; ++i) {
            const angle = i*2*Math.PI/barCount
            const start = inner*(starts[i] || 1)
            // A stroke of no length draws nothing, so a silent bar keeps a
            // hair of length and its round caps make it a dot.
            const length = Math.max(0.5, Math.min(1.15, heights[i] || 0)*reach)
            const c = Math.cos(angle), s = Math.sin(angle)
            lines.push([Qt.point(cx+start*c, cy+start*s), Qt.point(cx+(start+length)*c, cy+(start+length)*s)])
        }
        segments = lines
    }
    function step(seconds) {
        const dt = Math.min(0.05, seconds)
        const k = physics.stiffness, c = 2*physics.damping*Math.sqrt(k)
        const spectrum = app.audioSpectrum
        let moving = false
        for (let i = 0; i < barCount; ++i) {
            const target = listening ? (spectrum[bandFor(i)] || 0) : 0
            let x = heights[i] || 0, v = speeds[i] || 0
            // Four substeps keep the stiff spring stable at a slow frame.
            for (let n = 0; n < 4; ++n) {
                const a = -k*(x-target) - c*v
                v += a*dt/4; x += v*dt/4
            }
            heights[i] = x; speeds[i] = v
            if (Math.abs(x-target) > 0.002 || Math.abs(v) > 0.02) moving = true
        }
        settling = moving
        rebuild()
    }
    property bool settling: false
    onWidthChanged: rebuild()
    onHeightChanged: rebuild()
    onStartsChanged: rebuild()
    Connections { target: app; function onSettingsChanged() { if (!app.motion) { ring.heights = []; ring.speeds = []; ring.rebuild() } } }

    FrameAnimation {
        objectName: "spectrumRingFrames"
        running: (ring.listening || ring.settling) && app.motion
        onTriggered: ring.step(frameTime)
    }
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: "transparent"
            strokeColor: Theme.primary
            strokeWidth: ring.barWidth
            capStyle: ShapePath.RoundCap
            PathMultiline { paths: ring.segments }
        }
    }
}
