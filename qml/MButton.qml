import QtQuick
import QtQuick.Controls
AbstractButton {
    id: control
    property string symbol: ""
    property string tip: text
    property bool leftAligned: false
    property bool filled: false
    property bool tonal: false
    property bool selected: false
    property color ink: filled ? Theme.primaryText : selected ? Theme.primary : Theme.text
    implicitWidth: text.length ? buttonLabel.implicitWidth + (symbol.length ? 32 : 0) + 36 : 48
    implicitHeight: 48
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : 0.38
    Accessible.name: tip
    ToolTip {
        visible: control.hovered && control.tip.length > 0
        delay: 650; text: control.tip
        padding: 10
        contentItem: SungText { text: control.tip; font.pixelSize: 12; color: Theme.background }
        background: Rectangle { color: Theme.text; radius: 8 }
    }
    background: Rectangle {
        radius: control.down ? 14 : control.height / 2
        color: control.filled ? Theme.primary : control.tonal || control.selected ? Theme.high : "transparent"
        border.color: control.visualFocus ? Theme.primary : "transparent"
        border.width: 2
        Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        Behavior on radius { enabled: app.motion; SpringAnimation { spring: 5; damping: 0.8; mass: 0.8 } }
        Rectangle {
            anchors.fill: parent; radius: parent.radius
            color: control.ink
            opacity: control.down ? 0.14 : control.hovered ? 0.08 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        }
    }
    contentItem: Item {
        Row {
            id: contentRow; anchors.verticalCenter: parent.verticalCenter; x: control.leftAligned?18:(parent.width-width)/2; spacing: 8
            Icon { visible: control.symbol.length > 0; name: control.symbol; ink: control.ink; anchors.verticalCenter: parent.verticalCenter }
            SungText { id: buttonLabel; visible: control.text.length > 0; text: control.text; color: control.ink; width: control.leftAligned ? Math.max(0,control.width-(control.symbol.length?76:36)) : implicitWidth; elide: Text.ElideRight; font.weight: Font.Medium; anchors.verticalCenter: parent.verticalCenter }
        }
    }
    scale: down ? 0.96 : 1
    Behavior on scale { enabled: app.motion; SpringAnimation { spring: 5; damping: 0.8; mass: 0.6 } }
}
