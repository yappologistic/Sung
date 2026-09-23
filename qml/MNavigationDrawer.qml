import QtQuick
import QtQuick.Controls

// Material 3 modal navigation drawer.
//
// When a window is too narrow to keep a rail beside the content, Material moves
// navigation into a sheet that comes in from the leading edge over a scrim. The
// sheet sits on the same surface a rail would, rounds only the corners that
// face the content, and rests one level off the page. Everything the rail
// carried goes in it, which is what keeps a destination reachable on a window
// that has no room to show one.
Popup {
    id: drawer

    default property alias content: body.data

    modal: true
    dim: true
    // A popup only sees a key press if it holds focus.
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    x: 0
    y: 0
    width: Math.min(360, parent ? parent.width-56 : 360)
    height: parent ? parent.height : 0
    padding: 12

    Overlay.modal: Rectangle { color: Theme.scrimColor() }
    background: Rectangle {
        objectName: "drawerSurface"
        color: Theme.surfaceLow
        // NavigationDrawerTokens.ContainerShape is CornerLargeEnd: square
        // against the window edge, 16dp on the edge that faces the content.
        topRightRadius: Theme.shapeLarge
        bottomRightRadius: Theme.shapeLarge
        MElevation { anchors.fill: parent; radius: Theme.shapeLarge; level: 1 }
    }
    contentItem: Item { id: body }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "x"; from: -drawer.width; to: 0; duration: Theme.springSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial }
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.springFastEffectsMs }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "x"; to: -drawer.width; duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial }
            NumberAnimation { property: "opacity"; to: 0; duration: Theme.exitDuration }
        }
    }
}
