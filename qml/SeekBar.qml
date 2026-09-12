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
    property color inactiveColor:Theme.high
    readonly property real thumbWidth: volumeMode ? 12 : 4
    readonly property real thumbCenter: visualPosition * (availableWidth - thumbWidth) + thumbWidth/2
    readonly property real trackGap: volumeMode ? 0 : 6
    wheelEnabled: volumeMode
    from: 0; to: volumeMode ? 1 : Math.max(1,app.duration)
    value: volumeMode ? app.volume : fineSeeking ? fineValue : app.position
    enabled: volumeMode || app.duration>0
    onMoved: { if(volumeMode) app.volume=value; else app.seek(value); }
    Accessible.name: volumeMode ? "Volume" : "Playback position"
    property bool fineSeeking:false
    property real fineValue:0
    readonly property bool interacting:pressed || fineSeeking
    Accessible.description: volumeMode?"Volume":"Hold Shift and drag for precise seeking. Shift and arrow keys seek by 100 milliseconds."
    Keys.onLeftPressed:event=>{if(!volumeMode && (event.modifiers&Qt.ShiftModifier)){app.seek(app.position-100);event.accepted=true;}else event.accepted=false;}
    Keys.onRightPressed:event=>{if(!volumeMode && (event.modifiers&Qt.ShiftModifier)){app.seek(app.position+100);event.accepted=true;}else event.accepted=false;}
    Keys.onEscapePressed:event=>{if(fineSeeking){fineSeeking=false;event.accepted=true;}else event.accepted=false;}
    MouseArea {anchors.fill:parent;enabled:!s.volumeMode && s.enabled;acceptedButtons:Qt.LeftButton
        property real anchorX:0;property real anchorValue:0
        onPressed:mouse=>{if(!(mouse.modifiers&Qt.ShiftModifier)){mouse.accepted=false;return;}anchorX=mouse.x;anchorValue=app.position;s.fineValue=anchorValue;s.fineSeeking=true;s.forceActiveFocus();}
        onPositionChanged:mouse=>{if(s.fineSeeking)s.fineValue=Math.max(s.from,Math.min(s.to,anchorValue+(mouse.x-anchorX)*(s.to-s.from)/Math.max(1,s.availableWidth)*0.1));}
        onReleased:{if(s.fineSeeking){app.seek(Math.round(s.fineValue));s.fineSeeking=false;}}
        onCanceled:s.fineSeeking=false
    }
    ToolTip {visible:s.volumeMode&&s.hovered;text:Math.round(app.volume*100)+"%";delay:180}
    hoverEnabled: true
    HoverHandler { id: seekHover }
    readonly property real previewValue: (interacting || (visualFocus && !seekHover.hovered)) ? value : from+(to-from)*Math.max(0,Math.min(1,(seekHover.point.position.x-leftPadding-thumbWidth/2)/Math.max(1,availableWidth-thumbWidth)))
    readonly property string previewLine: {app.lyricLines;app.lyricOffset;return !volumeMode && (seekHover.hovered || interacting || visualFocus)?app.previewLyric(previewValue):"";}
    ToolTip {
        objectName: "seekPreview"; visible: !s.volumeMode && s.enabled && s.visible && (seekHover.hovered || s.interacting || s.visualFocus)
        delay: s.interacting || s.visualFocus ? 0 : 180; timeout: -1
        x: Math.max(0,Math.min(s.width-width, ((s.interacting || (s.visualFocus && !seekHover.hovered))?s.thumbCenter:seekHover.point.position.x)-width/2))
        y: -height-6; width: s.previewLine?Math.min(280,Math.max(120,s.width)):76
        padding: 10
        contentItem: Column {
            spacing: 4
            SungText { width: parent.width; text: app.formatTime(s.previewValue); color: Theme.text; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter }
            SungText { width: parent.width; visible: !!s.previewLine; text: s.previewLine; color: Theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap; maximumLineCount: 2; horizontalAlignment: Text.AlignHCenter }
        }
        background: Rectangle { color: Theme.high; radius: 12; border.color: Theme.outline }
    }
    background: Item {
        id: track
        x: s.leftPadding; y: s.topPadding+(s.availableHeight-height)/2
        width: s.availableWidth; height: s.volumeMode ? 4 : 14
        Rectangle { x: Math.min(parent.width,s.thumbCenter+s.thumbWidth/2+s.trackGap); width: parent.width-x; anchors.verticalCenter: parent.verticalCenter; height: 4; radius: 2; color: s.inactiveColor }
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
        width: s.volumeMode ? 12 : 4; height: s.volumeMode ? 12 : s.interacting ? 30 : 24
        radius: width/2; color: Theme.primary
        Behavior on height { enabled: app.motion; SpringAnimation { spring: 5; damping: 0.8; epsilon: 0.1 } }
        Rectangle {
            objectName: "sliderFocusRing"
            anchors.centerIn: parent
            width: parent.width+12; height: parent.height+12; radius: 8
            color: "transparent"; border.width: 2; border.color: Theme.primary
            visible: s.visualFocus
        }
    }
}
