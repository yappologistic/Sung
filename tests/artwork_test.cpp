#include "roundedart.h"
#include "motionbackdrop.h"
#include "artworkurl.h"
#include "m3motion.h"
#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QNetworkCacheMetaData>
#include <QNetworkDiskCache>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QPainter>
#include <QPainterPath>
#include <QtTest>
#include <memory>
#include <QProcess>
#include <QMovie>
#include <QMediaPlayer>
#include <QMediaMetaData>
#include <QVideoFrame>
#include <cmath>

// A response the loader will take from the network cache instead of the
// network, so what a surface fetches for a URL can be checked offline.
static void seed(const QUrl &url,const QByteArray &bytes,const char *type="image/png") {
  QNetworkDiskCache disk;disk.setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/art");
  QNetworkCacheMetaData meta;meta.setUrl(url);meta.setExpirationDate(QDateTime::currentDateTimeUtc().addDays(1));
  meta.setRawHeaders({{"Content-Type",type},{"Cache-Control","max-age=86400"}});
  auto device=disk.prepare(meta);QVERIFY(device);device->write(bytes);disk.insert(device);
}
static QByteArray solid(int width,int height,Qt::GlobalColor color) {
  QImage image(width,height,QImage::Format_RGB32);image.fill(color);
  QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);image.save(&buffer,"PNG");return bytes;
}

