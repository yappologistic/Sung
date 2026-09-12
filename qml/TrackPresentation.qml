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
            NumberAnimation { target: presentation; property: "fade"; to: 0; duration: 90; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve }
            NumberAnimation { target: presentation; property: "offset"; to: -8*presentation.direction; duration: 90; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.exitCurve }
        }
        ScriptAction { script: {presentation.shown=presentation.track;presentation.offset=8*presentation.direction;} }
        ParallelAnimation {
            NumberAnimation { target: presentation; property: "fade"; to: 1; duration: 180; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve }
            NumberAnimation { target: presentation; property: "offset"; to: 0; duration: 220; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve }
        }
    }
    Component.onCompleted: {shown=track;fade=1;}
}
