#include "uitest.h"
#include "backend.h"
#include "rowselection.h"
#include <QDir>
#include <QDataStream>
#include <qpa/qwindowsysteminterface.h>
#include <QFile>
#include <QQuickItem>
#include <QJSValue>
#include <QFontInfo>
#include <QQuickWindow>
#include <QWheelEvent>
#include <QtTest>
#include <cstdio>
#include <functional>

static QQuickItem *findItem(QQuickItem *root, const QString &name) {
  if (root->objectName() == name)
    return root;
  for (auto child : root->childItems())
    if (auto found = findItem(child, name))
      return found;
  return nullptr;
}
static bool validIconSizes(QQuickItem *item) {
  if (item->objectName()=="materialIcon" && (qAbs(item->width()-item->property("size").toReal())>.5 || qAbs(item->height()-item->property("size").toReal())>.5)) return false;
  for(auto child:item->childItems()) if(!validIconSizes(child)) return false;
  return true;
}
void runUiTests(Backend *b, QQuickWindow *w) {
  int failures = 0;
  const QString dir = qEnvironmentVariable("SUNG_TEST_OUTPUT");
  QDir().mkpath(dir);
  auto check = [&](bool ok, const char *name) {
    fprintf(stdout, "%s %s\n", ok ? "PASS" : "FAIL", name);
    fflush(stdout);
    if (!ok)
      ++failures;
  };
  auto until = [&](std::function<bool()> predicate, int timeout = 20000) {
    QElapsedTimer t;
    t.start();
    while (!predicate() && t.elapsed() < timeout)
      QTest::qWait(50);
    return predicate();
  };
  auto shot = [&](const QString &name) {
    QTest::mouseMove(w,QPoint(3,3));
    QTest::qWait(700);
    check(w->grabWindow().save(dir + "/" + name + ".png"),
          qPrintable("capture " + name));
  };
  auto click = [&](const QString &name) {
    auto item = findItem(w->contentItem(), name);
    if (!item) {
      check(false, qPrintable("find " + name));
      return;
    }
    auto point =
        item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, point.toPoint());
    QTest::qWait(150);
  };
  w->resize(1180, 800);
  auto label=findItem(w->contentItem(),"sungText");
  check(label && QFontInfo(label->property("font").value<QFont>()).family()=="Google Sans Flex","Google Sans Flex rendered font");
  b->setTheme("dark");
  b->setMotion(true);
  b->setAutoplay(false);
  b->home();
  check(until([&] { return !b->busy(); }), "home request finished");
  check(!b->sections().isEmpty(), "live home shelves");
  QTest::qWait(2000);
  shot("home-dark");
  click("nav_search");
  auto search = findItem(w->contentItem(), "searchField");
  check(search && search->hasActiveFocus(), "search navigation focuses field");
  if (search) {
    search->setProperty("text", "Nujabes Feather");
    QTest::keyClick(w, Qt::Key_Return);
  }
  check(until([&] { return !b->busy() && b->page() == "search"; }),
        "search via keyboard");
  check(b->results()->count() > 0, "live song search");
  QTest::qWait(800);
  shot("search-dark");
  if (b->results()->count()) {
    b->setVolume(0.08);
    b->playResults();
    check(until([&] { return b->playing() && b->position() > 1500; }, 80000),
          "native streamed audio progresses");
    shot("playing-dark");
    check(validIconSizes(w->contentItem()), "active audio icons retain their intended size");
    click("playButton");
    check(!b->playing(), "player pause button");
    auto p = b->position();
    QTest::qWait(350);
    check(qAbs(b->position() - p) < 200, "pause holds position");
    b->seek(30000);
    check(until([&] { return b->position() >= 29500; }, 5000), "stream seek");
    click("playButton");
    check(until([&] { return b->playing() && b->position() > 31000; }, 10000),
          "resume after seek");
    b->pause();
    b->toggleLike(b->current());
    check(b->liked(), "favorite current track");
    const auto id = b->createPlaylist("Evening rotation");
    b->addToPlaylist(id, b->current());
    b->openPlaylist(id);
    check(b->results()->count() == 1, "local playlist stores song");
    b->library("history");
    check(b->results()->count()>0,"playback adds history");
    b->clearHistory();
    check(b->results()->count()==0,"clear history");
    b->undo();
    check(b->results()->count()>0,"undo restores history");
    b->back();
    click("queueButton");
    check(w->property("side").toString() == "queue",
          "queue button opens panel");
    check(validIconSizes(w->contentItem()), "playing row icons stay bounded");
    shot("queue-dark");
    w->setProperty("side", "now");
    shot("now-dark");
    b->fetchLyrics();
    check(until([&] { return !b->lyricsBusy(); }), "lyrics request finished");
    check(!b->lyrics().isEmpty(), "live lyrics");
    w->setProperty("side", "lyrics");
    shot("lyrics-dark");
  }
  w->setProperty("side", "");
  b->setTheme("light");
  shot("search-light");
  click("settingsButton");
  shot("settings-light");
  QTest::keyClick(w, Qt::Key_Escape);
  w->resize(800, 600);
  b->setTheme("dark");
  w->setProperty("side", "queue");
  shot("narrow-queue");
  w->setProperty("side", "");
  click("nav_library");
  check(w->property("destination").toString() == "library",
        "library navigation");
  shot("library-narrow");
  check(validIconSizes(w->contentItem()), "icons retain logical sizes at device scale");
  auto nav=findItem(w->contentItem(),"nav_home");
  if(nav) check(qAbs(nav->mapToScene(QPointF(nav->width()/2,0)).x()-44)<1,"navigation centered in 88px rail");
  b->pause();
  b->save();
  fprintf(stdout, "RESULT %d failures\n", failures);
  fflush(stdout);
  QCoreApplication::exit(failures ? 2 : 0);
}

