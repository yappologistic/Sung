#include "m3shape.h"
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <vector>

namespace m3 {
namespace {
constexpr double kPi = 3.14159265358979323846;
// graphics-shapes' own tolerance for a length that is nothing (Utils.kt).
constexpr double kDistanceEpsilon = 1e-4;

struct Point {
  double x = 0, y = 0;
  Point operator+(Point o) const { return {x + o.x, y + o.y}; }
  Point operator-(Point o) const { return {x - o.x, y - o.y}; }
  Point operator*(double k) const { return {x * k, y * k}; }
  double dot(Point o) const { return x * o.x + y * o.y; }
  double cross(Point o) const { return x * o.y - y * o.x; }
  double length() const { return std::hypot(x, y); }
  Point direction() const { return *this * (1 / length()); }
  Point rotate90() const { return {-y, x}; }
};

struct Cubic {
  Point a0, c0, c1, a1;
  bool line = false;
  Point at(double t) const {
    const double m = 1 - t;
    return a0 * (m * m * m) + c0 * (3 * m * m * t) + c1 * (3 * m * t * t) + a1 * (t * t * t);
  }
  bool zeroLength() const {
    return std::abs(a0.x - a1.x) < kDistanceEpsilon && std::abs(a0.y - a1.y) < kDistanceEpsilon;
  }
};

Cubic straightLine(Point from, Point to) {
  return {from, from + (to - from) * (1.0 / 3), from + (to - from) * (2.0 / 3), to, true};
}

// Cubic.circularArc: one cubic for the whole arc, its handles 4/3 tan(θ/4)
// of the radius long.
Cubic circularArc(Point centre, Point from, Point to) {
  const Point p0d = (from - centre).direction(), p1d = (to - centre).direction();
  const Point r0 = p0d.rotate90(), r1 = p1d.rotate90();
  const bool clockwise = r0.dot(to - centre) >= 0;
  const double cosa = p0d.dot(p1d);
  if (cosa > 0.999)
    return straightLine(from, to);
  const double k = (from - centre).length() * 4 / 3 *
                   (std::sqrt(2 * (1 - cosa)) - std::sqrt(1 - cosa * cosa)) / (1 - cosa) *
                   (clockwise ? 1 : -1);
  return {from, from + r0 * k, to - r1 * k, to};
}

struct Vertex {
  Point at;
  double rounding = 0;
};

// RoundedPolygon's constructor, for corners without smoothing: no shape in
// this library asks for any, so the flanking curves smoothing adds are
// always of no length and are left out. Each corner is cut back along both
// sides far enough for a circle of its rounding radius to meet them, and a
// side too short for both of its corners' cuts shares itself out between
// them in proportion, shrinking the radius to match.
std::vector<Cubic> rounded(const std::vector<Vertex> &vertices) {
  const int n = int(vertices.size());
  struct Corner {
    Point d1, d2;
    double radius = 0, cut = 0;
  };
  std::vector<Corner> corners(n);
  for (int i = 0; i < n; ++i) {
    const Point p0 = vertices[(i + n - 1) % n].at, p1 = vertices[i].at, p2 = vertices[(i + 1) % n].at;
    const Point v01 = p0 - p1, v21 = p2 - p1;
    if (v01.length() <= 0 || v21.length() <= 0)
      continue;
    auto &c = corners[i];
    c.d1 = v01.direction();
    c.d2 = v21.direction();
    c.radius = vertices[i].rounding;
    const double cosAngle = c.d1.dot(c.d2), sinAngle = std::sqrt(1 - cosAngle * cosAngle);
    c.cut = sinAngle > 1e-3 ? c.radius * (cosAngle + 1) / sinAngle : 0;
  }
  std::vector<double> sideRatio(n);
  for (int i = 0; i < n; ++i) {
    const double expected = corners[i].cut + corners[(i + 1) % n].cut;
    const double side = (vertices[i].at - vertices[(i + 1) % n].at).length();
    sideRatio[i] = expected > side ? side / expected : 1;
  }
  std::vector<Cubic> arcs(n);
  std::vector<bool> point(n, true);
  for (int i = 0; i < n; ++i) {
    const auto &c = corners[i];
    const Point p1 = vertices[i].at;
    const double allowed = std::min(c.cut * sideRatio[(i + n - 1) % n], c.cut * sideRatio[i]);
    if (c.cut < kDistanceEpsilon || allowed < kDistanceEpsilon || c.radius < kDistanceEpsilon) {
      arcs[i] = straightLine(p1, p1);
      continue;
    }
    const double cut = std::min(allowed, c.cut);
    const double radius = c.radius * cut / c.cut;
    const double centreDistance = std::sqrt(radius * radius + cut * cut);
    const Point centre = p1 + ((c.d1 + c.d2) * 0.5).direction() * centreDistance;
    arcs[i] = circularArc(centre, p1 + c.d1 * cut, p1 + c.d2 * cut);
    point[i] = false;
  }
  std::vector<Cubic> cubics;
  for (int i = 0; i < n; ++i) {
    if (!point[i] && !arcs[i].zeroLength())
      cubics.push_back(arcs[i]);
    const Cubic edge = straightLine(arcs[i].a1, arcs[(i + 1) % n].a0);
    if (!edge.zeroLength())
      cubics.push_back(edge);
  }
  return cubics;
}

Point rotated(Point p, double degrees, Point about = {}) {
  const double a = degrees * kPi / 180, c = std::cos(a), s = std::sin(a);
  const Point o = p - about;
  return Point{o.x * c - o.y * s, o.x * s + o.y * c} + about;
}

// RoundedPolygon(numVertices, radius, rounding): the first vertex at three
// o'clock and the rest evenly round.
std::vector<Vertex> regular(int count, double radius, double rounding) {
  std::vector<Vertex> out;
  for (int i = 0; i < count; ++i)
    out.push_back({{radius * std::cos(2 * kPi * i / count), radius * std::sin(2 * kPi * i / count)}, rounding});
  return out;
}

// RoundedPolygon.circle: a polygon whose corners are rounded by the radius of
// the circle it stands for, so the arcs meet in one.
std::vector<Vertex> circle(int count) { return regular(count, 1 / std::cos(kPi / count), 1); }

// RoundedPolygon.star: outer and inner vertices alternating, every corner
// rounded alike.
std::vector<Vertex> star(int count, double inner, double rounding) {
  std::vector<Vertex> out;
  for (int i = 0; i < count; ++i) {
    out.push_back({{std::cos(kPi / count * 2 * i), std::sin(kPi / count * 2 * i)}, rounding});
    out.push_back({{inner * std::cos(kPi / count * (2 * i + 1)), inner * std::sin(kPi / count * (2 * i + 1))}, rounding});
  }
  return out;
}

// MaterialShapes.customPolygon and doRepeat: a few points about (0.5, 0.5)
// repeated round the centre, every other repeat mirrored when asked.
std::vector<Vertex> custom(const std::vector<Vertex> &points, int reps, bool mirroring = false) {
  const Point centre{0.5, 0.5};
  std::vector<Vertex> out;
  const int np = int(points.size());
  if (mirroring) {
    std::vector<double> angles, distances;
    for (const auto &p : points) {
      const Point o = p.at - centre;
      angles.push_back(std::atan2(o.y, o.x) * 180 / kPi);
      distances.push_back(o.length());
    }
    const int actual = reps * 2;
    const double section = 360.0 / actual;
    for (int it = 0; it < actual; ++it)
      for (int index = 0; index < np; ++index) {
        const int i = it % 2 == 0 ? index : np - 1 - index;
        if (i > 0 || it % 2 == 0) {
          const double a = (section * it + (it % 2 == 0 ? angles[i] : section - angles[i] + 2 * angles[0])) * kPi / 180;
          out.push_back({Point{std::cos(a), std::sin(a)} * distances[i] + centre, points[i].rounding});
        }
      }
  } else {
    for (int it = 0; it < np * reps; ++it)
      out.push_back({rotated(points[it % np].at, (it / np) * 360.0 / reps, centre), points[it % np].rounding});
  }
  return out;
}

using Transform = Point (*)(Point);

struct Definition {
  const char *name;
  std::vector<Vertex> (*vertices)();
  Transform transform = nullptr;
};

// MaterialShapes.kt, companion object, each shape's construction as written
// there. The oval and the cookies are turned after rounding, as there.
const Definition kDefinitions[] = {
    {"circle", [] { return circle(10); }},
    {"square", [] { return std::vector<Vertex>{{{0.5, 0.5}, 0.3}, {{-0.5, 0.5}, 0.3}, {{-0.5, -0.5}, 0.3}, {{0.5, -0.5}, 0.3}}; }},
    {"oval", [] { return circle(8); }, [](Point p) { return rotated({p.x, p.y * 0.64}, -45); }},
    {"pill", [] { return custom({{{0.961, 0.039}, 0.426}, {{1.001, 0.428}, 0}, {{1.000, 0.609}, 1.0}}, 2, true); }},
    {"pentagon", [] { return custom({{{0.500, -0.009}, 0.172}, {{1.030, 0.365}, 0.164}, {{0.828, 0.970}, 0.169}}, 1, true); }},
    {"sunny", [] { return star(8, 0.8, 0.15); }},
    {"verySunny", [] { return custom({{{0.500, 1.080}, 0.085}, {{0.358, 0.843}, 0.085}}, 8); }},
    {"cookie4Sided", [] { return custom({{{1.237, 1.236}, 0.258}, {{0.500, 0.918}, 0.233}}, 4); }},
    {"cookie6Sided", [] { return custom({{{0.723, 0.884}, 0.394}, {{0.500, 1.099}, 0.398}}, 6); }},
    {"cookie7Sided", [] { return star(7, 0.75, 0.5); }, [](Point p) { return rotated(p, -90); }},
    {"cookie9Sided", [] { return star(9, 0.8, 0.5); }, [](Point p) { return rotated(p, -90); }},
    {"cookie12Sided", [] { return star(12, 0.8, 0.5); }, [](Point p) { return rotated(p, -90); }},
    {"clover4Leaf", [] { return custom({{{0.500, 0.074}, 0}, {{0.725, -0.099}, 0.476}}, 4, true); }},
    {"clover8Leaf", [] { return custom({{{0.500, 0.036}, 0}, {{0.758, -0.101}, 0.209}}, 8); }},
    {"burst", [] { return custom({{{0.500, -0.006}, 0.006}, {{0.592, 0.158}, 0.006}}, 12); }},
    {"softBurst", [] { return custom({{{0.193, 0.277}, 0.053}, {{0.176, 0.055}, 0.053}}, 10); }},
    {"flower", [] { return custom({{{0.370, 0.187}, 0}, {{0.416, 0.049}, 0.381}, {{0.479, 0.001}, 0.095}}, 8, true); }},
    {"puffyDiamond", [] { return custom({{{0.870, 0.130}, 0.146}, {{0.818, 0.357}, 0}, {{1.000, 0.332}, 0.853}}, 4, true); }},
};
constexpr int kShapeCount = int(sizeof(kDefinitions) / sizeof(kDefinitions[0]));

// Each curve is followed closely enough that the chord never strays more than
// a thousandth of the corner's radius from the arc it stands for.
constexpr int kStepsPerCurve = 32;

struct Outline {
  // The outline in the box's own units, centre at the origin and the longer
  // side running from -1 to 1, as RoundedPolygon.normalized and toShape
  // leave it.
  std::vector<Point> points;
  // Each point's angle about the centre, unwrapped so it only increases and
  // ends one full turn past where it starts.
  std::vector<double> angles;
};

Outline build(const Definition &definition) {
  auto cubics = rounded(definition.vertices());
  if (definition.transform)
    for (auto &c : cubics)
      c = {definition.transform(c.a0), definition.transform(c.c0), definition.transform(c.c1),
           definition.transform(c.a1), c.line};
  // RoundedPolygon.calculateBounds defaults to the approximate bounds, those
  // of the anchors and the handles, and normalized() and toShape both size
  // and centre the shape by them. A handle stands proud of its arc, so a
  // Material circle is drawn a little inside its box, and so is this one.
  double left = 1e9, top = 1e9, right = -1e9, bottom = -1e9;
  for (const auto &c : cubics)
    for (const Point p : {c.a0, c.c0, c.c1, c.a1}) {
      left = std::min(left, p.x);
      right = std::max(right, p.x);
      top = std::min(top, p.y);
      bottom = std::max(bottom, p.y);
    }
  const Point centre{(left + right) / 2, (top + bottom) / 2};
  const double scale = 2 / std::max(right - left, bottom - top);
  Outline out;
  for (const auto &c : cubics) {
    const int steps = c.line ? 1 : kStepsPerCurve;
    for (int k = 0; k < steps; ++k)
      out.points.push_back((c.at(double(k) / steps) - centre) * scale);
  }
  // Wind the outline so its angle increases, then unwrap it.
  double area = 0;
  for (size_t i = 0; i < out.points.size(); ++i)
    area += out.points[i].cross(out.points[(i + 1) % out.points.size()]);
  if (area < 0)
    std::reverse(out.points.begin(), out.points.end());
  double previous = 0;
  for (size_t i = 0; i < out.points.size(); ++i) {
    double a = std::atan2(out.points[i].y, out.points[i].x);
    if (i > 0)
      while (a < previous - kPi)
        a += 2 * kPi;
    out.angles.push_back(a);
    previous = a;
  }
  return out;
}

const std::vector<Outline> &outlines() {
  // Built once, together, on first use; a function-local static is safe to
  // reach from the render thread too.
  static const std::vector<Outline> all = [] {
    std::vector<Outline> built;
    for (const auto &definition : kDefinitions)
      built.push_back(build(definition));
    return built;
  }();
  return all;
}

int indexOf(const QString &name) {
  for (int i = 0; i < kShapeCount; ++i)
    if (name == QLatin1String(kDefinitions[i].name))
      return i;
  return -1;
}

// The radius where a ray from the centre at `angle` leaves the outline. The
// shapes are star-shaped about their centre, so the outline's angle rises
// monotonically and the one edge the ray crosses is found by bisection.
double radiusAt(const Outline &outline, double angle) {
  const auto &a = outline.angles;
  const int n = int(a.size());
  double t = std::fmod(angle - a[0], 2 * kPi);
  if (t < 0)
    t += 2 * kPi;
  t += a[0];
  const int upper = int(std::upper_bound(a.begin(), a.end(), t) - a.begin());
  const Point p = outline.points[(upper - 1 + n) % n], q = outline.points[upper % n];
  const Point u{std::cos(angle), std::sin(angle)}, e = q - p;
  const double denominator = u.cross(e);
  if (std::abs(denominator) < 1e-12)
    return std::min(p.length(), q.length());
  return p.cross(e) / denominator;
}

// The radii of the frame `progress` of the way from one shape to the other,
// at `count` even angles.
std::vector<double> frame(int from, int to, double progress, int count) {
  const auto &all = outlines();
  std::vector<double> radii(count);
  for (int i = 0; i < count; ++i) {
    const double angle = 2 * kPi * i / count;
    const double a = from < 0 ? 1 : radiusAt(all[from], angle);
    const double b = to < 0 ? a : radiusAt(all[to], angle);
    radii[i] = std::max(0.0, a + (b - a) * progress);
  }
  return radii;
}

// The outline drawn for a shape or a morph frame, in box units. A shape at
// rest is its own curves; a frame between two is their radii blended half a
// degree apart, which at the largest cover keeps each chord within a
// hundredth of a pixel of the frame it stands for. A spring carries a morph
// a little past its end, and the frame stays inside its box rather than
// being cut off by it. The mask and the ring's offset both read this, so
// they agree exactly.
std::vector<Point> drawn(int from, int to, double progress) {
  const bool morphing = to >= 0 && progress != 0;
  const int still = morphing ? (progress == 1 ? to : -2) : from;
  if (still >= 0)
    return outlines()[still].points;
  std::vector<double> radii;
  if (still == -1)
    radii.assign(720, 1.0); // an unknown name is a circle
  else
    radii = frame(from, to, progress, 720);
  std::vector<Point> points(radii.size());
  for (size_t i = 0; i < radii.size(); ++i) {
    const double angle = 2 * kPi * double(i) / double(radii.size());
    points[i] = {std::clamp(radii[i] * std::cos(angle), -1.0, 1.0), std::clamp(radii[i] * std::sin(angle), -1.0, 1.0)};
  }
  return points;
}
} // namespace

QStringList shapeNames() {
  QStringList names;
  names.reserve(kShapeCount);
  for (const auto &definition : kDefinitions)
    names << QString::fromLatin1(definition.name);
  return names;
}

bool hasShape(const QString &name) { return indexOf(name) >= 0; }

QList<double> shapeOutline(const QString &name, int steps) {
  const int count = qBound(8, steps, 512);
  const int index = indexOf(name);
  QList<double> radii;
  radii.reserve(count + 1);
  for (int i = 0; i <= count; ++i)
    radii.append(index < 0 ? 1.0 : radiusAt(outlines()[index], i * 2 * kPi / count));
  return radii;
}

QPainterPath shapePath(const QString &name, const QRectF &bounds, const QString &toName,
                       double progress) {
  const double half = qMin(bounds.width(), bounds.height()) / 2;
  const QPointF centre = bounds.center();
  QPainterPath path;
  for (const auto &p : drawn(indexOf(name), indexOf(toName), progress)) {
    const QPointF point(centre.x() + p.x * half, centre.y() + p.y * half);
    // isEmpty() stays true after a lone moveTo, so count instead.
    if (path.elementCount() == 0)
      path.moveTo(point);
    else
      path.lineTo(point);
  }
  path.closeSubpath();
  return path;
}

QList<double> shapeOffset(const QString &name, const QString &toName, double progress,
                          double margin, int steps) {
  const int count = qBound(8, steps, 512);
  const auto points = drawn(indexOf(name), indexOf(toName), progress);
  const int samples = int(points.size());
  const double m = std::max(0.0, margin);
  // A point at distance r from the centre is within `margin` of a ray only if
  // their angles differ by at most asin(margin / r), so each ray need only
  // look at the edges that start within that, plus the widest angle an edge
  // spans, of it. The outline's angle only rises, so the edges in the window
  // are found by bisection. During a morph this runs every frame.
  std::vector<double> angles(samples);
  double nearest = 1e9, widest = 0;
  for (int j = 0; j < samples; ++j) {
    double a = std::atan2(points[j].y, points[j].x);
    if (j > 0) {
      while (a < angles[j - 1])
        a += 2 * kPi;
      widest = std::max(widest, a - angles[j - 1]);
    }
    angles[j] = a;
    nearest = std::min(nearest, points[j].length());
  }
  widest = std::max(widest, angles[0] + 2 * kPi - angles[samples - 1]);
  const double window = m < nearest ? std::asin(m / nearest) + widest + 1e-6 : kPi;
  QList<double> out;
  out.reserve(count + 1);
  for (int i = 0; i <= count; ++i) {
    const double angle = 2 * kPi * i / count;
    const Point u{std::cos(angle), std::sin(angle)};
    // The farthest the ray is still within `margin` of the outline: of each
    // edge's two parallels at that distance, and of the disc round each
    // corner between edges. The shape and so its offset are star-shaped, so
    // past that point the ray is clear for good.
    double reach = 0;
    const auto edge = [&](int j) {
      const Point a = points[j], b = points[(j + 1) % samples], e = b - a;
      const double along = u.dot(a), across = m * m - (a.dot(a) - along * along);
      // A ray through a vertex meets both edges at their very ends, where
      // rounding can put it a hair outside each; the tolerances keep it.
      if (across >= -1e-12)
        reach = std::max(reach, along + std::sqrt(std::max(0.0, across)));
      const double length = e.length(), denominator = u.cross(e);
      if (length < 1e-12 || std::abs(denominator) < 1e-12)
        return;
      const Point normal = e.rotate90() * (m / length);
      for (const Point shifted : {a + normal, a - normal}) {
        const double s = shifted.cross(u) / denominator;
        const double t = shifted.cross(e) / denominator;
        if (s >= -1e-9 && s <= 1 + 1e-9 && t > reach)
          reach = t;
      }
    };
    if (window >= kPi) {
      for (int j = 0; j < samples; ++j)
        edge(j);
    } else {
      // The ray's angle a turn either side as well, for the window that
      // straddles where the outline's angles start.
      for (const double turn : {-2 * kPi, 0.0, 2 * kPi}) {
        const auto from = std::lower_bound(angles.begin(), angles.end(), angle + turn - window);
        const auto to = std::upper_bound(angles.begin(), angles.end(), angle + turn + window);
        for (auto it = from; it < to; ++it)
          edge(int(it - angles.begin()));
      }
    }
    out.append(reach);
  }
  return out;
}

} // namespace m3
