import QtQuick
import QtQuick.Controls
Slider {
    id: slider
    implicitHeight: 40
    background: Rectangle {
        x: slider.leftPadding; y: slider.topPadding+(slider.availableHeight-height)/2
        width: slider.availableWidth; height: 4; radius: 2; color: Theme.high
        Rectangle { width: slider.visualPosition*parent.width; height: parent.height; radius: 2; color: Theme.primary }
    }
    handle: Rectangle {
        x: slider.leftPadding+slider.visualPosition*(slider.availableWidth-width)
        y: slider.topPadding+(slider.availableHeight-height)/2
        width: 4; height: slider.pressed?30:24; radius: 2; color: Theme.primary
        border.width: slider.visualFocus?2:0; border.color: Theme.text
        Behavior on height { enabled: app.motion; SpringAnimation { spring: 5; damping: 0.8; epsilon: 0.1 } }
    }
}
