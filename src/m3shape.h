#pragma once
#include <QPainterPath>
#include <QRectF>
#include <QStringList>

// Material 3's shape library.
//
// Material publishes its shapes as RoundedPolygons: a list of vertices, each
// with a corner rounding, which androidx.graphics.shapes turns into cubic
// curves (MaterialShapes.kt, RoundedPolygon.kt). Qt has no equivalent, so the
// rounding is ported here and the vertex lists are Material's own. Each shape
// is placed the way Compose's RoundedPolygon.toShape places it: normalised to
// the unit square by the bounds of its curves' control points, and centred.
//
// A morph is done on radii: every shape here is star-shaped about the centre
// of its box, so each has exactly one radius at each angle, and the frame
// between two shapes is the radii of one moving towards the other's.
namespace m3 {

// The shapes this build knows, in the order Material lists them.
QStringList shapeNames();
bool hasShape(const QString &name);

// The outline sampled at `steps` even angles from 0 to a full turn inclusive,
// as radii from the centre of the shape's box, in units of half the box. A
// radius past one is possible off the axes, as at the corners of the square.
// Unknown names are a circle.
QList<double> shapeOutline(const QString &name, int steps);

// The outline as a closed path filling the largest square centred in
// `bounds`. With a second shape and a progress between, it is the frame of the
// morph from one to the other, kept inside that square while a spring
// overshoots.
QPainterPath shapePath(const QString &name, const QRectF &bounds,
                       const QString &toName = {}, double progress = 0);

// The morph frame's outline pushed out by `margin` in every direction, as
// radii at `steps` even angles in the same units as shapeOutline. A ring of
// radial marks started on this keeps `margin` clear of the shape even where
// its edge runs nearly along the ray, as down the notch of a clover.
QList<double> shapeOffset(const QString &name, const QString &toName,
                          double progress, double margin, int steps);

} // namespace m3