#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
void runUiAudit(Backend *b, QQuickWindow *w) {
  const QString dir = qEnvironmentVariable("SUNG_TEST_OUTPUT");
  QDir().mkpath(dir);
  int failures = 0;
  auto check = [&](bool good, const QString &label) {
    fprintf(stdout, "%s %s\n", good ? "PASS" : "FAIL", qPrintable(label));
    fflush(stdout);
    if (!good)
      ++failures;
  };
  auto wait = [&] {
    QElapsedTimer time;
    time.start();
    while (b->busy() && time.elapsed() < 47000)
      QTest::qWait(50);
    check(!b->busy(),"catalog request completes within watchdog");
    QTest::qWait(700);
  };
  auto shot = [&](const QString &name) {
    QTest::mouseMove(w, QPoint(3, 3));
    QTest::qWait(750);
    check(w->grabWindow().save(dir + "/" + name + ".png"), "capture " + name);
  };
  auto click = [&](const QString &name) {
    auto item = findItem(w->contentItem(), name);
    if (!item) {
      check(false, "find " + name);
      return;
    }
    QTest::mouseClick(
        w, Qt::LeftButton, Qt::NoModifier,
        item->mapToScene(QPointF(item->width() / 2, item->height() / 2))
            .toPoint());
    QTest::qWait(350);
  };
  auto popup = [&](const QString &name) {
    return w->findChild<QObject *>(name);
  };
  auto query = [&](const QString &q, const QString &filter) {
    w->setProperty("destination", "search");
    w->setProperty("filter", filter);
    auto field = findItem(w->contentItem(), "searchField");
    if (field)
      field->setProperty("text", q);
    b->search(q, filter);
    wait();
  };
  w->resize(1180, 800);
  b->setTheme("dark");
  b->setMotion(false);
  b->setAutoplay(false);
  b->clearQueue();
  b->home();
  wait();
  shot("01-home");
  query("Nujabes", "songs");
  shot("02-search-songs");
  auto song = b->results()->get(0);
  b->enqueue(song);
  if (!b->isLiked(song.value("id").toString()))
    b->toggleLike(song);
  query("Nujabes", "albums");
  shot("03-search-albums");
  if (b->results()->count()) {
    b->open(b->results()->get(0));
    wait();
    shot("04-album");
  }
  query("Nujabes", "artists");
  check(b->results()->count()>0 && b->results()->get(0).value("title").toString() != "Untitled", "artist search names");
  shot("05-search-artists");
  if (b->results()->count()) {
    b->open(b->results()->get(0));
    wait();
    shot("06-artist");
  }
  query("Jazz", "playlists");
  shot("07-search-playlists");
  if (b->results()->count()) {
    b->open(b->results()->get(0));
    wait();
    shot("08-online-playlist");
  }
  query("Nujabes Feather", "videos");
  shot("09-search-videos");
  click("nav_library");
  shot("10-liked-songs");
  click("historyTab");
  shot("11-history");
  click("playlistsTab");
  shot("12-playlists-empty");
  click("newPlaylistButton");
  auto field = findItem(w->contentItem(), "playlistName");
  check(field != nullptr, "playlist field is accessible");
  if (field)
    field->setProperty("text", "");
  if(auto p = popup("playlistDialog")) check(!p->property("acceptEnabled").toBool(), "blank playlist Save disabled");
  QTest::keyClick(w, Qt::Key_Return);
  check(b->playlists().isEmpty(), "blank playlist cannot submit");
  shot("13-playlist-form-empty");
  if (field) {
    field->setProperty("text", "Night drive");
    field->forceActiveFocus();
    QTest::keyClick(w, Qt::Key_Return);
  }
  QTest::qWait(400);
  check(b->playlists().size() == 1, "create playlist through form");
  shot("14-playlist-created");
  if (!b->playlists().isEmpty()) {
    auto id = b->playlists().last().toMap().value("id").toString();
    b->addToPlaylist(id, song);
    w->setProperty("localPlaylist", id);
    b->openPlaylist(id);
    shot("15-local-playlist");
  }
  w->setProperty("menuItem", song);
  w->setProperty("menuIndex", 0);
  w->setProperty("menuQueue", false);
  if (auto p = popup("trackActions")) {
    p->setProperty("x", 400);
    p->setProperty("y", 150);
    QMetaObject::invokeMethod(p, "open");
    shot("16-track-menu");
    QMetaObject::invokeMethod(p, "close");
  } else
    check(false, "find track actions");
  if (auto p = popup("addPlaylistDialog")) {
    QMetaObject::invokeMethod(p, "open");
    auto choice=findItem(w->contentItem(),"playlistChoice");
    check(choice && choice->isVisible() && choice->height()>=44,"existing playlist choice is visible");
    shot("17-add-to-playlist");
    click("playlistChoice");
    check(!p->property("opened").toBool(),"choose existing playlist closes dialog");
    QMetaObject::invokeMethod(p, "close");
  }
  click("settingsButton");
  shot("18-settings-dark");
  QQmlExpression role(qmlContext(w), w, "Theme.primaryText");
  auto value = role.evaluate();
  fprintf(stdout, "PRIMARY_INK_DARK %s error=%s\n",
          qPrintable(value.toString()), qPrintable(role.error().toString()));
  check(value.value<QColor>() == QColor("#572008"), "dark primary foreground role");
  b->setTheme("light");
  value = role.evaluate();
  fprintf(stdout, "PRIMARY_INK_LIGHT %s\n", qPrintable(value.toString()));
  check(value.value<QColor>() == QColor("#ffffff"), "light primary foreground role");
  shot("19-settings-light");
  QTest::keyClick(w, Qt::Key_Escape);
  w->resize(800, 600);
  w->setProperty("side", "queue");
  shot("20-compact-queue");
  w->setProperty("side", "");
  w->resize(1180, 800);
  w->setProperty("destination", "search");
  w->setProperty("localPlaylist", "");
  b->openLink("https://example.com/unsupported");
  wait();
  shot("21-link-error");
  b->dismissError();
  query("zzzxxyyqqq987654321abcnoresult", "songs");
  b->results()->assign({}); // Deterministic empty-result presentation fixture.
  shot("22-empty-search-fixture");
  b->setCookieFile(QUrl::fromLocalFile("/nonexistent/sung-cookie-test"));
  shot("23-cookie-error");
  b->dismissError();
  b->clearQueue();
  check(b->queue()->count()==0, "clear queue");
  b->undo();
  check(b->queue()->count()==1, "undo restores queue");
  b->startSearch();
  check(b->page()=="search", "search entry state");
  b->library("playlists");
  b->back();
  check(w->property("destination").toString()=="search", "Back restores selected destination");
  b->notifyError("ConnectionError: test fixture", "catalog");
  check(b->canRetry(), "network failure offers retry");
  shot("24-retry-fixture");
  b->dismissError();
  auto editId=b->createPlaylist("Dialog test");
  w->setProperty("editPlaylistId",editId);
  w->setProperty("playlistAction","rename");
  if(auto p=popup("playlistDialog")) {
    QMetaObject::invokeMethod(p,"open");
    QTest::qWait(200);
    auto name=findItem(w->contentItem(),"playlistName");
    if(name) name->setProperty("text","Renamed through dialog");
    click("playlistDialog_button_0");
    b->openPlaylist(editId);
    check(b->title()=="Renamed through dialog","rename through Save button");
  }
  auto count=b->playlists().size();
  if(auto p=popup("deletePlaylistDialog")) {
    QMetaObject::invokeMethod(p,"open");
    shot("25-delete-dialog");
    click("deletePlaylistDialog_button_6");
    check(b->playlists().size()==count,"cancel deletion preserves playlist");
    QMetaObject::invokeMethod(p,"open");
    QTest::qWait(200);
    click("deletePlaylistDialog_button_5");
    check(b->playlists().size()==count-1,"confirm deletion through button");
    b->undo();
    check(b->playlists().size()==count,"undo restores deleted playlist");
  }
  b->save();
  fprintf(stdout, "AUDIT_RESULT %d failures\n", failures);
  fflush(stdout);
  QCoreApplication::exit(failures ? 2 : 0);
}

void runRecoveryTests(Backend *b, QQuickWindow *w) {
  int failures=0;
  auto check=[&](bool ok,const char *name){fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",name);fflush(stdout);if(!ok)++failures;};
  auto until=[](std::function<bool()> predicate,int timeout=85000){QElapsedTimer t;t.start();while(!predicate()&&t.elapsed()<timeout)QTest::qWait(50);return predicate();};
  b->setAutoplay(false);b->setRepeat(0);b->setVolume(0);
  b->playItem({{"id","YXHKjnuIUHE"},{"videoId","YXHKjnuIUHE"},{"kind","song"},{"title","Rare"},{"artist","NEFFEX"}});
  check(until([&]{return b->playing()&&b->position()>1500;}),"reported track progresses through native decoder");
  check(b->media()->source().isLocalFile(),"normal playback uses complete buffered audio");
  b->seek(60000);
  check(until([&]{return b->playing()&&b->position()>61000;},12000),"reported track seeks and progresses");
  const auto firstPosition=b->position();
  for(int i=0;i<32;++i)b->media()->errorOccurred(QMediaPlayer::ResourceError,"Injected demux error burst");
  check(until([&]{return !b->resolving()&&b->playing();}),"32 errors from one source consume only one recovery");
  check(b->position()>=firstPosition-300,"recovery immediately restores position without restarting the track");
  check(until([&]{return b->position()>firstPosition+1000;},5000),"recovery continues from restored position");
  check(b->error().isEmpty(),"automatic recovery does not leave error banner");
  const auto secondPosition=b->position();
  b->media()->errorOccurred(QMediaPlayer::ResourceError,"Injected repeated demux I/O failure");
  check(until([&]{return b->media()->source().isLocalFile()&&!b->resolving()&&b->playing();}),"repeated demux failure buffers audio and resumes position");
  check(b->position()>=secondPosition-300,"second recovery immediately restores position");
  check(b->error().isEmpty(),"buffered recovery clears playback error");
  b->seek(200000);
  check(until([&]{return b->playing()&&b->position()>202000;},12000),"buffered audio supports seeking near end");
  const auto dir=qEnvironmentVariable("SUNG_TEST_OUTPUT");QDir().mkpath(dir);
  check(w->grabWindow().save(dir+"/recovered-wave.png"),"capture recovered playback waveform");
  b->pause();const auto pos=b->position();QTest::qWait(500);
  check(qAbs(b->position()-pos)<250,"recovered playback pauses accurately");
  b->play();check(until([&]{return b->position()>pos+1000;},5000),"recovered playback resumes");
  b->stop();fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);QCoreApplication::exit(failures?2:0);
}

