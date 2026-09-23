// The spring conversion is checked against the physics it claims to follow,
// not against its own output. Every assertion here re-derives what Material's
// published damping ratio and stiffness mean and asks whether the curve Qt is
// handed says the same thing.
#include "m3motion.h"
#include <QTest>
#include <QVariantList>
#include <cmath>

namespace {

// Walk the fitted spline and read y at a given x, the way an easing curve is
// read. The spline's x is strictly increasing, so the segment is found by
// stepping and the parameter inside it by bisection.
double curveAt(const QVariantList &curve, double x) {
  double px = 0, py = 0;
  for (int i = 0; i + 5 < curve.size(); i += 6) {
    const double c1x = curve[i].toDouble(), c1y = curve[i + 1].toDouble();
    const double c2x = curve[i + 2].toDouble(), c2y = curve[i + 3].toDouble();
    const double ex = curve[i + 4].toDouble(), ey = curve[i + 5].toDouble();
    if (x > ex && i + 11 < curve.size()) {
      px = ex;
      py = ey;
      continue;
    }
    const auto at = [&](double t, bool wantY) {
      const double m = 1 - t;
      const double a = m * m * m, b = 3 * m * m * t, c = 3 * m * t * t, d = t * t * t;
      return wantY ? a * py + b * c1y + c * c2y + d * ey : a * px + b * c1x + c * c2x + d * ex;
    };
    double low = 0, high = 1;
    for (int step = 0; step < 60; ++step) {
      const double mid = (low + high) / 2;
      if (at(mid, false) < x)
        low = mid;
      else
        high = mid;
    }
    return at((low + high) / 2, true);
  }
  return 1;
}

} // namespace

class M3MotionTest : public QObject {
  Q_OBJECT
private slots:
  // The curve Qt animates on has to be the curve the spring describes. A
  // difference bigger than the threshold the spring is allowed to stop at
  // would be motion the specification did not ask for.
  void followsTheSpringItCameFrom_data() {
    QTest::addColumn<double>("damping");
    QTest::addColumn<double>("stiffness");
    for (bool expressive : {true, false})
      for (const char *name : {"fastSpatial", "defaultSpatial", "slowSpatial", "fastEffects",
                               "defaultEffects", "slowEffects"}) {
        const auto tokens = m3::springTokens(expressive, QString::fromLatin1(name));
        QTest::newRow(QByteArray(expressive ? "expressive-" : "standard-") + name)
            << tokens.damping << tokens.stiffness;
      }
  }
  void followsTheSpringItCameFrom() {
    QFETCH(double, damping);
    QFETCH(double, stiffness);
    const auto fitted = m3::spring(damping, stiffness);
    const double settle = m3::springSettleSeconds(damping, stiffness);
    double worst = 0;
    for (int i = 0; i <= 200; ++i) {
      const double x = i / 200.0;
      worst = std::max(worst, std::abs(curveAt(fitted.curve, x) -
                                       m3::springResponse(damping, stiffness, settle * x)));
    }
    QVERIFY2(worst < 0.012, qPrintable(QString("worst departure %1").arg(worst)));
  }

  // An easing curve is read left to right, so its x has to climb, it has to
  // start where the animation starts and land exactly on the target.
  void isAValidEasingCurve() {
    for (bool expressive : {true, false})
      for (const char *name : {"fastSpatial", "defaultSpatial", "slowSpatial", "fastEffects",
                               "defaultEffects", "slowEffects"}) {
        const auto tokens = m3::springTokens(expressive, QString::fromLatin1(name));
        const auto curve = m3::spring(tokens.damping, tokens.stiffness).curve;
        QCOMPARE(curve.size() % 6, 0);
        double previous = 0;
        for (int i = 0; i < curve.size(); i += 2) {
          const double x = curve[i].toDouble();
          QVERIFY2(x >= previous - 1e-9, qPrintable(QString("%1 went back to %2").arg(name).arg(x)));
          previous = x;
        }
        QCOMPARE(curve[curve.size() - 2].toDouble(), 1.0);
        QCOMPARE(curve[curve.size() - 1].toDouble(), 1.0);
      }
  }

