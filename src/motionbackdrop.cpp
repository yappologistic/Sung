#include "motionbackdrop.h"
#include "softimage.h"
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGOpacityNode>
#include <QSGTexture>
#include <QtMath>
#include <memory>

namespace {
// The edge picture's long side. Blurred and enlarged, 128 samples across a
// 1600px window are 12.5px apart, well inside the blur's own spread.
constexpr int edgeSamples = 128;
// How far in from the view's border the frame is fully sharp, as a share of
// the view's shorter side.
constexpr qreal featherShare = 0.16;
// The blur's standard deviation in view pixels. Three box passes of radius r
// spread as a Gaussian with sigma sqrt(r(r+1)) samples.
constexpr qreal blurSigma = 48;
// The frame moves on every video frame; the edge follows every other one.
// At a 30fps cover that is 15 blurs a second, and the edge is too soft for
// the lag to show where it crosses into the sharp frame.
constexpr int edgeEvery = 2;

// The part of `frame` a cover fill of `view` shows, in the frame's pixels.
QRect visiblePart(const QSize &frame, const QSizeF &view) {
  if (frame.isEmpty() || view.isEmpty()) return {};
  const qreal scale = qMax(view.width() / frame.width(), view.height() / frame.height());
  const int width = qBound(1, qRound(view.width() / scale), frame.width());
  const int height = qBound(1, qRound(view.height() / scale), frame.height());
  return QRect((frame.width() - width) / 2, (frame.height() - height) / 2, width, height);
}

qreal smoothstep(qreal t) {
  t = qBound(0.0, t, 1.0);
  return t * t * (3 - 2 * t);
}

struct Edge {
  QImage image;
  QVector<scrimcontrast::Sample> top, bottom;
};

Edge buildEdge(const QImage &frame, const QSizeF &view, qreal topBand, qreal bottomBand) {
  Edge edge;
  const QRect part = visiblePart(frame.size(), view);
  if (part.isEmpty() || frame.depth() < 8) return edge;
  // A view onto the frame's own pixels: only the shrunk copy is new memory.
  const int bytes = frame.depth() / 8;
  const QImage window(frame.constBits() + part.y() * frame.bytesPerLine() + part.x() * bytes,
                      part.width(), part.height(), frame.bytesPerLine(), frame.format());
  const qreal longSide = qMax(view.width(), view.height());
  const QSize size(qMax(3, qRound(edgeSamples * view.width() / longSide)),
                   qMax(3, qRound(edgeSamples * view.height() / longSide)));
  const QImage small = window.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                           .convertToFormat(QImage::Format_ARGB32);
  if (small.isNull()) return edge;
  // The bands are sampled before the blur, which would otherwise average a
  // bright highlight into its dark surroundings and understate the scrim.
  const qreal rows = size.height() / view.height();
  if (topBand > 0)
    edge.top = scrimcontrast::sample(small, QRect(0, 0, size.width(), qMax(1, qCeil(topBand * rows))));
  if (bottomBand > 0) {
    const int height = qMax(1, qCeil(bottomBand * rows));
    edge.bottom = scrimcontrast::sample(small, QRect(0, size.height() - height, size.width(), height));
  }
  const qreal spacing = longSide / edgeSamples;
  const qreal sigma = blurSigma / spacing;
  const int radius = qMax(1, qRound((std::sqrt(4 * sigma * sigma + 1) - 1) / 2));
  QImage soft = softimage::softened(small, radius);
  if (soft.isNull()) return edge;
  soft = std::move(soft).convertToFormat(QImage::Format_ARGB32_Premultiplied);
  const qreal feather = featherShare * qMin(view.width(), view.height());
  const qreal across = view.width() / size.width(), down = view.height() / size.height();
  QVector<qreal> inX(size.width()), inY(size.height());
  for (int x = 0; x < size.width(); ++x)
    inX[x] = smoothstep(qMin(x + 0.5, size.width() - x - 0.5) * across / feather);
  for (int y = 0; y < size.height(); ++y)
    inY[y] = smoothstep(qMin(y + 0.5, size.height() - y - 0.5) * down / feather);
  for (int y = 0; y < size.height(); ++y) {
    auto *line = reinterpret_cast<QRgb *>(soft.scanLine(y));
    for (int x = 0; x < size.width(); ++x) {
      // Clear where both axes are inside the feather, opaque at any border,
      // so a corner fades along both edges at once.
      const qreal alpha = 1 - inX[x] * inY[y];
      const QRgb pixel = line[x];
      line[x] = qRgba(qRound(qRed(pixel) * alpha), qRound(qGreen(pixel) * alpha),
                      qRound(qBlue(pixel) * alpha), qRound(qAlpha(pixel) * alpha));
    }
  }
  edge.image = std::move(soft);
  return edge;
}

// One picture's two rectangles and the textures they own. The textures are
// released with the node, on the render thread, as the scene graph requires.
struct Layer : QSGNode {
  QSGImageNode *frame = nullptr, *edge = nullptr;
  std::unique_ptr<QSGTexture> frameTexture, edgeTexture;
  qint64 frameKey = 0, edgeKey = 0;
  void clear() {
    removeAllChildNodes();
    delete frame; delete edge;
    frame = edge = nullptr;
    frameTexture.reset(); edgeTexture.reset();
    frameKey = edgeKey = 0;
  }
  ~Layer() override { clear(); }
};

QSGImageNode *imageNode(QQuickWindow *window) {
  auto *node = window->createImageNode();
  node->setFiltering(QSGTexture::Linear);
  node->setOwnsTexture(false);
  return node;
}

void upload(QQuickWindow *window, QSGImageNode *node, std::unique_ptr<QSGTexture> &texture,
            qint64 &key, const QImage &image) {
  if (key == image.cacheKey() && texture) return;
  std::unique_ptr<QSGTexture> next(window->createTextureFromImage(image));
  node->setTexture(next.get());
  texture = std::move(next);
  key = image.cacheKey();
}

// A node joins the tree only once it has a texture and a rectangle: the
// software renderer reads both the moment a node is added.
void sync(QQuickWindow *window, Layer *layer, const QImage &frame, const QImage &edge, const QRectF &bounds) {
  if (frame.isNull()) { layer->clear(); return; }
  const bool newFrame = !layer->frame;
  if (newFrame) layer->frame = imageNode(window);
  upload(window, layer->frame, layer->frameTexture, layer->frameKey, frame);
  // Cropping in the texture instead of clipping the item keeps the node a
  // plain rectangle, which is what the software renderer draws fastest.
  layer->frame->setSourceRect(visiblePart(frame.size(), bounds.size()));
  layer->frame->setRect(bounds);
  if (newFrame) layer->prependChildNode(layer->frame);
  if (edge.isNull()) {
    if (layer->edge) {
      layer->removeChildNode(layer->edge);
      delete layer->edge; layer->edge = nullptr;
      layer->edgeTexture.reset(); layer->edgeKey = 0;
    }
    return;
  }
  const bool newEdge = !layer->edge;
  if (newEdge) layer->edge = imageNode(window);
  upload(window, layer->edge, layer->edgeTexture, layer->edgeKey, edge);
  layer->edge->setSourceRect(QRectF(QPointF(), edge.size()));
  layer->edge->setRect(bounds);
  if (newEdge) layer->appendChildNode(layer->edge);
}
} // namespace