void runLyricsTests(Backend *b,QQuickWindow *w) {
  int failures=0;
  auto check=[&](bool ok,const char *name){fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",name);fflush(stdout);if(!ok)++failures;};
  auto until=[](std::function<bool()> predicate,int timeout=45000){QElapsedTimer t;t.start();while(!predicate()&&t.elapsed()<timeout)QTest::qWait(50);return predicate();};
  b->setMotion(true);b->setVolume(0);b->setAutoplay(false);b->setTheme("system");
  b->playItem({{"id","fdz_cabS9BU"},{"videoId","fdz_cabS9BU"},{"kind","song"},{"title","Thinking out Loud"},{"artist","Ed Sheeran"},{"art","https://i.ytimg.com/vi/fdz_cabS9BU/hqdefault.jpg"}});
  b->fetchLyrics();
  check(until([&]{return !b->lyricsBusy();}),"live timed lyric request completed");
  check(b->lyricLines().size()>10,"live YouTube timestamps available");
  check(until([&]{return b->playing()&&b->position()>1000;},85000),"lyric test audio progresses");
  w->setProperty("side","lyrics");QTest::qWait(600);b->pause();
  if(b->lyricLines().size()>10){
    const auto line=b->lyricLines()[10].toMap();b->seek(line.value("start").toLongLong()+100);
    check(b->lyricIndex()==10,"paused seek immediately selects matching lyric");QTest::qWait(600);
    auto list=findItem(w->contentItem(),"liveLyrics");
    check(list&&list->isVisible()&&list->property("currentIndex").toInt()==10,"lyrics panel follows current timestamp");
    if(list){
      auto active=qobject_cast<QQuickItem*>(list->property("currentItem").value<QObject*>());QQuickItem *previous=nullptr;
      if(active)for(auto sibling:active->parentItem()->childItems())if(sibling->objectName()=="lyricLine"&&sibling->property("index").toInt()==9)previous=sibling;
      b->setKeepCompletedLyrics(false);QTest::qWait(250);check(previous&&previous->opacity()<0.01&&!previous->isEnabled()&&active&&active->opacity()>0.99,"completed lyrics hide without hiding the current line");
      b->setKeepCompletedLyrics(true);QTest::qWait(250);check(previous&&previous->opacity()>0.99&&previous->isEnabled(),"completed lyrics can be restored");
    }
    if(list){
      auto active=qobject_cast<QQuickItem*>(list->property("currentItem").value<QObject*>());
      auto label=active?findItem(active,"lyricLabel"):nullptr;
      check(label&&qAbs(label->scale()-1)<0.01&&label->opacity()>0.99,"current lyric is full size and fully visible");
      QQuickItem *neighbor=nullptr;
      if(active)for(auto sibling:active->parentItem()->childItems())if(sibling!=active&&sibling->objectName()=="lyricLine"&&sibling->property("index").toInt()==11)neighbor=findItem(sibling,"lyricLabel");
      check(neighbor&&qAbs(neighbor->scale()-0.86)<0.01&&neighbor->opacity()<0.4,"surrounding lyrics are smaller and dimmer");
      if(active&&label){const auto height=active->height();b->seek(b->lyricLines()[11].toMap().value("start").toLongLong()+100);QTest::qWait(500);check(qAbs(active->height()-height)<0.01,"lyric emphasis keeps line layout stable");b->seek(line.value("start").toLongLong()+100);QTest::qWait(500);}
    }
    if(list){auto item=qobject_cast<QQuickItem*>(list->property("currentItem").value<QObject*>());if(item){const auto p=item->mapToScene(QPointF(item->width()/2,item->height()/2));QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,p.toPoint());check(qAbs(b->position()-line.value("start").toLongLong())<300,"click lyric seeks to line");}else check(false,"active lyric is instantiated");}
  }
  const auto dir=qEnvironmentVariable("SUNG_TEST_OUTPUT");QDir().mkpath(dir);
  check(w->grabWindow().save(dir+"/live-lyrics.png"),"capture live lyric panel");
  auto panel=findItem(w->contentItem(),"sidePanel");bool widths=true;
  for(int i=0;i<8;++i){w->setProperty("side",i%2?"lyrics":"");QTest::qWait(60);widths=widths&&panel&&panel->width()>=0;}
  QTest::qWait(550);check(widths&&panel&&qAbs(panel->width()-(w->width()<1000?w->width()-104:360))<2,"interrupted panel motion settles at correct width");
  b->setMotion(false);w->setProperty("side","");QTest::qWait(30);
  check(panel&&!panel->isVisible()&&panel->property("revealWidth").toReal()<1,"reduced motion closes panel immediately");
  w->setProperty("side","lyrics");QTest::qWait(30);
  auto list=findItem(w->contentItem(),"liveLyrics");check(list&&list->property("highlightMoveDuration").toInt()==0,"reduced motion disables lyric scrolling animation");
  if(list&&b->lyricLines().size()>11){b->seek(b->lyricLines()[11].toMap().value("start").toLongLong()+100);QTest::qWait(30);auto active=qobject_cast<QQuickItem*>(list->property("currentItem").value<QObject*>());auto label=active?findItem(active,"lyricLabel"):nullptr;check(label&&qAbs(label->scale()-1)<0.01&&label->opacity()>0.99,"reduced motion applies lyric emphasis immediately");}
  b->stop();check(b->lyricLines().isEmpty(),"stop clears synchronized lyrics");
  fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);QCoreApplication::exit(failures?2:0);
}

