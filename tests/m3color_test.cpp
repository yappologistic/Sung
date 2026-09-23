// Material 3 dynamic color. The guarantees the interface relies on are that a
// requested tone really is that tone, that a hue survives the trip through the
// solver, and that the roles read off the palettes clear Material's contrast
// floors in both themes.
#include "m3color.h"
#include <QtTest>

namespace {
double contrast(const QColor &a, const QColor &b) {
  const auto luminance = [](const QColor &c) {
    const auto channel = [](double v) {
      return v <= 0.040449936 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
  };
  const double x = luminance(a), y = luminance(b);
  return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}
double hueGap(double a, double b) {
  const double difference = std::fmod(std::abs(a - b), 360.0);
  return difference > 180.0 ? 360.0 - difference : difference;
}
const QList<QColor> &sources() {
  static const QList<QColor> list{QColor("#ff0000"), QColor("#00ff00"), QColor("#0000ff"),
                                  QColor("#964829"), QColor("#1f4f6b"), QColor("#d98324"),
                                  QColor("#3d2a52"), QColor("#4fa3a5"), QColor("#ffffff"),
                                  QColor("#000000"), QColor("#7f7f7f")};
  return list;
}
} // namespace

class M3ColorTest : public QObject {
  Q_OBJECT
private slots:
  // Tone is CIE L*, and it is the axis every contrast promise rests on, so the
  // solver has to reproduce it rather than approach it.
  void tonesAreExact() {
    for (double hue = 0; hue < 360; hue += 24)
      for (double chroma : {4.0, 16.0, 36.0, 80.0})
        for (double tone : {0.0, 6.0, 12.0, 30.0, 50.0, 80.0, 90.0, 98.0, 100.0}) {
          const auto color = m3::solve(hue, chroma, tone);
          QVERIFY2(color.isValid(), qPrintable(QString("no color for %1/%2/%3").arg(hue).arg(chroma).arg(tone)));
          QVERIFY2(std::abs(m3::toneOf(color) - tone) < 0.5,
                   qPrintable(QString("tone %1 became %2").arg(tone).arg(m3::toneOf(color))));
        }
  }

  // Chroma is clamped to what sRGB can hold, but hue must never drift.
  void huesSurviveTheSolver() {
    for (double hue = 0; hue < 360; hue += 12)
      for (double tone : {20.0, 40.0, 60.0, 80.0}) {
        const auto color = m3::solve(hue, 36.0, tone);
        const auto back = m3::measure(color);
        QVERIFY2(back.chroma > 1.0, "a chromatic request must not collapse to grey");
        QVERIFY2(hueGap(back.hue, hue) < 2.0,
                 qPrintable(QString("hue %1 became %2").arg(hue).arg(back.hue)));
      }
  }

  // Measuring a colour and solving for what was measured returns the colour.
  void measurementRoundTrips() {
    for (const auto &source : sources()) {
      const auto hct = m3::measure(source);
      const auto back = m3::solve(hct.hue, hct.chroma, hct.tone);
      QVERIFY2(std::abs(m3::toneOf(back) - m3::toneOf(source)) < 0.5, qPrintable(source.name()));
      if (hct.chroma > 5.0)
        QVERIFY2(hueGap(m3::measure(back).hue, hct.hue) < 2.0, qPrintable(source.name()));
    }
  }

  // A tonal palette has to climb: tone 10 is always darker than tone 90.
  void palettesClimbWithTone() {
    for (const auto &source : sources()) {
      const auto palettes = m3::palettesFor(source);
      for (const auto *palette : {&palettes.primary, &palettes.secondary, &palettes.tertiary,
                                  &palettes.neutral, &palettes.neutralVariant}) {
        double previous = -1;
        for (double tone = 0; tone <= 100; tone += 5) {
          const double measured = m3::toneOf(palette->tone(tone));
          QVERIFY2(measured > previous, qPrintable(QString("tone %1 did not rise").arg(tone)));
          previous = measured;
        }
      }
    }
  }

  // Neutral palettes tint the surfaces; they must never colour them.
  void neutralsStayNeutral() {
    for (const auto &source : sources()) {
      const auto palettes = m3::palettesFor(source);
      for (double tone = 10; tone <= 95; tone += 5) {
        QVERIFY(m3::measure(palettes.neutral.tone(tone)).chroma <= 7.0);
        QVERIFY(m3::measure(palettes.neutralVariant.tone(tone)).chroma <= 9.0);
      }
    }
  }

