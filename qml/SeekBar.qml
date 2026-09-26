import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
Slider {
    id: s
    readonly property bool handlesArrowKeys: true
    objectName: "seekBar"
    stepSize: volumeMode ? app.volumeStep/100 : 5000
    // Material's expressive slider: a 16dp track, a handle that is a 4dp bar
    // as tall as the touch target, and a 6dp gap held open on either side of
    // it so the handle never sits on the position it is reporting.
    implicitHeight: Theme.sliderHandleHeight.xsmall
    // A host that gives the slider less room than Material's handle asks for,
    // as the mini player does, gets the handle its room allows rather than one
    // that hangs out of it.
    readonly property real handleHeight: Math.min(Theme.sliderHandleHeight.xsmall, height)
    property bool volumeMode: false
    property color inactiveColor:Theme.secondaryContainer
    readonly property real thumbWidth: Theme.sliderHandle
    readonly property real thumbCenter: visualPosition * (availableWidth - thumbWidth) + thumbWidth/2
    readonly property real trackGap: Theme.sliderGap
    readonly property real trackHeight: Theme.sliderTrack.xsmall
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
    // Seeking by wheel goes straight to playback: assigning the slider's value
    // here would replace the binding that keeps it following the track.
    WheelHandler {
        enabled: !s.volumeMode && s.enabled && app.duration>0
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        property real carried: 0
        onWheel: event=>{
            const ticks=(event.angleDelta.y||event.angleDelta.x)/120;
            carried+=ticks;
            const whole=carried>0?Math.floor(carried):Math.ceil(carried);
            if(!whole)return;
            carried-=whole;
            app.seek(Math.max(0,Math.min(app.duration,app.position+whole*5000)));
        }
    }
    readonly property real previewValue: (interacting || (visualFocus && !seekHover.hovered)) ? value : from+(to-from)*Math.max(0,Math.min(1,(seekHover.point.position.x-leftPadding-thumbWidth/2)/Math.max(1,availableWidth-thumbWidth)))
    readonly property string previewLine: {app.lyricLines;app.lyricOffset;return !volumeMode && (seekHover.hovered || interacting || visualFocus)?app.previewLyric(previewValue):"";}
    ToolTip {
        objectName: "seekPreview"; visible: !s.volumeMode && s.enabled && s.visible && (seekHover.hovered || s.interacting || s.visualFocus)
        delay: s.interacting || s.visualFocus ? 0 : 180; timeout: -1
        x: Math.max(0,Math.min(s.width-width, ((s.interacting || (s.visualFocus && !seekHover.hovered))?s.thumbCenter:seekHover.point.position.x)-width/2))
        // This is a slider's value indicator, not a tooltip and not a card.
        // Material draws it against the theme, on the inverse surface with the
        // ink that goes there, and leaves 12dp between it and the track.
        y: -height-12; width: s.previewLine?Math.min(280,Math.max(120,s.width)):76
        padding: 10
        contentItem: Column {
            spacing: 4
            SungText { width: parent.width; text: app.formatTime(s.previewValue); color: Theme.inverseSurfaceText; font.pixelSize: Theme.labelLarge; labelRole: true; horizontalAlignment: Text.AlignHCenter }
            SungText { width: parent.width; visible: !!s.previewLine; text: s.previewLine; color: Theme.inverseSurfaceText; font.pixelSize: Theme.bodySmall; wrapMode: Text.Wrap; maximumLineCount: 2; horizontalAlignment: Text.AlignHCenter }
        }
        background: Rectangle { objectName: "seekPreviewContainer"; color: Theme.inverseSurface; radius: Theme.shapeFull(height) }
    }
    background: Item {
        id: track
        x: s.leftPadding; y: s.topPadding+(s.availableHeight-height)/2
        width: s.availableWidth; height: s.trackHeight
        Rectangle {
            objectName: "seekInactiveTrack"; x: Math.min(parent.width,s.thumbCenter+s.thumbWidth/2+s.trackGap)
            width: parent.width-x; height: parent.height; radius: Theme.shapeFull(height)
            // Slider.kt:3085-3088 gives the corner facing the handle 2dp.
            topLeftRadius: 2; bottomLeftRadius: 2
            color: s.enabled ? s.inactiveColor : Theme.sliderQuiet(Theme.disabledTrackOpacity)
        }
        Rectangle {
            objectName: "seekActiveTrack"; visible: s.volumeMode
            width: Math.max(0,s.thumbCenter-s.thumbWidth/2-s.trackGap); height: parent.height
            radius: Theme.shapeFull(height); topRightRadius: 2; bottomRightRadius: 2
            color: s.enabled ? Theme.primary : Theme.sliderQuiet(Theme.disabledContentOpacity)
        }
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
                    PathPolyline { path: {if(s.volumeMode)return [];const mid=s.trackHeight/2;let points=[];for(let x=0;x<=wave.width+3;x+=3)points.push(Qt.point(x,mid+wave.amplitude*Math.sin(x*Math.PI/14)));return points;} }
                }
                // A render-thread transform moves static geometry; no per-frame JS painting.
                // WavyProgressIndicator.kt:106-107 advances one wavelength
                // each second. This path's sin(x*pi/14) repeats every 28px.
                XAnimator { objectName: "seekWaveMotion"; target: wave; from: 0; to: -28; duration: 1000; loops: Animation.Infinite; running: !s.volumeMode && app.playing && app.motion && s.visible && played.width>0 && s.Window.window && s.Window.window.visible && s.Window.window.visibility!==Window.Minimized }
            }
        }
        // Material marks where the track ends, so a position short of the end
        // still says there is more of it.
        Rectangle {
            objectName: "seekStop"
            x: parent.width-Theme.sliderStop-Theme.sliderGap; anchors.verticalCenter: parent.verticalCenter
            width: Theme.sliderStop; height: Theme.sliderStop; radius: Theme.shapeFull(height)
            // Slider.kt:1679-1684 draws the stop with the disabled active
            // track colour, onSurface at 38%, when the slider is disabled.
            color: s.enabled ? Theme.primary : Theme.sliderQuiet(Theme.disabledContentOpacity)
            visible: s.thumbCenter+s.thumbWidth/2+s.trackGap < x
        }
    }
    handle: Item {
        x: s.leftPadding+s.visualPosition*(s.availableWidth-Theme.sliderHandle)
        y: s.topPadding+(s.availableHeight-height)/2
        width: Theme.sliderHandle; height: s.handleHeight
        Rectangle {
            objectName: "seekHandle"
            anchors.centerIn: parent
            // Slider.kt:2472-2497 narrows the handle on focus, press and drag;
            // SliderTokens.FocusHandleWidth and PressedHandleWidth are 2dp.
            width: s.interacting || s.visualFocus ? Theme.sliderHandlePressed : Theme.sliderHandle
            height: parent.height
            radius: Theme.shapeFull(width)
            color: s.enabled ? Theme.primary : Theme.sliderQuiet(Theme.disabledContentOpacity)
            Behavior on width { enabled: app.motion; NumberAnimation { duration: Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastEffects } }
        }
        MFocusRing {
            objectName: "sliderFocusRing"
            targetRadius: Theme.shapeFull(parent.width)
            visible: s.visualFocus
        }
    }
}
