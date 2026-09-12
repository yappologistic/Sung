import QtQuick
import QtQuick.Controls
TextField {
    id: field
    property string label: ""
    property color labelSurface: Theme.container
    readonly property bool floatingLabel: activeFocus || length > 0 || preeditText.length > 0
    readonly property bool handlesTextInput: true
    implicitHeight: 56
    leftPadding: 16; rightPadding: 16
    topPadding: 16; bottomPadding: 16
    selectByMouse: true
    verticalAlignment: TextInput.AlignVCenter
    font.family: Theme.fontFamily; font.pixelSize: Theme.bodyLarge
    color: Theme.text; placeholderTextColor: Theme.muted
    selectionColor: Theme.primary; selectedTextColor: Theme.primaryText
    Accessible.name: label || placeholderText
    background: Rectangle {
        radius: 4; color: "transparent"
        border.width: field.activeFocus ? 2 : 1
        border.color: field.activeFocus ? Theme.primary : Theme.controlOutline
        Behavior on border.color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
    }
    SungText {
        id: fieldLabel; objectName: "fieldLabel"
        x: field.leftPadding
        y: field.floatingLabel ? -height/2 : (field.height-height)/2
        text: field.label; visible: text.length > 0
        font.pixelSize: field.floatingLabel ? Theme.labelMedium : Theme.bodyLarge
        color: field.activeFocus ? Theme.primary : Theme.muted
        Accessible.ignored: true
        Rectangle { anchors.fill: parent; anchors.leftMargin: -4; anchors.rightMargin: -4; color: field.labelSurface; visible: field.floatingLabel; z: -1 }
        Behavior on y { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
        Behavior on font.pixelSize { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
    }
}