MotionBackdrop::MotionBackdrop(QQuickItem *parent) : QQuickItem(parent) {
  setFlag(ItemHasContents);
}

void MotionBackdrop::setAnimation(MotionArtwork *animation) {
  if (m_animation == animation) return;
  if (m_animation) disconnect(m_animation, nullptr, this, nullptr);
  m_animation = animation;
  if (animation) connect(animation, &MotionArtwork::frameChanged, this, &MotionBackdrop::refresh);
  emit animationChanged();
  refresh();
}

void MotionBackdrop::setStill(RoundedArt *still) {
  if (m_still == still) return;
  if (m_still) disconnect(m_still, nullptr, this, nullptr);
  m_still = still;
  // readyChanged, not sourceChanged: a new source is announced while the old
  // picture is still decoded, and would be taken for the new one.
  if (still) connect(still, &RoundedArt::readyChanged, this, &MotionBackdrop::refresh);
  emit stillChanged();
  refresh();
}

void MotionBackdrop::setMix(qreal mix) {
  mix = qBound(0.0, mix, 1.0);
  if (qFuzzyCompare(m_mix, mix)) return;
  m_mix = mix;
  if (m_mix >= 1 && !m_previous.frame.isNull()) {
    m_previous = {};
    solveScrims();
  }
  emit mixChanged();
  update();
}

