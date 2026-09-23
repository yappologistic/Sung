import QtQuick

// Material 3 pull to refresh, drawn with the Expressive loading indicator.
//
// The loading indicator replaced the indeterminate circular progress spinner,
// and pull-to-refresh is the interaction Material names for it. Pulling drives
// the indicator's determinate morph from a circle towards the soft burst; once
// the refresh is running it takes over its own shape sequence.
Item {
    id: refresher
    objectName: "pullToRefresh"

    // The list being pulled, and what to do once it has been pulled far enough.
    property Flickable target: null
    property bool busy: false
    property bool enabled: true
    signal triggered()

    // PullToRefreshDefaults.PositionalThreshold; the component has no tokens.
    readonly property real threshold: 80
    // How far past the top the list has been dragged.
    readonly property real pulled: target && enabled ? Math.max(0, -(target.contentY - target.originY) - target.topMargin) : 0
    readonly property real progress: Math.max(0, Math.min(1, pulled/threshold))
    readonly property bool armed: progress >= 1

    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    y: busy ? 16 : Math.min(refresher.threshold, refresher.pulled) - height
    width: 48; height: 48
    visible: enabled && (pulled > 0 || busy)
    opacity: busy ? 1 : progress
    z: 20
    Behavior on y { enabled: app.motion && refresher.busy; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }

    // Releasing past the threshold refreshes; releasing short of it does not.
    Connections {
        target: refresher.target
        enabled: refresher.enabled
        function onDraggingChanged() {
            if (!refresher.target.dragging && refresher.armed && !refresher.busy)
                refresher.triggered()
        }
    }

    MLoadingIndicator {
        objectName: "refreshIndicator"
        anchors.fill: parent
        running: true
        // Material's contained variant: the shape sits on a filled container.
        trackColor: Theme.primaryContainer
        ink: Theme.containerText
        progress: refresher.busy ? -1 : refresher.progress
        label: refresher.busy ? "Refreshing" : "Pull to refresh"
    }
}
