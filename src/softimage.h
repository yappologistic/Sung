#pragma once
#include <QImage>

// A blur that needs no shader, so it works on the software renderer too.
// Covers and motion frames are blurred on the CPU at a small size and drawn
// enlarged, which is where the softness comes from.
namespace softimage {
// One separable box pass with a running sum: O(pixels) regardless of radius.
// Edges clamp, so a softened cover keeps its border color instead of fading out.
inline void boxPass(const QImage &source, QImage &target, int radius, bool horizontal) {
  const int width = source.width(), height = source.height();
  const int sourceStride = source.bytesPerLine() / 4, targetStride = target.bytesPerLine() / 4;
  const auto *read = reinterpret_cast<const QRgb *>(source.constBits());
  auto *write = reinterpret_cast<QRgb *>(target.bits());
  const int lines = horizontal ? height : width, count = horizontal ? width : height;
  const int step = horizontal ? 1 : sourceStride, lineStep = horizontal ? sourceStride : 1;
  const int writeStep = horizontal ? 1 : targetStride, writeLineStep = horizontal ? targetStride : 1;
  const int span = radius * 2 + 1;
  for (int line = 0; line < lines; ++line) {
    const QRgb *in = read + line * lineStep;
    QRgb *out = write + line * writeLineStep;
    int a = 0, r = 0, g = 0, b = 0;
    const auto at = [&](int i) { return in[qBound(0, i, count - 1) * step]; };
    for (int i = -radius; i <= radius; ++i) {
      const QRgb p = at(i);
      a += qAlpha(p); r += qRed(p); g += qGreen(p); b += qBlue(p);
    }
    for (int i = 0; i < count; ++i) {
      out[i * writeStep] = qRgba(r / span, g / span, b / span, a / span);
      const QRgb drop = at(i - radius), add = at(i + radius + 1);
      a += qAlpha(add) - qAlpha(drop); r += qRed(add) - qRed(drop);
      g += qGreen(add) - qGreen(drop); b += qBlue(add) - qBlue(drop);
    }
  }
}
// Three box passes approximate a Gaussian closely enough that enlarging the
// result shows a gradient rather than the seams between source pixels.
inline QImage softened(const QImage &source, int radius) {
  if (source.isNull() || radius <= 0)
    return {};
  QImage image = source.convertToFormat(QImage::Format_ARGB32);
  if (image.isNull() || image.width() < 3 || image.height() < 3)
    return {};
  radius = qBound(1, radius, qMin(image.width(), image.height()) / 3);
  QImage scratch(image.size(), QImage::Format_ARGB32);
  if (scratch.isNull())
    return {};
  for (int pass = 0; pass < 3; ++pass) {
    boxPass(image, scratch, radius, true);
    boxPass(scratch, image, radius, false);
  }
  return image;
}
} // namespace softimage
