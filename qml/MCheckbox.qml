import QtQuick
import QtQuick.Controls

// Material 3 checkbox.
//
// An 18dp square with a 2dp corner, drawn as an outline until it is checked and
// as a filled container after. The square is small on purpose: the state layer
// around it is 40dp and the target 48dp, so what you aim at is far larger than
// what you see.
AbstractButton {
    id: control

    checkable: true
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitWidth: Theme.selectionTarget
    implicitHeight: Theme.selectionTarget
    Accessible.role: Accessible.CheckBox
    Accessible.checked: checked

    // A control that only reports a state, because something around it owns
    // the gesture, takes no input and yet is not disabled.
    property bool presentational: false
    readonly property bool dimmed: !enabled && !presentational
    readonly property color mark: dimmed ? Qt.rgba(Theme.text.r,Theme.text.g,Theme.text.b,Theme.disabledContentOpacity)
                                : checked ? Theme.primary : Theme.muted

    // Driven from the change itself, so drawing in and snapping away cannot
    // race the binding that says which way the box is going.
    function syncMark(animate) {
        markDraw.stop()
        if (checked && animate && app.motion) markDraw.start()
        else markReveal.width = checked ? Theme.checkboxSize : 0
    }
    onCheckedChanged: syncMark(true)
    Component.onCompleted: syncMark(false)

    background: Item {
        // Material's state layer is wider than the box and narrower than the
        // target, so the pointer lights up the control rather than the row.
        Rectangle {
            objectName: "checkboxStateLayer"
            anchors.centerIn: parent
            width: Theme.selectionStateLayer; height: width
            radius: Theme.shapeFull(width)
            color: control.checked ? Theme.primary : Theme.text
            opacity: control.down ? Theme.pressedOpacity : control.visualFocus ? Theme.focusOpacity : control.hovered ? Theme.hoverOpacity : 0
            Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        }
        // material-web draws the checkbox's focus ring as a 44dp circle
        // centred on the control (checkbox/internal/_checkbox.scss:65-69,
        // radio/internal/_radio.scss:74-78), at the ring's usual width.
        Rectangle {
            objectName: "checkboxFocusRing"
            anchors.centerIn: parent
            width: 44; height: width; radius: Theme.shapeFull(width)
            color: "transparent"; border.width: Theme.focusRingWidth; border.color: Theme.focusRing
            visible: control.visualFocus
        }
    }
    contentItem: Item {
        Rectangle {
            objectName: "checkboxBox"
            anchors.centerIn: parent
            width: Theme.checkboxSize; height: width
            radius: Theme.checkboxCorner
            color: control.checked ? control.mark : "transparent"
            border.width: control.checked ? 0 : 2
            border.color: control.mark
            // Checkbox.kt:1038-1047: the box fills in on DefaultEffects and
            // empties on FastEffects.
            Behavior on color { enabled: app.motion; ColorAnimation { duration: control.checked ? Theme.springEffectsMs : Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: control.checked ? Theme.springEffects : Theme.springFastEffects } }
            // Checkbox.kt:594-611 draws the tick in from its start on
            // DefaultSpatial and snaps it away when the box is cleared. The
            // tick runs left to right, so uncovering it from the left draws it.
            Item {
                id: markReveal
                objectName: "checkboxMarkReveal"
                anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                height: parent.height; clip: true
                width: 0
                NumberAnimation { id: markDraw; objectName: "checkboxMarkDraw"; target: markReveal; property: "width"; from: 0; to: Theme.checkboxSize; duration: Theme.springSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial }
                Icon {
                    objectName: "checkboxMark"
                    name: "check"; size: Theme.checkboxSize
                    // CheckboxTokens.SelectedDisabledIconColor is Surface:
                    // the tick stays cut out of the dimmed box.
                    ink: control.dimmed ? Theme.surface : Theme.primaryText
                    visible: control.checked
                }
            }
        }
    }
}
