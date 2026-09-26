#pragma once
#include <QColor>
#include <QImage>
#include <QRect>
#include <QVector>
#include <array>
#include <cmath>
#include <initializer_list>

// How much of a surface colour a scrim has to lay over a picture before ink
// on it keeps its contrast. MCU color_spec_2021.ts:241-248 measures
// onSurfaceVariant against a surface; a picture behind the scrim changes that
// surface, so the picture's own pixels decide the smallest scrim that meets
// the same target.
namespace scrimcontrast {
struct Sample { float red, green, blue, coverage; };

// A 16 by 16 grid over `region` (the whole image when empty), edges included.
// Pictures are softened before they are drawn behind text, so the blur has
// already removed the detail between grid points, and each later solve reads
// at most 256 samples however often palette motion asks.
inline QVector<Sample> sample(const QImage &image, QRect region = {}) {
  QVector<Sample> samples;
  if (image.isNull()) return samples;
  region = region.isEmpty() ? image.rect() : region.intersected(image.rect());
  if (region.isEmpty()) return samples;
  constexpr int side = 16;
  samples.reserve(side * side);
  for (int y = 0; y < side; ++y) {
    const int sy = region.top() + y * (region.height() - 1) / (side - 1);
    for (int x = 0; x < side; ++x) {
      const QRgb pixel = image.pixel(region.left() + x * (region.width() - 1) / (side - 1), sy);
      samples.append({float(qRed(pixel) / 255.0), float(qGreen(pixel) / 255.0),
                      float(qBlue(pixel) / 255.0), float(qAlpha(pixel) / 255.0)});
    }
  }
  return samples;
}

inline const std::array<double, 256> &linearChannels() {
  static const auto values = [] {
    std::array<double, 256> table{};
    for (int i = 0; i < 256; ++i) {
      const double value = i / 255.0;
      table[i] = value <= 0.04045 ? value / 12.92
                                  : std::pow((value + 0.055) / 1.055, 2.4);
    }
    return table;
  }();
  return values;
}
inline double channelLinear(double value) {
  // The palette animates through fractional sRGB values. Interpolate between
  // 8-bit table entries so a changing role does not invoke pow per pixel.
  const double entry = qBound(0.0, value * 255.0, 255.0);
  const int index = int(entry);
  const double fraction = entry - index;
  const auto &table = linearChannels();
  return index == 255 ? table[255]
                      : table[index] + (table[index + 1] - table[index]) * fraction;
}
inline double luminance(double red, double green, double blue) {
  return 0.2126 * channelLinear(red) + 0.7152 * channelLinear(green)
         + 0.0722 * channelLinear(blue);
}

using Sets = std::initializer_list<const QVector<Sample> *>;

// The lowest contrast ink reaches anywhere in the sampled pictures once a
// scrim of `alpha` lies over them. With nothing sampled, the bare surface.
inline qreal minimumContrast(Sets sets, const QColor &surface, const QColor &ink, qreal alpha) {
  const double sr = surface.redF(), sg = surface.greenF(), sb = surface.blueF();
  const double inkL = luminance(ink.redF(), ink.greenF(), ink.blueF());
  double minimum = 100;
  for (const auto *samples : sets) {
    for (const auto &pixel : *samples) {
      const double reveal = (1 - alpha) * pixel.coverage;
      const double washL = luminance(sr + reveal * (pixel.red - sr),
                                     sg + reveal * (pixel.green - sg),
                                     sb + reveal * (pixel.blue - sb));
      const double ratio = (qMax(washL, inkL) + 0.05) / (qMin(washL, inkL) + 0.05);
      minimum = qMin(minimum, ratio);
    }
  }
  return minimum == 100 ? (qMax(luminance(sr, sg, sb), inkL) + 0.05) /
                              (qMin(luminance(sr, sg, sb), inkL) + 0.05) : minimum;
}

// The smallest scrim at or above `base` that holds `target` everywhere.
// WCAG 1.4.3 asks 4.5:1 of body text; callers pass the contrast level's own.
inline qreal minimumScrim(Sets sets, const QColor &surface, const QColor &ink,
                          qreal base, qreal target) {
  base = qBound(0.0, base, 1.0);
  if (minimumContrast(sets, surface, ink, base) >= target) return base;
  qreal low = base, high = 1;
  for (int i = 0; i < 12; ++i) {
    const qreal mid = (low + high) / 2;
    if (minimumContrast(sets, surface, ink, mid) >= target) high = mid;
    else low = mid;
  }
  return high;
}
} // namespace scrimcontrast
