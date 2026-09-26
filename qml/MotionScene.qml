import QtQuick
import Sung.Native 1.0

// The Motion layout's backdrop: the song's cover filling the view, animated
// where the song has an animated cover, sharp in the middle and melting into
// a blurred copy of itself at the edges. Two scrims keep the controls at the
// top and the details at the bottom readable, and leave with the controls so
// the picture has the whole screen while nobody is using them.
Item {
    id: scene
    objectName: "motionScene"
    property bool shown: false
    property bool controlsShown: true
    // How far down the top controls reach, and how far up the bottom ones.
    property real topBand: 0
    property real bottomBand: 0
    visible: opacity > 0
    opacity: shown ? 1 : 0
    // The ambient backdrop leaves on SlowEffects as this arrives on it, so
    // one hands over to the other without the surface showing between them.
    Behavior on opacity { NumberAnimation { duration: Theme.springSlowEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSlowEffects } }
    // Only a loader: the still arrives through the cache, network and video
    // frame lookup every other cover uses. 1600px fills a 1600px window at
    // one to one, and a fixed size means resizing never decodes it again.
    RoundedArt {
        id: still
        objectName: "motionStill"
        visible: false
        radius: 0
        pixels: 1600
        source: scene.visible ? (app.current.art || "") : ""
    }
    MotionBackdrop {
        id: picture
        objectName: "motionBackdrop"
        anchors.fill: parent
        still: still
        animation: scene.visible && app.currentMotionArt && app.currentMotionArt === motionArtwork.source.toString() ? motionArtwork : null
        surface: Theme.background
        ink: Theme.muted
        contrast: Theme.textContrast
        topBand: scene.topBand
        bottomBand: scene.bottomBand
        onSceneChanged: {
            if (mix >= 1) return
            if (app.motion && scene.visible) crossfade.restart()
            else mix = 1
        }
    }
    // One picture handing over to the next is a change of colour, not of
    // place, so it takes DefaultEffects, the spring RoundedArt crossfades
    // covers on (ExpressiveMotionTokens.kt:24-25).
    NumberAnimation {
        id: crossfade
        objectName: "motionCrossfade"
        target: picture; property: "mix"; from: 0; to: 1
        duration: Theme.springEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springEffects
    }
    // The solved scrims move as the video brightens, on the same effects
    // spring, which cannot overshoot into a scrim thinner than the solve.
    property real topScrim: picture.topScrim
    property real bottomScrim: picture.bottomScrim
    Behavior on topScrim { NumberAnimation { duration: Theme.springEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springEffects } }
    Behavior on bottomScrim { NumberAnimation { duration: Theme.springEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springEffects } }
    function veil(strength, share) {
        return Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, strength * share)
    }
    Item {
        anchors.fill: parent
        opacity: scene.controlsShown ? 1 : 0
        // The controls' own fade, so text and its scrim leave together.
        Behavior on opacity { NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve } }
        // Each scrim holds its solved strength across the band the text is
        // in, then thins to nothing beyond it: as far again at the top, where
        // the band is one row of icons and a short ramp reads as a bar, half
        // as far at the bottom, where the band is already tall. The thinning
        // follows a smoothstep: a straight ramp leaves a visible crease where
        // it meets the flat part over a saturated picture.
        Rectangle {
            objectName: "motionTopScrim"
            width: parent.width; height: scene.topBand * 2
            visible: scene.topBand > 0
            gradient: Gradient {
                GradientStop { position: 0; color: scene.veil(scene.topScrim, 1) }
                GradientStop { position: 1 / 2; color: scene.veil(scene.topScrim, 1) }
                GradientStop { position: 1 / 2 + 1 / 8; color: scene.veil(scene.topScrim, 0.84375) }
                GradientStop { position: 1 / 2 + 2 / 8; color: scene.veil(scene.topScrim, 0.5) }
                GradientStop { position: 1 / 2 + 3 / 8; color: scene.veil(scene.topScrim, 0.15625) }
                GradientStop { position: 1; color: scene.veil(scene.topScrim, 0) }
            }
        }
        Rectangle {
            objectName: "motionBottomScrim"
            width: parent.width; height: scene.bottomBand * 1.5
            y: parent.height - height
            visible: scene.bottomBand > 0
            gradient: Gradient {
                GradientStop { position: 0; color: scene.veil(scene.bottomScrim, 0) }
                GradientStop { position: 1 / 12; color: scene.veil(scene.bottomScrim, 0.15625) }
                GradientStop { position: 2 / 12; color: scene.veil(scene.bottomScrim, 0.5) }
                GradientStop { position: 3 / 12; color: scene.veil(scene.bottomScrim, 0.84375) }
                GradientStop { position: 1 / 3; color: scene.veil(scene.bottomScrim, 1) }
                GradientStop { position: 1; color: scene.veil(scene.bottomScrim, 1) }
            }
        }
    }
}
