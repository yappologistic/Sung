import QtQuick
import QtQuick.Controls
MenuItem {
    id: control
    implicitHeight: 48
    height: visible ? implicitHeight : 0
    leftPadding: 14; rightPadding: 14
    palette.windowText: control.enabled ? Theme.text : Theme.muted
    indicator: Icon {
        name: "check"; size: 20; ink: control.enabled ? Theme.text : Theme.muted
        x: control.mirrored ? control.width-width-control.rightPadding : control.leftPadding
        y: (control.height-height)/2
        visible: control.checkable && control.checked
    }
    contentItem: SungText {
        objectName: "menuItemLabel"
        text: control.text; color: control.enabled ? Theme.text : Theme.muted
        opacity: control.enabled ? 1 : 0.5; font.pixelSize: Theme.bodyLarge
        readonly property real indicatorSpace: control.checkable && control.indicator ? control.indicator.width+12 : 0
        leftPadding: control.mirrored ? 0 : indicatorSpace
        rightPadding: control.mirrored ? indicatorSpace : 0
    }
    background: Item {
        Rectangle {
            anchors.fill: parent; radius: 12; color: Theme.text
            opacity: control.down || control.visualFocus ? Theme.pressedOpacity : control.highlighted ? Theme.hoverOpacity : 0
            Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        }
        Rectangle { anchors.fill: parent; anchors.margins: 2; radius: 10; color: "transparent"; border.color: Theme.primary; border.width: 2; visible: control.visualFocus }
    }
}
