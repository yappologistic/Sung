#include "m3motion.h"
#include <QString>
#include <cmath>

namespace m3 {
namespace {

// Compose stops a float animation once it is this close to its target, so this
// is where the spring is finished and the duration ends.
constexpr double kDisplacementThreshold = 0.01;
// Segments in the fitted spline. Eight puts the fit inside the threshold the
// spring itself is allowed to stop at, so more would be describing a difference
// the animation is not required to express.
constexpr int kSegments = 8;

// Material's published damping ratios and stiffnesses. Effects springs are
// critically damped in both schemes and share their values; only the spatial
// ones differ, which is what the two schemes are.
const SpringTokens kExpressive[6] = {
    {0.6, 800.0},  // fastSpatial
    {0.8, 380.0},  // defaultSpatial
    {0.8, 200.0},  // slowSpatial
    {1.0, 3800.0}, // fastEffects
    {1.0, 1600.0}, // defaultEffects
    {1.0, 800.0},  // slowEffects
};
const SpringTokens kStandard[6] = {
    {0.9, 1400.0}, {0.9, 700.0}, {0.9, 300.0}, {1.0, 3800.0}, {1.0, 1600.0}, {1.0, 800.0},
};
const char *const kNames[6] = {"fastSpatial",  "defaultSpatial",  "slowSpatial",
                               "fastEffects", "defaultEffects", "slowEffects"};

} // namespace

double springResponse(double damping, double stiffness, double seconds) {
  const double w0 = std::sqrt(stiffness);
  if (seconds <= 0)
    return 0;
  if (damping < 1.0) {
    const double wd = w0 * std::sqrt(1 - damping * damping);
    return 1 - std::exp(-damping * w0 * seconds) *
                   (std::cos(wd * seconds) + (damping * w0 / wd) * std::sin(wd * seconds));
  }
  return 1 - std::exp(-w0 * seconds) * (1 + w0 * seconds);
}

double springVelocity(double damping, double stiffness, double seconds) {
  const double w0 = std::sqrt(stiffness);
  if (damping < 1.0) {
    const double wd = w0 * std::sqrt(1 - damping * damping);
    return (w0 * w0 / wd) * std::exp(-damping * w0 * seconds) * std::sin(wd * seconds);
  }
  return w0 * w0 * seconds * std::exp(-w0 * seconds);
}

static double springSettleSecondsAt(double damping, double stiffness, double threshold) {
  const double w0 = std::sqrt(stiffness);
  if (damping < 1.0) {
    // The ringing decays inside an envelope, so the last crossing of the
    // threshold is where that envelope reaches it.
    const double envelope = std::sqrt(1 - damping * damping);
    return -std::log(threshold * envelope) / (damping * w0);
  }
  // Critically damped has no closed form for this; the response is monotonic,
  // so a bisection lands on it exactly.
  double low = 0, high = 100;
  for (int i = 0; i < 200; ++i) {
    const double mid = (low + high) / 2;
    if (std::exp(-mid) * (1 + mid) > threshold)
      low = mid;
    else
      high = mid;
  }
  return low / w0;
}

double springSettleSeconds(double damping, double stiffness) {
  return springSettleSecondsAt(damping, stiffness, kDisplacementThreshold);
}

static Spring springWithThreshold(double damping, double stiffness, double threshold) {
  Spring out;
  const double settle = springSettleSecondsAt(damping, stiffness, threshold);
  out.durationMs = int(std::lround(settle * 1000));
  // The spline runs in the easing curve's own coordinates, where x is the
  // fraction of the duration. A derivative in those coordinates is therefore
  // the real one scaled by the duration.
  const double step = 1.0 / kSegments;
  for (int i = 0; i < kSegments; ++i) {
    const double x0 = i * step, x1 = (i + 1) * step;
    const double y0 = springResponse(damping, stiffness, settle * x0);
    const double d0 = settle * springVelocity(damping, stiffness, settle * x0);
    // The last point is the target itself. The spring is allowed to stop a
    // threshold short of it, and a curve is not, so the spline closes the gap.
    const bool last = i == kSegments - 1;
    const double y1 = last ? 1.0 : springResponse(damping, stiffness, settle * x1);
    const double d1 = last ? 0.0 : settle * springVelocity(damping, stiffness, settle * x1);
    // Hermite to bezier: a third of the segment along each tangent.
    out.curve << x0 + step / 3 << y0 + d0 * step / 3;
    out.curve << x1 - step / 3 << y1 - d1 * step / 3;
    out.curve << (last ? 1.0 : x1) << y1;
  }
  return out;
}

Spring spring(double damping, double stiffness) {
  return springWithThreshold(damping, stiffness, kDisplacementThreshold);
}

Spring loadingMorphSpring() {
  // LoadingIndicator.kt:400-419 specifies 0.6/200 with a 0.1 threshold.
  return springWithThreshold(0.6, 200.0, 0.1);
}

SpringTokens springTokens(bool expressive, const QString &name) {
  const SpringTokens *set = expressive ? kExpressive : kStandard;
  for (int i = 0; i < 6; ++i)
    if (name == QLatin1String(kNames[i]))
      return set[i];
  return set[1];
}

QVariantMap motionScheme(bool expressive) {
  const SpringTokens *set = expressive ? kExpressive : kStandard;
  QVariantMap out;
  for (int i = 0; i < 6; ++i) {
    const auto s = spring(set[i].damping, set[i].stiffness);
    QVariantMap entry{{"ms", s.durationMs}, {"curve", s.curve}};
    if (i == 0) {
      // Keep the six scheme keys intact. The loading component's dedicated
      // spring travels in the same backend map under FastSpatial.
      const auto morph = loadingMorphSpring();
      entry.insert(QStringLiteral("loadingMorph"),
                   QVariantMap{{"ms", morph.durationMs}, {"curve", morph.curve}});
    }
    out.insert(QString::fromLatin1(kNames[i]),
               entry);
  }
  return out;
}

} // namespace m3
