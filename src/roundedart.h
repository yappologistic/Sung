#pragma once
#include <QFutureWatcher>
#include <QImage>
#include <QNetworkReply>
#include <QPointer>
#include <QQuickPaintedItem>
#include <functional>
#include <QVariantAnimation>
#include <memory>
#include "motionartwork.h"

class RoundedArt : public QQuickPaintedItem {
  Q_OBJECT
  friend class ArtworkTest;
  Q_PROPERTY(MotionArtwork *animation READ animation WRITE setAnimation NOTIFY animationChanged)
  Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
  Q_PROPERTY(qreal radius READ radius WRITE setRadius NOTIFY radiusChanged)
  // A named shape from Material's library to mask with, instead of the corner
  // radius. Empty, or a name this build does not know, keeps the rounded rect.
  Q_PROPERTY(QString shape READ shape WRITE setShape NOTIFY shapeChanged)
  Q_PROPERTY(int pixels READ pixels WRITE setPixels NOTIFY pixelsChanged)
  Q_PROPERTY(int blur READ blur WRITE setBlur NOTIFY blurChanged)
  Q_PROPERTY(bool crossfade READ crossfade WRITE setCrossfade NOTIFY crossfadeChanged)
  Q_PROPERTY(bool transitioning READ transitioning NOTIFY transitionChanged)
  Q_PROPERTY(bool fit READ fit WRITE setFit NOTIFY fitChanged)
  Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
public:
  explicit RoundedArt(QQuickItem *p = nullptr);
  ~RoundedArt() override;
  QUrl source() const { return m_source; }
  void setSource(const QUrl &);
  qreal radius() const { return m_radius; }
  void setRadius(qreal r) {
    if (m_radius == r)
      return;
    m_radius = r;
    // A rounded edge is masked into the render target and needs the item's
    // own resolution, so gaining or losing one changes what the target may be.
    fitTextureSize();
    emit radiusChanged();
    update();
  }
  QString shape() const { return m_shape; }
  void setShape(const QString &s) {
    if (m_shape == s)
      return;
    m_shape = s;
    fitTextureSize();
    emit shapeChanged();
    update();
  }
  bool crossfade() const {return m_crossfade;}
  void setCrossfade(bool value);
  bool transitioning() const {return !m_previous.isNull();}
  bool fit() const {return m_fit;}
  void setFit(bool value){if(m_fit==value)return;m_fit=value;emit fitChanged();update();}
  int pixels() const { return m_pixels; }
  void setPixels(int p) {
    p = qBound(48, p, 1600);
    if (p == m_pixels)
      return;
    m_pixels = p;
    emit pixelsChanged();
    reload(true);
  }
  int blur() const { return m_blur; }
  // Softens the decoded cover once, so enlarging it stays smooth instead of
  // showing the interpolation between a handful of source pixels.
  void setBlur(int radius);
  MotionArtwork *animation() const { return m_animation; }
  void setAnimation(MotionArtwork *animation);
  bool ready() const { return (m_animation && !m_animation->frame().isNull()) || !m_image.isNull() || !m_previous.isNull(); }
  void paint(QPainter *) override;
  Q_INVOKABLE QColor seedColor() const;
  static void clearCaches();
  static std::function<QNetworkRequest(const QUrl &)> resolveServerArt;
  // What to draw for one of YouTube's video frames: the album cover the
  // backend has matched to it, or an empty URL to draw the frame itself.
  static std::function<QUrl(const QUrl &)> resolveVideoFrame;
  // After what a frame resolves to has changed, every live surface drawing one
  // fetches again, with a crossfade where the surface has one.
  static void refreshFrames();
signals:
  void animationChanged();
  void sourceChanged();
  void radiusChanged();
  void shapeChanged();
  void pixelsChanged();
  void blurChanged();
  void readyChanged();
  void fitChanged();
  void crossfadeChanged();
  void transitionChanged();

private:
  void reload(bool preserve=false);
  void refresh();
  void imageReady();
  void soften();
  // Keeps the render target no larger than the picture it carries.
  void fitTextureSize();
  void geometryChange(const QRectF &, const QRectF &) override;
  const QImage &shown() const;
  void finishTransition();
  bool m_crossfade=false,m_previousFit=false;
  qreal m_mix=1;
  QImage m_previous;
  QImage m_softImage,m_softPrevious;
  int m_blur=0;
  std::unique_ptr<QVariantAnimation> m_fade;
  bool m_fit=false,m_originalSizeFallback=false;
  QUrl m_source;
  QImage m_image;
  QPointer<MotionArtwork> m_animation;
  QPointer<QNetworkReply> m_reply;
  // Identifies the newest decode asked for. A result carrying anything else
  // belongs to a cover this surface has already moved on from.
  quint64 m_decode=0;
  QString m_shape;
  qreal m_radius = 16;
  int m_pixels = 360;
};
