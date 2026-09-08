import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
Slider {
    id: s
    readonly property bool handlesArrowKeys: true
    objectName: "seekBar"
    stepSize: volumeMode ? app.volumeStep/100 : 5000
    implicitHeight: 40
    property bool volumeMode: false
    readonly property real thumbWidth: volumeMode ? 12 : 4
    readonly property real thumbCenter: visualPosition * (availableWidth - thumbWidth) + thumbWidth/2
    readonly property real trackGap: volumeMode ? 0 : 6
    wheelEnabled: volumeMode
    from: 0; to: volumeMode ? 1 : Math.max(1,app.duration)
    value: volumeMode ? app.volume : app.position
    enabled: volumeMode || app.duration>0
    onMoved: { if(volumeMode) app.volume=value; else app.seek(value); }
    Accessible.name: volumeMode ? "Volume" : "Playback position"
    background: Item {
        id: track
        x: s.leftPadding; y: s.topPadding+(s.availableHeight-height)/2
        width: s.availableWidth; height: s.volumeMode ? 4 : 14
        Rectangle { x: Math.min(parent.width,s.thumbCenter+s.thumbWidth/2+s.trackGap); width: parent.width-x; anchors.verticalCenter: parent.verticalCenter; height: 4; radius: 2; color: Theme.high }
        Rectangle { visible: s.volumeMode; width: s.thumbCenter; height: 4; radius: 2; color: Theme.primary }
        Item {
            id: played; objectName: "playedWave"
            visible: !s.volumeMode
            width: Math.max(0,s.thumbCenter-s.thumbWidth/2-s.trackGap); height: track.height; clip: true; layer.enabled: true; layer.smooth: true
            Shape {
                id: wave; objectName: "seekWave"
                width: track.width+28; height: track.height
                preferredRendererType: Shape.CurveRenderer
                property real amplitude: app.playing ? 3 : 1
                Behavior on amplitude { NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve } }
                ShapePath {
                    strokeColor: Theme.primary; strokeWidth: 3; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                    PathPolyline { path: {if(s.volumeMode)return [];let points=[];for(let x=0;x<=wave.width+3;x+=3)points.push(Qt.point(x,7+wave.amplitude*Math.sin(x*Math.PI/14)));return points;} }
                }
                // A render-thread transform moves static geometry; no per-frame JS painting.
                XAnimator { target: wave; from: 0; to: -28; duration: 1400; loops: Animation.Infinite; running: !s.volumeMode && app.playing && app.motion && s.visible && played.width>0 && s.Window.window && s.Window.window.visible && s.Window.window.visibility!==Window.Minimized }
            }
        }
        Rectangle { x: parent.width-4; y: (parent.height-4)/2; width: 4; height: 4; radius: 2; color: Theme.muted; visible: !s.volumeMode && s.thumbCenter+s.thumbWidth/2+s.trackGap<parent.width-4 }
    }
    handle: Rectangle {
        x: s.leftPadding+s.visualPosition*(s.availableWidth-width)
        y: s.topPadding+(s.availableHeight-height)/2
        width: s.volumeMode ? 12 : 4; height: s.volumeMode ? 12 : s.pressed ? 30 : 24
        radius: width/2; color: Theme.primary
        Behavior on height { enabled: app.motion; SpringAnimation { spring: 5; damping: 0.8; epsilon: 0.1 } }
        border.width: s.activeFocus ? 2 : 0; border.color: Theme.text
    }
}
