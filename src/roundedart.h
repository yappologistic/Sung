#pragma once
#include <QImage>
#include <QNetworkReply>
#include <QPointer>
#include <QQuickPaintedItem>
#include <functional>

class RoundedArt : public QQuickPaintedItem {
  Q_OBJECT
  friend class ArtworkTest;
  Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
  Q_PROPERTY(qreal radius READ radius WRITE setRadius NOTIFY radiusChanged)
  Q_PROPERTY(int pixels READ pixels WRITE setPixels NOTIFY pixelsChanged)
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
  int pixels() const { return m_pixels; }
  void setPixels(int p) {
    p = qBound(48, p, 800);
    if (p == m_pixels)
      return;
    m_pixels = p;
    emit pixelsChanged();
    reload();
  }
  bool ready() const { return !m_image.isNull(); }
  void paint(QPainter *) override;
  static void clearCaches();
  static std::function<QUrl(const QUrl &)> resolveServerArt;
signals:
  void sourceChanged();
  void radiusChanged();
  void pixelsChanged();
  void readyChanged();

private:
  void reload();
  QUrl m_source;
  QImage m_image;
  QPointer<QNetworkReply> m_reply;
  qreal m_radius = 16;
  int m_pixels = 360;
};
