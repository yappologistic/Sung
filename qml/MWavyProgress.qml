import QtQuick
import QtQuick.Shapes

// Material 3 linear wavy progress indicator.
//
// Material's determinate linear indicator draws its active part as a wave: a
// 4dp stroke with a 3dp amplitude on a 40dp wavelength, a 4dp gap before the
// remaining track, and a 4dp stop indicator at the far end. The container is
// 10dp tall to leave the wave room to move. The indeterminate form uses a
// shorter 20dp wavelength and travels instead of filling.
Item {
    id: indicator

    // Nought to one. Below nought the indicator is indeterminate.
    property real progress: -1
    property color ink: Theme.primary
    property color trackColor: Theme.secondaryContainer
    property string label: "Loading"

    readonly property bool determinate: progress >= 0
    readonly property real amount: Math.max(0, Math.min(1, progress))

    // Material's tokens for the linear indicator.
    readonly property real stroke: 4
    readonly property real amplitude: 3
    readonly property real wavelength: determinate ? 40 : 20
    readonly property real gap: 4
    readonly property real stopSize: 4

    readonly property bool animating: visible && app.motion
        && Window.window !== null && Window.window.visible
        && Window.window.visibility !== Window.Minimized

    implicitHeight: 10
    implicitWidth: 240
    Accessible.role: Accessible.ProgressBar
    Accessible.name: label
    Accessible.description: determinate ? Math.round(amount*100) + "%" : "Working"

    // The indeterminate run travels a segment across the track rather than
    // filling it, so its ends are both free to move.
    property real sweep: 0
    NumberAnimation on sweep {
        objectName: "wavySweepAnimation"
        // ProgressIndicator.kt:1048-1055 runs head and tail in a 1750ms cycle.
        from: 0; to: 1; duration: 1750; loops: Animation.Infinite
        running: indicator.animating && !indicator.determinate
    }
    readonly property real runStart: determinate ? 0 : Math.max(0, sweep*1.4-0.4)
    readonly property real runEnd: determinate ? amount : Math.min(1, sweep*1.4)

    // The remaining track, set back from the active part by Material's gap.
    Rectangle {
        objectName: "wavyTrack"
        x: Math.min(parent.width, indicator.runEnd*parent.width + indicator.gap)
        width: Math.max(0, parent.width - x - (indicator.determinate ? indicator.stopSize + indicator.gap : 0))
        anchors.verticalCenter: parent.verticalCenter
        height: indicator.stroke; radius: Theme.shapeFull(height)
        color: indicator.trackColor
    }
    Rectangle {
        objectName: "wavyTrackHead"
        visible: !indicator.determinate && indicator.runStart > 0
        width: Math.max(0, indicator.runStart*parent.width - indicator.gap)
        anchors.verticalCenter: parent.verticalCenter
        height: indicator.stroke; radius: Theme.shapeFull(height)
        color: indicator.trackColor
    }
    // Material keeps a stop indicator at the end of a determinate track.
    Rectangle {
        objectName: "wavyStop"
        visible: indicator.determinate && indicator.amount < 1
        x: parent.width - indicator.stopSize
        anchors.verticalCenter: parent.verticalCenter
        width: indicator.stopSize; height: indicator.stopSize
        radius: Theme.shapeFull(width)
        color: indicator.ink
    }

    // The active part. The wave itself is static geometry moved by the render
    // thread, so nothing is painted per frame.
    Item {
        id: run
        objectName: "wavyRun"
        x: indicator.runStart*parent.width
        width: Math.max(0, (indicator.runEnd-indicator.runStart)*parent.width)
        height: parent.height
        // Layering the clip is what actually holds the wave inside the run;
        // the seek bar cuts its own wave the same way.
        clip: true; layer.enabled: true; layer.smooth: true
        // Held still against the indicator while the run itself moves, so the
        // wave reads as one shape the active part is cut out of.
        Item {
            id: hold
            x: -run.x; width: indicator.width; height: parent.height
            Shape {
                id: wave
                objectName: "wavyShape"
                width: indicator.width + indicator.wavelength
                height: indicator.height
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: indicator.ink; strokeWidth: indicator.stroke
                    fillColor: "transparent"; capStyle: ShapePath.RoundCap
                    PathPolyline {
                        path: {
                            const points = []
                            const middle = indicator.height/2
                            for (let x = 0; x <= wave.width+2; x += 2)
                                points.push(Qt.point(x, middle + indicator.amplitude*Math.sin(x*2*Math.PI/indicator.wavelength)))
                            return points
                        }
                    }
                }
                XAnimator {
                    objectName: "wavyTravelAnimation"
                    target: wave; from: 0; to: -indicator.wavelength
                    // WavyProgressIndicator.kt:106-107,174-175 travels one
                    // wavelength each second in either indicator mode.
                    duration: 1000; loops: Animation.Infinite
                    running: indicator.animating
                }
            }
        }
    }
}
