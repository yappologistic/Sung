#pragma once
// Material 3 motion physics.
//
// Material no longer describes motion as a duration and an easing curve. It
// describes it as a spring, published as a damping ratio and a stiffness, and
// the duration falls out of the physics rather than being chosen. A spring
// given a longer distance takes longer; a spring with a lower damping ratio
// rings before it rests. Those are the two things a fixed curve cannot say.
//
// Qt Quick animates on curves, so a spring has to be converted to one. This
// converts it honestly: it solves the spring's own step response and fits a
// cubic bezier spline to it, using the response's analytic derivative for the
// tangents so the fit follows the real curve rather than a smoothed guess. The
// duration is the spring's settling time.
//
// Two schemes. Expressive rings; standard does not, or barely. Spatial springs
// move things and are underdamped, so they pass their target and come back.
// Effects springs carry colour and opacity, where passing the target would mean
// showing a wrong value, so Material makes them critically damped and they
// never overshoot.
#include <QVariantList>
#include <QVariantMap>

namespace m3 {

// One of Material's springs, as Qt Quick needs it.
struct Spring {
  // The settling time, which is the duration the animation runs for.
  int durationMs = 0;
  // A cubic bezier spline: control, control, endpoint, repeated, ending at
  // (1,1). This is what easing.bezierCurve takes.
  QVariantList curve;
};

// Material's published constants for the two schemes. Mass is 1, as Compose
// leaves it, so stiffness alone sets the natural frequency.
struct SpringTokens {
  double damping;
  double stiffness;
};

// The step response of a unit-mass spring, and its derivative. Exposed so the
// conversion can be checked against the physics rather than against itself.
double springResponse(double damping, double stiffness, double seconds);
double springVelocity(double damping, double stiffness, double seconds);
// How long until the response stays within Compose's displacement threshold of
// its target, which is when the animation is over.
double springSettleSeconds(double damping, double stiffness);

Spring spring(double damping, double stiffness);
// LoadingIndicator.kt:400-419 uses its own 0.1 visibility threshold so the
// shape morph finishes within the next 650ms slot.
Spring loadingMorphSpring();

// The six scheme springs and LoadingIndicator's separate loadingMorph spring.
// Each value has "ms" and "curve".
QVariantMap motionScheme(bool expressive);
SpringTokens springTokens(bool expressive, const QString &name);

} // namespace m3
