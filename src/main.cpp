#include "backend.h"
#include "rowselection.h"
#include "desktoptheme.h"
#include "mpris.h"
#include "roundedart.h"
#include "windowresources.h"
#include <QDir>
#include <QCache>
#include <QMutex>
#include <QMutexLocker>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlNetworkAccessManagerFactory>
#include <QQuickImageProvider>
#include <QQuickPaintedItem>
#include <QEventLoop>
#include <QAbstractAnimation>
#include <QtMath>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QTimer>
#include <unistd.h>
#ifdef SUNG_DIAGNOSTICS
#include "uitest.h"
#include <QElapsedTimer>
void runBenchmark(Backend *, QQuickWindow *);
#endif
#include <cstdio>
#include <algorithm>
#include <functional>

class Symbols : public QQuickImageProvider {
public:
  Symbols() : QQuickImageProvider(QQuickImageProvider::Image) {}
  QImage requestImage(const QString &id, QSize *size,
                      const QSize &requested) override {
    auto parts = id.split('/');
    QString name = parts.value(0);
    if (name.contains(".."))
      return {};
    QSize s = !requested.isEmpty() ? requested : QSize(24, 24);
    s = s.boundedTo(QSize(256, 256));
    // Material draws each symbol at several optical sizes rather than scaling
    // one of them, so a glyph keeps its stroke weight wherever it is used. The
    // caller says how many points it is asking for, because the pixels it wants
    // depend on the display and the optical size does not.
    const int points = parts.value(1).toInt();
    if (points > 0 && points <= 22) name += "_20";
    else if (points > 32) name += "_40";
    // Cache the untinted raster, not the QML texture. Window remapping still gets
    // fresh textures, while theme transitions reuse the same SVG coverage mask.
    QMutexLocker lock(&m_mutex);
    const QString key=name+":"+QString::number(s.width())+"x"+QString::number(s.height());
    QImage img;
    if (const auto mask=m_masks.object(key)) img=*mask;
    else {
      QFile f(":/assets/icons/" + name + ".svg");
      // A symbol with no drawing at that optical size falls back to its own.
      if (!f.open(QIODevice::ReadOnly)) {
        f.setFileName(":/assets/icons/" + parts.value(0) + ".svg");
        if (!f.open(QIODevice::ReadOnly)) return {};
      }
      QSvgRenderer svg(f.readAll());
      img=QImage(s,QImage::Format_ARGB32_Premultiplied);
      img.fill(Qt::transparent);
      QPainter painter(&img);svg.render(&painter);painter.end();
      m_masks.insert(key,new QImage(img),int(img.sizeInBytes()));
    }
    QPainter p(&img);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(img.rect(), QColor("#" + parts.value(2, "ffffff")));
    p.end();
    if (size)
      *size = s;
    return img;
  }
private:
  QCache<QString,QImage> m_masks{512*1024};
  QMutex m_mutex;
};
int main(int argc, char **argv) {
#ifdef SUNG_DIAGNOSTICS
  QElapsedTimer startupTimer;startupTimer.start();
#endif
  if (!qEnvironmentVariableIsSet("QT_FFMPEG_DECODING_HW_DEVICE_TYPES"))
    qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", ",");
  if (!qEnvironmentVariableIsSet("QT_FFMPEG_ENCODING_HW_DEVICE_TYPES"))
    qputenv("QT_FFMPEG_ENCODING_HW_DEVICE_TYPES", ",");
  QGuiApplication app(argc, argv);
  app.setApplicationName("sung");
  app.setApplicationDisplayName("Sung");
  app.setOrganizationName("Sung");
  app.setApplicationVersion("0.12.0");
  app.setDesktopFileName("sung");
#ifdef SUNG_DIAGNOSTICS
  if(app.arguments().contains("--immersive-polish-test"))app.setDesktopFileName("sung-immersive-test");
#endif
  const auto args = app.arguments();
  if (args.contains("--version")) {
    fprintf(stdout, "Sung 0.12.0\n");
    return 0;
  }
  QLocalSocket peer;
  peer.connectToServer("sung-" + QString::number(getuid()));
  if (!args.contains("--isolated") && peer.waitForConnected(120)) {
    peer.write(args.size() > 1 ? args.last().toUtf8() : QByteArray("raise"));
    peer.flush();
    peer.waitForBytesWritten(150);
    return 0;
  }
  QLocalServer server;
  if (!args.contains("--isolated")) {
    QLocalServer::removeServer("sung-" + QString::number(getuid()));
    server.setSocketOptions(QLocalServer::UserAccessOption);
    server.listen("sung-" + QString::number(getuid()));
  }
  QQuickStyle::setStyle("Basic");
  QFont font(QFontDatabase::families().contains("Google Sans Flex")
                 ? "Google Sans Flex"
                 : "Noto Sans");
  font.setPixelSize(14);
  app.setFont(font);
  qmlRegisterType<RowSelection>("Sung.Native", 1, 0, "RowSelection");
  qmlRegisterType<RoundedArt>("Sung.Native", 1, 0, "RoundedArt");
  MotionArtwork motionArtwork;
  qmlRegisterUncreatableType<MotionArtwork>("Sung.Native",1,0,"MotionArtwork","Shared current artwork");
  Backend backend;
  // Tests, smoke runs and screenshots start from a clean profile; none of
  // them should have to dismiss the first-run flow.
  if (args.contains("--isolated")) backend.setOnboarded(true);
  RoundedArt::resolveServerArt=[&backend](const QUrl &url){return backend.server()->artworkRequest(url);};
  RoundedArt::resolveVideoFrame=[&backend](const QUrl &frame){return backend.albumCoverFor(frame);};
  QObject::connect(&backend,&Backend::videoCoversChanged,&app,[]{RoundedArt::refreshFrames();});
  QObject::connect(backend.server(),&MusicServer::accountChanged,&app,[]{RoundedArt::clearCaches();});
  DesktopTheme desktopTheme;
  QObject::connect(&backend,&Backend::artworkCacheCleared,&app,[]{RoundedArt::clearCaches();});
  bool exposeMpris = !args.contains("--isolated");
#ifdef SUNG_DIAGNOSTICS
  exposeMpris = exposeMpris || args.contains("--mpris-test");
#endif
  if (exposeMpris) registerMpris(&backend);
  WindowResources windowResources;
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("windowResources", &windowResources);
  engine.addImageProvider("symbols", new Symbols);
  engine.rootContext()->setContextProperty("app", &backend);
  engine.rootContext()->setContextProperty("motionArtwork", &motionArtwork);
  engine.rootContext()->setContextProperty("desktopTheme", &desktopTheme);
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
  engine.load(QUrl("qrc:/qml/Main.qml"));
  if (engine.rootObjects().isEmpty())
    return 1;
  auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
  QObject::connect(&backend, &Backend::raiseRequested, window, [window] {
    QMetaObject::invokeMethod(window,"restorePlayer");
    window->raise();
    window->requestActivate();
  });
  if(args.contains("--mini"))QTimer::singleShot(0,window,[window]{QMetaObject::invokeMethod(window,"openMiniPlayer");});
  QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
    auto socket = server.nextPendingConnection();
    QObject::connect(socket, &QLocalSocket::readyRead, &backend, [&, socket] {
      auto v = QString::fromUtf8(socket->readAll());
      if (v.startsWith("https://"))
        backend.openLink(v);
      if(v=="--mini")QMetaObject::invokeMethod(window,"openMiniPlayer");
      else emit backend.raiseRequested();
      socket->disconnectFromServer();
    });
    QObject::connect(socket, &QLocalSocket::disconnected, socket,
                     &QObject::deleteLater);
  });
