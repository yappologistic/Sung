#include "m3color.h"
#include <QtGlobal>
#include <cmath>

namespace m3 {
namespace {

constexpr double kPi = 3.14159265358979323846;

double sanitizeDegrees(double degrees) {
  degrees = std::fmod(degrees, 360.0);
  return degrees < 0 ? degrees + 360.0 : degrees;
}

// sRGB transfer function, over components scaled to 0-100.
double linearized(double component) {
  const double v = component;
  return 100.0 * (v <= 0.040449936 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4));
}
double delinearized(double component) {
  const double v = qBound(0.0, component / 100.0, 1.0);
  return v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

double labF(double t) {
  constexpr double e = 216.0 / 24389.0;
  return t > e ? std::cbrt(t) : (24389.0 / 27.0 * t + 16.0) / 116.0;
}
double labInverseF(double ft) {
  constexpr double e = 216.0 / 24389.0;
  const double cubed = ft * ft * ft;
  return cubed > e ? cubed : (116.0 * ft - 16.0) / (24389.0 / 27.0);
}
double yFromTone(double tone) { return 100.0 * labInverseF((tone + 16.0) / 116.0); }
double toneFromY(double y) { return 116.0 * labF(y / 100.0) - 16.0; }

QColor fromLinearRgb(double r, double g, double b) {
  return QColor::fromRgbF(qBound(0.0, delinearized(r), 1.0), qBound(0.0, delinearized(g), 1.0),
                          qBound(0.0, delinearized(b), 1.0));
}

// CAM16 viewing conditions, fixed to the ones Material measures colors under:
// a D65 white point, an L* 50 background and an average surround.
struct ViewingConditions {
  double aw, nbb, ncb, c, nc, n, fl, flRoot, z;
  double rgbD[3];
};

const ViewingConditions &conditions() {
  static const ViewingConditions vc = [] {
    const double whiteX = 95.047, whiteY = 100.0, whiteZ = 108.883;
    const double rW = 0.401288 * whiteX + 0.650173 * whiteY - 0.051461 * whiteZ;
    const double gW = -0.250268 * whiteX + 1.204414 * whiteY + 0.045854 * whiteZ;
    const double bW = -0.002079 * whiteX + 0.048952 * whiteY + 0.953127 * whiteZ;
    const double surround = 2.0;
    const double f = 0.8 + surround / 10.0;
    const double c = f >= 0.9 ? 0.59 + (0.69 - 0.59) * ((f - 0.9) * 10.0)
                              : 0.525 + (0.59 - 0.525) * ((f - 0.8) * 10.0);
    const double adaptingLuminance = (200.0 / kPi) * yFromTone(50.0) / 100.0;
    double d = f * (1.0 - (1.0 / 3.6) * std::exp((-adaptingLuminance - 42.0) / 92.0));
    d = qBound(0.0, d, 1.0);
    const double k = 1.0 / (5.0 * adaptingLuminance + 1.0);
    const double k4 = k * k * k * k;
    const double k4F = 1.0 - k4;
    const double fl = k4 * adaptingLuminance + 0.1 * k4F * k4F * std::cbrt(5.0 * adaptingLuminance);
    const double n = yFromTone(50.0) / whiteY;
    const double z = 1.48 + std::sqrt(n);
    const double nbb = 0.725 / std::pow(n, 0.2);
    ViewingConditions out{};
    out.rgbD[0] = d * (100.0 / rW) + 1.0 - d;
    out.rgbD[1] = d * (100.0 / gW) + 1.0 - d;
    out.rgbD[2] = d * (100.0 / bW) + 1.0 - d;
    const double factors[3] = {std::pow(fl * out.rgbD[0] * rW / 100.0, 0.42),
                               std::pow(fl * out.rgbD[1] * gW / 100.0, 0.42),
                               std::pow(fl * out.rgbD[2] * bW / 100.0, 0.42)};
    const double rgbA[3] = {400.0 * factors[0] / (factors[0] + 27.13),
                            400.0 * factors[1] / (factors[1] + 27.13),
                            400.0 * factors[2] / (factors[2] + 27.13)};
    out.aw = (2.0 * rgbA[0] + rgbA[1] + 0.05 * rgbA[2]) * nbb;
    out.nbb = nbb;
    out.ncb = nbb;
    out.c = c;
    out.nc = f;
    out.n = n;
    out.fl = fl;
    out.flRoot = std::pow(fl, 0.25);
    out.z = z;
    return out;
  }();
  return vc;
}

double adapt(double component) {
  const double af = std::pow(conditions().fl * std::abs(component) / 100.0, 0.42);
  return std::copysign(400.0 * af / (af + 27.13), component);
}
double unadapt(double adapted) {
  const double magnitude = std::abs(adapted);
  const double base = std::max(0.0, 27.13 * magnitude / (400.0 - magnitude));
  return std::copysign(std::pow(base, 1.0 / 0.42), adapted);
}

// The colour whose CAM16 hue and chroma are the ones asked for and whose tone
// lands on the requested Y, or an invalid colour when sRGB cannot hold it.
QColor resultByJ(double hueRadians, double chroma, double y) {
  const auto &vc = conditions();
  double j = std::sqrt(y) * 11.0;
  const double tInnerCoeff = 1.0 / std::pow(1.64 - std::pow(0.29, vc.n), 0.73);
  const double eHue = 0.25 * (std::cos(hueRadians + 2.0) + 3.8);
  const double p1 = eHue * (50000.0 / 13.0) * vc.nc * vc.ncb;
  const double hSin = std::sin(hueRadians), hCos = std::cos(hueRadians);
  for (int round = 0; round < 5; ++round) {
    const double jNormalized = j / 100.0;
    const double alpha = chroma == 0.0 || j == 0.0 ? 0.0 : chroma / std::sqrt(jNormalized);
    const double t = std::pow(alpha * tInnerCoeff, 1.0 / 0.9);
    const double ac = vc.aw * std::pow(jNormalized, 1.0 / vc.c / vc.z);
    const double p2 = ac / vc.nbb;
    const double gamma =
        23.0 * (p2 + 0.305) * t / (23.0 * p1 + 11.0 * t * hCos + 108.0 * t * hSin);
    const double a = gamma * hCos, b = gamma * hSin;
    const double rA = (460.0 * p2 + 451.0 * a + 288.0 * b) / 1403.0;
    const double gA = (460.0 * p2 - 891.0 * a - 261.0 * b) / 1403.0;
    const double bA = (460.0 * p2 - 220.0 * a - 6300.0 * b) / 1403.0;
    const double rS = unadapt(rA), gS = unadapt(gA), bS = unadapt(bA);
    // Undiscounted CAM16 response back to linear sRGB, in one step.
    const double red = 1373.2198709594231 * rS - 1100.4251190754821 * gS - 7.278681089101213 * bS;
    const double green = -271.815969077903 * rS + 559.6580465940733 * gS - 32.46047482791194 * bS;
    const double blue = 1.9622899599665666 * rS - 57.173814538844006 * gS + 308.7233197812385 * bS;
    if (red < 0 || green < 0 || blue < 0)
      return {};
    const double fnj = 0.2126 * red + 0.7152 * green + 0.0722 * blue;
    if (fnj <= 0)
      return {};
    if (round == 4 || std::abs(fnj - y) < 0.002) {
      if (red > 100.01 || green > 100.01 || blue > 100.01)
        return {};
      return fromLinearRgb(red, green, blue);
    }
    j = j - (fnj - y) * j / (2.0 * fnj);
  }
  return {};
}

} // namespace

double toneOf(const QColor &color) {
  const double y = 0.2126 * linearized(color.redF()) + 0.7152 * linearized(color.greenF()) +
                   0.0722 * linearized(color.blueF());
  return toneFromY(y);
}

Hct measure(const QColor &color) {
  const auto &vc = conditions();
  const double r = linearized(color.redF()), g = linearized(color.greenF()),
               b = linearized(color.blueF());
  const double x = 0.41233895 * r + 0.35762064 * g + 0.18051042 * b;
  const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
  const double z = 0.01932141 * r + 0.11916382 * g + 0.95034478 * b;
  const double rC = 0.401288 * x + 0.650173 * y - 0.051461 * z;
  const double gC = -0.250268 * x + 1.204414 * y + 0.045854 * z;
  const double bC = -0.002079 * x + 0.048952 * y + 0.953127 * z;
  const double rA = adapt(vc.rgbD[0] * rC), gA = adapt(vc.rgbD[1] * gC),
               bA = adapt(vc.rgbD[2] * bC);
  const double a = (11.0 * rA - 12.0 * gA + bA) / 11.0;
  const double bb = (rA + gA - 2.0 * bA) / 9.0;
  const double u = (20.0 * rA + 20.0 * gA + 21.0 * bA) / 20.0;
  const double p2 = (40.0 * rA + 20.0 * gA + bA) / 20.0;
  const double hue = sanitizeDegrees(std::atan2(bb, a) * 180.0 / kPi);
  const double j = 100.0 * std::pow(p2 * vc.nbb / vc.aw, vc.c * vc.z);
  const double huePrime = hue < 20.14 ? hue + 360.0 : hue;
  const double eHue = 0.25 * (std::cos(huePrime * kPi / 180.0 + 2.0) + 3.8);
  const double t = eHue * (50000.0 / 13.0) * vc.nc * vc.ncb * std::sqrt(a * a + bb * bb) /
                   (u + 0.305);
  const double alpha = std::pow(t, 0.9) * std::pow(1.64 - std::pow(0.29, vc.n), 0.73);
  return {hue, alpha * std::sqrt(j / 100.0), toneFromY(y)};
}

QColor solve(double hue, double chroma, double tone) {
  tone = qBound(0.0, tone, 100.0);
  const double y = yFromTone(tone);
  const double grey = qBound(0.0, delinearized(y), 1.0);
  if (chroma < 0.0001 || tone < 0.0001 || tone > 99.9999)
    return QColor::fromRgbF(grey, grey, grey);
  const double hueRadians = sanitizeDegrees(hue) * kPi / 180.0;
  // Approach the requested chroma from above; the first value sRGB can hold at
  // this tone and hue is the answer.
  for (double c = chroma; c > 0; c -= 0.25)
    if (const auto found = resultByJ(hueRadians, c, y); found.isValid())
      return found;
  return QColor::fromRgbF(grey, grey, grey);
}

namespace {
// Material turns the secondary and tertiary hues by an amount that depends on
// where the source sits on the wheel, so a scheme stays balanced whatever it is
// given. The breaks and the rotations are Material's own tables.
double rotatedHue(double hue, const QList<double> &breaks, const QList<double> &rotations) {
  for (int i = 0; i < breaks.size() - 1; ++i)
    if (breaks[i] <= hue && hue < breaks[i + 1])
      return sanitizeDegrees(hue + rotations[i]);
  return hue;
}
const QList<double> kVibrantBreaks{0, 41, 61, 101, 131, 181, 251, 301, 360};
const QList<double> kExpressiveBreaks{0, 21, 51, 121, 151, 191, 271, 321, 360};
} // namespace

Variant variantFor(const QString &name) {
  if (name == "neutral") return Variant::Neutral;
  if (name == "vibrant") return Variant::Vibrant;
  if (name == "expressive") return Variant::Expressive;
  if (name == "content") return Variant::Content;
  return Variant::TonalSpot;
}

QStringList variantNames() { return {"neutral", "tonalSpot", "vibrant", "expressive", "content"}; }

Palettes palettesFor(const QColor &source, Variant variant) {
  // Every variant is the same five palettes spread differently. The neutral
  // ones carry a trace of the source hue, which is what tints the surfaces
  // without colouring them.
  const auto hct = measure(source);
  const double hue = hct.hue, chroma = hct.chroma;
  switch (variant) {
  case Variant::Neutral:
    return {TonalPalette{hue, 12.0}, TonalPalette{hue, 8.0}, TonalPalette{hue, 16.0},
            TonalPalette{hue, 2.0}, TonalPalette{hue, 2.0}};
  case Variant::Vibrant:
    return {TonalPalette{hue, 200.0},
            TonalPalette{rotatedHue(hue, kVibrantBreaks, {18, 15, 10, 12, 15, 18, 15, 12, 12}), 24.0},
            TonalPalette{rotatedHue(hue, kVibrantBreaks, {35, 30, 20, 25, 30, 35, 30, 25, 25}), 32.0},
            TonalPalette{hue, 10.0}, TonalPalette{hue, 12.0}};
  case Variant::Expressive:
    return {TonalPalette{sanitizeDegrees(hue + 240.0), 40.0},
            TonalPalette{rotatedHue(hue, kExpressiveBreaks, {45, 95, 45, 20, 45, 90, 45, 45, 45}), 24.0},
            TonalPalette{rotatedHue(hue, kExpressiveBreaks, {120, 120, 20, 45, 20, 15, 20, 120, 120}), 32.0},
            TonalPalette{sanitizeDegrees(hue + 15.0), 8.0},
            TonalPalette{sanitizeDegrees(hue + 15.0), 12.0}};
  case Variant::Content:
    // Material picks Content's tertiary by walking the colour wheel for an
    // analogous hue and correcting it if it lands somewhere disliked. Without
    // that machinery this takes the same step round the wheel the default
    // scheme takes, at the source's own chroma.
    return {TonalPalette{hue, chroma},
            TonalPalette{hue, std::max(chroma - 32.0, chroma * 0.5)},
            TonalPalette{sanitizeDegrees(hue + 60.0), chroma},
            TonalPalette{hue, chroma / 8.0},
            TonalPalette{hue, chroma / 8.0 + 4.0}};
  case Variant::TonalSpot:
    break;
  }
  return {TonalPalette{hue, 36.0},
          TonalPalette{hue, 16.0},
          TonalPalette{sanitizeDegrees(hue + 60.0), 24.0},
          TonalPalette{hue, 6.0},
          TonalPalette{hue, 8.0}};
}

double contrastRatio(const QColor &a, const QColor &b) {
  const double x = 0.2126 * linearized(a.redF()) + 0.7152 * linearized(a.greenF()) +
                   0.0722 * linearized(a.blueF());
  const double y = 0.2126 * linearized(b.redF()) + 0.7152 * linearized(b.greenF()) +
                   0.0722 * linearized(b.blueF());
  return (std::max(x, y) / 100.0 + 0.05) / (std::min(x, y) / 100.0 + 0.05);
}

namespace {
// Material shifts a role's tone away from its backgrounds when a scheme asks
// for more contrast than the standard tones carry. Sung sets text on its accent
// colour, so primary holds the 4.5:1 text floor against every surface it can
// land on rather than the 3:1 floor a purely decorative accent would need.
double toneMeeting(const TonalPalette &palette, double start, bool lighten,
                   const QList<QColor> &backgrounds, double target) {
  for (double tone = start; lighten ? tone <= 100 : tone >= 0; tone += lighten ? 1 : -1) {
    const auto candidate = palette.tone(tone);
    bool clears = true;
    for (const auto &background : backgrounds)
      if (contrastRatio(candidate, background) < target)
        clears = false;
    if (clears)
      return tone;
  }
  return lighten ? 100 : 0;
}
} // namespace

namespace {
// Material gives every role that carries text or a boundary four target
// contrast ratios, one for each contrast level, and interpolates between them.
struct ContrastCurve {
  double low, normal, medium, high;
  double at(double level) const {
    if (level <= -1) return low;
    if (level < 0) return low + (normal - low) * (level + 1);
    if (level < 0.5) return normal + (medium - normal) * (level / 0.5);
    if (level < 1) return medium + (high - medium) * ((level - 0.5) / 0.5);
    return high;
  }
};
// The curves Material publishes for the roles this scheme hands out.
constexpr ContrastCurve kOnSurface{4.5, 7, 11, 21};
constexpr ContrastCurve kOnSurfaceVariant{3, 4.5, 7, 11};
constexpr ContrastCurve kPrimary{3, 4.5, 7, 7};
constexpr ContrastCurve kOutline{1.5, 3, 4.5, 7};
constexpr ContrastCurve kOutlineVariant{1, 1, 3, 4.5};
} // namespace

QVariantMap scheme(const QColor &source, bool dark, Variant variant, double contrast) {
  const auto p = palettesFor(source, variant);
  QVariantMap roles;
  const auto put = [&roles](const char *name, const QColor &color) { roles.insert(name, color); };
  // The surfaces primary can be drawn on, so its tone can be checked against
  // all of them before any of them are published.
  const QList<QColor> surfaces =
      dark ? QList<QColor>{p.neutral.tone(6), p.neutral.tone(10), p.neutral.tone(12),
                           p.neutral.tone(17), p.neutral.tone(22)}
           : QList<QColor>{p.neutral.tone(98), p.neutral.tone(96), p.neutral.tone(94),
                           p.neutral.tone(92), p.neutral.tone(90)};
  contrast = qBound(0.0, contrast, 1.0);
  // Text and boundaries move to meet the ratio their curve asks for at this
  // level; the containers they sit on stay where Material puts them.
  const auto surfaceTone = dark ? p.neutral.tone(6) : p.neutral.tone(98);
  const double primaryTone = toneMeeting(p.primary, dark ? 80 : 40, dark, surfaces, kPrimary.at(contrast));
  const double onSurfaceTone =
      toneMeeting(p.neutral, dark ? 90 : 10, dark, {surfaceTone}, kOnSurface.at(contrast));
  const double onSurfaceVariantTone =
      toneMeeting(p.neutralVariant, dark ? 80 : 30, dark, {surfaceTone}, kOnSurfaceVariant.at(contrast));
  const double outlineTone =
      toneMeeting(p.neutralVariant, dark ? 60 : 50, dark, {surfaceTone}, kOutline.at(contrast));
  const double outlineVariantTone =
      toneMeeting(p.neutralVariant, dark ? 30 : 80, dark, {surfaceTone}, kOutlineVariant.at(contrast));
  // Error is drawn on the same surfaces as primary and answers the same
  // contrast curve, so it tightens with the rest of the scheme rather than
  // staying where a fixed colour would leave it.
  const double errorTone = toneMeeting(p.error, dark ? 80 : 40, dark, surfaces, kPrimary.at(contrast));
  if (dark) {
    put("primary", p.primary.tone(primaryTone));
    put("onPrimary", p.primary.tone(20));
    put("primaryContainer", p.primary.tone(30));
    put("onPrimaryContainer", p.primary.tone(90));
    put("secondary", p.secondary.tone(80));
    put("onSecondary", p.secondary.tone(20));
    put("secondaryContainer", p.secondary.tone(30));
    put("onSecondaryContainer", p.secondary.tone(90));
    put("tertiary", p.tertiary.tone(80));
    put("onTertiary", p.tertiary.tone(20));
    put("tertiaryContainer", p.tertiary.tone(30));
    put("onTertiaryContainer", p.tertiary.tone(90));
    put("background", p.neutral.tone(6));
    put("surface", p.neutral.tone(6));
    put("surfaceDim", p.neutral.tone(6));
    put("surfaceBright", p.neutral.tone(24));
    put("surfaceContainerLowest", p.neutral.tone(4));
    put("surfaceContainerLow", p.neutral.tone(10));
    put("surfaceContainer", p.neutral.tone(12));
    put("surfaceContainerHigh", p.neutral.tone(17));
    put("surfaceContainerHighest", p.neutral.tone(22));
    put("onSurface", p.neutral.tone(onSurfaceTone));
    put("onSurfaceVariant", p.neutralVariant.tone(onSurfaceVariantTone));
    put("outline", p.neutralVariant.tone(outlineTone));
    put("outlineVariant", p.neutralVariant.tone(outlineVariantTone));
    put("scrim", p.neutral.tone(0));
    put("inverseSurface", p.neutral.tone(90));
    put("inverseOnSurface", p.neutral.tone(20));
    put("inversePrimary", p.primary.tone(40));
    put("error", p.error.tone(errorTone));
    put("onError", p.error.tone(20));
    put("errorContainer", p.error.tone(30));
    put("onErrorContainer", p.error.tone(90));
  } else {
    put("primary", p.primary.tone(primaryTone));
    put("onPrimary", p.primary.tone(100));
    put("primaryContainer", p.primary.tone(90));
    put("onPrimaryContainer", p.primary.tone(10));
    put("secondary", p.secondary.tone(40));
    put("onSecondary", p.secondary.tone(100));
    put("secondaryContainer", p.secondary.tone(90));
    put("onSecondaryContainer", p.secondary.tone(10));
    put("tertiary", p.tertiary.tone(40));
    put("onTertiary", p.tertiary.tone(100));
    put("tertiaryContainer", p.tertiary.tone(90));
    put("onTertiaryContainer", p.tertiary.tone(10));
    put("background", p.neutral.tone(98));
    put("surface", p.neutral.tone(98));
    put("surfaceDim", p.neutral.tone(87));
    put("surfaceBright", p.neutral.tone(98));
    put("surfaceContainerLowest", p.neutral.tone(100));
    put("surfaceContainerLow", p.neutral.tone(96));
    put("surfaceContainer", p.neutral.tone(94));
    put("surfaceContainerHigh", p.neutral.tone(92));
    put("surfaceContainerHighest", p.neutral.tone(90));
    put("onSurface", p.neutral.tone(onSurfaceTone));
    put("onSurfaceVariant", p.neutralVariant.tone(onSurfaceVariantTone));
    put("outline", p.neutralVariant.tone(outlineTone));
    put("outlineVariant", p.neutralVariant.tone(outlineVariantTone));
    put("scrim", p.neutral.tone(0));
    put("inverseSurface", p.neutral.tone(20));
    put("inverseOnSurface", p.neutral.tone(95));
    put("inversePrimary", p.primary.tone(80));
    put("error", p.error.tone(errorTone));
    put("onError", p.error.tone(100));
    put("errorContainer", p.error.tone(90));
    put("onErrorContainer", p.error.tone(10));
  }
  // The fixed accents. Every other role flips its tone between light and dark;
  // these hold the same tone in both, so anything painted with them keeps its
  // identity when the theme changes underneath it.
  const auto putFixed = [&roles](const QString &accent, const TonalPalette &palette) {
    const QString capital = accent.at(0).toUpper() + accent.mid(1);
    roles.insert(accent + "Fixed", palette.tone(90));
    roles.insert(accent + "FixedDim", palette.tone(80));
    roles.insert("on" + capital + "Fixed", palette.tone(10));
    roles.insert("on" + capital + "FixedVariant", palette.tone(30));
  };
  putFixed("primary", p.primary);
  putFixed("secondary", p.secondary);
  putFixed("tertiary", p.tertiary);
  return roles;
}

} // namespace m3
