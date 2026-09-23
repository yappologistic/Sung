import QtQuick
Item {
    id: presentation;objectName:"trackPresentation"
    property var track: app.current
    property var shown: ({})
    property real fade: 1
    property real offset: 0
    property int direction: 1
    readonly property bool canAnimate: app.motion && visible && Window.window && Window.window.visible && Window.window.visibility!==Window.Minimized
    function update() {
        change.stop();
        if(!canAnimate || !shown.id || !track.id || shown.id===track.id){shown=track;fade=1;offset=0;return;}
        direction=app.playbackDirection;offset=0;change.start();
    }
    onTrackChanged: update()
    onCanAnimateChanged: if(!canAnimate){change.stop();shown=track;fade=1;offset=0;}
    SequentialAnimation {
        id: change
        ParallelAnimation {
            // FastEffects fades the outgoing track, as Menu.kt:1829-1831 does.
            NumberAnimation { objectName: "presentationFadeOut"; target: presentation; property: "fade"; to: 0; duration: Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastEffects }
            // PaneMotion.kt:150-177 uses DefaultSpatial for pane movement.
            NumberAnimation { objectName: "presentationOffsetOut"; target: presentation; property: "offset"; to: -8*presentation.direction; duration: Theme.springSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial }
        }
        ScriptAction { script: {presentation.shown=presentation.track;presentation.offset=8*presentation.direction;} }
        ParallelAnimation {
            // DefaultEffects returns opacity without overshooting it.
            NumberAnimation { objectName: "presentationFadeIn"; target: presentation; property: "fade"; to: 1; duration: Theme.springEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springEffects }
            // PaneMotion.kt:150-177 uses DefaultSpatial for the incoming pane.
            NumberAnimation { objectName: "presentationOffsetIn"; target: presentation; property: "offset"; to: 0; duration: Theme.springSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial }
        }
    }
    Component.onCompleted: {shown=track;fade=1;}
}