#ifdef SUNG_DIAGNOSTICS
  if (qEnvironmentVariableIsSet("SUNG_STARTUP_PROBE")) {
    fprintf(stdout,"STARTUP_READY_MS %.3f\n",startupTimer.nsecsElapsed()/1e6);fflush(stdout);
    QObject::connect(window,&QQuickWindow::frameSwapped,&app,[&] {
      fprintf(stdout,"STARTUP_FRAME_MS %.3f\n",startupTimer.nsecsElapsed()/1e6);fflush(stdout);
      app.quit();
    },Qt::QueuedConnection);
    QTimer::singleShot(10000,&app,[&]{app.exit(2);});
    return app.exec();
  }
  // What the interface actually builds, once the first frame is up. The value
  // of SUNG_OBJECT_PROBE is the smallest subtree worth naming, so a large
  // number reports only the totals. Parts that are meant to be built on demand
  // should be absent here until something asks for them, and a painted
  // surface's render target should be no larger than the picture in it.
  if (qEnvironmentVariableIsSet("SUNG_OBJECT_PROBE")) {
    const int floor = qEnvironmentVariableIntValue("SUNG_OBJECT_PROBE");
    QMetaObject::Connection objectProbe;
    objectProbe=QObject::connect(window,&QQuickWindow::frameSwapped,&app,[&] {
      // Opening a popup below renders more frames inside the settling loop.
      // Observe only the first frame, rather than recursively reopening it.
      QObject::disconnect(objectProbe);
      // SUNG_PROBE_OPEN names a dialog to open first, which is how a deferred
      // part is confirmed to arrive when it is finally asked for.
      if (qEnvironmentVariableIsSet("SUNG_PROBE_OPEN")) {
        const auto name = qEnvironmentVariable("SUNG_PROBE_OPEN");
        if (auto *dialog = window->findChild<QObject *>(name)) {
          QElapsedTimer opening;opening.start();
          QMetaObject::invokeMethod(dialog,"open");
          fprintf(stdout,"DIALOG_OPEN_MS %s %.3f\n",qPrintable(name),opening.nsecsElapsed()/1e6);
          QEventLoop settle;QTimer::singleShot(900,&settle,&QEventLoop::quit);settle.exec();
          fprintf(stdout,"OPENED %s visible=%d built=%d headline=%d\n",qPrintable(name),
                  dialog->property("visible").toBool(),dialog->property("built").toBool(),
                  window->findChild<QQuickItem *>("dialogTitle") != nullptr);
        } else fprintf(stdout,"OPENED %s NOT FOUND\n",qPrintable(name));
      }
      const qreal dpr = window->effectiveDevicePixelRatio();
      QHash<QByteArray,int> tally;int total=0,painted=0;double targets=0;
      std::function<void(QObject *)> walk=[&](QObject *o){
        ++total;++tally[o->metaObject()->className()];
        if (auto *surface=qobject_cast<QQuickPaintedItem *>(o)) {
          ++painted;
          const QSize size = surface->textureSize().isEmpty()
              ? QSize(qCeil(surface->width()*dpr),qCeil(surface->height()*dpr))
              : surface->textureSize();
          const double mib = size.width()*double(size.height())*4/1048576.0;
          targets += mib;
          if (surface->width()>0 && surface->height()>0)
            fprintf(stdout,"  PAINTED %-22s %4.0fx%-4.0f target %4dx%-4d %6.2f MiB  shown=%d pixels=%d blur=%d radius=%.0f\n",
                    qPrintable(surface->objectName().isEmpty()
                               ? QString::fromLatin1(surface->metaObject()->className())
                               : surface->objectName()),
                    surface->width(),surface->height(),size.width(),size.height(),mib,
                    surface->isVisible(),surface->property("pixels").toInt(),
                    surface->property("blur").toInt(),surface->property("radius").toDouble());
        }
        for (auto *child:o->children()) walk(child);
      };
      for (auto *root:engine.rootObjects()) walk(root);
      // The same counts arranged as a tree, so a heavy part can be located
      // rather than guessed at from its type alone.
      std::function<int(QObject *,int)> branch=[&](QObject *o,int depth)->int{
        int n=1;QList<QPair<int,QObject *>> kids;
        for (auto *child:o->children()) {const int k=branch(child,-1);n+=k;kids.append({k,child});}
        if (depth>=0) {
          std::sort(kids.begin(),kids.end(),[](auto &a,auto &b){return a.first>b.first;});
          for (const auto &[k,child]:kids) {
            if (k<floor) continue;
            fprintf(stdout,"%*s%5d %s %s\n",depth*2+2,"",k,child->metaObject()->className(),
                    qPrintable(child->objectName()));
            branch(child,depth+1);
          }
        }
        return n;
      };
      fprintf(stdout,"SUBTREES\n");
      for (auto *root:engine.rootObjects()) branch(root,0);
      QList<QPair<int,QByteArray>> ranked;
      for (auto it=tally.cbegin();it!=tally.cend();++it) ranked.append({it.value(),it.key()});
      std::sort(ranked.begin(),ranked.end(),[](auto &a,auto &b){return a.first>b.first;});
      fprintf(stdout,"OBJECTS_TOTAL %d\nPAINTED_ITEMS %d\nPAINTED_TARGET_MIB %.2f\n",total,painted,targets);
      for (int i=0;i<qMin(30,int(ranked.size()));++i)
        fprintf(stdout,"  %5d %s\n",ranked[i].first,ranked[i].second.constData());
      fflush(stdout);app.quit();
    },Qt::QueuedConnection);
    QTimer::singleShot(10000,&app,[&]{app.exit(2);});
    return app.exec();
  }
  // What keeps the process awake while nothing is happening. Counts the frames
  // drawn over the window given in SUNG_IDLE_PROBE seconds, then names every
  // animation and timer still running, so an idle cost can be traced to the
  // thing causing it rather than guessed at.
  if (qEnvironmentVariableIsSet("SUNG_IDLE_PROBE")) {
    const int seconds = qMax(2, qEnvironmentVariableIntValue("SUNG_IDLE_PROBE"));
    auto *frames = new int(0);
    QObject::connect(window, &QQuickWindow::frameSwapped, &app, [frames] { ++*frames; });
    QTimer::singleShot(seconds * 1000, &app, [&app, &engine, frames, seconds] {
      fprintf(stdout, "IDLE_FRAMES %d over %ds (%.1f/s)\n", *frames, seconds, *frames / double(seconds));
      const auto path = [](QObject *o) {
        QStringList parts;
        for (QObject *n = o; n && parts.size() < 5; n = n->parent())
          parts.prepend(n->objectName().isEmpty() ? QString::fromLatin1(n->metaObject()->className())
                                                  : n->objectName());
        return parts.join('/');
      };
      // A QML animation is not a QAbstractAnimation; QQuickAbstractAnimation
      // owns one. So the declarative types are found by their own property.
      QHash<QString, int> animations, timers;
      std::function<void(QObject *)> walk = [&](QObject *o) {
        const QByteArray type = o->metaObject()->className();
        if (auto *a = qobject_cast<QAbstractAnimation *>(o)) {
          if (a->state() == QAbstractAnimation::Running)
            ++animations[QString("%1  [%2]").arg(path(o), QString::fromLatin1(type))];
        } else if (auto *t = qobject_cast<QTimer *>(o)) {
          if (t->isActive()) ++timers[QString("%1 every %2ms").arg(path(o)).arg(t->interval())];
        } else if (o->metaObject()->indexOfProperty("running") >= 0 &&
                   o->property("running").toBool()) {
          if (type.contains("Timer"))
            ++timers[QString("%1 every %2ms").arg(path(o)).arg(o->property("interval").toInt())];
          else if (type.contains("Animation") || type.contains("Behavior") ||
                   type.contains("Transition"))
            ++animations[QString("%1  [%2]").arg(path(o), QString::fromLatin1(type))];
        }
        for (auto *child : o->children()) walk(child);
      };
      for (auto *root : engine.rootObjects()) walk(root);
      fprintf(stdout, "RUNNING_ANIMATIONS %d\n", int(animations.size()));
      for (auto it = animations.cbegin(); it != animations.cend(); ++it)
        fprintf(stdout, "  %dx %s\n", it.value(), qPrintable(it.key()));
      fprintf(stdout, "RUNNING_TIMERS %d\n", int(timers.size()));
      for (auto it = timers.cbegin(); it != timers.cend(); ++it)
        fprintf(stdout, "  %dx %s\n", it.value(), qPrintable(it.key()));
      fflush(stdout);
      app.quit();
    });
    return app.exec();
  }
  if (args.contains("--benchmark")) {
    QTimer::singleShot(0, &app, [&] { runBenchmark(&backend, window); });
    return app.exec();
  }
  if(args.contains("--immersive-edges-test")){QTimer::singleShot(0,&app,[&]{runImmersiveEdgeTests(&backend,window);});return app.exec();}
  if(args.contains("--immersive-preferences-test")){QTimer::singleShot(0,&app,[&]{runImmersivePreferencesTest(&backend,window);});return app.exec();}
  if(args.contains("--material-emphasis-test")){QTimer::singleShot(0,&app,[&]{runMaterialEmphasisTests(&backend,window);});return app.exec();}
  if(args.contains("--material-anatomy-test")){QTimer::singleShot(0,&app,[&]{runMaterialAnatomyTests(&backend,window);});return app.exec();}
  if(args.contains("--material-controls-test")){QTimer::singleShot(0,&app,[&]{runMaterialControlsTests(&backend,window);});return app.exec();}
  if(args.contains("--material-scale-test")){QTimer::singleShot(0,&app,[&]{runMaterialScaleTests(&backend,window);});return app.exec();}
  if(args.contains("--material-grain-test")){QTimer::singleShot(0,&app,[&]{runMaterialGrainTests(&backend,window);});return app.exec();}
  if(args.contains("--material-scheme-test")){QTimer::singleShot(0,&app,[&]{runMaterialSchemeTests(&backend,window);});return app.exec();}
  if(args.contains("--material-sizing-test")){QTimer::singleShot(0,&app,[&]{runMaterialSizingTests(&backend,window);});return app.exec();}
  if(args.contains("--material-expressive-test")){QTimer::singleShot(0,&app,[&]{runMaterialExpressiveTests(&backend,window);});return app.exec();}
  if(args.contains("--material-detail-test")){QTimer::singleShot(0,&app,[&]{runMaterialDetailTests(&backend,window);});return app.exec();}
  if(args.contains("--material-components-test")){QTimer::singleShot(0,&app,[&]{runMaterialComponentTests(&backend,window);});return app.exec();}
  if(args.contains("--material-foundations-test")){QTimer::singleShot(0,&app,[&]{runMaterialFoundationTests(&backend,window);});return app.exec();}
  if(args.contains("--window-wash-test")){QTimer::singleShot(0,&app,[&]{runWindowWashTests(&backend,window);});return app.exec();}
  if(args.contains("--playlist-versions-test")){QTimer::singleShot(0,&app,[&]{runPlaylistVersionsTests(&backend,window);});return app.exec();}
  if(args.contains("--listening-stats-test")){QTimer::singleShot(0,&app,[&]{runListeningStatsTests(&backend,window);});return app.exec();}
  if(args.contains("--queue-history-test")){QTimer::singleShot(0,&app,[&]{runQueueHistoryTests(&backend,window);});return app.exec();}
  if(args.contains("--track-details-test")){QTimer::singleShot(0,&app,[&]{runTrackDetailsTests(&backend,window);});return app.exec();}
  if(args.contains("--crossfade-ui-test")){QTimer::singleShot(0,&app,[&]{runCrossfadeUiTests(&backend,window);});return app.exec();}
  if(args.contains("--singalong-test")){QTimer::singleShot(0,&app,[&]{runSingAlongTests(&backend,window);});return app.exec();}
  if(args.contains("--artist-hero-test")){QTimer::singleShot(0,&app,[&]{runArtistHeroTests(&backend,window);});return app.exec();}
  if(args.contains("--navigation-motion-test")){QTimer::singleShot(0,&app,[&]{runNavigationMotionTests(&backend,window);});return app.exec();}
  if(args.contains("--dynamic-color-test")){QTimer::singleShot(0,&app,[&]{runDynamicColorTests(&backend,window);});return app.exec();}
  if(args.contains("--tour")){QTimer::singleShot(0,&app,[&]{runTourCapture(&backend,window);});return app.exec();}
  if(args.contains("--interface-audit-test")){QTimer::singleShot(0,&app,[&]{runInterfaceAuditTests(&backend,window);});return app.exec();}
  if(args.contains("--playback-memory-test")){QTimer::singleShot(0,&app,[&]{runPlaybackMemoryTests(&backend,window);});return app.exec();}
  if(args.contains("--library-exchange-test")){QTimer::singleShot(0,&app,[&]{runLibraryExchangeTests(&backend,window);});return app.exec();}
  if(args.contains("--backdrop-pulse-test")){QTimer::singleShot(0,&app,[&]{runBackdropPulseTests(&backend,window);});return app.exec();}
  if(args.contains("--home-rail-test")){QTimer::singleShot(0,&app,[&]{runHomeRailTests(&backend,window);});return app.exec();}
  if(args.contains("--onboarding-test")){QTimer::singleShot(0,&app,[&]{runOnboardingTests(&backend,window);});return app.exec();}
  if(args.contains("--ambient-immersive-test")){QTimer::singleShot(0,&app,[&]{runAmbientImmersiveTests(&backend,window);});return app.exec();}
  if(args.contains("--personalization-test")){QTimer::singleShot(0,&app,[&]{runPersonalizationTests(&backend,window);});return app.exec();}
  if(args.contains("--immersive-polish-test")){QTimer::singleShot(0,&app,[&]{runImmersivePolishTests(&backend,window);});return app.exec();}
  if(args.contains("--interaction-refinement-test")){QTimer::singleShot(0,&app,[&]{runInteractionRefinementTests(&backend,window);});return app.exec();}
  if(args.contains("--listening-refinement-test")){QTimer::singleShot(0,&app,[&]{runListeningRefinementTests(&backend,window);});return app.exec();}
  if(args.contains("--visual-refinement-test")){QTimer::singleShot(0,&app,[&]{runVisualRefinementTests(&backend,window);});return app.exec();}
  if(args.contains("--playback-polish-test")){QTimer::singleShot(0,&app,[&]{runPlaybackPolishTests(&backend,window);});return app.exec();}
  if(args.contains("--library-polish-test")){QTimer::singleShot(0,&app,[&]{runLibraryPolishTests(&backend,window);});return app.exec();}
  if(args.contains("--product-polish-test")){QTimer::singleShot(0,&app,[&]{runProductPolishTests(&backend,window);});return app.exec();}
  if(args.contains("--server-remote-test")){QTimer::singleShot(0,&app,[&]{runRemoteServerTest(&backend,window);});return app.exec();}
  if(args.contains("--server-test") || args.contains("--jellyfin-test")){QTimer::singleShot(0,&app,[&]{runServerTests(&backend,window);});return app.exec();}
  if(args.contains("--online-artwork-live-test")){QTimer::singleShot(0,&app,[&]{runOnlineArtworkLiveTests(&backend,window);});return app.exec();}
  if(args.contains("--online-artwork-test")){QTimer::singleShot(0,&app,[&]{runOnlineArtworkTests(&backend,window);});return app.exec();}
  if(args.contains("--local-artwork-test")){QTimer::singleShot(0,&app,[&]{runLocalArtworkTests(&backend,window);});return app.exec();}
  if(args.contains("--folder-import-test")){QTimer::singleShot(0,&app,[&]{runFolderImportTests(&backend,window);});return app.exec();}
  if(args.contains("--interaction-test")){QTimer::singleShot(0,&app,[&]{runInteractionTests(&backend,window);});return app.exec();}
  if(args.contains("--audio-indicator-test")){QTimer::singleShot(0,&app,[&]{runAudioIndicatorTests(&backend,window);});return app.exec();}
  if(args.contains("--visual-delight-test")){QTimer::singleShot(0,&app,[&]{runVisualDelightTests(&backend,window);});return app.exec();}
  if(args.contains("--library-qol-test")){QTimer::singleShot(0,&app,[&]{runLibraryQolTests(&backend,window);});return app.exec();}
  if(args.contains("--qol-test")){QTimer::singleShot(0,&app,[&]{runQolTests(&backend,window);});return app.exec();}
  if(args.contains("--visual-polish-test")){QTimer::singleShot(0,&app,[&]{runVisualPolishTests(&backend,window);});return app.exec();}
  if(args.contains("--search-selection-test")){QTimer::singleShot(0,&app,[&]{runSearchSelectionTests(&backend,window);});return app.exec();}
  if (args.contains("--features-test")) {
    QTimer::singleShot(0, &app, [&] { runFeatureTests(&backend, window); });
    return app.exec();
  }
  if (args.contains("--lyrics-test")) {
    QTimer::singleShot(0, &app, [&] { runLyricsTests(&backend, window); });
    return app.exec();
  }
  if (args.contains("--recovery-test")) {
    QTimer::singleShot(0, &app, [&] { runRecoveryTests(&backend, window); });
    return app.exec();
  }
  if (args.contains("--audit")) {
    QTimer::singleShot(0, &app, [&] { runUiAudit(&backend, window); });
    return app.exec();
  }
  if (args.contains("--ui-test")) {
    QTimer::singleShot(0, &app, [&] { runUiTests(&backend, window); });
    return app.exec();
  }
