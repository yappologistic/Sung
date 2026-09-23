import QtQuick
import QtQuick.Shapes

// Material 3 loading indicator.
//
// Material retired the indeterminate circular progress spinner in favour of a
// shape that morphs while it turns. The indeterminate form walks a sequence of
// seven shapes (soft burst, nine sided cookie, pentagon, pill, sunny, four
// sided cookie, oval), holding each for 650ms and adding a quarter turn per
// morph on top of a continuous rotation that takes 4666ms. The determinate form
// morphs a circle into the soft burst as progress runs from nought to one.
//
// The shapes come from the shared library, which writes each one as the radius
// it carries at a given angle; the morph interpolates those radii. Material
// builds them from RoundedPolygon and morphs by matching cubic curves, which Qt
// Quick has no equivalent of. The silhouettes, the sequence and the timings are
// Material's; the curve matching underneath them is not.
Item {
    id: indicator

    property bool running: false
    property color ink: Theme.primary
    // Filling the container gives Material's contained variant; leaving it
    // clear gives the uncontained one, which is the default.
    property color trackColor: "transparent"
    property string label: "Loading"
    // Nought to one drives the circle-to-burst morph; below nought the
    // indicator runs its own indeterminate sequence instead.
    property real progress: -1
    readonly property bool determinate: progress >= 0

    readonly property bool animating: running && !determinate && visible && app.motion
        && Window.window !== null && Window.window.visible
        && Window.window.visibility !== Window.Minimized

    // Material's container is 48dp with a 38dp active indicator inside it.
    implicitWidth: 48
    implicitHeight: 48
    visible: running
    Accessible.role: Accessible.Indicator
    Accessible.name: label
    Accessible.ignored: !visible

    // Material's sequence for the indeterminate form, and the circle the
    // determinate one grows out of. The outlines come from the shared shape
    // library rather than being worked out here.
    readonly property var sequence: ["softBurst","cookie9Sided","pentagon","pill","sunny","cookie4Sided","oval"]
    readonly property int shapeCount: sequence.length
    readonly property int sampleCount: 96
    property int morphIndex: 0
    property real morphProgress: 0
    property real turn: 0
    property real spin: 0

    // Fetched when the morph moves on, not per frame.
    readonly property var fromRadii: app.shapeOutline(determinate ? "circle" : sequence[morphIndex], sampleCount)
    readonly property var toRadii: app.shapeOutline(determinate ? sequence[0] : sequence[(morphIndex+1)%shapeCount], sampleCount)

    Rectangle {
        anchors.fill: parent
        radius: Theme.shapeFull(Math.min(width,height))
        color: indicator.trackColor
    }

    Shape {
        id: outline
        objectName: "loadingShape"
        anchors.centerIn: parent
        width: Math.min(indicator.width,indicator.height)*38/48
        height: width
        preferredRendererType: Shape.CurveRenderer
        // A morph turns the shape a further quarter as it runs, which is what
        // stops the sequence from ever settling into a loop the eye can follow.
        rotation: indicator.determinate ? 0 : indicator.morphProgress*90+indicator.turn+indicator.spin
        ShapePath {
            fillColor: indicator.ink
            strokeColor: "transparent"
            PathPolyline {
                path: {
                    const from=indicator.fromRadii, to=indicator.toRadii
                    const held=indicator.determinate ? Math.min(1,indicator.progress) : indicator.morphProgress
                    const extent=outline.width/2
                    const points=[]
                    for(let i=0;i<from.length;++i) {
                        const angle=i*2*Math.PI/(from.length-1)
                        const radius=(from[i]*(1-held)+to[i]*held)*extent
                        points.push(Qt.point(extent+radius*Math.cos(angle),extent+radius*Math.sin(angle)))
                    }
                    return points
                }
            }
        }
    }

    NumberAnimation {
        target: indicator; property: "spin"; from: 0; to: 360
        duration: 4666; loops: Animation.Infinite; running: indicator.animating
    }
    SequentialAnimation {
        running: indicator.animating; loops: Animation.Infinite
        NumberAnimation {
            objectName: "loadingMorphAnimation"
            // LoadingIndicator.kt:400-419 uses its 0.6/200 morph spring with
            // a 0.1 visibility threshold, rather than FastSpatial.
            target: indicator; property: "morphProgress"; from: 0; to: 1; duration: Theme.loadingMorphSpringMs
            easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.loadingMorphSpring
        }
        // LoadingIndicator.kt:404-419 starts one morph each 650ms.
        PauseAnimation { objectName: "loadingMorphPause"; duration: Math.max(0,650-Theme.loadingMorphSpringMs) }
        ScriptAction {
            script: {
                indicator.turn=(indicator.turn+90)%360
                indicator.morphIndex=(indicator.morphIndex+1)%indicator.shapeCount
                indicator.morphProgress=0
            }
        }
    }
}
