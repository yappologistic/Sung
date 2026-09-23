import QtQuick

// Material 3 vertical drag handle.
//
// Material names this for pane expansion: a capsule the pointer can take hold
// of to change how a window is split. At rest it is a 4dp by 48dp outline
// capsule inside a 24dp target; pressed or dragged it thickens to 12dp by 52dp,
// squares off to a medium corner and takes the full onSurface ink, so the grip
// answers before the pane has moved. A vertical handle means horizontal travel.
Item {
    id: handle

    property bool pressed: false
    property bool dragging: false
    readonly property bool held: pressed || dragging

    implicitWidth: 24
    implicitHeight: 52

    Rectangle {
        objectName: "dragHandleGrip"
        anchors.centerIn: parent
        width: handle.held ? 12 : 4
        height: handle.held ? 52 : 48
        radius: handle.held ? Theme.shapeMedium : Theme.shapeFull(width)
        // Material draws a drag handle in the outline role and in the surface
        // ink while it is held, because it is something you take hold of
        // rather than a rule between two things.
        color: handle.held ? Theme.text : Theme.outline
        Behavior on width { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
        Behavior on height { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
        Behavior on radius { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
        Behavior on color { ColorAnimation { duration: Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastEffects } }
    }
}