  // The contrast floors Material sets for text and for boundaries.
  void schemesClearContrastFloors() {
    for (const auto &source : sources())
      for (bool dark : {false, true}) {
        const auto roles = m3::scheme(source, dark);
        const auto color = [&roles](const char *name) { return roles.value(name).value<QColor>(); };
        const auto surface = color("surface");
        const QString where = source.name() + (dark ? " dark" : " light");
        const auto atLeast = [&](const char *front, const char *back, double floor) {
          const double measured = contrast(color(front), color(back));
          QVERIFY2(measured >= floor, qPrintable(QString("%1: %2 on %3 is %4, needs %5")
                                                     .arg(where, front, back)
                                                     .arg(measured)
                                                     .arg(floor)));
        };
        atLeast("onSurface", "surface", 4.5);
        atLeast("onSurfaceVariant", "surface", 4.5);
        atLeast("onPrimary", "primary", 4.5);
        atLeast("onPrimaryContainer", "primaryContainer", 4.5);
        atLeast("onSecondaryContainer", "secondaryContainer", 4.5);
        atLeast("onTertiaryContainer", "tertiaryContainer", 4.5);
        atLeast("inverseOnSurface", "inverseSurface", 4.5);
        // Boundaries and accents only carry the 3:1 floor for large shapes.
        atLeast("outline", "surface", 3.0);
        atLeast("primary", "surface", 4.5);
        // Sung draws body text and accents on the container ladder as well as
        // on the base surface, so the floors have to hold all the way up it.
        for (const char *step : {"surfaceContainerLow", "surfaceContainer", "surfaceContainerHigh",
                                 "surfaceContainerHighest"}) {
          atLeast("onSurface", step, 4.5);
          atLeast("onSurfaceVariant", step, 4.5);
          atLeast("primary", step, 4.5);
        }
        QVERIFY2(contrast(color("surfaceContainerHighest"), surface) < 3.0,
                 "surface containers are steps, not boundaries");
      }
  }

  // The container ladder has to rise away from the surface in dark themes and
  // sink towards it in light ones, the way Material stacks elevation by tone.
  void surfaceLadderRunsTheRightWay() {
    for (const auto &source : sources()) {
      for (bool dark : {false, true}) {
        const auto roles = m3::scheme(source, dark);
        const auto tone = [&roles](const char *name) {
          return m3::toneOf(roles.value(name).value<QColor>());
        };
        const QList<double> ladder{tone("surfaceContainerLowest"), tone("surfaceContainerLow"),
                                   tone("surfaceContainer"), tone("surfaceContainerHigh"),
                                   tone("surfaceContainerHighest")};
        for (int i = 1; i < ladder.size(); ++i)
          if (dark)
            QVERIFY2(ladder[i] > ladder[i - 1], "dark containers lighten as they stack");
          else
            QVERIFY2(ladder[i] < ladder[i - 1], "light containers darken as they stack");
      }
    }
  }

  // A grey cover has no hue to spread, so the scheme must still be usable.
  void greySourcesDegradeGracefully() {
    for (const auto &source : {QColor("#000000"), QColor("#ffffff"), QColor("#808080")}) {
      for (bool dark : {false, true}) {
        const auto roles = m3::scheme(source, dark);
        QVERIFY(roles.value("primary").value<QColor>().isValid());
        QVERIFY(contrast(roles.value("onSurface").value<QColor>(),
                         roles.value("surface").value<QColor>()) >= 4.5);
      }
    }
  }

