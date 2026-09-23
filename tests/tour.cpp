// A guided capture of every section of the interface. Each stop asserts that
// the section it photographs is really on screen, so a missing view fails the
// run instead of quietly producing an empty picture.
#include "uitest.h"
#include "tour.h"
#include "backend.h"
#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <qpa/qwindowsysteminterface.h>
#include <functional>

namespace {
QQuickItem *shownItem(QQuickItem *root, const QString &name) {
  if (!root->isVisible())
    return nullptr;
  if (root->objectName() == name)
    return root;
  for (auto child : root->childItems())
    if (auto found = shownItem(child, name))
      return found;
  return nullptr;
}

QQuickItem *shownButtonWithText(QQuickItem *root, const QString &text) {
  if (!root->isVisible())
    return nullptr;
  if (root->inherits("QQuickAbstractButton") && root->property("text").toString() == text)
    return root;
  for (auto child : root->childItems())
    if (auto found = shownButtonWithText(child, text))
      return found;
  return nullptr;
}

struct Tour {
  Backend *backend;
  QQuickWindow *window;
  QString directory;
  int failures = 0;
  int stop = 0;
  TourCapture capture;
  TourFinish complete;

  void check(bool ok, const QString &label) {
    fprintf(stdout, "%s %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
    fflush(stdout);
    if (!ok)
      ++failures;
  }
  bool until(const std::function<bool()> &predicate, int timeout = 8000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout)
      QTest::qWait(25);
    return predicate();
  }
  bool insideSettingsScroll(QQuickItem *row) {
    auto scroll = shownItem(window->contentItem(), "settingsScroll");
    return row && scroll && scroll->mapRectToScene(scroll->boundingRect()).contains(
        row->mapToScene(row->boundingRect().center()));
  }
  // Every stop names the section it expects to find before it photographs it.
  void shot(const QString &name, const QString &expect = {}, QQuickWindow *target = nullptr) {
    if (!target)
      target = window;
    QTest::qWait(capture ? 60 : 300);
    if (!expect.isEmpty())
      check(shownItem(target->contentItem(), expect), name + " shows " + expect);
    if (capture) {
      check(capture(target, name, ++stop), "capture " + name);
      return;
    }
    const auto file = QString("%1/%2-%3.png").arg(directory).arg(++stop, 2, 10, QChar('0')).arg(name);
    check(target->grabWindow().save(file), "capture " + name);
  }
  void click(QQuickItem *item, const QString &name) {
    check(item, "find " + name);
    if (!item)
      return;
    const auto point = item->mapToScene(item->boundingRect().center()).toPoint();
    QTest::mouseMove(window, point);
    QTest::qWait(60);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
    QTest::qWait(320);
  }
  void click(const QString &name) {
    click(shownItem(window->contentItem(), name), name);
  }
  void type(const QString &value) {
    for (const QChar letter : value)
      QTest::keyClick(window, letter.toLatin1());
  }
  QObject *dialog(const QString &name, const QString &method = "open") {
    auto found = window->findChild<QObject *>(name);
    check(found, "reach " + name);
    if (found) {
      QMetaObject::invokeMethod(found, qPrintable(method));
      QTest::qWait(420);
    }
    return found;
  }
  void closeDialog(QObject *target) {
    if (target)
      QMetaObject::invokeMethod(target, "close");
    QTest::qWait(320);
  }
  void finish() {
    if (complete)
      failures += complete();
    fprintf(stdout, "RESULT %d failures\n", failures);
    fflush(stdout);
    QCoreApplication::exit(failures ? 1 : 0);
  }
};

// A cover with enough contrast that the ambient backdrop and accent extraction
// have something real to work from.
void paintCover(const QString &path, const QColor &base, const QColor &accent) {
  QImage cover(640, 640, QImage::Format_RGB32);
  QPainter paint(&cover);
  paint.fillRect(cover.rect(), base);
  paint.setRenderHint(QPainter::Antialiasing);
  paint.fillRect(0, 0, 640, 240, accent);
  paint.setBrush(accent.lighter(130));
  paint.setPen(Qt::NoPen);
  paint.drawEllipse(QPoint(430, 430), 150, 150);
  paint.end();
  cover.save(path);
}

bool encodeTrack(Tour &c, const QString &path, const QString &title, const QString &album,
                 const QString &artist, int track) {
  QProcess encode;
  encode.start("ffmpeg", {"-nostdin", "-v", "error", "-f", "lavfi", "-i",
                          "anullsrc=r=8000:cl=mono", "-t", "150",
                          "-metadata", "title=" + title,
                          "-metadata", "album=" + album,
                          "-metadata", "artist=" + artist,
                          "-metadata", "album_artist=" + artist,
                          "-metadata", "date=2026",
                          "-metadata", "track=" + QString::number(track), path});
  const bool ok = encode.waitForFinished(20000) && encode.exitCode() == 0;
  c.check(ok, "generate " + QFileInfo(path).fileName());
  return ok;
}
} // namespace

