#include "roundedart.h"
#include <QBuffer>
#include <QDateTime>
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

class ArtworkTest : public QObject {
  Q_OBJECT
private slots:
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
  void videoOrientationAndFrameBounds() {
    QImage image(2,3,QImage::Format_RGB32);image.fill(Qt::red);image.setPixelColor(0,2,Qt::blue);
    QVideoFrame frame(image);frame.setRotation(QtVideo::Rotation::Clockwise90);
    MotionArtwork motion;motion.setRunning(true);motion.publishVideo(frame);
    QCOMPARE(motion.frame().size(),QSize(3,2));QCOMPARE(motion.frame().pixelColor(0,0),QColor(Qt::blue));
    frame.setMirrored(true);motion.publishVideo(frame);QCOMPARE(motion.frame().pixelColor(2,0),QColor(Qt::blue));
    QImage large(1600,800,QImage::Format_RGB32);large.fill(Qt::blue);motion.publishVideo(QVideoFrame(large));
    QCOMPARE(motion.frame().size(),QSize(800,400));
  }
  void localCoverIsBoundedAndShared() {
    QTemporaryDir dir;QImage picture(512,512,QImage::Format_RGB32);picture.fill(Qt::red);const auto path=dir.filePath("cover.jpg");QVERIFY(picture.save(path));
    RoundedArt a,b;a.setSource(QUrl::fromLocalFile(path));b.setSource(QUrl::fromLocalFile(path));QVERIFY(a.ready());QCOMPARE(a.m_image.constBits(),b.m_image.constBits());QVERIFY(a.m_image.width()<=360);
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
};
QTEST_MAIN(ArtworkTest)
#include "artwork_test.moc"
