import QtQuick
import QtQuick.Controls

AbstractButton {
    id: control
    property bool selected: false
    property bool selectable: true
    implicitWidth: label.implicitWidth + 24 + (selectable ? 26 : 0)
    implicitHeight: 40
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: text
    Accessible.role: selectable ? Accessible.CheckBox : Accessible.Button
    Accessible.checkable: selectable
    Accessible.checked: selected
    opacity: enabled ? 1 : 0.38
    background: Rectangle {
        y: 4; height: control.height - 8; radius: 8
        color: control.selected ? Theme.primaryContainer : "transparent"
        border.width: control.selected ? 0 : 1
        border.color: Theme.controlOutline
        Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        Rectangle {
            anchors.fill: parent; radius: parent.radius
            color: control.selected ? Theme.containerText : Theme.text
            opacity: control.down || control.visualFocus ? Theme.pressedOpacity : control.hovered ? Theme.hoverOpacity : 0
            Behavior on opacity { NumberAnimation { duration: Theme.fast } }
        }
        Rectangle {
            anchors.fill: parent; anchors.margins: -3; radius: 11
            color: "transparent"; border.width: 2; border.color: Theme.primary
            visible: control.visualFocus
        }
    }
    contentItem: Item {
        Icon { x: 12; name: "check"; size: 18; visible: control.selectable && control.selected; ink: Theme.containerText; anchors.verticalCenter: parent.verticalCenter }
        SungText {
            id: label; x: control.selectable && control.selected ? 38 : (parent.width-implicitWidth)/2
            text: control.text; font.pixelSize: Theme.labelLarge; font.weight: Font.Medium
            color: control.selected ? Theme.containerText : Theme.text; anchors.verticalCenter: parent.verticalCenter
            Behavior on x { NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
        }
    }
}
