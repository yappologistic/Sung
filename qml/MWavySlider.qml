import QtQuick
import QtQuick.Controls
import QtQuick.Shapes

// Material 3 Expressive wavy slider. The filled part of the track is a wave
// on a 4dp stroke with a 3dp amplitude on a 40dp wavelength, set back from
// the remaining track by a 4dp gap, with a 4dp stop at the far end. Material's
// wavy slider has no thumb: the fill edge carries the position, and a focus
// ring stands in for one while a keyboard drives the slider. The wave rides
// its wavelength only while the slider is being worked, and reduced motion
// lays the whole track straight.
Slider {
    id: w
    objectName: "wavySlider"
    property color inactiveColor: Theme.secondaryContainer
    stepSize: app.volumeStep/100
    from: 0; to: 1
    value: app.volume
    // Interaction writes the value property directly, which would drop the
    // binding that tracks the app's volume. A drag re-attaches it on release;
    // wheel and arrow keys never press, so moved() re-attaches for them.
    onMoved: {
        app.volume=value;
        if(!pressed)value=Qt.binding(function(){return app.volume});
    }
    onPressedChanged: if(!pressed)value=Qt.binding(function(){return app.volume})
    wheelEnabled: true
    implicitHeight: Theme.sliderHandleHeight.xsmall
    Accessible.name: "Volume"
    hoverEnabled: true
    readonly property bool working: pressed || visualFocus || hovered
    // The wave lies nearly flat at rest and lifts while the slider is worked.
    property real amplitude: app.motion ? (working ? 3 : 1.5) : 0
    Behavior on amplitude { NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve } }
    readonly property bool animating: visible && app.motion
        && Window.window !== null && Window.window.visible
        && Window.window.visibility !== Window.Minimized
    readonly property real stroke: 4
    readonly property real wavelength: 40
    readonly property real gap: 4
    readonly property real stopSize: 4
    readonly property real thumbWidth: Theme.sliderHandle
    readonly property real fill: visualPosition*(availableWidth-thumbWidth)
    ToolTip {visible:w.hovered&&!w.pressed;text:Math.round(w.value*100)+"%";delay:180}
    background: Item {
        id: track
        x: w.leftPadding; y: w.topPadding+(w.availableHeight-height)/2
        width: w.availableWidth; height: parent.height
        // The remaining track, set back from the wave by Material's gap.
        Rectangle {
            objectName: "wavySliderTrack"
            x: Math.min(parent.width,w.fill+w.gap)
            width: Math.max(0,parent.width-x-(w.value<1?w.stopSize+w.gap:0))
            anchors.verticalCenter: parent.verticalCenter
            height: w.stroke; radius: Theme.shapeFull(height)
            color: w.enabled ? w.inactiveColor : Theme.sliderQuiet(Theme.disabledTrackOpacity)
        }
        // The filled wave. The geometry is static and the render thread moves
        // it, so nothing is painted per frame; holding the wave still against
        // the track while the clip moves is what keeps it one shape.
        Item {
            objectName: "wavySliderRun"
            width: Math.max(0,w.fill); height: parent.height
            clip: true; layer.enabled: true; layer.smooth: true
            Item {
                id: hold; width: parent.width+w.wavelength; height: parent.height
                Shape {
                    id: wave
                    width: hold.width; height: hold.height
                    preferredRendererType: Shape.CurveRenderer
                    ShapePath {
                        strokeColor: w.enabled ? Theme.primary : Theme.sliderQuiet(Theme.disabledContentOpacity)
                        strokeWidth: w.stroke; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                        PathPolyline {
                            path: {
                                const points=[],middle=track.height/2
                                for(let x=0;x<=wave.width+2;x+=2)
                                    points.push(Qt.point(x,middle+w.amplitude*Math.sin(x*2*Math.PI/w.wavelength)))
                                return points
                            }
                        }
                    }
                    XAnimator {
                        target: wave; from: 0; to: -w.wavelength
                        duration: 1200; loops: Animation.Infinite
                        running: w.animating && w.working
                    }
                }
            }
        }
        // Material keeps a stop indicator at the end of an unfilled track.
        Rectangle {
            objectName: "wavySliderStop"
            visible: w.value<1
            x: parent.width-w.stopSize; anchors.verticalCenter: parent.verticalCenter
            width: w.stopSize; height: w.stopSize; radius: Theme.shapeFull(width)
            color: w.enabled ? Theme.primary : Theme.sliderQuiet(Theme.disabledContentOpacity)
        }
    }
    handle: Item {
        objectName: "wavySliderHandle"
        x: w.leftPadding+w.visualPosition*(w.availableWidth-Theme.sliderHandle)
        y: w.topPadding+(w.availableHeight-height)/2
        width: Theme.sliderHandle; height: parent.height
        Rectangle {
            objectName: "wavySliderFocusRing"
            anchors.centerIn: parent
            width: parent.width+12; height: 16; radius: Theme.shapeSmall
            color: "transparent"; border.width: 2; border.color: Theme.focusRing
            visible: w.visualFocus
        }
    }
}
