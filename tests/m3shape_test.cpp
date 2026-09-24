// The shape library is checked against the geometry Material's definitions
// imply, worked out here by hand, not against its own output: a circle's
// size inside its box follows from the handle of a 36 degree cubic arc, a
// square's corner from a 0.3 rounding, and a cookie's lobes from where
// RoundedPolygon.star puts its vertices.
#include "m3shape.h"
#include <QTest>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;

QList<QPointF> points(const QPainterPath &path) {
  QList<QPointF> out;
  for (int i = 0; i < path.elementCount(); ++i)
    out.append(path.elementAt(i));
  return out;
}

double distanceToOutline(QPointF p, const QList<QPointF> &outline) {
  double best = 1e9;
  for (int i = 0; i < outline.size(); ++i) {
    const QPointF a = outline[i], b = outline[(i + 1) % outline.size()], e = b - a;
    const double length = QPointF::dotProduct(e, e);
    const double t = length > 0 ? qBound(0.0, QPointF::dotProduct(p - a, e) / length, 1.0) : 0;
    const QPointF d = p - (a + e * t);
    best = std::min(best, std::hypot(d.x(), d.y()));
  }
  return best;
}
} // namespace

class M3ShapeTest : public QObject {
  Q_OBJECT
private slots:
  void everyShapeIsAnOutlineAboutItsCentre() {
    // The morph, the mask and the ring all read one radius per angle, which
    // only holds if the outline's angle about the centre keeps rising.
    const QRectF box(0, 0, 200, 200);
    for (const auto &name : m3::shapeNames()) {
      const auto outline = points(m3::shapePath(name, box));
      QVERIFY2(outline.size() > 8, qPrintable(name));
      double previous = 0, turned = 0;
      for (int i = 0; i <= outline.size(); ++i) {
        const QPointF p = outline[i % outline.size()] - box.center();
        const double angle = std::atan2(p.y(), p.x());
        if (i > 0) {
          double step = angle - previous;
          if (step < -kPi)
            step += 2 * kPi;
          if (step > kPi)
            step -= 2 * kPi;
          QVERIFY2(step > -1e-9, qPrintable(name + " turns back on itself"));
          turned += step;
        }
        previous = angle;
      }
      QVERIFY2(qAbs(turned - 2 * kPi) < 1e-6, qPrintable(name));
      // toShape sizes the shape by its control points, so the longer side
      // reaches the box or stops short of it where a handle stands proud of
      // an arc, and nothing leaves it. The four-sided cookie's broad corners
      // stop shortest, at 88%, as it does beside the square on Material's
      // own sheet of the shapes.
      const QRectF bounds = m3::shapePath(name, box).boundingRect();
      QVERIFY2(box.adjusted(-1e-6, -1e-6, 1e-6, 1e-6).contains(bounds), qPrintable(name));
      QVERIFY2(std::max(bounds.width(), bounds.height()) > 0.87 * box.width(), qPrintable(name));
    }
  }

  void aCircleSitsInsideItsHandles() {
    // Ten 36 degree arcs, each one cubic whose handles are 4/3 tan(9 deg) of
    // the radius long. The outermost handle, 18 degrees off the axis, sets
    // the box, so the circle fills 1 / (cos 18 + 4/3 tan 9 sin 18) of it.
    const double handle = 4.0 / 3 * std::tan(kPi / 20);
    const double expected = 1 / (std::cos(kPi / 10) + handle * std::sin(kPi / 10));
    for (double r : m3::shapeOutline("circle", 360))
      QVERIFY2(qAbs(r - expected) < 3e-4, qPrintable(QString::number(r)));
  }

  void aSquaresCornersAreRoundedByThreeTenths() {
    // A unit square rounded by 0.3: the box is the square, so on the axes
    // the edge is at 1 and at 45 degrees the corner's circle, radius 0.6 at
    // (0.4, 0.4) in box units, is at 0.4 root 2 plus 0.6.
    const auto r = m3::shapeOutline("square", 360);
    QVERIFY(qAbs(r[0] - 1) < 1e-6);
    QVERIFY(qAbs(r[90] - 1) < 1e-6);
    QVERIFY2(qAbs(r[45] - (0.4 * std::sqrt(2.0) + 0.6)) < 1e-3, qPrintable(QString::number(r[45])));
  }

