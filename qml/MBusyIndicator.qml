import QtQuick
import QtQuick.Controls
import QtQuick.Shapes

Item {
    id: indicator
    property bool running: false
    property color ink: Theme.primary
    property color trackColor: Theme.primaryContainer
    property string label: "Loading"
    property real strokeWidth: width < 32 ? 3 : 4
    readonly property bool animating: running && visible && app.motion
        && Window.window !== null && Window.window.visible
        && Window.window.visibility !== Window.Minimized
    implicitWidth: 40
    implicitHeight: 40
    visible: running
    Accessible.role: Accessible.Indicator
    Accessible.name: label
    Accessible.ignored: !visible

    // Rounded M3 arcs; only the small indicator geometry changes per frame.
    // No Canvas texture uploads, particles, or offscreen effect layers.
    Shape {
        id: ring
        anchors.centerIn: parent
        width: Math.min(indicator.width, indicator.height)
        height: width
        preferredRendererType: Shape.CurveRenderer
        property real cycleStart: 0
        rotation: cycleStart
        property real head: 0
        property real tail: 0
        readonly property real sweep: indicator.animating ? 28 + head - tail : 100
        readonly property real start: indicator.animating ? tail - 90 : -90
        readonly property real radius: (width - indicator.strokeWidth) / 2
        ShapePath {
            strokeColor: indicator.trackColor
            strokeWidth: indicator.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: ring.width/2; centerY: ring.height/2
                radiusX: ring.radius; radiusY: ring.radius
                startAngle: ring.start + ring.sweep + 18
                sweepAngle: Math.max(0, 360 - ring.sweep - 36)
            }
        }
        ShapePath {
            strokeColor: indicator.ink
            strokeWidth: indicator.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: ring.width/2; centerY: ring.height/2
                radiusX: ring.radius; radiusY: ring.radius
                startAngle: ring.start; sweepAngle: ring.sweep
            }
        }
        RotationAnimator {
            target: indicator; from: 0; to: 360; duration: 2200
            loops: Animation.Infinite; running: indicator.animating
        }
        SequentialAnimation {
            running: indicator.animating; loops: Animation.Infinite
            PropertyAction { target: ring; property: "head"; value: 0 }
            PropertyAction { target: ring; property: "tail"; value: 0 }
            NumberAnimation { target: ring; property: "head"; from: 0; to: 252; duration: 667; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.4,0,0.2,1,1,1] }
            NumberAnimation { target: ring; property: "tail"; from: 0; to: 252; duration: 667; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.4,0,0.2,1,1,1] }
            // Advance the rotation when the arc resets, preserving continuity.
            ScriptAction { script: ring.cycleStart = (ring.cycleStart + 252) % 360 }
        }
    }
}
