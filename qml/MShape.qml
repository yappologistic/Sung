import QtQuick
import QtQuick.Shapes

// One of Material's named shapes, filled. The outline comes from the same
// source the artwork mask and the loading indicator use, so a placeholder and
// the cover that replaces it are cut to exactly the same silhouette.
//
// A shape can also morph. Material's shape library "supports easy
// transitioning, or morphing, between shapes" (Shape, Shape morph), and says
// what a morph is for: interaction states, actions in progress, and "changes
// in the environment, like sound" (Shape, Morph shapes to connect function and
// feeling). Every outline here is a radius at each angle, so a morph is the
// radii of one shape moving towards the other's, and `pulse` pushes the
// outline's departures from a circle further out: a cookie's scallops deepen
// with it while its points stay put. Whoever drives either value owns the
// motion; this only draws the frame it is given.
Shape {
    id: outline

    property string shape: "circle"
    property string toShape: ""
    property real progress: 0
    property real pulse: 0
    property color color: Theme.high
    property color strokeColor: "transparent"
    property real strokeWidth: 0
    // One radius per pixel across, and never fewer than 96: a chord between
    // samples cuts across a concave valley, and on a large cover a coarser
    // outline would let the placeholder behind show there as a thin line.
    readonly property int sampleCount: Math.max(96, Math.min(512, Math.ceil(Math.min(width, height))))
    readonly property var fromRadii: app.shapeOutline(shape, sampleCount)
    readonly property var toRadii: toShape.length ? app.shapeOutline(toShape, sampleCount) : fromRadii

    preferredRendererType: Shape.CurveRenderer
    ShapePath {
        fillColor: outline.color
        strokeColor: outline.strokeColor
        strokeWidth: outline.strokeWidth
        PathPolyline {
            path: {
                const from = outline.fromRadii, to = outline.toRadii
                const t = outline.progress, deepen = 1 + outline.pulse
                // A stroke is centred on the outline, so it is drawn half its
                // width inside the item rather than clipped at the edge.
                const extent = Math.min(outline.width, outline.height)/2 - outline.strokeWidth/2
                const cx = outline.width/2, cy = outline.height/2
                const points = []
                for (let i = 0; i < from.length; ++i) {
                    const angle = i*2*Math.PI/(from.length-1)
                    const radius = 1 - (1 - (from[i] + (to[i]-from[i])*t))*deepen
                    points.push(Qt.point(cx + radius*extent*Math.cos(angle),
                                         cy + radius*extent*Math.sin(angle)))
                }
                return points
            }
        }
    }
}
