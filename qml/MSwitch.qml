import QtQuick
import QtQuick.Controls
Switch {
    id: control
    implicitHeight: 48
    indicator: Rectangle {
        implicitWidth: 52; implicitHeight: 32
        x: control.leftPadding; y: (control.height-height)/2
        radius: 16
        color: control.checked ? Theme.primary : Theme.high
        border.width: control.checked ? 0 : 2; border.color: control.activeFocus ? Theme.primary : Theme.outline
        Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        Rectangle {
            x: control.checked ? parent.width-width-4 : 8
            anchors.verticalCenter: parent.verticalCenter
            width: control.checked ? 24 : 16; height: width; radius: width/2
            color: control.checked ? Theme.primaryText : Theme.muted
            Behavior on x { enabled: app.motion; SpringAnimation { spring: 5; damping: 0.8 } }
            Behavior on width { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        }
    }
    contentItem: SungText { text: control.text; leftPadding: 64; font.pixelSize: 15 }
}