  // The fixed accents are the one family that does not move with the theme.
  void fixedAccentsHoldTheirTone() {
    for (const auto &source : {QColor("#3f6ad8"), QColor("#c0392b"), QColor("#2f8f5b")}) {
      const auto light = m3::scheme(source, false);
      const auto dark = m3::scheme(source, true);
      for (const auto &accent : {"primary", "secondary", "tertiary"}) {
        const QString base(accent);
        const QString capital = base.at(0).toUpper() + base.mid(1);
        for (const auto &role : {base + "Fixed", base + "FixedDim", "on" + capital + "Fixed",
                                 "on" + capital + "FixedVariant"}) {
          QVERIFY2(light.contains(role), qPrintable(role + " is published"));
          QCOMPARE(light.value(role).value<QColor>(), dark.value(role).value<QColor>());
        }
        const auto fixed = light.value(base + "Fixed").value<QColor>();
        const auto dim = light.value(base + "FixedDim").value<QColor>();
        const auto ink = light.value("on" + capital + "Fixed").value<QColor>();
        const auto variant = light.value("on" + capital + "FixedVariant").value<QColor>();
        // Material's tones: the container at 90, its dimmer twin at 80, and the
        // two inks at 10 and 30.
        QVERIFY(qAbs(m3::toneOf(fixed) - 90) < 1.5);
        QVERIFY(qAbs(m3::toneOf(dim) - 80) < 1.5);
        QVERIFY(qAbs(m3::toneOf(ink) - 10) < 1.5);
        QVERIFY(qAbs(m3::toneOf(variant) - 30) < 1.5);
        QVERIFY2(contrast(ink, fixed) >= 4.5, "the fixed accent carries its own text");
        QVERIFY2(contrast(variant, fixed) >= 4.5, "and its secondary text too");
      }
    }
  }

  // The inverse accent is the tone the other theme would have used, which is
  // what lets a snackbar sit against the theme rather than in it.
  void inverseRolesCrossTheThemes() {
    for (const auto &source : {QColor("#3f6ad8"), QColor("#c0392b"), QColor("#2f8f5b")}) {
      const auto light = m3::scheme(source, false);
      const auto dark = m3::scheme(source, true);
      QVERIFY(light.contains("inversePrimary") && dark.contains("inversePrimary"));
      // Light schemes take the dark theme's tone 80, and dark ones tone 40.
      QVERIFY(qAbs(m3::toneOf(light.value("inversePrimary").value<QColor>()) - 80) < 1.5);
      QVERIFY(qAbs(m3::toneOf(dark.value("inversePrimary").value<QColor>()) - 40) < 1.5);
      for (const auto &roles : {light, dark}) {
        const auto surface = roles.value("inverseSurface").value<QColor>();
        QVERIFY2(contrast(roles.value("inverseOnSurface").value<QColor>(), surface) >= 4.5,
                 "the inverse surface carries its own text");
        QVERIFY2(contrast(roles.value("inversePrimary").value<QColor>(), surface) >= 3.0,
                 "and its action stands off it");
      }
    }
  }

  // Each variant spreads the same five palettes differently. The numbers are
  // Material's own.
  void variantsSpreadThePalettes() {
    const QColor source("#3f6ad8");
    const auto hue = m3::measure(source).hue;
    const auto neutral = m3::palettesFor(source, m3::Variant::Neutral);
    QCOMPARE(neutral.primary.chroma, 12.0);
    QCOMPARE(neutral.neutral.chroma, 2.0);
    const auto balanced = m3::palettesFor(source, m3::Variant::TonalSpot);
    QCOMPARE(balanced.primary.chroma, 36.0);
    QCOMPARE(balanced.neutral.chroma, 6.0);
    const auto vibrant = m3::palettesFor(source, m3::Variant::Vibrant);
    QCOMPARE(vibrant.primary.chroma, 200.0);
    const auto expressive = m3::palettesFor(source, m3::Variant::Expressive);
    QCOMPARE(expressive.primary.chroma, 40.0);
    // Expressive turns the primary hue two thirds of the way round on purpose.
    double turn = std::abs(expressive.primary.hue - hue);
    if (turn > 180) turn = 360 - turn;
    QVERIFY2(turn > 90, "expressive detaches from the source hue");
    const auto content = m3::palettesFor(source, m3::Variant::Content);
    QVERIFY2(qAbs(content.primary.chroma - m3::measure(source).chroma) < 0.01,
             "content keeps the source's own chroma");
    QCOMPARE(m3::variantFor("vibrant"), m3::Variant::Vibrant);
    QCOMPARE(m3::variantFor("nonsense"), m3::Variant::TonalSpot);
  }

