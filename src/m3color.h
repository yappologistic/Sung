#pragma once
// Material 3 dynamic color.
//
// A cover supplies one source color; Material derives the whole interface from
// it. The path is the one the Material 3 guidelines describe: measure the source
// in HCT (CAM16 hue and chroma over CIE L* tone), spread five tonal palettes
// around it, then resolve each role's tone against its backgrounds and
// contrast curve.
//
// Tone is L*, so the tone numbers the guidelines quote are what carry the
// contrast guarantees. This solver reproduces the requested tone and hue
// exactly and approaches the requested chroma from above, stepping down until
// the color fits inside sRGB. Material's own solver walks the gamut boundary
// analytically to land on the last representable chroma; stepping finds the
// same tone and hue with chroma within a fraction of a unit, which no eye and
// no contrast check can separate.
#include <QColor>
#include <QHash>
#include <QStringList>
#include <QVariantMap>

namespace m3 {

// A color measured in HCT. Hue is degrees, chroma is unbounded in principle and
// reaches about 120 inside sRGB, tone is CIE L* from 0 to 100.
struct Hct {
  double hue = 0;
  double chroma = 0;
  double tone = 0;
};

Hct measure(const QColor &color);
// The sRGB color closest to this hue and chroma at exactly this tone.
QColor solve(double hue, double chroma, double tone);

// One Material tonal palette: a fixed hue and chroma read at any tone.
struct TonalPalette {
  double hue = 0;
  double chroma = 0;
  QColor tone(double value) const { return solve(hue, chroma, value); }
};

// The palettes a scheme is built from: five spread around the source color,
// and one that does not follow it.
struct Palettes {
  TonalPalette primary, secondary, tertiary, neutral, neutralVariant;
  // Material's sixth palette. Error is the one part of a scheme that does not
  // follow the source colour: it sits at a fixed hue and chroma so a warning
  // reads as a warning whatever the artwork happens to be.
  TonalPalette error{25.0, 84.0};
};

// Material's scheme variants. Each one is a different answer to how much of the
// source color the interface should take: Neutral barely tints, Tonal spot is
// the default, Vibrant maxes the chroma out, Expressive turns the hue right
// around on purpose, and Content keeps the source's own chroma.
enum class Variant { Neutral, TonalSpot, Vibrant, Expressive, Content };
Variant variantFor(const QString &name);
QStringList variantNames();

Palettes palettesFor(const QColor &source, Variant variant = Variant::TonalSpot);

// Every color role the interface uses, keyed by its Material name.
//
// `contrast` runs from 0 for Material's standard tones to 1 for its high
// contrast ones, passing through 0.5 for medium. Surface containers, accents,
// fixed roles and text each follow their own curve.
QVariantMap scheme(const QColor &source, bool dark, Variant variant = Variant::TonalSpot,
                   double contrast = 0);

// L* of a color, on the same 0-100 scale as tone.
double toneOf(const QColor &color);

// WCAG contrast between two colors, which is the measure Material's own
// accessible-colour guidance is written against and the one Sung solves its
// accent against. 4.5:1 is the floor for body text.
double contrastRatio(const QColor &a, const QColor &b);

} // namespace m3
