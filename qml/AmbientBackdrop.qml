import QtQuick
import Sung.Native 1.0

// A cover-derived backdrop for large playback surfaces. The artwork is decoded
// small and blurred once on load, so the wash costs one small texture and no
// per-frame effect, and a scrim keeps M3 surface contrast. Rounded surfaces
// hold still; only full-bleed ones drift, because a drifting cover cannot keep
// a rounded corner clean.
Item {
    id: backdrop
    objectName: "ambientBackdrop"
    property string url: ""
    property color scrim: Theme.background
    // Scrim coverage. Text sits on the scrim, never on the raw cover.
    property real dim: 0.8
    property bool allowed: true
    property string heldUrl: ""
    onUrlChanged: if (url) heldUrl = url
    property real corner: 0
    property real drift: 1
    readonly property bool active: allowed && app.ambientBackdrop && !!url
    onActiveChanged: if (active) heldUrl = url
    // The cover is part of the surface text sits on, so the decoded pixels
    // determine the smallest scrim meeting Theme.textContrast. Keep dim
    // whenever the decoded cover already passes.
    readonly property real effectiveDim: art.ready
        ? art.minimumScrim(scrim, Theme.muted, dim, Theme.textContrast) : dim
    readonly property bool drifts: corner <= 0
    readonly property bool animating: drifts && active && visible && app.motion && Window.window && Window.window.visible && Window.window.visibility!==Window.Minimized
    // The decoded low end already drives the playing indicator. Reusing it here
    // lets the wash breathe with the track instead of only on a timer. Rounded
    // backdrops sit this out because swelling past their bounds squares off the
    // corners the clip cannot round.
    readonly property bool reactive: animating && app.backdropPulse && app.playing
    readonly property real level: reactive ? Math.max(app.audioLevels[0] || 0, app.audioLevels[1] || 0) : 0
    property real pulse: level
    // The swell follows decoded audio on SlowEffects, the critically damped
    // spring that settles nearest the smoothing it replaces (about 235ms at
    // 1.0/800): a level that keeps moving retargets it before it settles, and
    // being an effects spring it cannot overshoot the level it is chasing.
    // Reduced motion disables the Behavior so the last change lands at once.
    Behavior on pulse { enabled: app.motion; NumberAnimation { duration: Theme.springSlowEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSlowEffects } }
    // Qt Quick Item visibility stops drawing immediately. Keep the last
    // cover visible until SlowEffects reaches zero, including coverless tracks.
    visible: active || opacity > 0
    opacity: active ? 1 : 0
    // The drifting cover is oversized on purpose and must not paint outside.
    clip: true
    // SlowEffects keeps the ambient wash's fade gentle and monotonic.
    Behavior on opacity { enabled: app.motion; NumberAnimation { duration: Theme.springSlowEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSlowEffects } }
    RoundedArt {
        id: art
        objectName: "ambientArt"
        anchors.fill: parent
        radius: backdrop.corner
        // Decoded small and then genuinely blurred, so enlarging it shows a
        // gradient rather than the seams between a handful of source pixels.
        pixels: 160
        blur: 22
        crossfade: backdrop.visible && app.motion && Window.window && Window.window.visible && Window.window.visibility!==Window.Minimized
        // The old cover survives the exit fade, then its source is released.
        source: backdrop.visible && backdrop.opacity>0
            ? (backdrop.active ? backdrop.url : backdrop.heldUrl) : ""
        transformOrigin: Item.Center
        // Drifting needs margin, so the cover is always a little oversized.
        scale: backdrop.drifts ? 1.08*backdrop.drift*(1+0.035*backdrop.pulse) : 1
        transform: Translate { id: sway }
    }
    SequentialAnimation {
        running: backdrop.animating; loops: Animation.Infinite
        NumberAnimation { target: backdrop; property: "drift"; to: 1.06; duration: 14000; easing.type: Easing.InOutSine }
        NumberAnimation { target: backdrop; property: "drift"; to: 1; duration: 14000; easing.type: Easing.InOutSine }
    }
    SequentialAnimation {
        running: backdrop.animating; loops: Animation.Infinite
        ParallelAnimation {
            NumberAnimation { target: sway; property: "x"; to: 24; duration: 19000; easing.type: Easing.InOutSine }
            NumberAnimation { target: sway; property: "y"; to: -16; duration: 19000; easing.type: Easing.InOutSine }
        }
        ParallelAnimation {
            NumberAnimation { target: sway; property: "x"; to: -24; duration: 19000; easing.type: Easing.InOutSine }
            NumberAnimation { target: sway; property: "y"; to: 16; duration: 19000; easing.type: Easing.InOutSine }
        }
    }
    onAnimatingChanged: if(!animating){backdrop.drift=1;sway.x=0;sway.y=0;}
    Rectangle {
        objectName: "ambientScrim"
        anchors.fill: parent
        radius: backdrop.corner
        color: Qt.rgba(backdrop.scrim.r,backdrop.scrim.g,backdrop.scrim.b,backdrop.effectiveDim)
    }
    // Controls sit at the bottom of these surfaces and need the deepest scrim.
    Rectangle {
        anchors.fill: parent
        radius: backdrop.corner
        gradient: Gradient {
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 0.55; color: "transparent" }
            GradientStop { position: 1; color: Qt.rgba(backdrop.scrim.r,backdrop.scrim.g,backdrop.scrim.b,0.6) }
        }
    }
}
