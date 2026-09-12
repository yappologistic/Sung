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
#include <QRegularExpression>

std::function<QNetworkRequest(const QUrl &)> RoundedArt::resolveServerArt;
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
void RoundedArt::finishTransition(){
  if(m_fade)m_fade->stop();
  m_previous={};m_mix=1;emit transitionChanged();emit readyChanged();update();
}
void RoundedArt::setCrossfade(bool value){if(value==m_crossfade)return;m_crossfade=value;if(!value)finishTransition();emit crossfadeChanged();}
void RoundedArt::imageReady(){
  if(m_crossfade && !m_previous.isNull() && !m_image.isNull()){
    if(!m_fade){m_fade=std::make_unique<QVariantAnimation>();m_fade->setDuration(220);m_fade->setStartValue(0.0);m_fade->setEndValue(1.0);m_fade->setEasingCurve(QEasingCurve::InOutCubic);
      connect(m_fade.get(),&QVariantAnimation::valueChanged,this,[this](const QVariant &value){m_mix=value.toReal();update();});
      connect(m_fade.get(),&QVariantAnimation::finished,this,&RoundedArt::finishTransition);
    }
    m_fade->stop();m_mix=0;m_fade->start();emit transitionChanged();
  }else finishTransition();
  emit readyChanged();update();
}
void RoundedArt::setSource(const QUrl &v) {
  if (m_source == v)
    return;
  if(m_fade)m_fade->stop();
  if(m_crossfade && !v.isEmpty()){
    if(!m_image.isNull()){m_previous=m_image;m_previousFit=m_fit;}
    m_mix=0;
  }else {m_previous={};m_mix=1;}
  m_source = v;m_originalSizeFallback=false;emit transitionChanged();
  emit sourceChanged();
  reload();
}
void RoundedArt::reload(bool preserve) {
  if (m_reply) {
    m_reply->disconnect(this);
    m_reply->abort();
    m_reply->deleteLater();
    m_reply = nullptr;
  }
  if(!preserve)m_image = {};
  emit readyChanged();
  update();
  if (m_source.isEmpty())
    return;
  const auto key = m_source.toString() + QLatin1Char('|') + QString::number(m_pixels);
  if (auto img = cache.object(key)) {
    m_image = *img;
    imageReady();
    return;
  }
  if(m_source.isLocalFile()) {
    const QFileInfo file(m_source.toLocalFile());if(file.size()>2*1024*1024){finishTransition();return;}
    QImageReader reader(file.absoluteFilePath());reader.setAutoTransform(true);const auto size=reader.size();
    if(!size.isValid()||size.width()>4096||size.height()>4096){finishTransition();return;}
    reader.setScaledSize(size.scaled(m_pixels,m_pixels,Qt::KeepAspectRatio));m_image=reader.read();
    if(m_image.format()==QImage::Format_RGB32)m_image=std::move(m_image).convertToFormat(QImage::Format_RGB888);
    if(!m_image.isNull())cache.insert(key,new QImage(m_image),m_image.sizeInBytes());
    imageReady();return;
  }
  const bool server=m_source.scheme()=="sungcover";
  QNetworkRequest serverRequest=server&&resolveServerArt?resolveServerArt(m_source):QNetworkRequest();
  QUrl url=server?serverRequest.url():m_source;
  // Only resize known Google thumbnail transforms; other artwork URLs are untouched.
  if(!m_originalSizeFallback && !server && (url.host()=="lh3.googleusercontent.com" || url.host()=="lh3.ggpht.com" || url.host()=="yt3.googleusercontent.com" || url.host()=="yt3.ggpht.com")){
    auto path=url.path();static const QRegularExpression dimensions("=w(\\d+)-h(\\d+)");const auto match=dimensions.match(path);
    if(match.hasMatch()){const int size=qMax(qMax(match.captured(1).toInt(),match.captured(2).toInt()),m_pixels);path.replace(match.capturedStart(),match.capturedLength(),QString("=w%1-h%1").arg(qMin(1600,size)));url.setPath(path);}
  }
  if(url.isEmpty() || (url.scheme()!="https" && !(server&&url.scheme()=="http"))){finishTransition();return;}
  QNetworkRequest req=server?serverRequest:QNetworkRequest(url);
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
  connect(r, &QNetworkReply::finished, this, [this, r, key, resized=(url!=m_source && !server)] {
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
    if(m_image.isNull() && resized && !m_originalSizeFallback){m_originalSizeFallback=true;reload();return;}
    imageReady();
  });
}
void RoundedArt::paint(QPainter *p) {
  const auto &image=m_animation && !m_animation->frame().isNull()?m_animation->frame():m_image;
  if(image.isNull() && m_previous.isNull())return;
  p->save();QPainterPath path;path.addRoundedRect(boundingRect(),m_radius,m_radius);p->setClipPath(path);p->setRenderHint(QPainter::SmoothPixmapTransform);
  const auto draw=[&](const QImage &art,bool fit,qreal opacity){if(art.isNull())return;const auto s=QSizeF(art.size()).scaled(boundingRect().size(),fit?Qt::KeepAspectRatio:Qt::KeepAspectRatioByExpanding);p->setOpacity(opacity);p->drawImage(QRectF((width()-s.width())/2,(height()-s.height())/2,s.width(),s.height()),art);};
  if(!m_previous.isNull()){
    draw(m_previous,m_previousFit,1);
    // Source with fractional coverage interpolates premultiplied color and alpha.
    // Plus with painter opacity does not preserve alpha in Qt's raster backend.
    p->setCompositionMode(QPainter::CompositionMode_Source);
    if(!image.isNull() && m_fit){
      const auto s=QSizeF(image.size()).scaled(boundingRect().size(),Qt::KeepAspectRatio);
      QPainterPath outside,inside;outside.addRect(boundingRect());inside.addRect(QRectF((width()-s.width())/2,(height()-s.height())/2,s.width(),s.height()));
      p->setCompositionMode(QPainter::CompositionMode_DestinationOut);p->setOpacity(m_mix);p->fillPath(outside.subtracted(inside),Qt::black);p->setCompositionMode(QPainter::CompositionMode_Source);
    }
    draw(image,m_fit,m_mix);
  }else draw(image,m_fit,1);
  p->restore();
}

void RoundedArt::clearCaches() { cache.clear(); if(manager()->cache())manager()->cache()->clear(); }

QColor RoundedArt::seedColor() const {
  if(m_image.isNull())return QColor(Qt::transparent);
  const auto sample=m_image.scaled(24,24,Qt::IgnoreAspectRatio,Qt::FastTransformation);
  double weights[24]={},red[24]={},green[24]={},blue[24]={};
  for(int y=0;y<sample.height();++y)for(int x=0;x<sample.width();++x){const auto c=sample.pixelColor(x,y);
    if(c.alphaF()<0.5 || c.hslSaturationF()<0.12 || c.lightnessF()<0.08 || c.lightnessF()>0.92)continue;
    const int bin=qBound(0,int(c.hslHueF()*24),23);const auto w=c.hslSaturationF();weights[bin]+=w;red[bin]+=c.redF()*w;green[bin]+=c.greenF()*w;blue[bin]+=c.blueF()*w;
  }
  int best=0;for(int i=1;i<24;++i)if(weights[i]>weights[best])best=i;
  if(weights[best]==0)return QColor(Qt::transparent);
  return QColor::fromRgbF(red[best]/weights[best],green[best]/weights[best],blue[best]/weights[best]);
}