  // Every variant, at every contrast level, still has to be readable.
  void everyVariantStaysReadable() {
    for (const auto &source : {QColor("#3f6ad8"), QColor("#c0392b"), QColor("#2f8f5b")})
      for (const auto &name : m3::variantNames())
        for (bool dark : {false, true})
          for (double level : {0.0, 0.5, 1.0}) {
            const auto roles = m3::scheme(source, dark, m3::variantFor(name), level);
            QVERIFY2(contrast(roles.value("onSurface").value<QColor>(),
                              roles.value("surface").value<QColor>()) >= 4.5,
                     qPrintable(name + " keeps body text readable"));
          }
  }

  // MCU color_spec_2021.ts:130-282, 312-739 gives each role its own
  // background and curve. The highest achievable sRGB ratio caps a target
  // when the background cannot reach it, notably 21:1 over a dark surface.
  void everyRoleFollowsItsContrastCurve() {
    for (const auto &source : {QColor("#3f6ad8"), QColor("#c0392b"), QColor("#2f8f5b"),
                               QColor("#808080"), QColor("#b75f38")})
      for (const auto &name : m3::variantNames())
        for (bool dark : {false, true})
          for (double level : {0.0, 0.5, 1.0}) {
            const auto roles = m3::scheme(source, dark, m3::variantFor(name), level);
            const auto color = [&](const QString &role) { return roles.value(role).value<QColor>(); };
            const QString where = QString("%1 %2 %3 contrast %4")
                                      .arg(source.name(), name, dark ? "dark" : "light")
                                      .arg(level);
            const auto check = [&](const QString &front, const QString &back, double requested) {
              const auto background = color(back);
              const double possible = std::max(contrast(QColor("#000000"), background),
                                               contrast(QColor("#ffffff"), background));
              const double wanted = std::min(requested, possible);
              QVERIFY2(contrast(color(front), background) >= wanted - 0.16,
                       qPrintable(QString("%1: %2 on %3 is %4, needs %5")
                                      .arg(where, front, back)
                                      .arg(contrast(color(front), background), 0, 'f', 2)
                                      .arg(wanted, 0, 'f', 2)));
            };
            const double on = level == 0 ? 7 : level == 0.5 ? 11 : 21;
            const double onContainer = level == 0 ? 4.5 : level == 0.5 ? 7 : 11;
            const double onVariant = level == 0 ? 4.5 : level == 0.5 ? 7 : 11;
            const QString surface = dark ? "surfaceBright" : "surfaceDim";
            check("onSurface", surface, on);
            check("onSurfaceVariant", surface, onVariant);
            check("inverseOnSurface", "inverseSurface", on);
            check("inversePrimary", "inverseSurface", level == 0 ? 4.5 : 7);
            for (const auto &family : {"Primary", "Secondary", "Tertiary", "Error"}) {
              const QString base = QString(family).toLower();
              check("on" + QString(family), base, on);
              check("on" + QString(family) + "Container", base + "Container", onContainer);
            }
            for (const auto &family : {"Primary", "Secondary", "Tertiary"}) {
              const QString base = QString(family).toLower();
              check("on" + QString(family) + "Fixed", base + "Fixed", on);
              check("on" + QString(family) + "Fixed", base + "FixedDim", on);
              check("on" + QString(family) + "FixedVariant", base + "Fixed", onContainer);
              check("on" + QString(family) + "FixedVariant", base + "FixedDim", onContainer);
            }
          }
  }

  // MCU color_spec_2021.ts:156-221 and 250-282: backgrounds and inverse
  // inks answer contrast independently of the accents.
  void highContrastMovesTheWholeScheme() {
    const QColor source("#3f6ad8");
    for (bool dark : {false, true}) {
      const auto normal = m3::scheme(source, dark, m3::Variant::TonalSpot, 0);
      const auto high = m3::scheme(source, dark, m3::Variant::TonalSpot, 1);
      const auto tone = [&](const QVariantMap &roles, const char *name) {
        return m3::toneOf(roles.value(name).value<QColor>());
      };
      QVERIFY2(qAbs(tone(high, "surfaceContainerHighest") - (dark ? 30 : 80)) < 1,
               "surfaceContainerHighest reaches MCU's high-contrast tone");
      for (const auto &role : {"surfaceContainer", "surfaceContainerHigh", "surfaceContainerHighest",
                               "primaryContainer", "onPrimaryContainer",
                               "inverseOnSurface"})
        QVERIFY2(normal.value(role) != high.value(role), qPrintable(QString(role) + " changes at high contrast"));
      QVERIFY(normal.value("onPrimaryFixed") != high.value("onPrimaryFixed") ||
              normal.value("onPrimaryFixedVariant") != high.value("onPrimaryFixedVariant"));
      // MCU dynamic_color.ts:490-503 leaves an already sufficient tone alone.
      // Light inversePrimary at T80 can already clear 7:1 on T20.
      if (dark)
        QVERIFY(normal.value("inversePrimary") != high.value("inversePrimary"));
    }
  }