class ArtworkTest : public QObject {
  Q_OBJECT
private slots:
  void concurrentLocalCoversSharePixels() {
    QTemporaryDir dir;
    const auto path=dir.filePath("shared.png");
    QImage source(1024,1024,QImage::Format_RGB32);source.fill(Qt::red);
    QVERIFY(source.save(path));RoundedArt::clearCaches();
    std::vector<std::unique_ptr<RoundedArt>> views;
    for(int i=0;i<32;++i) {
      auto view=std::make_unique<RoundedArt>();view->setPixels(1024);
      view->setSource(QUrl::fromLocalFile(path));views.push_back(std::move(view));
    }
    // Destruction and reuse must not cancel a decode other views still need.
    views.back().reset();views.pop_back();views.back()->setSource({});
    for(size_t i=0;i+1<views.size();++i)QTRY_VERIFY(views[i]->ready());
    const auto *pixels=views.front()->m_image.constBits();
    for(size_t i=0;i+1<views.size();++i)QCOMPARE(views[i]->m_image.constBits(),pixels);
    QVERIFY(!views.back()->ready());
    // A refresh during an outstanding read must not let that read poison
    // the fresh cache, or cancel the views subscribed to the old read.
    RoundedArt::clearCaches();
    views.back()->setSource(QUrl::fromLocalFile(path));
    source.fill(Qt::blue);QVERIFY(source.save(path));
    views.back()->refresh();QTRY_VERIFY(views.back()->ready());
    QCOMPARE(views.back()->m_image.pixelColor(0,0),QColor(Qt::blue));
    QCOMPARE(views.front()->m_image.pixelColor(0,0),QColor(Qt::red));
  }
  void coverCrossfade() {
    QTemporaryDir dir;QStringList files;for(const auto color:{Qt::red,Qt::blue,Qt::green}){QImage image(128,128,QImage::Format_RGB32);image.fill(color);const auto path=dir.filePath(QString::number(files.size())+".png");QVERIFY(image.save(path));files<<path;}
    RoundedArt art;art.setWidth(100);art.setHeight(100);art.setPixels(128);art.setCrossfade(true);art.setSource(QUrl::fromLocalFile(files[0]));QVERIFY(!art.transitioning());
    // A cover off disk is decoded on a pool thread, so a surface becomes ready
    // a moment after it is given a source rather than inside the call.
    QTRY_VERIFY(art.ready());
    art.setSource(QUrl::fromLocalFile(files[1]));QTRY_VERIFY(art.transitioning());QVERIFY(!art.m_previous.isNull());QTest::qWait(45);
    QTRY_VERIFY(art.m_fade && art.m_fade->state() == QAbstractAnimation::Running);
    // ExpressiveMotionTokens.kt:24-25 gives DefaultEffects 1/1600.
    const auto effects = m3::springTokens(true, QStringLiteral("defaultEffects"));
    QCOMPARE(art.m_fade->duration(), m3::spring(effects.damping, effects.stiffness).durationMs);
    const qreal midpoint = m3::springResponse(effects.damping, effects.stiffness,
                                              art.m_fade->duration() / 2000.0);
    QVERIFY(qAbs(art.m_fade->easingCurve().valueForProgress(0.5) - midpoint) < 0.015);
    QImage mixed(100,100,QImage::Format_ARGB32_Premultiplied);mixed.fill(Qt::transparent);{QPainter p(&mixed);art.paint(&p);}
    const auto capture = qEnvironmentVariable("SUNG_TEST_OUTPUT");
    if (!capture.isEmpty()) {
      QVERIFY(QDir().mkpath(capture));
      QVERIFY(mixed.save(capture + "/artwork-crossfade-midpoint.png"));
    }
    const auto color=mixed.pixelColor(50,50);QVERIFY(color.red()>30&&color.blue()>30);QVERIFY(color.alpha()>250);
    QTRY_VERIFY_WITH_TIMEOUT(!art.transitioning(),1000);QVERIFY(art.m_previous.isNull());
    art.setSource(QUrl::fromLocalFile(files[0]));QTRY_VERIFY(art.ready());art.setSource(QUrl::fromLocalFile(files[2]));QVERIFY(art.transitioning());
    art.setCrossfade(false);QVERIFY(!art.transitioning());QVERIFY(art.m_previous.isNull());QTRY_VERIFY(art.m_image.pixelColor(50,50).green()>200);
    QImage transparent(128,128,QImage::Format_ARGB32_Premultiplied);transparent.fill(Qt::transparent);const auto alphaFile=dir.filePath("transparent.png");QVERIFY(transparent.save(alphaFile));
    art.setCrossfade(true);art.setSource(QUrl::fromLocalFile(alphaFile));QTRY_VERIFY(art.ready());QTest::qWait(100);mixed.fill(Qt::transparent);{QPainter p(&mixed);art.paint(&p);}QVERIFY(qAbs(mixed.pixelColor(50,50).alphaF()-(1-art.m_mix))<0.02);
    art.setCrossfade(false);art.setSource(QUrl::fromLocalFile(files[2]));QTRY_VERIFY(art.ready());QImage wide(128,64,QImage::Format_RGB32);wide.fill(Qt::blue);const auto wideFile=dir.filePath("wide.png");QVERIFY(wide.save(wideFile));
    art.setCrossfade(true);art.setSource(QUrl::fromLocalFile(wideFile));art.setFit(true);QTRY_VERIFY(art.ready());QTest::qWait(100);mixed.fill(Qt::transparent);{QPainter p(&mixed);art.paint(&p);}QVERIFY(qAbs(mixed.pixelColor(50,8).alphaF()-(1-art.m_mix))<0.02);QVERIFY(mixed.pixelColor(50,50).alpha()>250);
    art.setCrossfade(true);art.setSource(QUrl::fromLocalFile(dir.filePath("missing.png")));QTRY_VERIFY(!art.ready());QVERIFY(!art.transitioning());
    art.setSource(QUrl::fromLocalFile(files[0]));art.setSource({});QVERIFY(!art.ready());QVERIFY(art.m_previous.isNull());
  }
  void decodedBackdropKeepsSmallTarget() {
    QTemporaryDir dir;
    QImage first(160, 160, QImage::Format_RGB32);
    first.fill(Qt::black);
    QImage next(160, 160, QImage::Format_RGB32);
    next.fill(Qt::white);
    const auto a = dir.filePath("first.png"), b = dir.filePath("next.png");
    QVERIFY(first.save(a));
    QVERIFY(next.save(b));
    RoundedArt::clearCaches();
    RoundedArt art;
    art.setRadius(0);
    art.setWidth(1180);
    art.setHeight(800);
    art.setPixels(160);
    art.setBlur(22);
    art.setCrossfade(true);
    art.setSource(QUrl::fromLocalFile(a));
    QTRY_VERIFY(art.ready());
    // QQuickPaintedItem::textureSize: the 160px source at 1180x800 needs
    // a 160x108 target. An empty size would allocate the full item.
    QCOMPARE(art.textureSize(), QSize(160, 108));
    const auto oldBlur = art.m_softImage;
    const auto oldBits = oldBlur.constBits();
    art.setSource(QUrl::fromLocalFile(b));
    QVERIFY(art.transitioning());
    QVERIFY(art.m_image.isNull());
    QCOMPARE(art.textureSize(), QSize(160, 108));
    QCOMPARE(art.m_softPrevious.constBits(), oldBits);
    QTRY_VERIFY(!art.m_image.isNull());
    QCOMPARE(art.m_softPrevious.constBits(), oldBits);
    QCOMPARE(art.m_scrimSamples.size(), 256);
    QCOMPARE(art.m_scrimPreviousSamples.size(), 256);
  }
  void shapeMorphMasksTheFrame() {
    // A cover morphing from one shape to another is cut to the frame
    // between them. Where the two differ most, a point just inside the
    // halfway radius is painted and one just outside is not, and at either
    // end the mask is that end's shape.
    QTemporaryDir dir;
    QImage cover(160, 160, QImage::Format_RGB32);
    cover.fill(Qt::white);
    const auto file = dir.filePath("white.png");
    QVERIFY(cover.save(file));
    RoundedArt art;
    art.setWidth(200);
    art.setHeight(200);
    art.setPixels(160);
    art.setSource(QUrl::fromLocalFile(file));
    QTRY_VERIFY(art.ready());
    art.setShape("cookie12Sided");
    art.setToShape("puffyDiamond");
    const auto from = m3::shapeOutline("cookie12Sided", 360), to = m3::shapeOutline("puffyDiamond", 360);
    int widest = 0;
    for (int i = 0; i < 360; ++i)
      if (qAbs(from[i] - to[i]) > qAbs(from[widest] - to[widest]))
        widest = i;
    QVERIFY(qAbs(from[widest] - to[widest]) > 0.15);
    const double angle = widest * M_PI / 180;
    const auto painted = [&](double morph, double radius) {
      art.setMorph(morph);
      QImage canvas(200, 200, QImage::Format_ARGB32_Premultiplied);
      canvas.fill(Qt::transparent);
      QPainter painter(&canvas);
      art.paint(&painter);
      painter.end();
      return qAlpha(canvas.pixel(int(std::lround(100 + radius * 100 * std::cos(angle))),
                                 int(std::lround(100 + radius * 100 * std::sin(angle))))) > 128;
    };
    for (const double morph : {0.0, 0.5, 1.0}) {
      const double edge = from[widest] + (to[widest] - from[widest]) * morph;
      QVERIFY2(painted(morph, edge - 0.03), qPrintable(QString("inside at %1").arg(morph)));
      QVERIFY2(!painted(morph, edge + 0.03), qPrintable(QString("outside at %1").arg(morph)));
    }
  }
  void backdropScrimProtectsInk() {
    QTemporaryDir dir;
    RoundedArt art;
    art.setBlur(22);
    const auto check = [&](const QColor &cover, const QColor &surface,
                           const QColor &ink, qreal base, bool mustRise) {
      QImage image(160, 160, QImage::Format_RGB32);
      image.fill(cover);
      const auto path = dir.filePath(cover.name() + ".png");
      if (!image.save(path)) return false;
      art.setSource(QUrl::fromLocalFile(path));
      QElapsedTimer timer;
      timer.start();
      while (art.m_image.isNull() && timer.elapsed() < 3000) QTest::qWait(10);
      if (art.m_image.isNull()) return false;
      const qreal alpha = art.minimumScrim(surface, ink, base, 4.5);
      if (mustRise && alpha <= base) return false;
      if (!mustRise && qAbs(alpha - base) > 0.001) return false;
      return art.minimumContrast(surface, ink, alpha) >= 4.5;
    };
    QVERIFY(check(Qt::black, QColor("#fff8f7"), QColor("#705c53"), 0.8, true));
    QVERIFY(check(QColor("#b97962"), QColor("#fff8f7"), QColor("#705c53"), 0.8, false));
    QVERIFY(check(Qt::white, QColor("#151211"), QColor("#d8c3bd"), 0.8, false));
  }
  void scrimSolveCost() {
    QTemporaryDir dir;
    QImage cover(160, 160, QImage::Format_RGB32);
    cover.fill(Qt::black);
    const auto path = dir.filePath("black.png");
    QVERIFY(cover.save(path));
    RoundedArt art;
    art.setPixels(160);
    art.setBlur(22);
    art.setSource(QUrl::fromLocalFile(path));
    QTRY_VERIFY(art.ready());
    QCOMPARE(art.m_scrimSamples.size(), 256);
    const QColor surface("#fff8f7"), ink("#705c53");
    QElapsedTimer timer;
    timer.start();
    qreal total = 0;
    for (int i = 0; i < 100; ++i)
      total += art.minimumScrim(surface, ink, 0.8, 4.5);
    qInfo().noquote() << "SCRIM_100_CALLS_MS" << timer.nsecsElapsed() / 1e6;
    QVERIFY(total > 80);
  }
  void scrimGridMatchesFullScan() {
    QTemporaryDir dir;
    QVector<QImage> covers;
    for (const QColor color : {QColor(Qt::black), QColor(Qt::white),
                               QColor("#ee2244"), QColor("#164fe5")}) {
      QImage image(160, 160, QImage::Format_RGB32);
      image.fill(color);
      covers.append(image);
    }
    QImage split(160, 160, QImage::Format_RGB32);
    split.fill(QColor("#ec283d"));
    { QPainter painter(&split); painter.fillRect(0, 80, 160, 80, QColor("#163cd0")); }
    covers.append(split);
    QImage checks(160, 160, QImage::Format_RGB32);
    QImage gradient(160, 160, QImage::Format_RGB32);
    for (int y = 0; y < 160; ++y) for (int x = 0; x < 160; ++x) {
      checks.setPixelColor(x, y, ((x / 8 + y / 8) & 1) ? QColor("#101018") : QColor("#f9d856"));
      gradient.setPixelColor(x, y, QColor(x * 255 / 159, y * 255 / 159, 170));
    }
    covers.append(checks);
    covers.append(gradient);

    const auto linear = [](double value) {
      return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    const auto luma = [&](double red, double green, double blue) {
      return 0.2126 * linear(red) + 0.7152 * linear(green) + 0.0722 * linear(blue);
    };
    const auto fullContrast = [&](const QImage &image, const QColor &surface,
                                  const QColor &ink, qreal alpha) {
      const double sr = surface.redF(), sg = surface.greenF(), sb = surface.blueF();
      const double inkL = luma(ink.redF(), ink.greenF(), ink.blueF());
      double minimum = 100;
      for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
        const QRgb pixel = image.pixel(x, y);
        const double reveal = (1 - alpha) * qAlpha(pixel) / 255.0;
        const double washL = luma(sr + reveal * (qRed(pixel) / 255.0 - sr),
                                  sg + reveal * (qGreen(pixel) / 255.0 - sg),
                                  sb + reveal * (qBlue(pixel) / 255.0 - sb));
        minimum = qMin(minimum, (qMax(washL, inkL) + 0.05) / (qMin(washL, inkL) + 0.05));
      }
      return minimum;
    };
    const auto fullScrim = [&](const QImage &image, const QColor &surface,
                               const QColor &ink) {
      qreal low = 0.8, high = 1;
      if (fullContrast(image, surface, ink, low) >= 4.5) return low;
      for (int i = 0; i < 12; ++i) {
        const qreal middle = (low + high) / 2;
        if (fullContrast(image, surface, ink, middle) >= 4.5) high = middle;
        else low = middle;
      }
      return high;
    };

    RoundedArt art;
    art.setPixels(160);
    art.setBlur(22);
    qreal largestDifference = 0;
    for (int i = 0; i < covers.size(); ++i) {
      const auto path = dir.filePath(QString::number(i) + ".png");
      QVERIFY(covers[i].save(path));
      art.setSource(QUrl::fromLocalFile(path));
      QTRY_VERIFY(!art.m_image.isNull());
      QCOMPARE(art.m_scrimSamples.size(), 256);
      QCOMPARE(art.m_scrimPreviousSamples.size(), 0);
      for (const auto &pair : {std::pair{QColor("#fff8f7"), QColor("#705c53")},
                              std::pair{QColor("#151211"), QColor("#d8c3bd")}}) {
        const qreal sparse = art.minimumScrim(pair.first, pair.second, 0.8, 4.5);
        const qreal full = fullScrim(art.m_softImage, pair.first, pair.second);
        largestDifference = qMax(largestDifference, qAbs(sparse - full));
        QVERIFY2(qAbs(sparse - full) < 0.01,
                 qPrintable(QString("cover %1 sparse %2 full %3").arg(i).arg(sparse).arg(full)));
      }
    }
    qInfo().noquote() << "SCRIM_MAX_OPACITY_DELTA" << largestDifference;
  }
  void fitAndLargeArtwork() {
    QTemporaryDir dir;QImage source(1600,800,QImage::Format_RGB32);source.fill(Qt::red);const auto file=dir.filePath("wide.jpg");QVERIFY(source.save(file));
    RoundedArt art;art.setWidth(100);art.setHeight(100);art.setRadius(0);art.setPixels(1600);art.setSource(QUrl::fromLocalFile(file));QTRY_VERIFY(art.ready());QCOMPARE(art.m_image.width(),1600);
    QImage fit(100,100,QImage::Format_ARGB32_Premultiplied);fit.fill(Qt::transparent);art.setFit(true);{QPainter painter(&fit);art.paint(&painter);}QCOMPARE(fit.pixelColor(50,0).alpha(),0);QVERIFY(fit.pixelColor(50,50).red()>240);
    QImage fill(100,100,QImage::Format_ARGB32_Premultiplied);fill.fill(Qt::transparent);art.setFit(false);{QPainter painter(&fill);art.paint(&painter);}QVERIFY(fill.pixelColor(50,0).red()>240);
    art.setPixels(9000);QCOMPARE(art.pixels(),1600);art.setPixels(128);QTRY_COMPARE(art.m_image.width(),128);
  }
  void softenedBackdrop() {
    QTemporaryDir dir;
    // Hard edges are exactly what made an enlarged cover look blocky.
    QImage checks(120,120,QImage::Format_RGB32);
    for(int y=0;y<120;++y)for(int x=0;x<120;++x)checks.setPixel(x,y,((x/10)+(y/10))%2?qRgb(20,30,200):qRgb(230,180,40));
    const auto path=dir.filePath("checks.png");QVERIFY(checks.save(path));
    const auto detail=[](const QImage &image){
      qint64 total=0;int samples=0;
      for(int y=1;y<image.height();++y)for(int x=1;x<image.width();++x){
        const auto a=image.pixelColor(x,y),b=image.pixelColor(x-1,y),c=image.pixelColor(x,y-1);
        total+=qAbs(a.red()-b.red())+qAbs(a.green()-b.green())+qAbs(a.blue()-b.blue());
        total+=qAbs(a.red()-c.red())+qAbs(a.green()-c.green())+qAbs(a.blue()-c.blue());
        samples+=2;
      }
      return samples?double(total)/samples:0.0;
    };
    RoundedArt art;art.setWidth(120);art.setHeight(120);art.setRadius(0);art.setPixels(120);
    art.setSource(QUrl::fromLocalFile(path));QTRY_VERIFY(art.ready());
    QCOMPARE(art.blur(),0);
    QVERIFY(art.m_softImage.isNull());
    const double sharp=detail(art.m_image);
    QVERIFY(sharp>20);
    art.setBlur(18);
    QCOMPARE(art.blur(),18);
    QVERIFY(!art.m_softImage.isNull());
    QCOMPARE(art.m_softImage.size(),art.m_image.size());
    const double soft=detail(art.m_softImage);
    // A real blur removes almost all of the edge energy, not just some of it.
    QVERIFY2(soft<sharp*0.1,qPrintable(QString("sharp %1 soft %2").arg(sharp).arg(soft)));
    // Averaging must not drain the picture toward the edges or toward grey.
    const auto centre=art.m_softImage.pixelColor(60,60),corner=art.m_softImage.pixelColor(2,2);
    QVERIFY(centre.alpha()==255 && corner.alpha()==255);
    QVERIFY(qAbs(centre.red()-125)<45 && qAbs(centre.blue()-120)<45);
    // Enlarging the softened cover is what the backdrop actually draws.
    QImage enlarged(600,600,QImage::Format_ARGB32_Premultiplied);enlarged.fill(Qt::transparent);
    {QPainter painter(&enlarged);art.paint(&painter);}
    QVERIFY(detail(enlarged)<detail(art.m_image)*0.05);
    art.setBlur(0);
    QVERIFY(art.m_softImage.isNull());
    QImage plain(600,600,QImage::Format_ARGB32_Premultiplied);plain.fill(Qt::transparent);
    {QPainter painter(&plain);art.paint(&painter);}
    QVERIFY(detail(plain)>detail(enlarged)*4);
    art.setBlur(400);QCOMPARE(art.blur(),128);QVERIFY(!art.m_softImage.isNull());
    art.setSource({});art.setBlur(12);QVERIFY(art.m_softImage.isNull());
  }
  void accentSampling() {
    QTemporaryDir dir;RoundedArt art;QCOMPARE(art.seedColor().alpha(),0);
    QImage picture(64,64,QImage::Format_RGB32);picture.fill(QColor("#e04466"));const auto path=dir.filePath("pink.png");QVERIFY(picture.save(path));art.setSource(QUrl::fromLocalFile(path));QTRY_VERIFY(art.ready());
    const auto color=art.seedColor();QVERIFY(color.red()>200 && color.blue()<150);
    picture.fill(Qt::gray);const auto gray=dir.filePath("gray.png");QVERIFY(picture.save(gray));art.setSource(QUrl::fromLocalFile(gray));QTRY_VERIFY(art.ready());QCOMPARE(art.seedColor().alpha(),0);
    art.setSource({});QCOMPARE(art.seedColor().alpha(),0);
  }
  void animatedCover_data() {
    QTest::addColumn<QString>("extension");
    for(const auto &format:{"gif","webp","mp4","webm"})QTest::newRow(format)<<QString(format);
  }
  void animatedCover() {
    QFETCH(QString,extension);QTemporaryDir dir;QVERIFY(dir.isValid());
    const auto path=dir.filePath("Cover #100% ü."+extension);
    QStringList args={"-nostdin","-v","error","-f","lavfi","-i","testsrc2=size=128x128:rate=10:duration=0.6"};
    if(extension=="mp4" || extension=="webm")args<<"-f"<<"lavfi"<<"-i"<<"sine=frequency=440:duration=0.6";
    args<<"-threads"<<"1"<<"-y"<<path;QProcess ff;ff.start("ffmpeg",args);
    QVERIFY(ff.waitForFinished(10000));QVERIFY2(ff.exitCode()==0,ff.readAllStandardError());
    MotionArtwork motion;QVERIFY(!motion.m_player && !motion.m_movie);QSignalSpy frames(&motion,&MotionArtwork::frameChanged);
    motion.setSource(QUrl::fromLocalFile(path));QVERIFY(motion.frame().isNull());
    motion.setRunning(true);QTRY_VERIFY_WITH_TIMEOUT(frames.count()>4 && !motion.frame().isNull(),5000);
    if(motion.m_movie)QCOMPARE(motion.m_movie->cacheMode(),QMovie::CacheNone);
    if(motion.m_player){QVERIFY(!motion.m_player->audioTracks().isEmpty());QVERIFY(!motion.m_player->audioOutput());QCOMPARE(motion.m_player->activeAudioTrack(),-1);}
    RoundedArt first,second;first.setAnimation(&motion);second.setAnimation(&motion);
    QVERIFY(first.ready());QCOMPARE(first.animation(),second.animation());
    QVERIFY(motion.frame().sizeInBytes()<=800*800*4);
    const int before=frames.count();QTest::qWait(1400);QVERIFY(frames.count()>before+7); // loops past the source duration
    motion.setRunning(false);QTest::qWait(200);const int paused=frames.count();const auto frame=motion.frame();
    QTest::qWait(400);QCOMPARE(frames.count(),paused);QCOMPARE(motion.frame(),frame);
    motion.setRunning(true);QTRY_VERIFY_WITH_TIMEOUT(frames.count()>paused+2,3000);
    motion.setSource({});QVERIFY(motion.frame().isNull());QVERIFY(!motion.m_player && !motion.m_movie && !motion.m_sink);QVERIFY(!first.ready());
    const auto corrupt=dir.filePath("broken."+extension);QFile file(corrupt);QVERIFY(file.open(QIODevice::WriteOnly));file.write("invalid cover");file.close();
    motion.setSource(QUrl::fromLocalFile(corrupt));QTest::qWait(300);QVERIFY(motion.frame().isNull());
    motion.setSource(QUrl("https://example.invalid/cover."+extension));QVERIFY(!motion.m_player && !motion.m_movie);
    // Repeated source replacement must release the previous decoder and frames.
    for(int i=0;i<4;++i){motion.setSource(QUrl::fromLocalFile(path));QTest::qWait(60);motion.setSource({});QVERIFY(motion.frame().isNull());}
  }
  void removedAnimatedCoverStopsDecoder_data() {
    QTest::addColumn<QString>("extension");
    QTest::newRow("gif") << QString("gif");
    QTest::newRow("webp") << QString("webp");
  }
  void removedAnimatedCoverStopsDecoder() {
    QFETCH(QString,extension);
    QTemporaryDir dir;QVERIFY(dir.isValid());
    const auto path=dir.filePath("cover."+extension);
    QStringList args={"-nostdin","-v","error","-f","lavfi","-i",
                      "testsrc2=size=64x64:rate=10:duration=0.6"};
    if(extension=="gif")args<<"-loop"<<"0";
    args<<"-y"<<path;
    QProcess ff;ff.start("ffmpeg",args);
    QVERIFY(ff.waitForFinished(10000));QVERIFY2(ff.exitCode()==0,ff.readAllStandardError());
    MotionArtwork motion;QSignalSpy frames(&motion,&MotionArtwork::frameChanged);
    motion.setRunning(true);motion.setSource(QUrl::fromLocalFile(path));
    QVERIFY(motion.m_movie);
    QSignalSpy errors(motion.m_movie.get(),&QMovie::error);
    // The six-frame WebP plays once in its file; Sung keeps it moving.
    QTRY_VERIFY_WITH_TIMEOUT(frames.count()>12&&!motion.frame().isNull(),3000);
    QVERIFY(QFile::remove(path));
    QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(),3000);
    QCOMPARE(motion.m_movie->state(),QMovie::NotRunning);
    QVERIFY(motion.frame().isNull());
    const auto count=frames.count();QTest::qWait(800);QCOMPARE(frames.count(),count);
    motion.setRunning(false);motion.setRunning(true);
    QCOMPARE(motion.m_movie->state(),QMovie::NotRunning);
    QCOMPARE(frames.count(),count);
  }
  void videoOrientationAndFrameBounds() {
    QImage image(2,3,QImage::Format_RGB32);image.fill(Qt::red);image.setPixelColor(0,2,Qt::blue);
    QVideoFrame frame(image);frame.setRotation(QtVideo::Rotation::Clockwise90);
    MotionArtwork motion;motion.setRunning(true);motion.publishVideo(frame);
    QCOMPARE(motion.frame().size(),QSize(3,2));QCOMPARE(motion.frame().pixelColor(0,0),QColor(Qt::blue));
    frame.setMirrored(true);motion.publishVideo(frame);QCOMPARE(motion.frame().pixelColor(2,0),QColor(Qt::blue));
    QImage large(1600,800,QImage::Format_RGB32);large.fill(Qt::blue);motion.publishVideo(QVideoFrame(large));
    QCOMPARE(motion.frame().size(),QSize(800,400));
  }
  // A list row is reused for another song while its cover is still being
  // decoded. The decode that is no longer wanted must not land on the row.
  void supersededLocalDecodeIsDiscarded() {
    QTemporaryDir dir;
    // The cover being abandoned is large and the one replacing it is small, so
    // the stale decode is still running when the wanted one has finished. That
    // is the ordering a reused list row actually hits.
    QImage slow(4000,4000,QImage::Format_RGB32);slow.fill(Qt::red);
    const auto slowPath=dir.filePath("slow.png");QVERIFY(slow.save(slowPath));
    QImage quick(64,64,QImage::Format_RGB32);quick.fill(Qt::green);
    const auto quickPath=dir.filePath("quick.png");QVERIFY(quick.save(quickPath));
    RoundedArt art;art.setPixels(512);
    // Both sources are set in the same turn of the event loop, so the first
    // decode is still in flight when the second replaces it.
    art.setSource(QUrl::fromLocalFile(slowPath));
    art.setSource(QUrl::fromLocalFile(quickPath));
    QTRY_VERIFY(art.ready());
    QCOMPARE(art.m_image.pixelColor(10,10),QColor(Qt::green));
    // Give the superseded decode every chance to arrive late and win.
    QTest::qWait(600);
    QCOMPARE(art.m_image.pixelColor(10,10),QColor(Qt::green));
  }
  // Clearing the source while a decode is running leaves nothing behind.
  void clearedSourceDropsPendingLocalDecode() {
    QTemporaryDir dir;QImage picture(512,512,QImage::Format_RGB32);picture.fill(Qt::red);
    const auto path=dir.filePath("cover.png");QVERIFY(picture.save(path));
    RoundedArt art;art.setPixels(512);
    art.setSource(QUrl::fromLocalFile(path));
    art.setSource({});
    QTest::qWait(200);
    QVERIFY(!art.ready());
    QVERIFY(art.m_image.isNull());
  }
  void localCoverIsBoundedAndShared() {
    QTemporaryDir dir;QImage picture(512,512,QImage::Format_RGB32);picture.fill(Qt::red);const auto path=dir.filePath("cover.jpg");QVERIFY(picture.save(path));
    RoundedArt a,b;a.setSource(QUrl::fromLocalFile(path));QTRY_VERIFY(a.ready());
    // The second surface answers from the shared cache the first filled.
    b.setSource(QUrl::fromLocalFile(path));QTRY_VERIFY(b.ready());QCOMPARE(a.m_image.constBits(),b.m_image.constBits());QVERIFY(a.m_image.width()<=360);
  }
  void concurrentViewsShareDecodedPixels() {
    QTemporaryDir profile;QVERIFY(profile.isValid());
    qputenv("XDG_CACHE_HOME",profile.path().toUtf8());
    QNetworkDiskCache disk;
    disk.setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/art");
    const QUrl url("https://sung-test.invalid/shared-cover.png");
    QImage original(400,400,QImage::Format_RGB32);
    for(int y=0;y<400;++y)for(int x=0;x<400;++x)original.setPixel(x,y,qRgb(x%256,y%256,(x+y)%256));
    QByteArray bytes;QBuffer buffer(&bytes);QVERIFY(buffer.open(QIODevice::WriteOnly));QVERIFY(original.save(&buffer,"PNG"));
    QNetworkCacheMetaData meta;meta.setUrl(url);meta.setExpirationDate(QDateTime::currentDateTimeUtc().addDays(1));
    meta.setRawHeaders({{"Content-Type","image/png"},{"Cache-Control","max-age=86400"}});
    auto device=disk.prepare(meta);QVERIFY(device);device->write(bytes);disk.insert(device);
    std::vector<std::unique_ptr<RoundedArt>> views;
    for(int i=0;i<12;++i){auto view=std::make_unique<RoundedArt>();view->setPixels(400);view->setSource(url);views.push_back(std::move(view));}
    for(const auto &view:views)QTRY_VERIFY_WITH_TIMEOUT(view->ready(),5000);
    QCOMPARE(views.front()->m_image.sizeInBytes(),qsizetype(400*400*3));
    // Compare the actual rounded, smoothly scaled painting path at fractional
    // as well as integral sizes; storage changes must not alter displayed pixels.
    for(const auto size:{QSizeF(190,190),QSizeF(149.75,103.5),QSizeF(420,420)}) {
      auto render=[&](const QImage &image) {
        QImage output(700,700,QImage::Format_ARGB32_Premultiplied);output.fill(Qt::transparent);
        QPainter painter(&output);painter.scale(1.6,1.6);painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;path.addRoundedRect(QRectF(QPointF(),size),20,20);painter.setClipPath(path);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const auto scaled=QSizeF(image.size()).scaled(size,Qt::KeepAspectRatioByExpanding);
        painter.drawImage(QRectF((size.width()-scaled.width())/2,(size.height()-scaled.height())/2,scaled.width(),scaled.height()),image);
        painter.end();return output;
      };
      QCOMPARE(render(views.front()->m_image),render(original));
    }
    for(const auto &view:views){
      QCOMPARE(view->m_image.convertToFormat(QImage::Format_RGB32),original);
      QCOMPARE(view->m_image.constBits(),views.front()->m_image.constBits());
    }
    // A separately sized view must not share a raster at the wrong resolution.
    RoundedArt small;small.setPixels(112);small.setSource(url);QTRY_VERIFY_WITH_TIMEOUT(small.ready(),5000);
    QCOMPARE(small.m_image.size(),QSize(112,112));
    QVERIFY(small.m_image.constBits()!=views.front()->m_image.constBits());
    views.front()->setSource({});QVERIFY(!views.front()->ready());
    QVERIFY(views.back()->ready());QCOMPARE(views.back()->m_image.convertToFormat(QImage::Format_RGB32),original);
    // Transparency must survive without RGB packing.
    const QUrl alphaUrl("https://sung-test.invalid/alpha-cover.png");
    QImage alpha(64,64,QImage::Format_ARGB32);
    for(int y=0;y<64;++y)for(int x=0;x<64;++x)alpha.setPixel(x,y,qRgba(x*4,y*4,80,(x+y)*2));
    QByteArray alphaBytes;QBuffer alphaBuffer(&alphaBytes);QVERIFY(alphaBuffer.open(QIODevice::WriteOnly));QVERIFY(alpha.save(&alphaBuffer,"PNG"));
    meta.setUrl(alphaUrl);device=disk.prepare(meta);QVERIFY(device);device->write(alphaBytes);disk.insert(device);
    RoundedArt transparent;transparent.setPixels(64);transparent.setSource(alphaUrl);QTRY_VERIFY_WITH_TIMEOUT(transparent.ready(),5000);
    QVERIFY(transparent.m_image.hasAlphaChannel());QCOMPARE(transparent.m_image.convertToFormat(QImage::Format_ARGB32),alpha);
    RoundedArt::clearCaches();QVERIFY(views.back()->ready());QCOMPARE(views.back()->m_image.convertToFormat(QImage::Format_RGB32),original);
  }
  void coverRequestSizes() {
    const QUrl frame("https://i.ytimg.com/vi/abcDEF123_-/hqdefault.jpg?sqp=-oaymwE&rs=AOn4");
    QCOMPARE(artworkurl::videoId(frame),QString("abcDEF123_-"));
    QVERIFY(artworkurl::videoId(QUrl("https://i.ytimg.com/vi/short/hqdefault.jpg")).isEmpty());
    QVERIFY(artworkurl::videoId(QUrl("https://ytimg.com.evil.example/vi/abcDEF123_-/hqdefault.jpg")).isEmpty());
    QVERIFY(artworkurl::videoId(QUrl("http://i.ytimg.com/vi/abcDEF123_-/hqdefault.jpg")).isEmpty());
    // A row draws the frame below its own 225 pixels; anything larger asks for the HD frame.
    QCOMPARE(artworkurl::sized(frame,150),frame);
    QCOMPARE(artworkurl::sized(frame,384),QUrl("https://i.ytimg.com/vi/abcDEF123_-/maxresdefault.jpg"));
    const QUrl google("https://yt3.googleusercontent.com/abc=w544-h544-l90-rj");
    QCOMPARE(artworkurl::sized(google,120),google);
    QCOMPARE(artworkurl::sized(google,800),QUrl("https://yt3.googleusercontent.com/abc=w800-h800-l90-rj"));
    QCOMPARE(artworkurl::sized(google,3000),QUrl("https://yt3.googleusercontent.com/abc=w1600-h1600-l90-rj"));
    const QUrl apple("https://is1-ssl.mzstatic.com/image/thumb/Music/ab/cd/100x100bb.jpg");
    QVERIFY(artworkurl::isAlbumCover(apple));
    QCOMPARE(artworkurl::sized(apple,384),QUrl("https://is1-ssl.mzstatic.com/image/thumb/Music/ab/cd/384x384bb.jpg"));
    for(const auto bad:{"http://is1-ssl.mzstatic.com/image/thumb/x/100x100bb.jpg","https://mzstatic.com.evil.example/image/thumb/x/100x100bb.jpg",
                        "https://is1-ssl.mzstatic.com/image/thumb/x/100x100bb.jpg?x=1","https://is1-ssl.mzstatic.com/image/thumb/x/cover.jpg","https://user@is1-ssl.mzstatic.com/image/thumb/x/100x100bb.jpg"})
      QVERIFY2(!artworkurl::isAlbumCover(QUrl(bad)),bad);
    QCOMPARE(artworkurl::sized(QUrl("https://example.com/cover.jpg"),800),QUrl("https://example.com/cover.jpg"));
    // The Cover Art Archive keeps three sizes beside the upload itself.
    const QUrl front("https://coverartarchive.org/release-group/11111111-2222-3333-4444-555555555555/front");
    QVERIFY(artworkurl::isArchiveCover(front));
    QVERIFY(artworkurl::isAlbumCover(front));
    QVERIFY(!artworkurl::isAppleCover(front));
    QCOMPARE(artworkurl::sized(front,120),QUrl(front.toString()+"-250"));
    QCOMPARE(artworkurl::sized(front,384),QUrl(front.toString()+"-500"));
    QCOMPARE(artworkurl::sized(front,800),QUrl(front.toString()+"-1200"));
    // Past the largest of them the upload is the only bigger picture there is.
    QCOMPARE(artworkurl::sized(front,1600),front);
    for(const auto bad:{"http://coverartarchive.org/release-group/11111111-2222-3333-4444-555555555555/front",
                        "https://coverartarchive.org.evil.example/release-group/11111111-2222-3333-4444-555555555555/front",
                        "https://coverartarchive.org/release-group/11111111-2222-3333-4444-555555555555/back",
                        "https://coverartarchive.org/release-group/short/front",
                        "https://coverartarchive.org/release-group/11111111-2222-3333-4444-555555555555/front?x=1"})
      QVERIFY2(!artworkurl::isArchiveCover(QUrl(bad)),bad);
  }
  void videoFrameLoadsHdFrame() {
    const QUrl frame("https://i.ytimg.com/vi/frameTest01/hqdefault.jpg?sqp=-oaymwE");
    seed(QUrl("https://i.ytimg.com/vi/frameTest01/maxresdefault.jpg"),solid(1280,720,Qt::blue));
    seed(frame,solid(400,225,Qt::red));
    RoundedArt large;large.setPixels(800);large.setSource(frame);QTRY_VERIFY_WITH_TIMEOUT(large.ready(),5000);
    QCOMPARE(large.m_image.size(),QSize(800,450));QVERIFY(large.m_image.pixelColor(10,10).blue()>200);
    RoundedArt row;row.setPixels(150);row.setSource(frame);QTRY_VERIFY_WITH_TIMEOUT(row.ready(),5000);
    QCOMPARE(row.m_image.size(),QSize(150,84));QVERIFY(row.m_image.pixelColor(10,10).red()>200);
    // An upload with no HD frame: that request fails and the catalogue's frame is used.
    const QUrl plain("https://i.ytimg.com/vi/frameTest02/hqdefault.jpg?sqp=-oaymwE");
    seed(QUrl("https://i.ytimg.com/vi/frameTest02/maxresdefault.jpg"),QByteArray("not an image"),"image/jpeg");
    seed(plain,solid(400,225,Qt::green));
    RoundedArt fallback;fallback.setPixels(800);fallback.setSource(plain);QTRY_VERIFY_WITH_TIMEOUT(fallback.ready(),5000);
    QVERIFY(fallback.m_originalSizeFallback);QVERIFY(fallback.m_image.pixelColor(10,10).green()>200);
  }
  void videoFrameResolvesToAlbumCover() {
    const QUrl frame("https://i.ytimg.com/vi/frameTest03/hqdefault.jpg?sqp=-oaymwE");
    seed(frame,solid(400,225,Qt::red));seed(QUrl("https://i.ytimg.com/vi/frameTest03/maxresdefault.jpg"),solid(1280,720,Qt::blue));
    seed(QUrl("https://is1-ssl.mzstatic.com/image/thumb/Test/800x800bb.jpg"),solid(800,800,Qt::green));
    seed(QUrl("https://coverartarchive.org/release-group/99999999-8888-7777-6666-555555555555/front-1200"),solid(900,900,Qt::yellow));
    const auto cover=[](const QUrl &url){return artworkurl::videoId(url)=="frameTest03"?QUrl("https://is1-ssl.mzstatic.com/image/thumb/Test/100x100bb.jpg"):QUrl();};
    RoundedArt::resolveVideoFrame=cover;
    RoundedArt art;art.setPixels(800);art.setSource(frame);QTRY_VERIFY_WITH_TIMEOUT(art.ready(),5000);
    QCOMPARE(art.m_image.size(),QSize(800,800));QVERIFY(art.m_image.pixelColor(10,10).green()>200);
    // What a frame stands for can change after it was drawn: live surfaces fetch again.
    RoundedArt::resolveVideoFrame=nullptr;RoundedArt::refreshFrames();
    QTRY_VERIFY_WITH_TIMEOUT(art.ready()&&art.m_image.pixelColor(10,10).blue()>200,5000);QCOMPARE(art.m_image.size(),QSize(800,450));
    art.setCrossfade(true);RoundedArt::resolveVideoFrame=cover;RoundedArt::refreshFrames();
    QVERIFY(art.transitioning());QTRY_VERIFY_WITH_TIMEOUT(!art.transitioning(),3000);QVERIFY(art.m_image.pixelColor(10,10).green()>200);
    RoundedArt other;other.setPixels(800);other.setSource(QUrl("https://sung-test.invalid/plain.png"));
    RoundedArt::resolveVideoFrame=nullptr;RoundedArt::refreshFrames();QVERIFY(!other.ready());
    // A frame answered by the archive rather than by Apple draws the same way.
    RoundedArt::resolveVideoFrame=[](const QUrl &url){return artworkurl::videoId(url)=="frameTest03"
        ?QUrl("https://coverartarchive.org/release-group/99999999-8888-7777-6666-555555555555/front"):QUrl();};
    // A size of its own, so this reads the archive rather than the decoded
    // frame the surfaces above already put in the shared cache.
    RoundedArt archive;archive.setCrossfade(false);archive.setPixels(900);archive.setSource(frame);
    QTRY_VERIFY_WITH_TIMEOUT(archive.ready(),5000);
    QCOMPARE(archive.m_image.size(),QSize(900,900));
    QVERIFY(archive.m_image.pixelColor(10,10).red()>200&&archive.m_image.pixelColor(10,10).green()>200&&archive.m_image.pixelColor(10,10).blue()<80);
    RoundedArt::resolveVideoFrame=nullptr;
  }
  // The Motion backdrop draws the animation while there is one, the still
  // otherwise, and never an empty view while the next picture is on its way.
  void motionBackdropChoosesAndHoldsItsPicture() {
    QTemporaryDir dir;
    const auto save=[&](const QString &name,Qt::GlobalColor color){
      const auto path=dir.filePath(name);QFile file(path);
      if(file.open(QIODevice::WriteOnly))file.write(solid(64,64,color));
      return QUrl::fromLocalFile(path);
    };
    const auto red=save("red.png",Qt::red),green=save("green.png",Qt::green);
    RoundedArt still;still.setPixels(64);still.setSource(red);
    QTRY_VERIFY(!still.picture().isNull());
    MotionBackdrop backdrop;backdrop.setSize({400,250});
    QSignalSpy scenes(&backdrop,&MotionBackdrop::sceneChanged);
    backdrop.setStill(&still);
    QVERIFY(backdrop.ready());QVERIFY(!backdrop.moving());QCOMPARE(scenes.count(),1);
    QVERIFY(backdrop.m_previous.frame.isNull());QCOMPARE(backdrop.mix(),1.0);

    MotionArtwork animation;animation.m_source=QUrl::fromLocalFile(dir.filePath("cover.mp4"));
    QImage frame(96,96,QImage::Format_ARGB32_Premultiplied);frame.fill(Qt::blue);animation.publish(frame);
    backdrop.setAnimation(&animation);
    QVERIFY(backdrop.moving());QCOMPARE(scenes.count(),2);
    // The still is kept underneath until the crossfade says it may go.
    QVERIFY(!backdrop.m_previous.frame.isNull());QCOMPARE(backdrop.mix(),0.0);
    backdrop.setMix(0.5);QVERIFY(!backdrop.m_previous.frame.isNull());
    backdrop.setMix(1);QVERIFY(backdrop.m_previous.frame.isNull());
    // Later frames of the same animation are the same scene.
    frame.fill(Qt::cyan);animation.publish(frame);
    QCOMPARE(scenes.count(),2);QCOMPARE(backdrop.m_current.frame.pixelColor(0,0),QColor(Qt::cyan));

    backdrop.setAnimation(nullptr);
    QVERIFY(!backdrop.moving());QCOMPARE(scenes.count(),3);backdrop.setMix(1);
    // A new still is decoded off the GUI thread. Until it lands, the old one
    // stays rather than the view going blank.
    still.setSource(green);
    QVERIFY(backdrop.ready());QVERIFY(backdrop.m_current.key.endsWith("red.png"));
    QTRY_VERIFY(backdrop.m_current.key.endsWith("green.png"));
    QCOMPARE(backdrop.m_current.frame.pixelColor(32,32),QColor(Qt::green));
    // With no cover at all there is nothing to show, and the last picture
    // fades out rather than cutting.
    backdrop.setMix(1);still.setSource(QUrl());
    QTRY_VERIFY(!backdrop.ready());
    QVERIFY(!backdrop.m_previous.frame.isNull());
    backdrop.setMix(1);QVERIFY(backdrop.m_previous.frame.isNull());
  }
  // The edge picture is the blur and the feather in one: opaque at the view's
  // border, clear in its middle, and the shape of the view, not the frame.
  void motionBackdropEdgeFeathersIntoABlur() {
    QImage checker(512,512,QImage::Format_RGB32);
    for(int y=0;y<512;++y)for(int x=0;x<512;++x)checker.setPixel(x,y,((x/8+y/8)%2)?qRgb(255,255,255):qRgb(0,0,0));
    MotionArtwork animation;animation.m_source=QUrl("file:///checker.mp4");animation.publish(checker);
    MotionBackdrop backdrop;backdrop.setSize({1280,800});backdrop.setAnimation(&animation);
    const QImage edge=backdrop.m_current.edge;
    QVERIFY(!edge.isNull());
    QCOMPARE(edge.format(),QImage::Format_ARGB32_Premultiplied);
    QCOMPARE(edge.width(),128);QCOMPARE(edge.height(),80);
    QVERIFY(qAlpha(edge.pixel(0,0))>=250);
    QVERIFY(qAlpha(edge.pixel(0,40))>=240);
    QVERIFY(qAlpha(edge.pixel(64,0))>=240);
    QCOMPARE(qAlpha(edge.pixel(64,40)),0);
    // Halfway through the feather it is neither.
    const int half=qAlpha(edge.pixel(6,40));
    QVERIFY2(half>40&&half<215,qPrintable(QString::number(half)));
    // A checkerboard blurred is grey: no black or white survives at the border.
    const QColor border=QColor::fromRgba(edge.pixel(0,40)).toRgb();
    QVERIFY2(border.red()>60&&border.red()<195,qPrintable(border.name()));
    // A new size is a new crop, not the old picture stretched.
    backdrop.setSize({600,800});
    QCOMPARE(backdrop.m_current.edge.width(),96);QCOMPARE(backdrop.m_current.edge.height(),128);
  }
  // Each band's scrim is solved from the picture under it, and holds the
  // most any frame of the scene has needed so a loop does not pump it.
  void motionBackdropScrimFollowsThePicture() {
    MotionArtwork animation;animation.m_source=QUrl("file:///bands.mp4");
    QImage frame(200,200,QImage::Format_RGB32);frame.fill(Qt::black);animation.publish(frame);
    MotionBackdrop backdrop;backdrop.setSize({400,400});
    backdrop.setSurface(QColor(20,18,24));backdrop.setInk(QColor(202,196,208));backdrop.setContrast(4.5);
    backdrop.setTopBand(60);backdrop.setBottomBand(120);
    backdrop.setAnimation(&animation);
    QCOMPARE(backdrop.bottomScrim(),0.0);QCOMPARE(backdrop.topScrim(),0.0);
    // White only in the bottom band: the bottom scrim thickens, the top not.
    frame.fill(Qt::black);
    {QPainter paint(&frame);paint.fillRect(0,150,200,50,Qt::white);}
    animation.publish(frame);animation.publish(frame.copy());
    QVERIFY(backdrop.bottomScrim()>0.5);QCOMPARE(backdrop.topScrim(),0.0);
    const qreal needed=backdrop.bottomScrim();
    const auto ratio=scrimcontrast::minimumContrast({&backdrop.m_current.bottom},QColor(20,18,24),QColor(202,196,208),needed);
    QVERIFY2(ratio>=4.5,qPrintable(QString::number(ratio)));
    // The loop darkens again; the scrim it needed stays.
    frame.fill(Qt::black);animation.publish(frame);animation.publish(frame.copy());
    QCOMPARE(backdrop.bottomScrim(),needed);
    // New inputs start the worst case again from what is on screen.
    backdrop.setContrast(3);
    QCOMPARE(backdrop.bottomScrim(),0.0);
  }
};
QTEST_MAIN(ArtworkTest)
#include "artwork_test.moc"
