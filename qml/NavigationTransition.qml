import QtQuick

// Material 3 navigation motion.
//
// Material separates two cases. Destinations reached from the rail have no
// spatial relationship to each other, so they *fade through*: the outgoing view
// leaves before the incoming one arrives while growing from 92%. Tabs inside
// a destination are peers along one line, so they share the *X axis*: the outgoing view
// leaves towards the side it came from and the incoming one arrives from the
// other, travelling 30dp.
//
// Opening a detail out of a list is neither of those: the two screens sit at
// consecutive levels of one hierarchy, so Material slides them horizontally
// while fading, and the direction says which way you went.
//
// The content only swaps once the outgoing half has finished, which is why the
// caller hands over the navigation itself rather than performing it first.
QtObject {
    id: root

    // The item to move. It needs a `shift` property fed into a Translate, so
    // the X axis can be travelled without fighting the layout that placed it.
    property Item target: null

    // Menu.kt:1829-1831 uses FastEffects for the staged fade; PaneMotion.kt:
    // 150-177 uses DefaultSpatial for pane travel and scale. The outgoing
    // FastEffects fade completes before the incoming pane starts.
    readonly property int leaveDuration: travel===0?Theme.springFastEffectsMs:Theme.springSpatialMs
    readonly property int arriveDuration: Theme.springSpatialMs
    readonly property int totalDuration: leaveDuration+arriveDuration
    readonly property real arriveScale: 0.92
    readonly property real axisTravel: 30

    readonly property bool running: leave.running || arrive.running
    property var pending: null
    property real travel: 0

    signal navigated()

    // Turning motion off during a staged transition finishes the pending
    // navigation in the same frame and hands the target back at rest.
    property Connections motionSettings: Connections {
        target: app
        function onSettingsChanged() {
            if (app.motion || !root.running) return
            const wasLeaving=root.leave.running
            root.settle()
            if(wasLeaving)root.swap()
        }
    }

    // Destinations with no spatial relationship.
    function fadeThrough(action) { begin(action,0) }
    // Peers along a line. `forward` is the direction of travel through them.
    function sharedAxisX(action,forward) { begin(action,forward?axisTravel:-axisTravel) }
    // Consecutive levels of a hierarchy: opening a detail, and coming back out
    // of it. Material moves these horizontally with a fade rather than fading
    // them through each other, because the two screens are related.
    function forwardBackward(action,forward) { begin(action,forward?axisTravel:-axisTravel) }

    function begin(action,distance) {
        if(!target || !app.motion) { settle(); if(action)action(); navigated(); return }
        if(running)settle()
        travel=distance
        pending=action
        leave.start()
    }

    // Hand the target back exactly as it was found, whatever was in flight.
    function settle() {
        leave.stop();arrive.stop()
        if(target){target.opacity=1;target.scale=1;target.shift=0}
    }

    function swap() {
        const action=pending
        pending=null
        if(action)action()
        navigated()
    }

    property Animation leave: ParallelAnimation {
        NumberAnimation {
            objectName: "navLeaveFade"
            // FastEffects is the critically damped exit fade from Menu.kt.
            target: root.target; property: "opacity"; to: 0; duration: Theme.springFastEffectsMs
            easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastEffects
        }
        NumberAnimation {
            objectName: "navLeaveShift"
            // PaneMotion.kt:150-177 uses DefaultSpatial for pane travel.
            target: root.target; property: "shift"; to: -root.travel; duration: root.travel===0?0:Theme.springSpatialMs
            easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial
        }
        onFinished: {
            root.swap()
            if(root.target){
                // Fading through shrinks the arriving view; sharing an axis
                // slides it in at full size from the far side.
                root.target.scale=root.travel===0?root.arriveScale:1
                root.target.shift=root.travel===0?0:root.travel
            }
            root.arrive.start()
        }
    }

    property Animation arrive: ParallelAnimation {
        NumberAnimation {
            objectName: "navArriveFade"
            // DefaultEffects restores a screen's opacity without overshoot;
            // PaneMotion.kt:150-177 pairs pane visibility with its bounds.
            target: root.target; property: "opacity"; to: 1; duration: Theme.springEffectsMs
            easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springEffects
        }
        NumberAnimation {
            objectName: "navArriveScale"
            // PaneMotion.kt:150-177 uses DefaultSpatial for pane bounds.
            target: root.target; property: "scale"; to: 1; duration: Theme.springSpatialMs
            easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial
        }
        NumberAnimation {
            objectName: "navArriveShift"
            // DefaultSpatial carries shared-axis travel; fade-through has none.
            target: root.target; property: "shift"; to: 0; duration: root.travel===0?0:Theme.springSpatialMs
            easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial
        }
    }
}