  // MCU Content uses the source tone for primaryContainer and a temperature
  // analogue for tertiary, as in dynamic_scheme.ts:663-670.
  void contentKeepsTheSourcesOwnTone() {
    const QColor source("#3f6ad8");
    const auto content = m3::scheme(source, false, m3::Variant::Content, 0);
    const auto primaryContainer = content.value("primaryContainer").value<QColor>();
    QVERIFY2(qAbs(m3::toneOf(primaryContainer) - m3::toneOf(source)) < 5,
             "Content starts primaryContainer at the source tone");
    const auto sourceHct = m3::measure(source);
    const auto tertiary = m3::palettesFor(source, m3::Variant::Content).tertiary;
    QVERIFY2(hueGap(tertiary.hue, sourceHct.hue + 60) > 5,
             "Content's temperature analogue differs from a fixed 60 degree turn");
    // MCU color_spec_2021.ts:429-449 searches down from light T90 when a
    // secondary palette cannot reach its requested chroma there.
    const auto secondaryPalette = m3::palettesFor(source, m3::Variant::Content).secondary;
    const auto secondaryBase = secondaryPalette.tone(90);
    const auto secondaryContainer = content.value("secondaryContainer").value<QColor>();
    QVERIFY2(m3::toneOf(secondaryContainer) < 90 &&
                 m3::measure(secondaryContainer).chroma > m3::measure(secondaryBase).chroma + 5,
             "Content moves the light secondary container toward its palette chroma");
  }

  // MCU dynamic_color.ts:392-479 moves the nearer container out of T50-59,
  // then keeps the accent at least ten tones farther from the surface.
  void contentPairsLeaveTheAwkwardZone() {
    const QColor source("#b75f38");
    QVERIFY(m3::toneOf(source) >= 50 && m3::toneOf(source) < 60);
    const auto dark = m3::scheme(source, true, m3::Variant::Content);
    const auto light = m3::scheme(source, false, m3::Variant::Content);
    const auto tone = [](const QVariantMap &roles, const char *name) {
      return m3::toneOf(roles.value(name).value<QColor>());
    };
    QVERIFY2(qAbs(tone(dark, "primaryContainer") - 60) < 1,
             "dark Content takes its source-toned container up to T60");
    QVERIFY2(qAbs(tone(light, "primaryContainer") - 49) < 1,
             "light Content takes its source-toned container down to T49");
    QVERIFY(tone(dark, "primary") - tone(dark, "primaryContainer") >= 9.5);
    QVERIFY(tone(light, "primaryContainer") - tone(light, "primary") >= 9.5);
  }

  // MCU color_spec_2021.ts:367-382, 452-467, 529-545 starts Content's
  // container ink at foregroundTone(container, 4.5) before its role curve.
  void contentContainerInksStartAtFourPointFive() {
    const auto roles = m3::scheme(QColor("#b75f38"), true, m3::Variant::Content);
    for (const char *family : {"Primary", "Secondary", "Tertiary"}) {
      const QString container = QString(family).toLower() + "Container";
      const QString ink = "on" + QString(family) + "Container";
      const auto front = roles.value(ink).value<QColor>();
      const auto back = roles.value(container).value<QColor>();
      const double ratio = contrast(front, back);
      QVERIFY2(ratio >= 4.35,
               qPrintable(QString("%1 on %2 is %3:1, below MCU's 4.5:1 floor")
                              .arg(ink, container).arg(ratio, 0, 'f', 2)));
    }
    for (const char *family : {"Primary", "Tertiary"}) {
      const auto ink = roles.value("on" + QString(family) + "Container").value<QColor>();
      const auto container = roles.value(QString(family).toLower() + "Container").value<QColor>();
      QVERIFY2(m3::toneOf(ink) < m3::toneOf(container),
               "a source-T50 Content container keeps MCU's darker foreground choice");
    }
    const double secondaryRatio = contrast(roles.value("onSecondaryContainer").value<QColor>(),
                                           roles.value("secondaryContainer").value<QColor>());
    QVERIFY2(secondaryRatio < 5.3,
             "the secondary Content ink begins near 4.5:1 rather than the fixed T90 ink");
  }

