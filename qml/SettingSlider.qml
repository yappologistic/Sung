import QtQuick
import QtQuick.Controls
Slider {
    id: slider
    readonly property bool handlesArrowKeys: true
    implicitHeight: 40
    readonly property real thumbCenter: visualPosition * (availableWidth - 4) + 2
    background: Item {
        x: slider.leftPadding; y: slider.topPadding+(slider.availableHeight-height)/2
        width: slider.availableWidth; height: 4
        Rectangle { objectName: "sliderActiveTrack"; width: Math.max(0, slider.thumbCenter-8); height: 4; radius: 2; color: Theme.primary }
        Rectangle { objectName: "sliderInactiveTrack"; x: Math.min(parent.width, slider.thumbCenter+8); width: parent.width-x; height: 4; radius: 2; color: Theme.high }
        Rectangle { x: parent.width-4; width: 4; height: 4; radius: 2; color: Theme.muted; visible: slider.thumbCenter+8<parent.width-4 }
    }
    handle: Rectangle {
        x: slider.leftPadding+slider.visualPosition*(slider.availableWidth-width)
        y: slider.topPadding+(slider.availableHeight-height)/2
        width: 4; height: slider.pressed?30:24; radius: 2; color: Theme.primary
        border.width: slider.visualFocus?2:0; border.color: Theme.text
        Behavior on height { enabled: app.motion; SpringAnimation { spring: 5; damping: 0.8; epsilon: 0.1 } }
    }
}
