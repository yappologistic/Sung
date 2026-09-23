import QtQuick
import QtQuick.Controls

// Material 3 exposed dropdown menu.
//
// Material draws a choice between a known set of options as a text field that
// cannot be typed into: the label sits above it, the current value reads as the
// field's text, and a trailing chevron turns to say the menu is open. It is a
// field rather than a button because what it holds is a value, not an action.
Item {
    id: control

    // Each option is {key, label, name}.
    property var options: []
    property var value
    property string label: ""
    signal chosen(var key)

    readonly property string currentLabel: {
        for (let i = 0; i < options.length; ++i)
            if (options[i].key === value) return options[i].label
        return ""
    }
    readonly property bool open: menu.visible
    readonly property bool menuOpen: menu.visible
    // So a control that replaces an older one can keep the name it was known by.
    property string fieldName: "dropdownField"

    implicitWidth: Math.max(160, metrics.advanceWidth(currentLabel) + 88)
    implicitHeight: 56
    FontMetrics { id: metrics; font.family: Theme.fontFamily; font.pixelSize: Theme.bodyLarge }

    AbstractButton {
        id: field
        objectName: control.fieldName
        anchors.fill: parent
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        // Qt Quick Accessible puts the combo role and its press action on the
        // focusable button; its text reports the current value to assistive tools.
        text: control.currentLabel
        Accessible.role: Accessible.ComboBox
        Accessible.name: control.label
        Accessible.description: control.currentLabel
        Accessible.onPressAction: field.clicked()
        onClicked: menu.opened ? menu.close() : menu.popup(control, 0, control.height + 4)

        background: Rectangle {
            radius: Theme.shapeExtraSmall
            color: "transparent"
            border.width: field.activeFocus || control.open ? 2 : 1
            border.color: field.activeFocus || control.open ? Theme.primary : Theme.outline
            Behavior on border.color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        }
        contentItem: Item {
            SungText {
                objectName: "dropdownValue"
                x: 16; anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 52
                text: control.currentLabel
                font.pixelSize: Theme.bodyLarge
                elide: Text.ElideRight
            }
            Icon {
                objectName: "dropdownChevron"
                name: "chevron"; size: 20; ink: Theme.muted
                anchors.right: parent.right; anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                rotation: control.open ? 270 : 90
                Behavior on rotation { enabled: app.motion; NumberAnimation { duration: Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastEffects } }
            }
        }
        Rectangle {
            objectName: "dropdownFocusRing"
            anchors.fill: field; anchors.margins: -3
            // MButton's keyboard ring sits 3px outside at 2px, while the
            // field's own focused primary border stays on its boundary.
            radius: Theme.shapeInside(Theme.shapeExtraSmall, -3)
            color: "transparent"; border.width: 2; border.color: Theme.focusRing
            visible: field.visualFocus
        }
    }

    // The floating label, which sits on the field's own boundary.
    SungText {
        objectName: "dropdownLabel"
        visible: control.label.length > 0
        x: 16; y: -height/2
        text: control.label
        font.pixelSize: Theme.labelMedium
        color: field.activeFocus || control.open ? Theme.primary : Theme.muted
        Rectangle { anchors.fill: parent; anchors.leftMargin: -4; anchors.rightMargin: -4; color: Theme.surfaceLow; z: -1 }
    }

    MMenu {
        id: menu
        objectName: "dropdownMenu"
        width: Math.max(control.width, 180)
        Repeater {
            model: control.options
            delegate: MMenuItem {
                required property var modelData
                objectName: modelData.name || ("dropdown_" + modelData.key)
                text: modelData.label
                checkable: true
                checked: control.value === modelData.key
                onTriggered: control.chosen(modelData.key)
            }
        }
    }
}