  // MCU color_spec_2021.ts:587-599 does not give error a Content branch.
  void contentDoesNotChangeErrorContainerInk() {
    const QColor source("#b75f38");
    const auto content = m3::scheme(source, false, m3::Variant::Content);
    const auto spot = m3::scheme(source, false, m3::Variant::TonalSpot);
    QCOMPARE(content.value("errorContainer").value<QColor>(),
             spot.value("errorContainer").value<QColor>());
    QCOMPARE(content.value("onErrorContainer").value<QColor>(),
             spot.value("onErrorContainer").value<QColor>());
  }

  // MCU dynamic_color.ts:392-479 makes Fixed nearer in light and FixedDim
  // nearer in dark. Both move together when either reaches T50-59.
  void fixedPairsUseTheirActualNearerRole() {
    const QColor source("#b75f38");
    const auto darkHigh = m3::scheme(source, true, m3::Variant::TonalSpot, 1);
    const auto darkFixed = m3::toneOf(darkHigh.value("primaryFixed").value<QColor>());
    const auto darkDim = m3::toneOf(darkHigh.value("primaryFixedDim").value<QColor>());
    // At the published dark-high surface T34, FixedDim T80 already clears
    // 4.5:1, so case 1 leaves both base tones alone.
    QVERIFY2(qAbs(darkDim-80) < 1 && qAbs(darkFixed-90) < 1 && darkFixed-darkDim >= 9.5,
             "dark FixedDim is nearer but needs no adjustment at this background");
    const auto lightQuarter = m3::scheme(source, false, m3::Variant::TonalSpot, 0.25);
    const auto lightFixed = m3::toneOf(lightQuarter.value("primaryFixed").value<QColor>());
    const auto lightDim = m3::toneOf(lightQuarter.value("primaryFixedDim").value<QColor>());
    QVERIFY2(qAbs(lightFixed-49) < 1 && lightDim <= 39.5,
             "light Fixed and FixedDim leave the awkward zone together");
  }

  // MCU color_spec_2021.ts:508-526 measures the HCT colour actually made at
  // the source tone before DislikeAnalyzer applies its rounded thresholds.
  void contentDislikeUsesAchievedChroma() {
    const QColor source("#8a685d");
    const auto tertiary = m3::palettesFor(source, m3::Variant::Content).tertiary;
    const auto achieved = m3::measure(tertiary.tone(m3::toneOf(source)));
    QVERIFY(tertiary.chroma > 16 && std::round(achieved.chroma) <= 16);
    const auto roles = m3::scheme(source, true, m3::Variant::Content);
    const double containerTone = m3::toneOf(roles.value("tertiaryContainer").value<QColor>());
    QVERIFY2(qAbs(containerTone - m3::toneOf(source)) < 1,
             "a non-disliked achieved chroma keeps the source container tone");
  }

  // Raising the level moves text and boundaries further from their surface.
  void contrastLevelsClimb() {
    const QColor source("#3f6ad8");
    for (bool dark : {false, true}) {
      double previous = 0;
      for (double level : {0.0, 0.5, 1.0}) {
        const auto roles = m3::scheme(source, dark, m3::Variant::TonalSpot, level);
        const double ratio = contrast(roles.value("onSurface").value<QColor>(),
                                      roles.value("surface").value<QColor>());
        QVERIFY2(ratio >= previous - 0.01, "a higher level is never weaker");
        previous = ratio;
      }
      // Material's high contrast target for body text.
      QVERIFY(previous >= 10);
    }
  }

  // Solving is on the theme's hot path; it has to stay cheap.
  void solvingIsFast() {
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < 200; ++i)
      m3::scheme(QColor::fromHsvF(double(i % 100) / 100.0, 0.7, 0.8), i % 2);
    QVERIFY2(timer.elapsed() < 2000,
             qPrintable(QString("200 schemes took %1ms").arg(timer.elapsed())));
  }
};
QTEST_GUILESS_MAIN(M3ColorTest)
#include "m3color_test.moc"