  void cookiesHaveTheirLobesWhereMaterialPutsThem() {
    const auto lobes = [&](const char *name, int count, double firstDegrees) {
      // The finest outline the library hands out, 512 radii to the turn.
      const auto r = m3::shapeOutline(name, 512);
      const int steps = int(r.size()) - 1;
      const auto at = [&](double degrees) {
        return r[(int(std::lround(degrees * steps / 360)) % steps + steps) % steps];
      };
      // toShape centres a shape by its box, and an odd number of lobes puts
      // the box's centre a little below the polygon's, so seen from there
      // only the lobe on the axis of symmetry keeps its place, and the two
      // sides mirror each other.
      if (count % 2) {
        for (double d = 0; d <= 180; d += 0.7)
          QVERIFY2(qAbs(at(firstDegrees + d) - at(firstDegrees - d)) < 2e-3,
                   qPrintable(QString("%1 mirrors at %2").arg(name).arg(d)));
      }
      double first = 0;
      for (int k = 0; k < (count % 2 ? 1 : count); ++k) {
        // A lobe's axis is halfway between where it rises through and falls
        // back through half its height. The top of a broad lobe is too flat
        // to find by its highest sample.
        const double expected = firstDegrees + 360.0 * k / count, half = 180.0 / count;
        const double top = at(expected), valley = std::min(at(expected - half), at(expected + half));
        QVERIFY2(valley < top - 0.05, qPrintable(QString("%1 valley %2").arg(name).arg(k)));
        const double level = (top + valley) / 2;
        double left = expected, right = expected;
        while (left > expected - half && at(left) > level)
          left -= 0.1;
        while (right < expected + half && at(right) > level)
          right += 0.1;
        const double axis = (left + right) / 2;
        // Within a degree and a half: the clover's own coordinates put its
        // leaves a fraction of a degree off the even spacing.
        QVERIFY2(qAbs(axis - expected) <= 1.5, qPrintable(QString("%1 lobe %2 at %3").arg(name).arg(k).arg(axis)));
        if (k == 0)
          first = top;
        QVERIFY2(qAbs(top - first) < 2e-3, qPrintable(QString("%1 repeats").arg(name)));
      }
    };
    // RoundedPolygon.star turned a quarter back: a lobe straight up.
    lobes("cookie7Sided", 7, 270);
    lobes("cookie9Sided", 9, 270);
    lobes("cookie12Sided", 12, 270);
    // The four-sided cookie's vertices are at (1.237, 1.236) about the middle:
    // its lobes are on the diagonals, a square with its sides drawn in.
    lobes("cookie4Sided", 4, 45);
    // The eight-leaf clover's first point is a notch straight up, so its
    // leaves sit half a period round; the flower's first petal points up.
    lobes("clover8Leaf", 8, 360.0 / 16 * 3);
    lobes("flower", 8, 270);
  }

  void aMorphFrameIsBetweenItsEnds() {
    // With no margin the offset outline is the frame itself.
    const auto a = m3::shapeOutline("cookie12Sided", 90), b = m3::shapeOutline("clover4Leaf", 90);
    const auto half = m3::shapeOffset("cookie12Sided", "clover4Leaf", 0.5, 0, 90);
    for (int i = 0; i < 90; ++i)
      QVERIFY2(qAbs(half[i] - (a[i] + b[i]) / 2) < 2e-3, qPrintable(QString::number(i)));
    // The ends are the shapes themselves.
    const QRectF box(0, 0, 100, 100);
    QCOMPARE(m3::shapePath("cookie12Sided", box, "clover4Leaf", 0), m3::shapePath("cookie12Sided", box));
    QCOMPARE(m3::shapePath("cookie12Sided", box, "clover4Leaf", 1), m3::shapePath("clover4Leaf", box));
    // A spring past its end still draws inside the box.
    const QRectF past = m3::shapePath("cookie4Sided", box, "puffyDiamond", 1.2).boundingRect();
    QVERIFY(box.adjusted(-1e-6, -1e-6, 1e-6, 1e-6).contains(past));
  }

  void theOffsetKeepsItsDistanceFromEveryEdge() {
    // The ring starts each bar on the offset. Measured square to the edge,
    // not along the ray, every start is the margin from the shape, down a
    // notch as much as on a lobe.
    const QRectF box(-1, -1, 2, 2);
    const double margin = 0.1;
    for (const auto &name : m3::shapeNames()) {
      const auto outline = points(m3::shapePath(name, box));
      const auto starts = m3::shapeOffset(name, {}, 0, margin, 72);
      double nearest = 1e9, farthest = 0;
      for (int i = 0; i < 72; ++i) {
        const double angle = 2 * kPi * i / 72;
        const double d = distanceToOutline(QPointF(starts[i] * std::cos(angle), starts[i] * std::sin(angle)), outline);
        nearest = std::min(nearest, d);
        farthest = std::max(farthest, d);
      }
      QVERIFY2(nearest > margin - 2e-3, qPrintable(QString("%1 %2").arg(name).arg(nearest)));
      QVERIFY2(nearest < margin + 2e-3, qPrintable(QString("%1 %2").arg(name).arg(nearest)));
      QVERIFY2(farthest < margin + 2e-3, qPrintable(QString("%1 %2").arg(name).arg(farthest)));
    }
  }

  void unknownNamesAreACircle() {
    QVERIFY(!m3::hasShape("heart"));
    for (double r : m3::shapeOutline("heart", 16))
      QCOMPARE(r, 1.0);
  }
};

QTEST_GUILESS_MAIN(M3ShapeTest)
#include "m3shape_test.moc"
