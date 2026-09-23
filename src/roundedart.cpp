#include "roundedart.h"
#include "artworkurl.h"
#include "m3shape.h"
#include <QBuffer>
#include <QFileInfo>
#include <QCache>
#include <QSet>
#include <QCoreApplication>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QPainter>
#include <QPainterPath>
#include <QStandardPaths>
#include <QThread>
#include <QQuickWindow>
#include <QThreadPool>
#include <QtMath>
#include <QtConcurrentRun>
#include <array>
#include <cmath>
#include <utility>

std::function<QNetworkRequest(const QUrl &)> RoundedArt::resolveServerArt;
std::function<QUrl(const QUrl &)> RoundedArt::resolveVideoFrame;
static QCache<QString, QImage> cache(8 * 1024 * 1024);
// Accessed only on the GUI thread. Views requesting the same local cover
// subscribe to one decode; QImage then shares its pixels without copying.
// https://doc.qt.io/qt-6/threads-modules.html#threads-and-implicitly-shared-classes
static QHash<QString, QFutureWatcher<QImage> *> localLoads;
// Every surface alive right now, so a change in what a URL stands for can
// reach the ones already drawing it. Surfaces live on the GUI thread only.
static QSet<RoundedArt *> &liveArt() { static QSet<RoundedArt *> set; return set; }
// Covers are decoded here rather than on Qt's global pool, so a burst of rows
// scrolling in cannot take every thread the rest of the app shares. Two
// threads keep a fast disk busy while leaving the GUI thread a core.
static QThreadPool *decodePool() {
  static QThreadPool *pool = nullptr;
  if (!pool) {
    pool = new QThreadPool(QCoreApplication::instance());
    pool->setMaxThreadCount(qBound(2, QThread::idealThreadCount() / 2, 4));
  }
  return pool;
}
static QNetworkAccessManager *artNetwork = nullptr;
static QNetworkAccessManager *manager() {
  auto &n = artNetwork;
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
// One separable box pass with a running sum: O(pixels) regardless of radius.
// Edges clamp, so a softened cover keeps its border color instead of fading out.
static void boxPass(const QImage &source, QImage &target, int radius, bool horizontal) {
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
static QImage softened(const QImage &source, int radius) {
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
QVector<RoundedArt::ScrimSample> RoundedArt::sampleScrim(const QImage &image) {
  QVector<ScrimSample> samples;
  if (image.isNull()) return samples;
  // AmbientBackdrop.qml blurs a 160px decode at radius 22. A 16 by 16
  // grid includes the edges and bounds later palette-frame solves to 256
  // samples, while the blur has already removed detail between grid points.
  constexpr int side = 16;
  samples.reserve(side * side);
  for (int y = 0; y < side; ++y) {
    const int sy = y * (image.height() - 1) / (side - 1);
    for (int x = 0; x < side; ++x) {
      const QRgb pixel = image.pixel(x * (image.width() - 1) / (side - 1), sy);
      samples.append({float(qRed(pixel) / 255.0), float(qGreen(pixel) / 255.0),
                      float(qBlue(pixel) / 255.0), float(qAlpha(pixel) / 255.0)});
    }
  }
  return samples;
}
RoundedArt::RoundedArt(QQuickItem *p) : QQuickPaintedItem(p) {
  setAntialiasing(true);
  liveArt().insert(this);
}
RoundedArt::~RoundedArt() {
  liveArt().remove(this);
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
  if(animation)connect(animation,&MotionArtwork::frameChanged,this,[this]{fitTextureSize();emit readyChanged();update();});
  emit animationChanged();emit readyChanged();update();
}
void RoundedArt::soften() {
  m_softImage = softened(m_image, m_blur);
  if (m_blur > 0) m_scrimSamples = sampleScrim(shown());
  else m_scrimSamples.clear();
  // A source change already carries the softened old cover. Blur it again
  // only when the blur radius itself changes.
  if (!m_previous.isNull() && m_softPrevious.isNull())
    m_softPrevious = softened(m_previous, m_blur);
  if (m_blur > 0 && !m_previous.isNull() && m_scrimPreviousSamples.isEmpty())
    m_scrimPreviousSamples = sampleScrim(m_softPrevious.isNull() ? m_previous : m_softPrevious);
}
const QImage &RoundedArt::shown() const {
  return m_blur > 0 && !m_softImage.isNull() ? m_softImage : m_image;
}
void RoundedArt::setBlur(int radius) {
  radius = qBound(0, radius, 128);
  if (radius == m_blur)
    return;
  m_blur = radius;
  m_softPrevious = {};
  m_scrimPreviousSamples.clear();
  soften();
  fitTextureSize();
  emit blurChanged();
  update();
}
void RoundedArt::finishTransition(){
  if(m_fade)m_fade->stop();
  m_previous={};m_softPrevious={};m_scrimPreviousSamples.clear();m_mix=1;fitTextureSize();emit transitionChanged();emit readyChanged();update();
}
void RoundedArt::setCrossfade(bool value){if(value==m_crossfade)return;m_crossfade=value;if(!value)finishTransition();emit crossfadeChanged();}
void RoundedArt::imageReady(){
  soften();
  fitTextureSize();
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
    if(!m_image.isNull()){m_previous=m_image;m_softPrevious=m_softImage;m_scrimPreviousSamples=m_scrimSamples;m_previousFit=m_fit;}
    m_mix=0;
  }else {m_previous={};m_softPrevious={};m_scrimPreviousSamples.clear();m_mix=1;}
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
  // Whatever a running decode was reading is no longer what this surface
  // shows, so its result is discarded when it arrives.
  ++m_decode;
  if(!preserve){m_image = {};m_softImage = {};m_scrimSamples.clear();fitTextureSize();}
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
    // Reading and scaling a cover costs milliseconds, which is a dropped
    // frame if it happens while the list is moving. The work goes to a pool
    // thread and the result is taken back on the GUI thread, so a row that
    // scrolls into view no longer stalls the one being drawn.
    const QString path=m_source.toLocalFile();
    const int pixels=m_pixels;
    // A generation counter: only the newest request for this surface may
    // deliver. A reused delegate that has moved on to another cover, or a
    // surface being destroyed, leaves its earlier decode with a stale token.
    const quint64 token=++m_decode;
    auto *watcher=localLoads.value(key);
    const bool start = !watcher;
    if (start) {
      watcher=new QFutureWatcher<QImage>(QCoreApplication::instance());
      localLoads.insert(key,watcher);
      connect(watcher,&QFutureWatcherBase::finished,watcher,[watcher,key]{
        // A cache clear or explicit refresh can retire a request while its
        // worker is still finishing. Do not repopulate the cache with it.
        if(localLoads.value(key)==watcher) {
          localLoads.remove(key);
          const auto image=watcher->result();
          if(!image.isNull())cache.insert(key,new QImage(image),image.sizeInBytes());
        }
        watcher->deleteLater();
      });
    }
    connect(watcher,&QFutureWatcherBase::finished,this,[this,watcher,token]{
      if(token!=m_decode)return;
      m_image=watcher->result();
      imageReady();
    });
    if(start)watcher->setFuture(QtConcurrent::run(decodePool(),[path,pixels]{
      const QFileInfo file(path);
      if(file.size()>2*1024*1024)return QImage();
      QImageReader reader(file.absoluteFilePath());reader.setAutoTransform(true);
      const auto size=reader.size();
      if(!size.isValid()||size.width()>4096||size.height()>4096)return QImage();
      reader.setScaledSize(size.scaled(pixels,pixels,Qt::KeepAspectRatio));
      QImage image=reader.read();
      if(image.format()==QImage::Format_RGB32)image=std::move(image).convertToFormat(QImage::Format_RGB888);
      return image;
    }));
    return;
  }
  const bool server=m_source.scheme()=="sungcover";
  QNetworkRequest serverRequest=server&&resolveServerArt?resolveServerArt(m_source):QNetworkRequest();
  QUrl url=server?serverRequest.url():m_source;
  // Ask the image service for the size this surface draws rather than the
  // thumbnail the catalogue handed out. A request that fails is retried once
  // with the source untouched, which is also how a missing HD frame degrades.
  if(!m_originalSizeFallback && !server){
    if(resolveVideoFrame && !artworkurl::videoId(url).isEmpty()){const auto cover=resolveVideoFrame(url);if(!cover.isEmpty())url=cover;}
    url=artworkurl::sized(url,m_pixels);
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
// A painted surface keeps a render target the size of the item, and the scene
// graph a texture beside it. Neither needs more pixels than the picture drawn
// into them: a cover decoded at `pixels`, or softened down to a wash, carries
// no detail above its own resolution, and rasterising it into a buffer several
// times that size costs the difference twice over for nothing. The ambient
// backdrop is the case that pays: a 160px cover filling a window was taking a
// multi-megabyte target at both ends.
//
// The cap only applies where the item has no rounded or shaped edge. That edge
// is masked into the same target, and it is the one thing in the surface that
// does need the item's own resolution to stay crisp.
void RoundedArt::fitTextureSize() {
  const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
  const QSize full(qCeil(width() * dpr), qCeil(height() * dpr));
  // Nothing drawn needs no target at all. A surface waiting on its cover, or
  // one whose cover never arrives, was still being given a buffer the size of
  // the item at both ends; a single pixel stretched over it is the same
  // transparency, and the real target is put back the moment a picture lands.
  const QImage &art = m_animation && !m_animation->frame().isNull() ? m_animation->frame() : shown();
  const QImage &behind = m_blur > 0 && !m_softPrevious.isNull() ? m_softPrevious : m_previous;
  if (art.isNull() && behind.isNull()) {
    setTextureSize(QSize(1, 1));
    return;
  }
  if (full.isEmpty() || m_radius > 0 || m3::hasShape(m_shape)) {
    setTextureSize({});
    return;
  }
  // Qt's empty textureSize means item-sized. During decode the prior image
  // is still painted, so keep its small target instead of a window-sized one.
  // QQuickPaintedItem::textureSize, Qt 6 documentation.
  const QImage &sized = art.isNull() ? behind : art;
  const int carried = qMax(sized.width(), sized.height());
  const int spans = qMax(full.width(), full.height());
  if (carried >= spans) {
    setTextureSize({});
    return;
  }
  setTextureSize(QSize(qMax(1, full.width() * carried / spans),
                       qMax(1, full.height() * carried / spans)));
}
namespace {
const std::array<double, 256> &linearChannels() {
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
double channelLinear(double value) {
  // The palette animates through fractional sRGB values. Interpolate between
  // 8-bit table entries so a changing role does not invoke pow per pixel.
  const double entry = qBound(0.0, value * 255.0, 255.0);
  const int index = int(entry);
  const double fraction = entry - index;
  const auto &table = linearChannels();
  return index == 255 ? table[255]
                      : table[index] + (table[index + 1] - table[index]) * fraction;
}
double luminance(double red, double green, double blue) {
  return 0.2126 * channelLinear(red) + 0.7152 * channelLinear(green)
         + 0.0722 * channelLinear(blue);
}
}

qreal RoundedArt::minimumContrast(const QColor &surface, const QColor &ink, qreal alpha) const {
  const double sr = surface.redF(), sg = surface.greenF(), sb = surface.blueF();
  const double inkL = luminance(ink.redF(), ink.greenF(), ink.blueF());
  double minimum = 100;
  if (m_scrimSamples.isEmpty() && !shown().isNull())
    m_scrimSamples = sampleScrim(shown());
  const QImage &previous = m_blur > 0 && !m_softPrevious.isNull() ? m_softPrevious : m_previous;
  if (m_scrimPreviousSamples.isEmpty() && !previous.isNull())
    m_scrimPreviousSamples = sampleScrim(previous);
  const auto scan = [&](const QVector<ScrimSample> &samples) {
    for (const auto &pixel : samples) {
      const double reveal = (1 - alpha) * pixel.coverage;
      const double washL = luminance(sr + reveal * (pixel.red - sr),
                                     sg + reveal * (pixel.green - sg),
                                     sb + reveal * (pixel.blue - sb));
      const double ratio = (qMax(washL, inkL) + 0.05) / (qMin(washL, inkL) + 0.05);
      minimum = qMin(minimum, ratio);
    }
  };
  scan(m_scrimSamples);
  scan(m_scrimPreviousSamples);
  return minimum == 100 ? (qMax(luminance(sr, sg, sb), inkL) + 0.05) /
                              (qMin(luminance(sr, sg, sb), inkL) + 0.05) : minimum;
}

qreal RoundedArt::minimumScrim(const QColor &surface, const QColor &ink,
                               qreal base, qreal target) const {
  // MCU color_spec_2021.ts:241-248 measures onSurfaceVariant against a
  // surface. WCAG 1.4.3 requires 4.5:1 body text. The cover changes that
  // surface. Palette motion can call this each frame; each solve reads at
  // most two fixed 16 by 16 grids sampled when the covers were decoded.
  base = qBound(0.0, base, 1.0);
  if (minimumContrast(surface, ink, base) >= target) return base;
  qreal low = base, high = 1;
  for (int i = 0; i < 12; ++i) {
    const qreal mid = (low + high) / 2;
    if (minimumContrast(surface, ink, mid) >= target) high = mid;
    else low = mid;
  }
  return high;
}
void RoundedArt::geometryChange(const QRectF &current, const QRectF &previous) {
  QQuickPaintedItem::geometryChange(current, previous);
  fitTextureSize();
}
void RoundedArt::paint(QPainter *p) {
  const auto &image=m_animation && !m_animation->frame().isNull()?m_animation->frame():shown();
  const auto &previous=m_blur>0 && !m_softPrevious.isNull()?m_softPrevious:m_previous;
  if(image.isNull() && previous.isNull())return;
  p->save();QPainterPath path;
  if(m3::hasShape(m_shape))path=m3::shapePath(m_shape,boundingRect());
  else path.addRoundedRect(boundingRect(),m_radius,m_radius);
  p->setClipPath(path);p->setRenderHint(QPainter::SmoothPixmapTransform);
  const auto draw=[&](const QImage &art,bool fit,qreal opacity){if(art.isNull())return;const auto s=QSizeF(art.size()).scaled(boundingRect().size(),fit?Qt::KeepAspectRatio:Qt::KeepAspectRatioByExpanding);p->setOpacity(opacity);p->drawImage(QRectF((width()-s.width())/2,(height()-s.height())/2,s.width(),s.height()),art);};
  if(!previous.isNull()){
    draw(previous,m_previousFit,1);
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

void RoundedArt::clearCaches() {
  cache.clear();localLoads.clear();
  // Clearing local data must not start the network stack as a side effect.
  if(artNetwork && artNetwork->cache())artNetwork->cache()->clear();
}
void RoundedArt::refreshFrames() {
  for(auto *art:std::as_const(liveArt()))if(!artworkurl::videoId(art->m_source).isEmpty())art->refresh();
}
// The source is the same; what it stands for is not. Drop the decoded copy
// so the reload cannot answer from memory, and keep the old picture up, as a
// crossfade origin where there is one, until the new one arrives.
void RoundedArt::refresh() {
  const auto key=m_source.toString()+QLatin1Char('|')+QString::number(m_pixels);
  cache.remove(key);
  localLoads.remove(key);
  if(m_fade)m_fade->stop();
  if(m_crossfade && !m_image.isNull()){m_previous=m_image;m_softPrevious=m_softImage;m_scrimPreviousSamples=m_scrimSamples;m_previousFit=m_fit;m_mix=0;}
  m_originalSizeFallback=false;emit transitionChanged();
  reload(true);
}

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
