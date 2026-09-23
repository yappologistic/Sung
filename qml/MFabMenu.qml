import QtQuick
import QtQuick.Controls

// Material 3 FAB and FAB menu.
//
// The FAB carries the primary action of a surface. Where that action has
// several related forms, the specification says to open a FAB menu of two to
// six of them, and that this replaces stacked small FABs and speed dials.
//
// Opening morphs the FAB into the menu's close button, which is the shape
// morph Material asks for on a change of state, and the items arrive one after
// another on the spatial spring rather than all at once.
Item {
    id: root
    objectName: "fabMenu"

    // Each action is {label, symbol, action}.
    property var actions: []
    property string symbol: "plus"
    property string label: ""
    property bool open: false
    readonly property int count: actions.length
    // Material's small FAB, which is the size it gives a secondary action:
    // 40dp at the medium corner, still carrying a 24dp glyph. Adding to the
    // library is not the screen's primary action, and at 56dp the FAB outweighed
    // everything around it.
    readonly property real fabSize: 40
    // Open, it becomes the menu's close button: 56dp, full round, a 20dp
    // glyph, on the primary role rather than its container
    // (FabMenuBaselineTokens.CloseButton*, and the final colours of
    // ToggleFloatingActionButtonDefaults in FloatingActionButtonMenu.kt).
    readonly property real closeSize: 56
    // Material's rail carries the FAB or extended FAB at its head, above the
    // destinations. A FAB up there opens downward, and towards the content
    // rather than away from it.
    property bool downward: false
    property bool leadingEdge: false
    // An extended FAB says in words what the action is. Material's small
    // extended FAB is 56dp with a 16dp corner, the label at title medium and
    // 16dp of padding either side of it.
    property bool extended: false

    implicitWidth: fab.width
    implicitHeight: fab.height
    z: 40

    function close() { open = false }
    onOpenChanged: open ? menu.open() : menu.close()

    // The menu is a temporary surface over the content, which is what Material
    // calls it, so it is a Popup: that puts it in the window's overlay instead
    // of inside whatever holds the FAB. In the rail the FAB sits in a 40dp
    // slot beside the destinations, and a menu drawn there would be painted
    // under the content surface and cut off at the rail's edge.
    Popup {
        id: menu
        objectName: "fabMenuPopup"
        parent: fab
        padding: 0
        background: null
        // Blocks what is behind it and dismisses on a press there, without
        // darkening it: the FAB menu shades nothing in the specification.
        modal: true
        dim: false
        // The menu takes the keyboard while it is open, which is what lets Tab
        // walk the actions and Escape put it away. Qt hands focus back to
        // whatever held it when the menu closes, which is the button itself.
        focus: true
        // A press anywhere else closes it, the FAB included: the menu blocks
        // what is behind it, so that press never reaches the button, and
        // pressing the button a second time reads as closing the menu.
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: root.open = false
        // The menu opens from the FAB's own edge, towards the content, 8dp
        // clear of it (FabMenuBaselineTokens.CloseButtonBetweenSpace).
        x: root.leadingEdge ? 0 : fab.width - width
        y: root.downward ? fab.height + 8 : -height - 8
        // What keeps the actions reachable: a menu wider than the room on that
        // side is moved back inside the window rather than hanging off it.
        margins: 12
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: app.motion ? Theme.springFastEffectsMs : 0 } }
        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: app.motion ? Theme.springFastEffectsMs : 0 } }

        contentItem: Column {
            id: items
            objectName: "fabMenuItems"
            // FabMenuBaselineTokens.ListItemBetweenSpace.
            spacing: 4
            // Items arrive from the FAB, nearest first. The column owns their
            // y, so the arrival is its own add transition: an item that
            // animates its own y fights the column, and every item lands on
            // the first. The menu fades in as a whole above, so an item's
            // opacity is left alone and cannot be stranded part way.
            add: Transition {
                enabled: app.motion
                SequentialAnimation {
                    PauseAnimation { duration: Math.min(3, ViewTransition.index) * 40 }
                    NumberAnimation {
                        property: "y"; from: ViewTransition.destination.y + (root.downward ? -16 : 16)
                        duration: Theme.springFastSpatialMs
                        easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial
                    }
                }
            }
            Repeater {
                model: root.open ? root.actions : []
                delegate: AbstractButton {
                    id: entry
                    required property var modelData
                    required property int index
                    objectName: "fabMenuItem_" + index
                    // The column places every item at its own leading edge, so
                    // the row that is not the widest is aligned by hand,
                    // towards the edge the menu opened from.
                    x: root.leadingEdge ? 0 : parent.width - width
                    // FabMenuBaselineTokens.ListItem*: 56dp tall, 24dp at each
                    // end, a 24dp icon 8dp from the label, raised three levels.
                    height: 56
                    // The ends are padding on the control. A Control stretches
                    // its content item to the width it is given, so a Row told
                    // to centre itself started at the left edge instead, and a
                    // wider item's icon sat on its rim.
                    leftPadding: 24; rightPadding: 24
                    implicitWidth: leftPadding + contentItem.implicitWidth + rightPadding
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    Accessible.name: modelData.label
                    onClicked: { root.close(); if (modelData.action) modelData.action() }
                    background: Rectangle {
                        radius: Theme.shapeFull(entry.height)
                        color: Theme.primaryContainer
                        MElevation { anchors.fill: parent; radius: parent.radius; level: 3 }
                        Rectangle {
                            anchors.fill: parent; radius: parent.radius
                            color: Theme.containerText
                            opacity: entry.down || entry.visualFocus ? Theme.pressedOpacity : entry.hovered ? Theme.hoverOpacity : 0
                            Behavior on opacity { NumberAnimation { duration: Theme.springFastEffectsMs } }
                        }
                    }
                    contentItem: Row {
                        spacing: 8
                        Icon { anchors.verticalCenter: parent.verticalCenter; name: entry.modelData.symbol || ""; size: 24; ink: Theme.containerText }
                        SungText {
                            id: entryLabel
                            anchors.verticalCenter: parent.verticalCenter
                            text: entry.modelData.label; color: Theme.containerText
                            // Compose sets a FAB menu item in title medium
                            // (FloatingActionButtonMenu.kt, FloatingActionButtonMenuItem).
                            font.pixelSize: Theme.titleMedium; typeRole: "titleMedium"
                        }
                    }
                }
            }
        }
    }

    AbstractButton {
        id: fab
        objectName: "fab"
        width: root.open ? root.closeSize
             : root.extended ? Theme.extendedFabInset*2 + 24 + Theme.extendedFabGap + fabLabel.implicitWidth
             : root.fabSize
        height: root.open ? root.closeSize : root.extended ? Theme.extendedFabHeight : root.fabSize
        Behavior on width { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
        Behavior on height { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: root.open ? "Close actions" : (root.label || "Actions")
        onClicked: root.open = !root.open
        background: Rectangle {
            objectName: "fabShape"
            color: root.open ? Theme.primary : Theme.primaryContainer
            // A small FAB rests at the medium shape step and morphs to full
            // when it becomes the menu's close button.
            radius: root.open ? Theme.shapeFull(fab.height) : root.extended ? Theme.shapeLarge : Theme.shapeMedium
            Behavior on radius { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
            Behavior on color { ColorAnimation { duration: Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastEffects } }
            MElevation { anchors.fill: parent; radius: parent.radius; level: 3 }
            Rectangle {
                anchors.fill: parent; radius: parent.radius
                color: root.open ? Theme.primaryText : Theme.containerText
                opacity: fab.down || fab.visualFocus ? Theme.pressedOpacity : fab.hovered ? Theme.hoverOpacity : 0
                Behavior on opacity { NumberAnimation { duration: Theme.springFastEffectsMs } }
            }
        }
        // A control stretches its content item to fill it, so the glyph needs a
        // wrapper to keep the 24dp Material asks for inside a 56dp FAB.
        contentItem: Item {
            SungText {
                id: fabLabel
                objectName: "fabLabel"
                visible: root.extended && !root.open
                text: root.label
                // The small extended FAB's label is title medium
                // (FloatingActionButton.kt, SmallExtendedFabTextStyle).
                font.pixelSize: Theme.titleMedium
                typeRole: "titleMedium"
                color: Theme.containerText
                anchors.verticalCenter: parent.verticalCenter
                x: Theme.extendedFabInset + 24 + Theme.extendedFabGap
            }
            Icon {
                objectName: "fabIcon"
                anchors.verticalCenter: parent.verticalCenter
                x: fabLabel.visible ? Theme.extendedFabInset : (parent.width-width)/2
                name: root.open ? "close" : root.symbol
                size: root.open ? 20 : 24
                ink: root.open ? Theme.primaryText : Theme.containerText
                rotation: root.open ? 90 : 0
                Behavior on rotation { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
            }
        }
    }
}
