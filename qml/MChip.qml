import QtQuick
import QtQuick.Controls

// Material 3 chip.
//
// Material has four: an assist chip acts, a filter chip turns a view on and
// off and shows a check while it is on, a suggestion chip offers something to
// try, and an input chip stands for something the reader entered and carries
// the means to take it back out. They share one container, at 32dp on an 8dp
// corner with a 48dp target around it.
AbstractButton {
    id: control
    property bool selected: false
    property bool selectable: true
    // "filter", "assist", "suggestion" or "input".
    property string variant: "filter"
    readonly property bool removable: variant === "input"
    // What an input chip's remove asks for.
    signal removed()
    property string symbol: ""
    readonly property bool leads: symbol.length > 0 || (selectable && selected)
    // Compose lays a chip out as a row padded 8dp at each end with 8dp between
    // its leading slot, its label and its trailing slot, and keeps both slots
    // even when they are empty (Chip.kt, ChipContent and
    // HorizontalElementsPadding). A bare label therefore has 16dp either side,
    // and an 18dp icon puts the label 34dp in. An input chip pads 4dp before an
    // empty leading slot (inputChipPadding).
    readonly property real leadRoom: leads ? 8 + Theme.chipIcon + 8 : removable ? 4 + 8 : 16
    readonly property real trailRoom: removable ? 8 + Theme.chipIcon + 8 : 16
    implicitWidth: leadRoom + label.implicitWidth + trailRoom
    implicitHeight: Theme.selectionTarget
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: text
    Accessible.role: selectable ? Accessible.CheckBox : Accessible.Button
    Accessible.checkable: selectable
    Accessible.checked: selected
    readonly property bool dimmed: !enabled
    background: Rectangle {
        y: (control.height-Theme.chipHeight)/2; height: Theme.chipHeight; radius: Theme.shapeSmall
        color: control.dimmed && control.selected
                 ? Qt.rgba(Theme.text.r,Theme.text.g,Theme.text.b,Theme.disabledSurfaceOpacity)
             : control.selected ? Theme.secondaryContainer : "transparent"
        border.width: control.selected ? 0 : 1
        border.color: control.dimmed ? Qt.rgba(Theme.text.r,Theme.text.g,Theme.text.b,Theme.disabledSurfaceOpacity) : Theme.outlineVariant
        Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        // The state layer is drawn in the ink of the label it sits under, as
        // Compose's ripple takes the chip's content colour.
        Rectangle {
            anchors.fill: parent; radius: parent.radius
            color: control.selected ? Theme.secondaryContainerText : Theme.muted
            opacity: control.down || control.visualFocus ? Theme.pressedOpacity : control.hovered ? Theme.hoverOpacity : 0
            Behavior on opacity { NumberAnimation { duration: Theme.fast } }
        }
        Rectangle {
            anchors.fill: parent; anchors.margins: -3; radius: Theme.shapeMedium
            color: "transparent"; border.width: 2; border.color: Theme.focusRing
            visible: control.visualFocus
        }
    }
    contentItem: Item {
        opacity: control.dimmed ? Theme.disabledContentOpacity : 1
        // FilterChipTokens: an unselected chip's leading icon is in the accent,
        // a selected one's in the ink of the container it now sits on.
        Icon {
            x: 8; size: Theme.chipIcon; anchors.verticalCenter: parent.verticalCenter
            besideText: Theme.labelLarge
            name: control.symbol.length ? control.symbol : "check"
            visible: control.leads
            ink: control.selected ? Theme.secondaryContainerText : Theme.primary
        }
        SungText {
            id: label; x: control.leads || control.removable ? control.leadRoom : (parent.width-implicitWidth)/2
            text: control.text; font.pixelSize: Theme.labelLarge; font.weight: Font.Medium; labelRole: true
            color: control.selected ? Theme.secondaryContainerText : Theme.muted; anchors.verticalCenter: parent.verticalCenter
            Behavior on x { NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
        }
        // An input chip stands for something entered, so it carries the means
        // to take it back out rather than needing somewhere else to undo it.
        AbstractButton {
            objectName: "chipRemove"
            visible: control.removable
            width: 26; height: parent.height
            anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter
            Accessible.name: "Remove " + control.text
            onClicked: control.removed()
            // InputChipTokens: the variant ink, or the container's ink once
            // the chip is selected.
            contentItem: Icon { name: "close"; size: Theme.chipIcon; ink: control.selected ? Theme.secondaryContainerText : Theme.muted }
        }
    }
}
