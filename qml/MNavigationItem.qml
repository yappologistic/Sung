import QtQuick
import QtQuick.Controls

AbstractButton {
    id: control
    property string symbol: ""
    property bool selected: false
    implicitWidth: 80
    implicitHeight: 68
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.PageTab
    Accessible.name: text
    Accessible.selected: selected
    background: Item {
        Rectangle {
            id: pill
            anchors.horizontalCenter: parent.horizontalCenter
            width: 64; height: 40
            radius: control.down ? 14 : 20
            color: control.selected ? Theme.high : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
            Behavior on radius { enabled: app.motion; SpringAnimation { spring: 5; damping: 0.8; mass: 0.8 } }
            Rectangle {
                anchors.fill: parent; radius: parent.radius
                color: Theme.primary
                opacity: control.down || control.visualFocus ? Theme.pressedOpacity : control.hovered ? Theme.hoverOpacity : 0
                Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
            }
        }
        Rectangle {
            objectName: "navigationFocusRing"
            anchors.fill: parent; anchors.margins: 2; radius: 18
            color: "transparent"; border.color: Theme.primary; border.width: 2
            visible: control.visualFocus
        }
    }
    contentItem: Item {
        Icon { anchors.horizontalCenter: parent.horizontalCenter; y: 8; name: control.symbol; ink: control.selected ? Theme.primary : Theme.text; Accessible.ignored: true }
        SungText {
            objectName: "navigationLabel"
            y: 42; width: parent.width; height: 20
            text: control.text; horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.labelMedium
            font.weight: control.selected ? Font.DemiBold : Font.Medium
            color: control.selected ? Theme.primary : Theme.muted
            Accessible.ignored: true
        }
    }
}
