#include "backend.h"
#include <QQuickWindow>
#include <QQuickItem>
#include <QSGRendererInterface>
#include <rhi/qrhi.h>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QNetworkDiskCache>
#include <QNetworkCacheMetaData>
#include <QStandardPaths>
#include <QBuffer>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QtTest>
#include <sys/resource.h>
#include <atomic>

static double cpuSeconds() {
  rusage u{}; getrusage(RUSAGE_SELF,&u);
  return u.ru_utime.tv_sec+u.ru_stime.tv_sec+(u.ru_utime.tv_usec+u.ru_stime.tv_usec)/1e6;
}
void runBenchmark(Backend *b,QQuickWindow *w) {
  const auto out=qEnvironmentVariable("SUNG_BENCH_OUTPUT");QDir().mkpath(out);
  // The runner fixes native window geometry before the workload starts.
  for(int i=0;i<100 && !QFile::exists(out+"/ready");++i)QTest::qWait(50);
  const auto mode=qEnvironmentVariable("SUNG_BENCH_MODE","playback");
  const int ms=qEnvironmentVariableIntValue("SUNG_BENCH_MS");
  b->setVolume(0); b->setTrackNotifications(false); b->setAutoplay(false);
  b->localTestSource(QUrl::fromLocalFile(qEnvironmentVariable("SUNG_BENCH_AUDIO")));
  for(int i=0;i<100 && (!b->playing()||b->duration()<100000);++i)QTest::qWait(50);
  if(!b->playing()||b->duration()<100000){qWarning("BENCHMARK playback fixture failed");QCoreApplication::exit(2);return;}
  b->fetchLyrics();
  for(int i=0;i<100 && b->lyricsBusy();++i)QTest::qWait(50);
  if(b->lyricLines().size()<20){qWarning("BENCHMARK lyrics fixture failed");QCoreApplication::exit(2);return;}
  if(qEnvironmentVariableIsSet("SUNG_BENCH_DISCARD_SCENE"))w->setPersistentSceneGraph(false);
  if(qEnvironmentVariableIsSet("SUNG_BENCH_DISCARD_GRAPHICS")){w->setPersistentSceneGraph(false);w->setPersistentGraphics(false);}
  if(qEnvironmentVariableIsSet("SUNG_BENCH_ART")) {
    if(!qEnvironmentVariableIsSet("SUNG_BENCH_ART_PRESEEDED")) {
    QNetworkDiskCache disk;
    disk.setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/art");
    for(int i=0;i<80;++i) {
      QImage art(544,544,QImage::Format_RGB32);
      for(int y=0;y<544;++y)for(int x=0;x<544;++x)art.setPixel(x,y,qRgb((x+i*19)%256,(y+i*37)%256,(x+y+i*11)%256));
      QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);art.save(&buffer,"PNG");
      QNetworkCacheMetaData meta;meta.setUrl(QUrl(QString("https://sung-benchmark.invalid/%1.png").arg(i)));
      meta.setExpirationDate(QDateTime::currentDateTimeUtc().addDays(1));
      meta.setRawHeaders({{"Content-Type","image/png"},{"Cache-Control","max-age=86400"}});
      if(auto device=disk.prepare(meta)){device->write(bytes);disk.insert(device);}
    }
    }
    // Existing player artwork may have requested the fixture before disk seeding.
    const auto reloadFixture=[](auto &&self,QQuickItem *item)->void {
      if(QByteArray(item->metaObject()->className())=="RoundedArt") {
        const auto source=item->property("source");
        if(source.toUrl().host()=="sung-benchmark.invalid") {
          item->setProperty("source",QUrl());item->setProperty("source",source);
        }
      }
      for(auto child:item->childItems())self(self,child);
    };
    reloadFixture(reloadFixture,w->contentItem());
    b->home();for(int i=0;i<100&&b->busy();++i)QTest::qWait(50);
    QTest::qWait(800);
  }
  // Render a warm frame even when the compositor keeps workspace 2 occluded.
  // Measurements explicitly report exposure; occluded FPS is not a performance gain.
  w->grabWindow();
  QQuickWindow *visible=w;
  if(mode=="lyrics")w->setProperty("side","lyrics");
  if(mode=="immersive")QMetaObject::invokeMethod(w,"toggleImmersive");
  if(mode=="mini"){
    QMetaObject::invokeMethod(w,"openMiniPlayer");
    visible=qobject_cast<QQuickWindow*>(w->property("miniPlayer").value<QObject*>());
    if(qEnvironmentVariableIsSet("SUNG_BENCH_DISCARD_SCENE"))visible->setPersistentSceneGraph(false);
  }
  b->pause();b->seek(60100);b->setMotion(false);QTest::qWait(700);
  visible->grabWindow();

  if(qEnvironmentVariableIsSet("SUNG_BENCH_SWEEP")) {
    for(int i=1;i<=95;++i) {b->seek(i*6000);QTest::qWait(25);visible->grabWindow();}
    b->seek(60100);QTest::qWait(250);
  }
  // Screenshots are taken after measurement, avoiding readback memory in RSS.
  b->setMotion(true);
  if(mode!="idle")b->play();
  if(mode=="hidden") {
    w->hide();
    if(auto idle=w->findChild<QTimer *>("renderResourceIdleTimer",Qt::FindDirectChildrenOnly))idle->start(1);
  }
  QTest::qWait(2000);
  std::atomic<int> frames{0},gpuSamples{0};std::atomic<double> gpuSeconds{0};
  auto gpuConnection=QObject::connect(visible,&QQuickWindow::afterFrameEnd,visible,[&]{
    auto swapchain=static_cast<QRhiSwapChain*>(visible->rendererInterface()->getResource(visible,QSGRendererInterface::RhiSwapchainResource));
    if(swapchain && swapchain->currentFrameCommandBuffer()){const auto elapsed=swapchain->currentFrameCommandBuffer()->lastCompletedGpuTime();if(elapsed>0){gpuSeconds.fetch_add(elapsed);++gpuSamples;}}
  },Qt::DirectConnection);
  auto connection=QObject::connect(visible,&QQuickWindow::frameSwapped,visible,[&]{++frames;},Qt::DirectConnection);
  QElapsedTimer timer;timer.start();const auto start=cpuSeconds();const auto position=b->position();
  // Use the real event loop. QTest::qWait polling would add artificial CPU wakeups.
  QEventLoop loop;QTimer finish;finish.setSingleShot(true);finish.setTimerType(Qt::PreciseTimer);
  QObject::connect(&finish,&QTimer::timeout,&loop,&QEventLoop::quit);
  finish.start(ms>0?ms:10000);loop.exec();
  const auto elapsed=timer.nsecsElapsed()/1e9;
  QObject::disconnect(connection);QObject::disconnect(gpuConnection);
  QJsonObject result{{"mode",mode},{"seconds",elapsed},{"cpu_percent_one_core",(cpuSeconds()-start)/elapsed*100},{"frames_per_second",frames.load()/elapsed},{"position_delta_ms",b->position()-position},{"width",visible->width()},{"height",visible->height()},{"dpr",visible->devicePixelRatio()},{"qobjects",w->findChildren<QObject*>().size()}};
  result["graphics_api"]=int(visible->rendererInterface()->graphicsApi());
  result["exposed"]=visible->isExposed();
  result["persistent_scene_graph"]=visible->isPersistentSceneGraph();
  if(gpuSamples>0){result["gpu_ms_per_frame"]=gpuSeconds.load()*1000/gpuSamples.load();result["gpu_samples"]=gpuSamples.load();}
  if(frames>0)result["cpu_ms_per_frame"]=(cpuSeconds()-start)*1000/frames.load();
  double uniqueKiB=0;
  QFile memory("/proc/self/smaps_rollup");if(memory.open(QIODevice::ReadOnly))for(const auto &line:memory.readAll().split('\n')){
    if(line.startsWith("Private_Clean:")||line.startsWith("Private_Dirty:"))uniqueKiB+=line.simplified().split(' ')[1].toDouble();
    if(line.startsWith("Rss:")||line.startsWith("Pss:")){auto fields=line.simplified().split(' ');result[fields[0]=="Rss:"?"rss_mib":"pss_mib"]=fields[1].toDouble()/1024;}
  }
  result["uss_mib"]=uniqueKiB/1024;
  QFile file(out+"/"+mode+".json");if(!file.open(QIODevice::WriteOnly)){QCoreApplication::exit(2);return;}file.write(QJsonDocument(result).toJson());
  b->pause();b->seek(60100);b->setMotion(false);QTest::qWait(700);
  if(visible)QTest::mouseMove(visible,QPoint(1,1));
  QTest::qWait(250);
  const auto resetWave=[](auto &&self,QQuickItem *item)->void {
    if(item->objectName()=="seekWave")item->setX(0);
    for(auto child:item->childItems())self(self,child);
  };
  if(visible){resetWave(resetWave,visible->contentItem());QTest::qWait(100);}
  if(visible && mode!="hidden")visible->grabWindow().save(out+"/"+mode+".png");
  QCoreApplication::quit();
}