void MotionBackdrop::refresh() {
  Scene next;
  if (m_animation && !m_animation->frame().isNull()) {
    next.key = QStringLiteral("motion:") + m_animation->source().toString();
    next.moving = true;
    next.frame = m_animation->frame();
  } else if (m_still && !m_still->picture().isNull()) {
    next.key = QStringLiteral("still:") + m_still->source().toString();
    next.frame = m_still->picture();
  } else if (m_still && !m_still->source().isEmpty()) {
    // The new cover is still on its way. The old one stays until it lands
    // rather than dropping to an empty view in between.
    return;
  }
  if (next.key == m_current.key) {
    if (next.frame.cacheKey() == m_current.frame.cacheKey()) return;
    m_current.frame = next.frame;
    if (++m_frames % edgeEvery == 0) rebuildEdge();
    update();
    return;
  }
  if (!m_current.frame.isNull()) {
    m_previous = std::move(m_current);
    m_mix = 0;
    emit mixChanged();
  } else {
    m_previous = {};
  }
  m_current = std::move(next);
  m_frames = 0;
  rebuildEdge();
  emit sceneChanged();
  update();
}

void MotionBackdrop::rebuildEdge() {
  if (m_current.frame.isNull() || width() <= 0 || height() <= 0) {
    m_current.edge = {};
    m_current.top.clear(); m_current.bottom.clear();
    solveScrims();
    return;
  }
  auto edge = buildEdge(m_current.frame, size(), m_topBand, m_bottomBand);
  m_current.edge = std::move(edge.image);
  m_current.top = std::move(edge.top);
  m_current.bottom = std::move(edge.bottom);
  solveScrims();
  update();
}

void MotionBackdrop::scrimInputChanged() {
  // New inputs make the old worst case meaningless; start again from what
  // is on screen now.
  m_current.topNeed = m_current.bottomNeed = 0;
  m_previous.topNeed = m_previous.bottomNeed = 0;
  emit scrimInputsChanged();
  solveScrims();
}

void MotionBackdrop::solveScrims() {
  const auto solve = [this](const QVector<scrimcontrast::Sample> &samples) {
    return samples.isEmpty() ? m_base
                             : scrimcontrast::minimumScrim({&samples}, m_surface, m_ink, m_base, m_contrast);
  };
  m_current.topNeed = qMax(m_current.topNeed, solve(m_current.top));
  m_current.bottomNeed = qMax(m_current.bottomNeed, solve(m_current.bottom));
  // While the previous picture is still showing through, it counts too.
  const bool fading = !m_previous.frame.isNull();
  const qreal top = qMax(m_current.topNeed, fading ? m_previous.topNeed : 0.0);
  const qreal bottom = qMax(m_current.bottomNeed, fading ? m_previous.bottomNeed : 0.0);
  if (qFuzzyCompare(1 + top, 1 + m_topScrim) && qFuzzyCompare(1 + bottom, 1 + m_bottomScrim)) return;
  m_topScrim = top;
  m_bottomScrim = bottom;
  emit scrimChanged();
}

void MotionBackdrop::geometryChange(const QRectF &current, const QRectF &previous) {
  QQuickItem::geometryChange(current, previous);
  if (current.size() != previous.size()) rebuildEdge();
}

QSGNode *MotionBackdrop::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
  auto *window = this->window();
  if (!window || (m_current.frame.isNull() && m_previous.frame.isNull())) {
    delete old;
    return nullptr;
  }
  // The previous picture underneath at full strength, the current one over it
  // at `mix`: the crossfade never shows the view behind either of them. With
  // nothing to follow it, the previous picture fades out instead.
  QSGNode *root = old;
  QSGOpacityNode *leaving, *arriving;
  if (!root) {
    root = new QSGNode;
    leaving = new QSGOpacityNode;
    arriving = new QSGOpacityNode;
    leaving->appendChildNode(new Layer);
    arriving->appendChildNode(new Layer);
    root->appendChildNode(leaving);
    root->appendChildNode(arriving);
  } else {
    leaving = static_cast<QSGOpacityNode *>(root->firstChild());
    arriving = static_cast<QSGOpacityNode *>(leaving->nextSibling());
  }
  const QRectF bounds = boundingRect();
  const bool fading = !m_previous.frame.isNull();
  sync(window, static_cast<Layer *>(leaving->firstChild()), m_previous.frame, m_previous.edge, bounds);
  sync(window, static_cast<Layer *>(arriving->firstChild()), m_current.frame, m_current.edge, bounds);
  leaving->setOpacity(fading && m_current.frame.isNull() ? 1 - m_mix : 1.0);
  arriving->setOpacity(fading ? m_mix : 1.0);
  return root;
}