void runFeatureTests(Backend *b,QQuickWindow *w) {
  int failures=0;
  auto check=[&](bool ok,const char *name){fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",name);fflush(stdout);if(!ok)++failures;};
  auto until=[](std::function<bool()> predicate,int timeout=45000){QElapsedTimer t;t.start();while(!predicate()&&t.elapsed()<timeout)QTest::qWait(50);return predicate();};
  const auto dir=qEnvironmentVariable("SUNG_TEST_OUTPUT");QDir().mkpath(dir);w->resize(1180,800);
  auto shot=[&](QQuickWindow *window,const QString &name){QTest::qWait(300);check(window->grabWindow().save(dir+"/"+name+".png"),qPrintable("capture "+name));};
  auto click=[&](QQuickWindow *window,const QString &name){auto item=findItem(window->contentItem(),name);if(!item){check(false,qPrintable("find "+name));return;}QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());QTest::qWait(100);};
  b->setTheme("system");b->setMotion(true);b->setVolume(0);b->setAutoplay(false);
  b->playItem({{"id","fdz_cabS9BU"},{"videoId","fdz_cabS9BU"},{"kind","song"},{"title","Thinking out Loud"},{"artist","Ed Sheeran"},{"art","https://i.ytimg.com/vi/fdz_cabS9BU/hqdefault.jpg"}});b->fetchLyrics();
  check(until([&]{return b->playing()&&b->position()>1000;},85000),"feature test uses real buffered audio");
  check(until([&]{return !b->lyricsBusy();}),"lyrics available for mini player");
  if(!b->lyricLines().isEmpty())b->seek(b->lyricLines().value(10).toMap().value("start").toLongLong()+100);
  const auto source=b->media()->source();const auto before=b->position();
  const auto beforeMini=w->grabWindow();
  click(w,"miniPlayerButton");
  auto mini=qobject_cast<QQuickWindow*>(w->property("miniPlayer").value<QObject*>());
  check(mini&&mini->isVisible()&&!w->isVisible(),"mini player replaces full window");
  check(b->playing()&&b->media()->source()==source&&b->position()>=before,"mini player preserves audio and timestamp");
  if(mini){
    if(auto idle=w->findChild<QTimer *>("renderResourceIdleTimer",Qt::FindDirectChildrenOnly)) {
      check(idle->isActive() && idle->interval()==30000,"hidden full player keeps resources warm for thirty seconds");
      idle->start(1);QTest::qWait(200);
      check(!w->isPersistentGraphics() && !w->isPersistentSceneGraph(),"hidden full player releases disposable graphics resources");
      check(b->playing() && mini->isVisible(),"resource cleanup preserves mini-player playback");
    }
    check(mini->width()==520&&mini->height()==216,"mini player retains compact geometry");
    auto line=findItem(mini->contentItem(),"miniLyricLine");check(line&&!line->property("text").toString().isEmpty(),"mini player shows current synchronized lyric");
    shot(mini,"mini-player");click(mini,"miniPlayButton");check(!b->playing(),"mini player pauses playback");
    click(mini,"miniVolumeButton");
    auto volume=findItem(mini->contentItem(),"miniVolumeSlider");
    check(volume&&volume->isVisible(),"mini volume popup opens");
    if(volume){volume->forceActiveFocus();QTest::keyClick(mini,Qt::Key_Right);check(b->volume()>0,"mini volume keyboard changes app volume");b->setVolume(0.5);const auto p=volume->mapToScene(QPointF(volume->width()/2,volume->height()/2));QWheelEvent wheel(p,mini->mapToGlobal(p.toPoint()),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QCoreApplication::sendEvent(mini,&wheel);check(qAbs(b->volume()-0.55)<0.01,"volume wheel changes volume by five percent");b->setVolumeStep(2);b->setVolume(0.5);QWheelEvent fineWheel(p,mini->mapToGlobal(p.toPoint()),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QCoreApplication::sendEvent(mini,&fineWheel);check(qAbs(b->volume()-0.52)<0.01,"volume wheel respects configured two percent step");b->setVolumeStep(5);b->setVolume(0);}
    QTest::mouseClick(mini,Qt::LeftButton,Qt::NoModifier,QPoint(30,30));QTest::qWait(100);
    click(mini,"miniPlayButton");check(until([&]{return b->playing();},5000),"mini player resumes playback");
    click(mini,"miniRestoreButton");QTest::qWait(200);
    check(w->isVisible()&&!w->property("compactMode").toBool()&&!mini->isVisible(),"restore hides mini window and restores full player");
  }
  check(w->isPersistentGraphics() && w->isPersistentSceneGraph(),"restored player keeps active render resources warm");
  auto restored=w->grabWindow();
  if(auto nav=findItem(w->contentItem(),"nav_home"))if(auto icon=findItem(nav,"materialIcon")){
    const qreal scale=qreal(restored.width())/w->width();auto pt=icon->mapToScene(QPointF());
    const QRect r(qRound(pt.x()*scale),qRound(pt.y()*scale),qRound(icon->width()*scale),qRound(icon->height()*scale));
    // Fractional-scale native rasterization can vary slightly after remapping.
    // Permit <=3/255 RMS color error; a blank or black icon still fails decisively.
    const auto expected=beforeMini.copy(r).convertToFormat(QImage::Format_RGB32);
    auto matchesIcon=[&]{
      restored=w->grabWindow();const auto actual=restored.copy(r).convertToFormat(QImage::Format_RGB32);
      if(actual.size()!=expected.size()||actual.isNull())return false;
      qint64 error=0;
      for(int y=0;y<actual.height();++y)for(int x=0;x<actual.width();++x){
        const QRgb a=expected.pixel(x,y),b=actual.pixel(x,y);
        for(int shift:{0,8,16}){const int d=int((a>>shift)&255)-int((b>>shift)&255);error+=d*d;}
      }
      return error<=qint64(actual.width())*actual.height()*3*9;
    };
    const bool iconsRestored=until(matchesIcon,3000);
    beforeMini.copy(r).save(dir+"/icon-before.png");restored.copy(r).save(dir+"/icon-restored.png");
    check(iconsRestored,"full-player icons survive mini-player round trip");
  }
  b->setPlaybackRate(1.5);QTest::qWait(400);QElapsedTimer rateClock;rateClock.start();const auto rateStart=b->position();QTest::qWait(2000);
  const double measuredRate=double(b->position()-rateStart)/rateClock.elapsed();
  check(measuredRate>1.2&&measuredRate<1.8,"real audio advances at selected playback speed");
  check(b->media()->source()==source&&!b->resolving(),"speed change preserves buffered source");
  auto rateDialog=w->findChild<QObject*>("rateDialog");
  if(rateDialog){QMetaObject::invokeMethod(rateDialog,"open");shot(w,"playback-speed");auto slider=findItem(w->contentItem(),"playbackRateSlider");if(slider){slider->forceActiveFocus();QTest::keyClick(w,Qt::Key_Left);check(qAbs(b->playbackRate()-1.45)<0.01,"speed slider keyboard works without seeking");}QMetaObject::invokeMethod(rateDialog,"close");}
  b->setPlaybackRate(1);b->pause();
  const auto timedStart=b->lyricLines().value(10).toMap().value("start").toLongLong();b->seek(timedStart-1000);b->setLyricOffset(1000);
  check(until([&]{return b->lyricIndex()==10;},3000),"lyric offset selects corrected live line");
  b->seekLyric(timedStart);check(until([&]{return qAbs(b->position()-(timedStart-1000))<400;},3000),"lyric seek compensates for saved offset");
  auto timing=w->findChild<QObject*>("lyricTimingDialog");
  if(timing){QMetaObject::invokeMethod(timing,"open");QTest::qWait(250);click(w,"lyricsLaterButton");check(b->lyricOffset()==750,"Later delays lyrics by a quarter second");click(w,"lyricsEarlierButton");check(b->lyricOffset()==1000,"Earlier advances lyrics by a quarter second");click(w,"lyricsSizeLarger");check(b->lyricTextSize()==26,"larger lyric text control works");click(w,"lyricsSizeSmaller");check(b->lyricTextSize()==25,"smaller lyric text control works");b->setLyricTextSize(30);click(w,"lyricsSizeReset");check(b->lyricTextSize()==25,"lyric size reset restores medium default");shot(w,"lyric-timing");QMetaObject::invokeMethod(timing,"close");}
  b->setLyricOffset(0);b->seek(timedStart+100);b->play();QTest::qWait(300);
  const auto normalSize=w->size();const auto immersivePosition=b->position();
  // Wayland may deny focus activation while the user is using another app.
  // Native visual runs exercise the visible buttons; offscreen runs test shortcuts.
  const bool pointerFullscreen=qEnvironmentVariableIsSet("SUNG_TEST_POINTER_FULLSCREEN");
  if(pointerFullscreen){w->setProperty("side","lyrics");QTest::qWait(500);click(w,"immersiveButton");}
  else {w->requestActivate();check(until([&]{return w->isActive();},3000),"player is active before fullscreen shortcut");QTest::keyClick(w,Qt::Key_F11);}
  check(until([&]{return w->visibility()==QWindow::FullScreen && w->property("immersive").toBool();},5000),"immersive enters fullscreen");
  check(b->playing()&&b->media()->source()==source&&b->position()>=immersivePosition,"immersive preserves playback and position");
  check(until([&]{return !b->lyricsBusy();}),"immersive lyrics load");
  auto immersiveLyrics=findItem(w->contentItem(),"liveLyrics");check(immersiveLyrics&&immersiveLyrics->isVisible(),"immersive shows live lyrics");
  check(until([&]{
    if(!immersiveLyrics)return false;
    auto current=qobject_cast<QQuickItem*>(immersiveLyrics->property("currentItem").value<QObject*>());
    if(!current)return false;
    const auto r=current->mapRectToItem(immersiveLyrics,current->boundingRect());
    return r.top()>=-1 && r.bottom()<=immersiveLyrics->height()+1 && immersiveLyrics->property("currentIndex").toInt()==b->lyricIndex();
  },3000),"immersive opens centered on the current lyric");
  auto immersivePlay=findItem(w->contentItem(),"immersivePlayButton");auto cover=findItem(w->contentItem(),"immersiveArtwork");
  check(immersivePlay&&QRectF(0,0,w->width(),w->height()).contains(immersivePlay->mapRectToScene(immersivePlay->boundingRect())),"immersive transport stays inside viewport");
  check(cover&&cover->width()<=420&&qAbs(cover->width()-cover->height())<2,"immersive artwork remains bounded and square");
  QTest::mouseMove(w,QPoint(4,4));shot(w,"immersive-lyrics");b->setKeepCompletedLyrics(false);QTest::qWait(250);shot(w,"focused-lyrics");b->setKeepCompletedLyrics(true);click(w,"immersivePlayButton");check(!b->playing(),"immersive pauses playback");
  click(w,"immersivePlayButton");check(until([&]{return b->playing();},5000),"immersive resumes playback");
  if(pointerFullscreen)click(w,"exitImmersiveButton");else {w->requestActivate();until([&]{return w->isActive();},3000);QTest::keyClick(w,Qt::Key_Escape);}
  check(until([&]{return !w->property("immersive").toBool()&&w->visibility()!=QWindow::FullScreen;},5000),"immersive exit restores normal player");
  check(until([&]{return w->size()==normalSize;},3000),"immersive restores previous geometry");
  w->setProperty("side","");QTest::qWait(500);
  auto devices=b->audioDevices();check(!devices.isEmpty(),"audio device choices include system default");
  if(devices.size()>1){const auto id=devices.last().toMap().value("id").toString();b->setAudioDeviceId(id);check(b->audioDeviceId()==id,"select per-app audio output");check(QString::fromLatin1(b->media()->audioOutput()->device().id().toBase64())==id,"Qt audio uses selected device");}
  b->setAudioDeviceId("");check(b->audioDeviceId().isEmpty(),"restore system-default output");
  check(until([&]{return b->playing();},5000)&&b->media()->source()==source,"output switching preserves buffered track");
  click(w,"settingsButton");
  auto settings=w->findChild<QObject*>("settingsDialog");auto audioDialog=w->findChild<QObject*>("audioDeviceDialog");
  click(w,"historyPauseSwitch");check(b->historyPaused(),"session history pause switch works");click(w,"historyPauseSwitch");check(!b->historyPaused(),"history switch resumes recording");
  click(w,"trackNotificationsSwitch");check(b->trackNotifications(),"track notifications switch enables preference");click(w,"trackNotificationsSwitch");check(!b->trackNotifications(),"track notifications switch disables preference");
  click(w,"volumeStepButton");QTest::qWait(200);click(w,"volumeStep_2");check(b->volumeStep()==2,"volume step menu selects two percent");b->setVolumeStep(5);shot(w,"listening-settings");
  if(audioDialog){QMetaObject::invokeMethod(audioDialog,"open");shot(w,"audio-output");QMetaObject::invokeMethod(audioDialog,"close");}else check(false,"audio output dialog exists");
  if(settings)QMetaObject::invokeMethod(settings,"close");
  QTest::qWait(250);
  b->pause();
  auto a=b->current();auto c=a;c["id"]="fixture0001";c["videoId"]="fixture0001";c["title"]="Quiet morning";c["artist"]="Test artist";c["seconds"]=95;
  auto d=c;d["id"]="fixture0002";d["videoId"]="fixture0002";d["title"]="Evening study";d["seconds"]=230;
  auto playlist=b->createPlaylist("Collection tools");b->addToPlaylist(playlist,a);b->addToPlaylist(playlist,c);b->addToPlaylist(playlist,d);b->openPlaylist(playlist);
  click(w,"collectionToolsButton");auto search=findItem(w->contentItem(),"collectionSearch");
  check(search&&search->isVisible()&&search->hasActiveFocus(),"collection filter opens with keyboard focus");
  if(search){for(char key:QByteArray("test artist"))QTest::keyClick(w,key);check(b->collection()->count()==2,"typed filter matches artist locally");}
  b->collection()->setSortKey("duration");check(b->collection()->get(0).value("title").toString()=="Quiet morning","duration sorting orders the displayed list");
  check(b->results()->get(0).value("id")==a.value("id"),"sorting preserves saved playlist order");
  shot(w,"collection-filter-fixture");
  b->collection()->setQuery("no match");check(b->collection()->count()==0,"empty local filter state");
  b->collection()->setQuery("");b->collection()->setSortKey("original");check(b->collection()->count()==3,"clear filtering restores collection");
  click(w,"collectionSortButton");QTest::qWait(250);
  auto sort=findItem(w->contentItem(),"sort_original");
  if(sort){auto label=findItem(sort,"menuItemLabel");auto indicator=qobject_cast<QQuickItem*>(sort->property("indicator").value<QObject*>());check(label&&indicator&&label->x()+label->property("leftPadding").toReal()>=indicator->x()+indicator->width()+8,"sort checkmark has separate space from label");}else check(false,"sort Original order item exists");
  if(sort){auto button=findItem(w->contentItem(),"collectionSortButton");const auto row=sort->mapRectToScene(sort->boundingRect());const auto anchor=button->mapRectToScene(button->boundingRect());check(qAbs(row.right()-anchor.right())<24&&qAbs(row.top()-anchor.bottom())<220,"sort menu stays aligned with its button within screen limits");auto indicator=qobject_cast<QQuickItem*>(sort->property("indicator").value<QObject*>());auto label=findItem(sort,"menuItemLabel");check(indicator&&label&&indicator->property("color")==label->property("color"),"sort checkmark uses readable theme foreground");}
  shot(w,"sort-menu-fixed");click(w,"sort_title");check(b->collection()->sortKey()=="title","sort menu selects Title");
  b->collection()->setSortKey("original");
  click(w,"pinCollectionButton");check(b->pins().size()==1,"pin current collection to Home");
  b->home();check(until([&]{return !b->busy();}),"Home loads with local pins");
  check(w->property("homeSections").value<QJSValue>().property(0).property("title").toString()=="Pinned","pinned shelf precedes public recommendations");
  shot(w,"pinned-home-fixture");b->open(b->pins().first().toMap());check(b->libraryId()==playlist,"Home pin opens saved collection");
  b->togglePin(b->pins().first().toMap());check(b->pins().isEmpty(),"unpin removes Home shortcut");
  b->setRepeat(2);b->setAutoplay(true);b->play();b->setSleep(-1);b->seek(b->duration()-700);
  check(until([&]{return !b->playing()&&b->sleepLabel()=="Off";},8000),"real track ends without repeating after sleep timer");
  b->setRepeat(0);b->setAutoplay(false);
  const auto preserved=b->current();b->enqueue(c);b->enqueue(d);const int queued=b->queue()->count();
  w->setProperty("menuItem",preserved);auto actions=w->findChild<QObject*>("trackActions");
  if(actions){QMetaObject::invokeMethod(actions,"open");QTest::qWait(250);shot(w,"keep-queue-menu");click(w,"playKeepQueueAction");}else check(false,"track actions menu exists");
  check(until([&]{return b->playing();},5000),"play now keeps queue with real playback");check(b->queue()->count()==queued,"play current song does not duplicate queue");
  w->setProperty("side","queue");QTest::qWait(500);check(!b->queueTime().isEmpty()&&!b->queueEnd().isEmpty(),"playing finite queue shows remaining time and finish estimate");
  const auto queueSource=b->media()->source();const auto queueCurrent=b->current();click(w,"smartShuffleQueueButton");check(b->playing()&&b->media()->source()==queueSource&&b->current()==queueCurrent,"smart shuffle preserves playing audio");
  auto summary=findItem(w->contentItem(),"queueTimeLabel");check(summary&&summary->isVisible(),"remaining queue time is visible");shot(w,"queue-time-shuffle");b->undo();check(b->playing()&&b->media()->source()==queueSource,"undo smart shuffle preserves playback");b->pause();check(b->queueEnd().isEmpty(),"paused queue hides finish estimate");
  // Exercise the deferred native-dialog module and the real acceptance bridges.
  check(w->property("fileDialogs").value<QObject*>()==nullptr,"file-dialog module stays uninstantiated until first use");
  const auto exportUrl=QUrl::fromLocalFile(dir+"/picker-library.json");
  QFile::remove(exportUrl.toLocalFile());
  auto openPicker=[&](const QString &kind,const QString &name)->QObject* {
    QMetaObject::invokeMethod(w,"openFileDialog",Q_ARG(QVariant,kind));QTest::qWait(200);
    auto dialogs=w->property("fileDialogs").value<QObject*>();
    if(!dialogs)dialogs=w->property("fileDialogs").value<QJSValue>().toQObject();
    auto picker=dialogs?dialogs->property(qPrintable(name)).value<QObject*>():nullptr;
    check(picker && picker->property("visible").toBool(),qPrintable("open deferred "+kind+" picker"));
    check(w->property("modalOpen").toBool(),"file picker suspends player shortcuts");
    return picker;
  };
  if(auto picker=openPicker("export","exportPicker")) {
    check(picker->property("fileMode").toInt()==2 && picker->property("defaultSuffix").toString()=="json","export picker retains save mode and JSON suffix");
    QMetaObject::invokeMethod(picker,"reject");
    picker->setProperty("selectedFile",exportUrl);QMetaObject::invokeMethod(picker,"open");QTest::qWait(250);
    QMetaObject::invokeMethod(picker,"accept");QTest::qWait(150);
    check(QFile::exists(exportUrl.toLocalFile()),"file-picker acceptance exports a library");
    const auto remembered=picker->property("currentFolder");
    auto again=openPicker("export","exportPicker");check(again==picker && again->property("currentFolder")==remembered,"reopening keeps the same file picker and directory");
    QMetaObject::invokeMethod(picker,"reject");QTest::qWait(100);
  }
  b->deletePlaylist(playlist);
  if(auto picker=openPicker("import","importPicker")) {
    QMetaObject::invokeMethod(picker,"reject");
    picker->setProperty("selectedFile",exportUrl);QMetaObject::invokeMethod(picker,"open");QTest::qWait(250);
    QMetaObject::invokeMethod(picker,"accept");QTest::qWait(150);
    bool restored=false;for(const auto &v:b->playlists())if(v.toMap().value("title")=="Collection tools")restored=true;
    check(restored,"file-picker acceptance imports the exported playlist");
  }
  const auto cookiesBefore=b->cookies();
  if(auto picker=openPicker("cookies","cookiePicker")) {
    check(picker->property("nameFilters").toStringList()==QStringList{"Cookie files (*.txt)"},"cookie picker retains its text-file filter");
    QMetaObject::invokeMethod(picker,"reject");QTest::qWait(100);
    check(b->cookies()==cookiesBefore,"canceling cookie picker preserves authentication settings");
  }
  check(!w->property("modalOpen").toBool(),"closing file pickers restores player shortcuts");
  b->deletePlaylist(playlist);b->stop();fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);QCoreApplication::exit(failures?2:0);
}

void runSearchSelectionTests(Backend *b,QQuickWindow *w) {
  auto until=[](std::function<bool()> condition,int timeout){QElapsedTimer clock;clock.start();while(!condition()&&clock.elapsed()<timeout)QTest::qWait(20);return condition();};
  int failures=0;
  auto check=[&](bool ok,const char *label){fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",label);fflush(stdout);if(!ok)++failures;};
  const auto dir=qEnvironmentVariable("SUNG_TEST_OUTPUT");QDir().mkpath(dir);
  auto shot=[&](const QString &name){QTest::qWait(300);check(w->grabWindow().save(dir+"/"+name+".png"),qPrintable("capture "+name));};
  auto flush=[&]{if(qEnvironmentVariableIsSet("SUNG_TEST_BACKGROUND_ACTIVATION")){for(const auto &name:{"tracksView","queueView","playlistChoices"})if(auto list=findItem(w->contentItem(),name))QMetaObject::invokeMethod(list,"forceLayout");w->grabWindow();QTest::qWait(50);}};
  auto click=[&](const QString &name,Qt::KeyboardModifiers mods=Qt::NoModifier){flush();auto item=findItem(w->contentItem(),name);if(!item){check(false,qPrintable("find "+name));return;}QTest::mouseClick(w,Qt::LeftButton,mods,item->mapToScene(QPointF(qMin(140.,item->width()/2),item->height()/2)).toPoint());QTest::qWait(300);};
  auto select=[&](const char *view){auto item=findItem(w->contentItem(),view);return item?qobject_cast<RowSelection*>(item->property("selection").value<QObject*>()):nullptr;};
  // Test-only focus delivery inside Qt; never requests compositor activation.
  if(qEnvironmentVariableIsSet("SUNG_TEST_BACKGROUND_ACTIVATION")){QWindowSystemInterface::handleFocusWindowChanged(w);QTest::qWait(100);}
  w->resize(1180,900);b->setVolume(0);b->setMotion(true);b->setAutoplay(false);b->clearQueue();
  QVariantList songs;for(int i=0;i<8;++i)songs.append(QVariantMap{{"id",QString("select%1").arg(i,5,10,QChar('0'))},{"videoId",QString("select%1").arg(i,5,10,QChar('0'))},{"title",QString("Aurora %1").arg(i)},{"artist","Fixture artist"},{"kind","song"},{"seconds",120}});
  const auto id=b->createPlaylist("Aurora evenings");b->addItemsToPlaylist(id,songs);b->openPlaylist(id);QTest::qWait(400);
  auto selection=select("tracksView");check(selection,"collection selection exists");
  click("trackRow_0",Qt::ControlModifier);click("trackRow_2",Qt::ShiftModifier);
  check(selection&&selection->rows()==QVariantList({0,1,2}),"Ctrl then Shift selects contiguous displayed songs without playback");check(b->currentIndex()==-1,"selection does not begin playback");shot("selection");
  click("trackRow_1",Qt::ControlModifier);check(selection&&selection->rows()==QVariantList({0,2}),"Ctrl toggles one selected song");
  if(selection){b->enqueueItems(selection->items(),true);check(b->queue()->count()==2 && b->queue()->get(1).value("id")==songs[2].toMap().value("id"),"bulk enqueue preserves displayed order");selection->clear();}
  b->collection()->setQuery("Aurora 4");QTest::qWait(250);click("trackRow_0",Qt::ControlModifier);
  auto view=findItem(w->contentItem(),"tracksView");QVariant indices;if(view)QMetaObject::invokeMethod(view,"sourceRows",Q_RETURN_ARG(QVariant,indices));
  check(indices.toList()==QVariantList({4}),"filtered selection maps back to saved source row");
  b->removePlaylistRows(id,indices.toList());check(b->results()->count()==7,"bulk remove uses filtered source mapping");b->undo();check(b->results()->count()==8,"bulk removal Undo restores playlist");
  b->collection()->setQuery("");b->collection()->setSortKey("original");QTest::qWait(300);
  click("trackRow_0",Qt::ControlModifier);click("trackRow_1",Qt::ControlModifier);
  auto drag=[&](const QString &from,const QString &to){
    flush();auto a=findItem(w->contentItem(),from),z=findItem(w->contentItem(),to);if(!a||!z){check(false,"drag endpoints exist");return;}
    const auto begin=a->mapToScene(QPointF(140,a->height()/2)).toPoint();const auto end=z->mapToScene(QPointF(140,z->height()-4)).toPoint();
    QTest::mousePress(w,Qt::LeftButton,Qt::NoModifier,begin);QTest::qWait(50);
    for(int i=1;i<=15;++i){QTest::mouseMove(w,begin+(end-begin)*i/15,20);}
    QTest::mouseRelease(w,Qt::LeftButton,Qt::NoModifier,end);QTest::qWait(400);
  };
  drag("trackRow_0","trackRow_3");
  check(b->results()->get(0).value("id")==songs[2].toMap().value("id") && b->results()->get(2).value("id")==songs[0].toMap().value("id"),"pointer drag moves selected playlist block at insertion marker");shot("reordered");b->undo();
  b->clearQueue();b->enqueueItems(songs.mid(0,4));w->setProperty("side","queue");QTest::qWait(600);
  click("queueRow_0",Qt::ControlModifier);click("queueRow_1",Qt::ControlModifier);drag("queueRow_0","queueRow_3");
  check(b->queue()->get(0).value("id")==songs[2].toMap().value("id"),"pointer drag reorders selected queue block");b->undo();check(b->queue()->get(0).value("id")==songs[0].toMap().value("id"),"queue reorder Undo restores order");shot("queue-selection");
  auto queueSelection=select("queueView");if(queueSelection)queueSelection->clear();if(selection)selection->clear();
  drag("trackRow_0","queueRow_1");check(b->queue()->count()==5,"drag collection song into queue inserts a copy");b->undo();check(b->queue()->count()==4,"cross-list drop has one Undo");
  w->setProperty("side","");b->rememberSearch("Aurora");
  auto field=findItem(w->contentItem(),"searchField");check(field,"search field exists");
  if(field){field->setProperty("text","");field->forceActiveFocus();QTest::qWait(250);check(!(field->property("suggestions").canConvert<QJSValue>()?field->property("suggestions").value<QJSValue>().toVariant():field->property("suggestions")).toList().isEmpty(),"focus shows recent searches");shot("recent-searches");
    for(char c:QByteArray("Aurora")){QTest::keyClick(w,c);}QTest::qWait(200);auto suggestions=(field->property("suggestions").canConvert<QJSValue>()?field->property("suggestions").value<QJSValue>().toVariant():field->property("suggestions")).toList();check(!suggestions.isEmpty() && suggestions.first().toMap().value("kind")=="local","typing finds local playlists before online search");shot("local-search");
    QTest::keyClick(w,Qt::Key_Down);QTest::keyClick(w,Qt::Key_Return);QTest::qWait(200);check(b->page()=="local"&&b->libraryId()==id,"keyboard activates local search result");
    field->forceActiveFocus();QTest::keyClick(w,Qt::Key_A,Qt::ControlModifier);for(char c:QByteArray("unknown local match"))QTest::keyClick(w,c);QTest::qWait(200);check(field->property("highlighted").toInt()==-1,"typing resets highlighted suggestion");QTest::keyClick(w,Qt::Key_Return);QTest::qWait(400);check(b->page()=="search"&&b->query()=="unknown local match","Enter searches online when no local choice is selected");
  }
  b->openPlaylist(id);QTest::qWait(300);click("trackRow_0",Qt::ControlModifier);
  QTest::keyClick(w,Qt::Key_A,Qt::ControlModifier);QTest::qWait(100);check(selection&&selection->count()==8,"Ctrl+A selects every loaded song without selecting non-song entries");
  QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(100);check(selection&&selection->count()==0,"Escape clears selection without leaving the page");
  click("trackRow_0",Qt::ControlModifier);click("trackRow_1",Qt::ControlModifier);
  const auto target=b->createPlaylist("Bulk destination");click("bulkCollectionPlaylist");flush();shot("bulk-playlist-picker");
  auto choices=findItem(w->contentItem(),"playlistChoices");QQuickItem *choice=nullptr;
  std::function<void(QQuickItem*)> findChoice=[&](QQuickItem *item){if(item->objectName()=="playlistChoice"&&item->property("text").toString()=="Bulk destination")choice=item;for(auto c:item->childItems())findChoice(c);};if(choices)findChoice(choices);
  check(choice,"bulk playlist picker offers existing playlists");
  if(choice){QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,choice->mapToScene(choice->boundingRect().center()).toPoint());}QTest::qWait(300);
  b->openPlaylist(target);QTest::qWait(200);check(b->results()->count()==2,"bulk playlist picker adds the whole selection once");b->undo();check(b->results()->count()==0,"bulk playlist addition has one Undo");
  b->deletePlaylist(target);b->openPlaylist(id);QTest::qWait(300);
  auto row=findItem(w->contentItem(),"trackRow_0");if(row){const auto point=row->mapToScene(QPointF(140,35)).toPoint();QTest::mousePress(w,Qt::LeftButton,Qt::NoModifier,point);QTest::mouseMove(w,point+QPoint(0,25),40);QTest::keyClick(w,Qt::Key_Escape);QTest::mouseRelease(w,Qt::LeftButton,Qt::NoModifier,point+QPoint(0,100));QTest::qWait(100);check(b->results()->rows==songs,"Escape cancels drag without modifying the playlist");}
  if(view){
    view->setProperty("contentY",0);QTest::qWait(200);auto first=findItem(w->contentItem(),"trackRow_0");
    if(first){const auto start=first->mapToScene(QPointF(140,35)).toPoint();const auto edge=view->mapToScene(QPointF(140,view->height()-8)).toPoint();QTest::mousePress(w,Qt::LeftButton,Qt::NoModifier,start);QTest::mouseMove(w,start+QPoint(0,25),30);QTest::mouseMove(w,edge,30);QTest::qWait(650);check(view->property("contentY").toReal()>0,"drag near the list edge scrolls to offscreen songs");QTest::keyClick(w,Qt::Key_Escape);QTest::mouseRelease(w,Qt::LeftButton,Qt::NoModifier,edge);}
  }
  const QString audioPath=dir+"/selection.wav";QFile audio(audioPath);
  if(audio.open(QIODevice::WriteOnly)){QDataStream stream(&audio);stream.setByteOrder(QDataStream::LittleEndian);const quint32 bytes=8000*2*30;stream.writeRawData("RIFF",4);stream<<quint32(36+bytes);stream.writeRawData("WAVEfmt ",8);stream<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(8000)<<quint32(16000)<<quint16(2)<<quint16(16);stream.writeRawData("data",4);stream<<bytes;audio.write(QByteArray(bytes,0));audio.close();}
  b->clearQueue();b->enqueueItems(songs.mid(0,4));b->playAt(1);b->pause();QTest::qWait(300);b->localTestSource(QUrl::fromLocalFile(audioPath));QTest::qWait(500);b->seek(1000);QTest::qWait(100);
  const auto source=b->media()->source();const auto token=b->trackToken();const auto current=b->current();const auto position=b->position();
  check(b->playing()&&position>=1000,"batch playback check uses decoded local audio");
  b->moveQueueRows({0,1},4);b->undo();b->removeQueueRows({0,3});b->undo();b->enqueueItems({songs[4]},true);b->undo();
  check(b->playing()&&b->current()==current&&b->trackToken()==token&&b->media()->source()==source&&b->position()>=position,"batch moves, noncurrent removal and Undo preserve real audio without reopening the decoder");
  b->toggleLike(songs[0].toMap());b->library("mixes");w->setProperty("side","");QTest::qWait(300);flush();shot("smart-mixes");
  check(b->results()->count()==3,"Mixes exposes three local collections");click("trackRow_0");check(b->libraryId()=="mix-recent"&&b->results()->count()>0,"recently liked mix opens through its row");shot("recently-liked");
  w->setProperty("side","lyrics");b->fetchLyrics();QTest::qWait(300);click("lyricTimingButton");shot("lyrics-tools");
  check(findItem(w->contentItem(),"importLyricsButton"),"lyrics import is available even without timed provider lyrics");
  click("importLyricsButton");QTest::qWait(300);check(w->property("fileDialogs").value<QObject*>()!=nullptr,"lazy native lyric picker loads successfully");
  if(auto dialogs=w->property("fileDialogs").value<QObject*>()){if(auto picker=dialogs->property("lyricPicker").value<QObject*>())QMetaObject::invokeMethod(picker,"reject");}
  QTest::keyClick(w,Qt::Key_Escape);b->toggleLike(songs[0].toMap());
  b->stop();b->clearQueue();b->library("files");QTest::qWait(200);click("addLocalFilesButton");
  if(auto dialogs=w->property("fileDialogs").value<QObject*>()){
    auto picker=dialogs->property("audioPicker").value<QObject*>();check(picker&&picker->property("fileMode").toInt()==1,"audio picker supports multiple files");if(picker)QMetaObject::invokeMethod(picker,"reject");
  }
  // A programmatically closed file-dialog window has no compositor activation
  // in the offscreen/background harness. Deliver focus inside Qt only.
  QTest::qWait(300);QWindowSystemInterface::handleFocusWindowChanged(nullptr);QWindowSystemInterface::handleFocusWindowChanged(w);QTest::qWait(100);
  b->importLocalFiles({QUrl::fromLocalFile(audioPath)});check(until([&]{return !b->importingLocal();},10000),"local file import finishes asynchronously");b->library("files");QTest::qWait(200);flush();
  check(b->results()->count()==1,"imported audio appears in Local files");shot("local-files");click("trackRow_0");check(until([&]{return b->playing();},5000),"local library row plays native audio");
  const auto local=b->current();const auto lrcPath=dir+"/local.lrc";QFile localLyrics(lrcPath);if(localLyrics.open(QIODevice::WriteOnly)){localLyrics.write("[00:01] Quiet morning\n[00:04] Another line\n[00:08] Quiet evening");localLyrics.close();}
  b->importLyrics(QUrl::fromLocalFile(lrcPath),local.value("id").toString());b->setLyricOffset(250);w->setProperty("side","lyrics");QTest::qWait(300);click("lyricSearchButton");
  auto lyricField=findItem(w->contentItem(),"lyricSearchField");check(lyricField&&lyricField->hasActiveFocus(),"lyric search opens with keyboard focus");
  if(lyricField){for(char c:QByteArray("quiet"))QTest::keyClick(w,c);QTest::qWait(200);auto results=findItem(w->contentItem(),"lyricSearchResults");check(results&&results->property("count").toInt()==2,"lyric search finds repeated case-insensitive matches");shot("lyric-search");QTest::keyClick(w,Qt::Key_Down);QTest::keyClick(w,Qt::Key_Return);QTest::qWait(100);check(b->position()>=7750&&b->position()<8500,"keyboard result seeks with lyric offset");
    click("lyricSearchButton");QTest::keyClick(w,Qt::Key_A,Qt::ControlModifier);for(char c:QByteArray("absent"))QTest::keyClick(w,c);QTest::qWait(150);check(results&&results->property("count").toInt()==0,"lyric search has an empty result state");QTest::keyClick(w,Qt::Key_Escape);check(w->property("side").toString()=="lyrics","Escape closes lyric search without closing the panel");}
  w->setProperty("immersive",true);QTest::qWait(300);click("immersiveLyricSearchButton");
  if(auto immersive=findItem(w->contentItem(),"immersivePlayer")){auto field=findItem(immersive,"lyricSearchField");check(field&&field->hasActiveFocus(),"immersive lyric search receives keyboard focus");if(field){for(char c:QByteArray("quiet"))QTest::keyClick(w,c);QTest::qWait(150);auto results=findItem(immersive,"lyricSearchResults");check(results&&results->property("count").toInt()==2,"immersive lyric search returns matching lines");shot("immersive-lyric-search");QTest::keyClick(w,Qt::Key_Escape);}}
  w->setProperty("immersive",false);QTest::qWait(100);
  b->addToPlaylist(id,local);b->openPlaylist(id);check(b->results()->count()==9,"local song mixes into an existing YouTube playlist");
  w->setProperty("side","");w->setProperty("localPlaylist",id);QTest::qWait(200);
  auto duplicate=local;duplicate["id"]="local_"+QString(64,'a');duplicate["title"]="Duplicate local song";
  auto missing=local;missing["id"]="local_"+QString(64,'b');missing["localPath"]=dir+"/missing.flac";missing["title"]="Missing local song";
  b->addItemsToPlaylist(id,{duplicate,missing});click("playlistCleanupButton");
  check(until([&]{return !b->cleanupBusy();},5000)&&b->cleanupItems().size()==2,"cleanup review identifies exact duplicate and missing local file");
  QTest::qWait(200);shot("playlist-cleanup");
  auto remove=findItem(w->contentItem(),"applyPlaylistCleanup");check(remove&&remove->property("text").toString()=="Remove 1","cleanup defaults to duplicates only");
  click("cleanupMissing");check(remove&&remove->property("text").toString()=="Remove 2","missing removal is explicitly selected");
  click("applyPlaylistCleanup");check(until([&]{return !b->cleanupBusy();},5000)&&b->results()->count()==9,"reviewed cleanup removes selected issues");
  check(b->playing()&&b->media()->source()==QUrl::fromLocalFile(audioPath),"playlist cleanup preserves native playback");
  QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(200);b->undo();check(b->results()->count()==11,"cleanup has a single Undo");b->removePlaylistRows(id,{9,10});
  b->library("files");QTest::qWait(200);click("musicFoldersButton");shot("music-folders-empty");click("addMusicFolderButton");
  auto dialogs=w->property("fileDialogs").value<QObject*>();auto folderPicker=dialogs?dialogs->property("folderPicker").value<QObject*>():nullptr;
  check(folderPicker&&folderPicker->property("visible").toBool(),"native music folder picker opens");
  const auto musicDir=dir+"/Music";QDir().mkpath(musicDir+"/Album");QFile::copy(audioPath,musicDir+"/Album/Folder song.wav");
  if(folderPicker){folderPicker->setProperty("selectedFolder",QUrl::fromLocalFile(musicDir));QMetaObject::invokeMethod(folderPicker,"accept");}
  QTest::qWait(200);QWindowSystemInterface::handleFocusWindowChanged(nullptr);QWindowSystemInterface::handleFocusWindowChanged(w);
  check(until([&]{return !b->importingLocal();},10000)&&b->musicFolders().contains(musicDir),"folder picker imports and remembers selected folder");
  check(b->results()->count()==2,"recursive folder import adds nested song");click("musicFoldersButton");shot("music-folders");QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(200);
  click("rescanFoldersButton");check(until([&]{return !b->importingLocal();},5000)&&b->results()->count()==2,"Rescan skips unchanged files without duplicate entries");
  b->forgetMusicFolder(musicDir);check(b->results()->count()==2&&QFile::exists(musicDir+"/Album/Folder song.wav"),"forgetting folder keeps music and original files");
  b->removeLocalFile(local.value("id").toString());check(QFile::exists(audioPath),"removing local library entry preserves original audio");b->resetLyrics();
  b->stop();b->deletePlaylist(id);b->clearQueue();fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);QCoreApplication::exit(failures?2:0);
}