  // Material's own distinction: a spatial spring is underdamped and passes its
  // target, an effects spring is critically damped and must not, because the
  // value it carries is a colour or an opacity and there is no such thing as
  // more than opaque.
  void effectsNeverOvershootAndSpatialDoes() {
    for (bool expressive : {true, false}) {
      for (const char *name : {"fastEffects", "defaultEffects", "slowEffects"}) {
        const auto t = m3::springTokens(expressive, QString::fromLatin1(name));
        QCOMPARE(t.damping, 1.0);
        const auto curve = m3::spring(t.damping, t.stiffness).curve;
        for (int i = 1; i < curve.size(); i += 2)
          QVERIFY2(curve[i].toDouble() <= 1.0 + 1e-9, name);
      }
      for (const char *name : {"fastSpatial", "defaultSpatial", "slowSpatial"}) {
        const auto t = m3::springTokens(expressive, QString::fromLatin1(name));
        QVERIFY2(t.damping < 1.0, name);
      }
    }
    // Expressive is the scheme that rings. Its default spatial spring passes
    // the target further than the standard one does, which is the difference
    // between the two schemes rather than a matter of taste.
    const auto loose = m3::springTokens(true, "defaultSpatial");
    const auto tight = m3::springTokens(false, "defaultSpatial");
    const auto peak = [](m3::SpringTokens t) {
      const double settle = m3::springSettleSeconds(t.damping, t.stiffness);
      double top = 0;
      for (int i = 0; i <= 400; ++i)
        top = std::max(top, m3::springResponse(t.damping, t.stiffness, settle * i / 400.0));
      return top;
    };
    QVERIFY2(peak(loose) > peak(tight),
             qPrintable(QString("expressive %1 against standard %2").arg(peak(loose)).arg(peak(tight))));
  }

  // The duration is the settling time, so a stiffer spring finishes sooner.
  // This is the ordering the scheme is built on and the reason fast, default
  // and slow mean anything.
  void durationsFollowStiffness() {
    for (bool expressive : {true, false}) {
      const auto scheme = m3::motionScheme(expressive);
      const auto ms = [&](const char *name) {
        return scheme.value(QString::fromLatin1(name)).toMap().value("ms").toInt();
      };
      QVERIFY(ms("fastSpatial") < ms("defaultSpatial"));
      QVERIFY(ms("defaultSpatial") < ms("slowSpatial"));
      QVERIFY(ms("fastEffects") < ms("defaultEffects"));
      QVERIFY(ms("defaultEffects") < ms("slowEffects"));
      // An effect resolves inside the movement it accompanies rather than
      // outlasting it.
      QVERIFY(ms("defaultEffects") < ms("defaultSpatial"));
      for (const char *name : {"fastSpatial", "defaultSpatial", "slowSpatial", "fastEffects",
                               "defaultEffects", "slowEffects"}) {
        const auto t = m3::springTokens(expressive, QString::fromLatin1(name));
        QCOMPARE(ms(name), int(std::lround(m3::springSettleSeconds(t.damping, t.stiffness) * 1000)));
      }
    }
  }

  // Every spring in the scheme is published, and the map is what QML reads.
  void schemeCarriesEverySpring() {
    for (bool expressive : {true, false}) {
      const auto scheme = m3::motionScheme(expressive);
      QCOMPARE(scheme.size(), 7);
      QVERIFY(!scheme.value("fastSpatial").toMap().contains("loadingMorph"));
      const auto morph = m3::loadingMorphSpring();
      QCOMPARE(scheme.value("loadingMorph").toMap().value("ms").toInt(), morph.durationMs);
      QCOMPARE(scheme.value("loadingMorph").toMap().value("curve").toList(), morph.curve);
      for (const auto &key : scheme.keys()) {
        const auto entry = scheme.value(key).toMap();
        QVERIFY2(entry.value("ms").toInt() > 0, qPrintable(key));
        QVERIFY2(!entry.value("curve").toList().isEmpty(), qPrintable(key));
      }
    }
  }
};

QTEST_MAIN(M3MotionTest)
#include "m3motion_test.moc"
