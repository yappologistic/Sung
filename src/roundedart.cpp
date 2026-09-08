#include "roundedart.h"
#include <QBuffer>
#include <QFileInfo>
#include <QCache>
#include <QCoreApplication>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QPainter>
#include <QPainterPath>
#include <QStandardPaths>
#include <utility>

std::function<QUrl(const QUrl &)> RoundedArt::resolveServerArt;
static QCache<QString, QImage> cache(8 * 1024 * 1024);
static QNetworkAccessManager *manager() {
  static QNetworkAccessManager *n = nullptr;
  if (!n) {
    n = new QNetworkAccessManager(QCoreApplication::instance());
    auto c = new QNetworkDiskCache(n);
    c->setCacheDirectory(
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
        "/art");
    c->setMaximumCacheSize(48 * 1024 * 1024);
    n->setCache(c);
    n->setTransferTimeout(15000);
  }
  return n;
}
RoundedArt::RoundedArt(QQuickItem *p) : QQuickPaintedItem(p) {
  setAntialiasing(true);
}
RoundedArt::~RoundedArt() {
  if (m_reply) {
    m_reply->disconnect(this);
    m_reply->abort();
    m_reply->deleteLater();
  }
}
void RoundedArt::setAnimation(MotionArtwork *animation) {
  if(m_animation==animation)return;
  if(m_animation)disconnect(m_animation,nullptr,this,nullptr);
  m_animation=animation;
  if(animation)connect(animation,&MotionArtwork::frameChanged,this,[this]{emit readyChanged();update();});
  emit animationChanged();emit readyChanged();update();
}
void RoundedArt::setSource(const QUrl &v) {
  if (m_source == v)
    return;
  m_source = v;
  emit sourceChanged();
  reload();
}
void RoundedArt::reload() {
  if (m_reply) {
    m_reply->disconnect(this);
    m_reply->abort();
    m_reply->deleteLater();
    m_reply = nullptr;
  }
  m_image = {};
  emit readyChanged();
  update();
  if (m_source.isEmpty())
    return;
  const auto key = m_source.toString() + QLatin1Char('|') + QString::number(m_pixels);
  if (auto img = cache.object(key)) {
    m_image = *img;
    emit readyChanged();
    update();
    return;
  }
  if(m_source.isLocalFile()) {
    const QFileInfo file(m_source.toLocalFile());if(file.size()>262144)return;
    QImageReader reader(file.absoluteFilePath());reader.setAutoTransform(true);const auto size=reader.size();
    if(!size.isValid()||size.width()>1024||size.height()>1024)return;
    reader.setScaledSize(size.scaled(m_pixels,m_pixels,Qt::KeepAspectRatio));m_image=reader.read();
    if(m_image.format()==QImage::Format_RGB32)m_image=std::move(m_image).convertToFormat(QImage::Format_RGB888);
    if(!m_image.isNull())cache.insert(key,new QImage(m_image),m_image.sizeInBytes());
    emit readyChanged();update();return;
  }
  const bool server=m_source.scheme()=="sungcover";
  const QUrl url=server&&resolveServerArt?resolveServerArt(m_source):m_source;
  if(url.isEmpty() || (url.scheme()!="https" && !(server&&url.scheme()=="http")))return;
  QNetworkRequest req(url);
  if(server){req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);req.setAttribute(QNetworkRequest::CacheSaveControlAttribute,false);}
  req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                   server?QNetworkRequest::AlwaysNetwork:QNetworkRequest::PreferCache);
  auto r = manager()->get(req);
  m_reply = r;
  connect(r, &QNetworkReply::downloadProgress, this,
          [r](qint64 got, qint64 total) {
            if (got > 4 * 1024 * 1024 || total > 4 * 1024 * 1024)
              r->abort();
          });
  connect(r, &QNetworkReply::finished, this, [this, r, key] {
    if (m_reply != r) {
      r->deleteLater();
      return;
    }
    m_reply = nullptr;
    // Concurrent views can finish the same artwork request together. Reuse
    // the first decoded image instead of retaining a private copy per view.
    if (const auto image = cache.object(key)) {
      m_image = *image;
    } else if (r->error() == QNetworkReply::NoError) {
      auto bytes = r->readAll();
      QBuffer buffer(&bytes);
      buffer.open(QIODevice::ReadOnly);
      QImageReader reader(&buffer);
      reader.setAutoTransform(true);
      auto size = reader.size();
      if (size.isValid() && size.width() <= 10000 && size.height() <= 10000) {
        reader.setScaledSize(
            size.scaled(m_pixels, m_pixels, Qt::KeepAspectRatio));
        m_image = reader.read();
        // Opaque covers need three color bytes, not an unused alpha byte.
        // Preserve images with alpha and higher precision in their source format.
        if (m_image.format() == QImage::Format_RGB32) {
          auto packed = std::move(m_image).convertToFormat(QImage::Format_RGB888);
          if (!packed.isNull()) m_image = std::move(packed);
        }
        if (!m_image.isNull())
          cache.insert(key, new QImage(m_image), m_image.sizeInBytes());
      }
    }
    r->deleteLater();
    emit readyChanged();
    update();
  });
}
void RoundedArt::paint(QPainter *p) {
  const auto &image=m_animation && !m_animation->frame().isNull()?m_animation->frame():m_image;
  if (image.isNull())
    return;
  QPainterPath path;
  path.addRoundedRect(boundingRect(), m_radius, m_radius);
  p->setClipPath(path);
  p->setRenderHint(QPainter::SmoothPixmapTransform);
  const auto s =
      QSizeF(image.size())
          .scaled(boundingRect().size(), Qt::KeepAspectRatioByExpanding);
  p->drawImage(QRectF((width() - s.width()) / 2, (height() - s.height()) / 2,
                      s.width(), s.height()),
               image);
}

void RoundedArt::clearCaches() { cache.clear(); if(manager()->cache())manager()->cache()->clear(); }
