import QtQuick

// Material's focus indicator: the ring a keyboard leaves on whatever it has
// reached. It is 3dp thick and stands 2dp clear of the control, in secondary
// (material-web tokens: md.sys.state.focus-indicator thickness 3px and
// outer-offset 2px; md.comp.focus-ring width 3px and outward-offset 2px). The
// gap is what keeps it legible on a control already painted in the accent.
//
// It is drawn outside the control unless the control meets its neighbours
// edge to edge, as rows in a list, items in a menu and tabs in a row do. There
// a ring outside would be cut off or run into the next one, so it is drawn
// just inside instead (md.comp.focus-ring inward-offset 0px).
//
// Controls show it for keyboard focus, never for a click: bind `visible` to
// the control's visualFocus, not activeFocus.
Rectangle {
    id: ring
    // What it outlines: its parent, or a sibling such as a control's
    // background. The target's radius is the ring's inner radius.
    property Item target: parent
    property real targetRadius: 0
    property bool inward: false
    readonly property real outset: inward ? 0 : Theme.focusRingOutset
    // Bound rather than anchored: an anchor to a target that is null when the
    // ring is made would leave it sized zero for good.
    x: (target && target !== parent ? target.x : 0) - outset
    y: (target && target !== parent ? target.y : 0) - outset
    width: (target ? target.width : 0) + outset*2
    height: (target ? target.height : 0) + outset*2
    radius: Theme.shapeInside(targetRadius, -outset)
    color: "transparent"
    border.width: Theme.focusRingWidth
    border.color: Theme.focusRing
}