void runGuidedTour(Backend *b, QQuickWindow *w, const TourCapture &capture,
                   const TourFinish &complete, bool motion) {
  Tour c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT"), 0, 0, capture, complete};
  QDir().mkpath(c.directory + "/music/Still Water");
  QDir().mkpath(c.directory + "/music/Night Ferry");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1440, 900);
  QTest::qWait(600);
  b->setTheme("dark");
  b->setMotion(motion);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);
  b->setLyricsFallback(false);

  // --- A small but real library to photograph ---
  paintCover(c.directory + "/music/Still Water/cover.png", QColor("#1f4f6b"), QColor("#d98324"));
  paintCover(c.directory + "/music/Night Ferry/cover.png", QColor("#3d2a52"), QColor("#4fa3a5"));
  const QStringList still{"The light arrives", "Across the still water", "A quiet moment"};
  const QStringList ferry{"Harbour lights", "Night ferry", "Coming ashore"};
  for (int i = 0; i < still.size(); ++i)
    if (!encodeTrack(c, QString("%1/music/Still Water/%2.flac").arg(c.directory).arg(i + 1, 2, 10, QChar('0')),
                     still[i], "Still Water", "Rill", i + 1))
      return c.finish();
  for (int i = 0; i < ferry.size(); ++i)
    if (!encodeTrack(c, QString("%1/music/Night Ferry/%2.flac").arg(c.directory).arg(i + 1, 2, 10, QChar('0')),
                     ferry[i], "Night Ferry", "Marble Coast", i + 1))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the tour library");
  b->library("files");
  c.check(c.until([&] { return b->results()->count() == 6; }), "six songs land in Local files");

  // Timed lyrics, so the lyric surfaces have real lines rather than a placeholder.
  QFile lrc(c.directory + "/tour.lrc");
  c.check(lrc.open(QIODevice::WriteOnly), "write the lyric fixture");
  lrc.write("[00:00]The light arrives\n[00:08]Across the still water\n[00:16]A quiet moment\n"
            "[00:24]We move with the tide\n[00:32]The evening settles\n[00:40]And the harbour goes quiet\n");
  lrc.close();

  b->enqueueItems(b->results()->rows);
  b->playAt(0);
  c.check(c.until([&] { return b->playing(); }), "local playback starts");
  b->importLyrics(QUrl::fromLocalFile(lrc.fileName()), b->current().value("id").toString());
  c.check(c.until([&] { return b->lyricLines().size() == 6; }), "timed lyrics load");
  b->seek(17000);
  QTest::qWait(400);

  // --- Home ---
  b->home();
  c.check(c.until([&] { return !b->busy(); }), "Home loads");
  w->setProperty("side", "");
  c.shot("home", "windowBackdrop");

  b->setTheme("light");
  QTest::qWait(500);
  c.shot("home-light", "windowBackdrop");
  b->setTheme("dark");
  QTest::qWait(500);

  // --- Search ---
  QMetaObject::invokeMethod(w, "focusSearch");
  QTest::qWait(300);
  b->search("aurora", "songs");
  c.check(c.until([&] { return !b->busy() && b->results()->count() > 0; }), "search returns results");
  c.shot("search", "searchFilters");

  // --- Library sections ---
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  c.check(c.until([&] { return b->results()->count() == 6; }), "Local files is ready to like from");
  for (int i = 0; i < 4; ++i)
    b->toggleLike(b->results()->get(i));
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("favorites")));
  c.check(c.until([&] { return b->results()->count() == 4; }), "liked songs collect");
  c.shot("library-liked", "libraryTabs");

  const auto playlist = b->createPlaylist("Evening drive");
  b->addItemsToPlaylist(playlist, b->results()->rows);
  b->saveSmartPlaylist({}, "Recent 2026", {{"yearFrom", 2026}, {"yearTo", 2026}});
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("playlists")));
  c.check(c.until([&] { return w->property("libraryTab") == "playlists"; }),
          "the library arrives on Playlists");
  b->setViewMode("grid");
  c.check(c.until([&] { return b->playlists().size() >= 2 && b->viewMode() == "grid"; }),
          "playlists appear in the cover grid");
  c.shot("library-playlists", "playlistGrid");

  b->openPlaylist(playlist);
  c.check(c.until([&] { return !b->busy() && b->collection()->count() == 4; }), "the playlist opens");
  c.shot("playlist-detail", "tracksView");

  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  c.check(c.until([&] { return b->results()->count() == 6; }), "Local files lists the library");
  c.shot("library-local-songs", "tracksView");

  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("local-albums")));
  c.check(c.until([&] { return b->results()->count() == 2; }), "two albums group");
  c.shot("library-local-albums", "localGroups");

  b->open(b->results()->get(0));
  c.check(c.until([&] { return !b->busy() && b->collection()->count() == 3; }), "an album opens");
  c.check(b->listPaneId() == "local-albums", "the album keeps its list pane");
  c.shot("album-detail", "tracksView");

  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("local-artists")));
  c.check(c.until([&] { return b->results()->count() == 2; }), "two artists group");
  c.shot("library-local-artists", "localGroups");

  b->open(b->results()->get(0));
  c.check(c.until([&] { return !b->busy() && b->collection()->count() == 3; }), "an artist opens");
  c.shot("artist-detail", "tracksView");

  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("mixes")));
  c.check(c.until([&] { return b->results()->count() > 0; }), "mixes are offered");
  c.shot("library-mixes", "tracksView");

  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("history")));
  c.check(c.until([&] { return !b->busy(); }), "history loads");
  c.shot("library-history", "libraryTabs");

  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("server")));
  QTest::qWait(500);
  c.shot("library-server-empty", "serverEmptyState");

  auto connection = c.dialog("serverConnectionDialog");
  c.shot("server-connection");
  c.closeDialog(connection);

  // --- The side panel, in each of its three modes ---
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  QTest::qWait(300);
  w->setProperty("side", "now");
  QTest::qWait(500);
  c.shot("side-now-playing", "nowBackdrop");

  w->setProperty("side", "lyrics");
  b->fetchLyrics();
  QTest::qWait(600);
  c.shot("side-lyrics", "sidePanel");

  w->setProperty("side", "queue");
  QTest::qWait(500);
  c.shot("side-queue", "queueView");
  w->setProperty("side", "");
  QTest::qWait(300);

  // --- Immersive player ---
  w->setProperty("immersive", true);
  c.check(c.until([&] { return shownItem(w->contentItem(), "immersivePlayer") != nullptr; }),
          "the immersive player loads");
  auto player = shownItem(w->contentItem(), "immersivePlayer");
  if (!player)
    return c.finish();
  auto layout = [&](const QString &key) {
    QMetaObject::invokeMethod(player, "layoutRequested", Q_ARG(QString, key));
    QTest::qWait(450);
  };
  layout("artwork");
  c.check(player->property("displayedLayout") == "artwork", "artwork layout applies");
  c.shot("immersive-artwork", "immersiveArtwork");

  layout("lyrics");
  c.check(c.until([&] { return player->property("displayedLayout") == "lyrics"; }), "lyrics layout applies");
  c.shot("immersive-lyrics", "immersivePlayer");

  layout("split");
  c.check(c.until([&] { return player->property("displayedLayout") == "split"; }), "split layout applies");
  c.shot("immersive-split", "immersivePlayer");

  QMetaObject::invokeMethod(player, "coverflowRequested", Q_ARG(bool, true));
  layout("artwork");
  c.check(c.until([&] { return shownItem(w->contentItem(), "immersiveCoverflow") != nullptr; }),
          "the up-next carousel appears");
  c.shot("immersive-coverflow", "immersiveCoverflow");
  QMetaObject::invokeMethod(player, "coverflowRequested", Q_ARG(bool, false));
  QTest::qWait(300);

  QMetaObject::invokeMethod(player, "queueRequested");
  QTest::qWait(600);
  c.shot("immersive-queue", "queueView");
  auto sheet = w->findChild<QObject *>("immersiveQueueSheet");
  c.closeDialog(sheet);
  w->setProperty("immersive", false);
  QTest::qWait(500);

  // --- Settings, one capture per category ---
  auto settings = c.dialog("settingsDialog");
  const QStringList categories{"appearance", "playback", "library", "connections", "privacy"};
  for (int i = 0; i < categories.size(); ++i) {
    if (settings)
      settings->setProperty("category", i);
    QTest::qWait(400);
    c.check(shownItem(w->contentItem(), "settingsCategory_" + QString::number(i)) != nullptr,
            "settings offers the " + categories[i] + " category");
    c.shot("settings-" + categories[i], "settingsOptions");
  }

  // Search puts the two appearance rows on screen before each real click.
  c.click("settingsSearch");
  c.type("Current view layout");
  QQuickItem *viewLayoutRow = nullptr;
  c.check(c.until([&] { viewLayoutRow = shownItem(w->contentItem(), "viewLayoutButton");
                       return c.insideSettingsScroll(viewLayoutRow); }),
          "settings search finds Current view layout");
  c.click(viewLayoutRow, "viewLayoutButton");
  auto viewLayout = w->findChild<QObject *>("viewLayoutDialog");
  c.check(viewLayout && viewLayout->property("visible").toBool(), "the view layout dialog opens from Settings");
  c.shot("view-layout");
  c.closeDialog(viewLayout);

  c.dialog("settingsDialog");
  c.click("settingsSearch");
  QTest::keyClick(w, Qt::Key_A, Qt::ControlModifier);
  c.type("Current artwork");
  QQuickItem *artworkRow = nullptr;
  c.check(c.until([&] { artworkRow = shownButtonWithText(w->contentItem(), "Current artwork");
                       return c.insideSettingsScroll(artworkRow); }),
          "settings search finds Current artwork");
  c.click(artworkRow, "Current artwork");
  auto artwork = w->findChild<QObject *>("artworkControls");
  c.check(artwork && artwork->property("visible").toBool(), "the artwork dialog opens from Settings");
  c.shot("artwork-controls");
  c.closeDialog(artwork);

  // --- Dialogs and secondary surfaces ---
  auto palette = c.dialog("commandPalette");
  c.shot("command-palette", "commandSearch");
  c.closeDialog(palette);

  auto shortcuts = c.dialog("shortcutHelp");
  c.shot("shortcut-help");
  c.closeDialog(shortcuts);

  b->home();
  c.check(c.until([&] { return !b->busy(); }), "Home reloads for its editor");
  auto editor = c.dialog("homeEditor");
  c.shot("home-editor");
  c.closeDialog(editor);

  auto sessions = c.dialog("sessionsDialog");
  c.shot("listening-sessions", "sessionsList");
  c.closeDialog(sessions);

  auto smart = w->findChild<QObject *>("smartPlaylistDialog");
  c.check(smart, "reach smartPlaylistDialog");
  if (smart) {
    QMetaObject::invokeMethod(smart, "edit", Q_ARG(QVariant, QVariant(QString())));
    QTest::qWait(450);
    c.shot("smart-playlist");
    c.closeDialog(smart);
  }

  auto details = w->findChild<QObject *>("trackDetailsDialog");
  c.check(details, "reach trackDetailsDialog");
  if (details) {
    QMetaObject::invokeMethod(details, "inspect", Q_ARG(QVariant, QVariant(b->current())));
    QTest::qWait(450);
    c.shot("track-details");
    c.closeDialog(details);
  }

  auto onboarding = c.dialog("onboarding");
  c.shot("onboarding");
  c.closeDialog(onboarding);
  b->setOnboarded(true);

  // --- Navigation in the top bar, and the two windows that change it ---
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("playlists")));
  QTest::qWait(200);
  b->openPlaylist(playlist);
  c.check(c.until([&] { return !b->busy() && !b->collectionItem().isEmpty(); }), "a collection is open to pin");
  b->togglePin(b->collectionItem());
  c.check(c.until([&] { return !b->pins().isEmpty(); }), "the collection is pinned");
  auto bar = shownItem(w->contentItem(), "navigationBar");
  c.check(bar, "the navigation bar is on screen");
  c.check(bar && bar->property("hugsContent").toBool(), "as the capsule the top bar centres");
  c.shot("navigation-capsule", "navigationBar");

  // Medium: still wide enough to centre the capsule between the two sides.
  w->resize(900, 700);
  QTest::qWait(600);
  c.check(bar && bar->property("hugsContent").toBool(), "a medium window keeps the capsule");
  c.shot("narrow-window", "navigationBar");

  // Compact, at the narrowest window Sung allows, which is where the bar has
  // least room and where its corners have the least width to round against.
  w->resize(480, 620);
  QTest::qWait(600);
  c.check(bar && !bar->property("hugsContent").toBool(), "a compact window spans the bar instead");
  c.shot("compact-window", "navigationBar");

  w->resize(1440, 900);
  QTest::qWait(500);

  // --- Mini player, in its own window ---
  QMetaObject::invokeMethod(w, "openMiniPlayer");
  QTest::qWait(900);
  auto mini = w->findChild<QQuickWindow *>("miniPlayerWindow");
  c.check(mini && mini->isVisible(), "the mini player opens");
  if (mini) {
    QTest::qWait(400);
    c.shot("mini-player", {}, mini);
  }
  QMetaObject::invokeMethod(w, "restorePlayer");
  QTest::qWait(600);

  b->stop();
  b->clearQueue();
  c.finish();
}

void runTourCapture(Backend *b, QQuickWindow *w) {
  runGuidedTour(b, w, {}, {}, true);
}
