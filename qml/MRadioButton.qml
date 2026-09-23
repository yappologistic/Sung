import QtQuick
import QtQuick.Controls

// Material 3 radio button.
//
// A 20dp ring that fills with a dot when it is the one chosen. Unlike a
// checkbox it never clears itself: choosing another option is what turns it
// off, which is why it is drawn as one of a set rather than as a switch.
AbstractButton {
    id: control

    checkable: true
    autoExclusive: true
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitWidth: Theme.selectionTarget
    implicitHeight: Theme.selectionTarget
    Accessible.role: Accessible.RadioButton
    Accessible.checked: checked

    // A control that only reports a state, because something around it owns
    // the gesture, takes no input and yet is not disabled.
    property bool presentational: false
    readonly property bool dimmed: !enabled && !presentational
    readonly property color mark: dimmed ? Qt.rgba(Theme.text.r,Theme.text.g,Theme.text.b,Theme.disabledContentOpacity)
                                : checked ? Theme.primary : Theme.muted

    background: Item {
        Rectangle {
            objectName: "radioStateLayer"
            anchors.centerIn: parent
            width: Theme.selectionStateLayer; height: width
            radius: Theme.shapeFull(width)
            color: control.checked ? Theme.primary : Theme.text
            opacity: control.down ? Theme.pressedOpacity : control.visualFocus ? Theme.focusOpacity : control.hovered ? Theme.hoverOpacity : 0
            Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        }
    }
    contentItem: Item {
        Rectangle {
            objectName: "radioRing"
            anchors.centerIn: parent
            width: Theme.radioSize; height: width
            radius: Theme.shapeFull(width)
            color: "transparent"
            border.width: 2; border.color: control.mark
            Rectangle {
                objectName: "radioDot"
                anchors.centerIn: parent
                width: control.checked ? Theme.radioSize/2 : 0
                height: width
                radius: Theme.shapeFull(width)
                color: control.mark
                Behavior on width { enabled: app.motion; NumberAnimation { duration: Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastEffects } }
            }
        }
    }
}
