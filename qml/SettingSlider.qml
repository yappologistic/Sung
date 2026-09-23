import QtQuick
import QtQuick.Controls

// Material 3 slider, at the extra small size.
//
// Material's expressive slider is a track you can see rather than a rule with a
// dot on it. The track is 16dp and fully rounded, the handle is a 4dp bar as
// tall as the touch target, and a 6dp gap is held open on either side of it so
// the handle never sits on the value it is setting. A stop indicator marks the
// end of the track while the value is short of it.
Slider {
    id: slider
    readonly property bool handlesArrowKeys: true
    // Material shows the value in a label above the handle while the slider is
    // being moved, so the number is where the eye already is.
    property string valueLabel: ""
    // One of Material's five slider sizes. Only the track and the handle grow.
    property string size: "xsmall"
    readonly property real trackHeight: Theme.sliderTrack[size]
    readonly property real fullHandle: Theme.sliderHandleHeight[size]
    // A host that gives the slider less room than Material's handle asks for
    // gets the handle its room allows rather than one that hangs out of it.
    readonly property real handleHeight: Math.min(fullHandle, height)

    implicitHeight: fullHandle
    readonly property real thumbCenter: visualPosition*(availableWidth-Theme.sliderHandle)+Theme.sliderHandle/2
    readonly property real trackEnd: availableWidth-Theme.sliderStop-Theme.sliderGap

    background: Item {
        x: slider.leftPadding; y: slider.topPadding+(slider.availableHeight-height)/2
        width: slider.availableWidth; height: slider.trackHeight
        Rectangle {
            objectName: "sliderActiveTrack"
            width: Math.max(0, slider.thumbCenter-Theme.sliderHandle/2-Theme.sliderGap)
            height: parent.height; radius: Theme.shapeFull(height)
            // Slider.kt:3085-3088 rounds the track corner facing the handle to 2dp.
            topRightRadius: 2; bottomRightRadius: 2
            color: slider.enabled ? Theme.primary : Theme.sliderQuiet(Theme.disabledContentOpacity)
        }
        Rectangle {
            objectName: "sliderInactiveTrack"
            x: Math.min(parent.width, slider.thumbCenter+Theme.sliderHandle/2+Theme.sliderGap)
            width: parent.width-x; height: parent.height
            radius: Theme.shapeFull(height)
            topLeftRadius: 2; bottomLeftRadius: 2
            color: slider.enabled ? Theme.secondaryContainer : Theme.sliderQuiet(Theme.disabledTrackOpacity)
        }
        Rectangle {
            objectName: "sliderStop"
            x: slider.trackEnd; anchors.verticalCenter: parent.verticalCenter
            width: Theme.sliderStop; height: Theme.sliderStop
            // Slider.kt:1679-1684 uses disabled active track ink for the stop.
            radius: Theme.shapeFull(height)
            color: slider.enabled ? Theme.primary : Theme.sliderQuiet(Theme.disabledContentOpacity)
            visible: slider.thumbCenter+Theme.sliderHandle/2+Theme.sliderGap < slider.trackEnd
        }
    }
    handle: Item {
        x: slider.leftPadding+slider.visualPosition*(slider.availableWidth-Theme.sliderHandle)
        y: slider.topPadding+(slider.availableHeight-height)/2
        width: Theme.sliderHandle; height: slider.handleHeight
        Rectangle {
            objectName: "sliderHandle"
            anchors.centerIn: parent
            // Slider.kt:2472-2497 and SliderTokens.FocusHandleWidth narrow the
            // handle to 2dp on focus as well as press and drag.
            width: slider.pressed || slider.visualFocus ? Theme.sliderHandlePressed : Theme.sliderHandle
            height: parent.height
            radius: Theme.shapeFull(width)
            color: slider.enabled ? Theme.primary : Theme.sliderQuiet(Theme.disabledContentOpacity)
            Behavior on width { enabled: app.motion; NumberAnimation { duration: Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastEffects } }
        }
        Rectangle {
            objectName: "sliderFocusRing"
            anchors.centerIn: parent
            width: parent.width+12; height: parent.height+4; radius: Theme.shapeSmall
            color: "transparent"; border.width: 2; border.color: Theme.focusRing
            visible: slider.visualFocus
        }
    }
    ToolTip {
        objectName: "sliderValueLabel"
        visible: slider.valueLabel.length > 0 && (slider.pressed || slider.visualFocus)
        delay: 0; timeout: -1
        x: slider.leftPadding + slider.visualPosition*(slider.availableWidth-width)
        // Material holds the value indicator a fixed distance off the handle.
        y: -height - 12
        padding: 8
        contentItem: SungText { objectName: "sliderValueText"; text: slider.valueLabel; color: Theme.inverseSurfaceText; font.pixelSize: Theme.labelLarge; labelRole: true }
        background: Rectangle { color: Theme.inverseSurface; radius: Theme.shapeFull(height) }
    }
}
