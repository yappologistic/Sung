import QtQuick
import QtQuick.Controls
Switch {
    id: control
    implicitHeight: 48
    hoverEnabled: true
    opacity: enabled ? 1 : 0.38
    indicator: Rectangle {
        implicitWidth: 52; implicitHeight: 32
        x: control.leftPadding; y: (control.height-height)/2
        radius: 16
        color: control.checked ? Theme.primary : Theme.high
        border.width: control.checked ? 0 : 2; border.color: Theme.controlOutline
        Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        Rectangle {
            anchors.fill: parent; anchors.margins: -4; radius: 20
            color: "transparent"; border.width: 2; border.color: Theme.primary
            visible: control.visualFocus
        }
        Rectangle {
            x: (control.checked ? 36 : 16) - width/2
            anchors.verticalCenter: parent.verticalCenter
            width: 40; height: 40; radius: 20
            color: control.checked ? Theme.primary : Theme.text
            opacity: control.down || control.visualFocus ? Theme.pressedOpacity : control.hovered ? Theme.hoverOpacity : 0
            Behavior on opacity { NumberAnimation { duration: Theme.fast } }
        }
        Rectangle {
            x: (control.checked ? 36 : 16) - width/2
            anchors.verticalCenter: parent.verticalCenter
            width: control.down ? 28 : control.checked ? 24 : 16; height: width; radius: width/2
            color: control.checked ? Theme.primaryText : Theme.muted
            Behavior on x { enabled: app.motion; SpringAnimation { spring: 5; damping: 0.8 } }
            Behavior on width { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        }
    }
    contentItem: SungText { text: control.text; leftPadding: 64; font.pixelSize: Theme.bodyLarge }
}