#endif
  if (args.contains("--smoke-local")) {
    const int i = args.indexOf("--smoke-local");
    if (i + 1 < args.size())
      backend.localTestSource(QUrl::fromLocalFile(args[i + 1]));
    QTimer::singleShot(4000, &app, [&] {
      bool ok = backend.playing() && backend.position() > 500;
      qInfo() << "LOCAL_PLAYBACK" << ok << backend.position();
      app.exit(ok ? 0 : 2);
    });
  } else if (args.contains("--smoke-stream")) {
    backend.setVolume(0.08);
    backend.playItem({{"id", "0_M2JX-Olv8"},
                      {"videoId", "0_M2JX-Olv8"},
                      {"kind", "song"},
                      {"title", "Feather"},
                      {"artist", "Nujabes"}});
    auto timer = new QTimer(&app);
    timer->setInterval(1000);
    QObject::connect(timer, &QTimer::timeout, &app, [&] {
      if (backend.position() > 3000) {
        fprintf(stdout, "STREAM_PLAYBACK %d %lld\n", backend.playing(),
                static_cast<long long>(backend.position()));
        fflush(stdout);
        app.exit(backend.playing() ? 0 : 2);
      }
    });
    timer->start();
    QTimer::singleShot(85000, &app, [&] {
      qInfo() << "STREAM_FAILED" << backend.error();
      app.exit(2);
    });
  } else if (!args.contains("--offline")) {
    bool linked = false;
    for (const auto &a : args)
      if (a.startsWith("https://")) {
        backend.openLink(a);
        linked = true;
        break;
      }
    if (!linked)
      QTimer::singleShot(0, &backend, &Backend::openStartPage);
  }
  if (args.contains("--screenshot")) {
    const int i = args.indexOf("--screenshot");
    const auto path = args.value(i + 1);
    QTimer::singleShot(2500, &app,
                       [window, path] { window->grabWindow().save(path); });
    QTimer::singleShot(3000, &app, &QCoreApplication::quit);
  }
  return app.exec();
}
