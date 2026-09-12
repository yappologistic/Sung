#pragma once
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
  Q_PROPERTY(int pixels READ pixels WRITE setPixels NOTIFY pixelsChanged)
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
    emit radiusChanged();
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
  MotionArtwork *animation() const { return m_animation; }
  void setAnimation(MotionArtwork *animation);
  bool ready() const { return (m_animation && !m_animation->frame().isNull()) || !m_image.isNull() || !m_previous.isNull(); }
  void paint(QPainter *) override;
  Q_INVOKABLE QColor seedColor() const;
  static void clearCaches();
  static std::function<QNetworkRequest(const QUrl &)> resolveServerArt;
signals:
  void animationChanged();
  void sourceChanged();
  void radiusChanged();
  void pixelsChanged();
  void readyChanged();
  void fitChanged();
  void crossfadeChanged();
  void transitionChanged();

private:
  void reload(bool preserve=false);
  void imageReady();
  void finishTransition();
  bool m_crossfade=false,m_previousFit=false;
  qreal m_mix=1;
  QImage m_previous;
  std::unique_ptr<QVariantAnimation> m_fade;
  bool m_fit=false,m_originalSizeFallback=false;
  QUrl m_source;
  QImage m_image;
  QPointer<MotionArtwork> m_animation;
  QPointer<QNetworkReply> m_reply;
  qreal m_radius = 16;
  int m_pixels = 360;
};
