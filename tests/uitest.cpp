#include <QPainter>
#include <QQmlProperty>
#include "uitest.h"
#include "backend.h"
#include "motionartwork.h"
#include <QQmlContext>
#include "rowselection.h"
#include "roundedart.h"
#include <QDir>
#include <QDataStream>
#include <qpa/qwindowsysteminterface.h>
#include <QFile>
#include <QQuickItem>
#include <QQmlComponent>
#include <QQmlEngine>
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
  if(auto pane=findItem(w->contentItem(),"lyricsView");pane&&list&&b->lyricLines().size()>11){
    pane->setProperty("following",false);list->setProperty("contentY",0);pane->setProperty("following",true);QTest::qWait(100);
    auto active=qobject_cast<QQuickItem*>(list->property("currentItem").value<QObject*>());
    const auto center=active?active->mapToItem(list,QPointF(0,active->height()/2)).y():-1;
    check(active&&qAbs(center-list->height()/2)<2,"resuming lyric follow immediately recenters paused current line");
  }
  b->stop();check(b->lyricLines().isEmpty(),"stop clears synchronized lyrics");
  fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);QCoreApplication::exit(failures?2:0);
}

void runFeatureTests(Backend *b,QQuickWindow *w) {
  int failures=0;
  auto check=[&](bool ok,const char *name){fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",name);fflush(stdout);if(!ok)++failures;};
  auto until=[](std::function<bool()> predicate,int timeout=45000){QElapsedTimer t;t.start();while(!predicate()&&t.elapsed()<timeout)QTest::qWait(50);return predicate();};
  const auto dir=qEnvironmentVariable("SUNG_TEST_OUTPUT");QDir().mkpath(dir);w->resize(1180,800);
  auto shot=[&](QQuickWindow *window,const QString &name){QTest::qWait(300);check(window->grabWindow().save(dir+"/"+name+".png"),qPrintable("capture "+name));};
  auto click=[&](QQuickWindow *window,const QString &name){auto item=findItem(window->contentItem(),name);if(!item){check(false,qPrintable("find "+name));return;}
    for(auto parent=item->parentItem();parent;parent=parent->parentItem())if(parent->property("contentY").isValid()){
      const auto point=item->mapToItem(parent,QPointF(0,0));
      if(point.y()<0||point.y()+item->height()>parent->height())parent->setProperty("contentY",qBound(0.0,parent->property("contentY").toDouble()+point.y()-parent->height()/2,qMax(0.0,parent->property("contentHeight").toDouble()-parent->height())));
    }
    QTest::qWait(50);QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());QTest::qWait(100);};
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
  if(sort){auto button=findItem(w->contentItem(),"collectionSortButton");const auto row=sort->mapRectToScene(sort->boundingRect());const auto anchor=button->mapRectToScene(button->boundingRect());check(qAbs(row.right()-anchor.right())<24&&qAbs(row.top()-anchor.bottom())<220,"sort menu stays aligned with its button within screen limits");auto indicator=qobject_cast<QQuickItem*>(sort->property("indicator").value<QObject*>());auto label=findItem(sort,"menuItemLabel");check(indicator&&label&&indicator->property("ink")==label->property("color"),"sort checkmark uses readable theme foreground");}
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
  auto keyboardView=findItem(w->contentItem(),"tracksView");
  if(keyboardView){
    keyboardView->setProperty("currentIndex",0);keyboardView->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(w,Qt::Key_Down);QTest::qWait(100);
    auto currentRow=findItem(w->contentItem(),"trackRow_1");
    check(keyboardView->property("currentIndex").toInt()==1 && currentRow && currentRow->property("keyboardCurrent").toBool(),"arrow navigation visibly focuses the current track");
    check(currentRow && currentRow->property("selectionVisible").toBool(),"keyboard-focused track exposes its selection control");
    shot("keyboard-track-focus");
  }
  b->collection()->setQuery("Aurora 4");QTest::qWait(250);click("trackRow_0",Qt::ControlModifier);
  auto view=findItem(w->contentItem(),"tracksView");QVariant indices;if(view)QMetaObject::invokeMethod(view,"sourceRows",Q_RETURN_ARG(QVariant,indices));
  check(indices.toList()==QVariantList({4}),"filtered selection maps back to saved source row");
  if(view){
    view->forceActiveFocus(Qt::TabFocusReason);QTest::keyClick(w,Qt::Key_F10,Qt::ShiftModifier);QTest::qWait(200);
    check(w->property("modalOpen").toBool() && w->property("menuIndex").toInt()==4,"keyboard context menu targets the filtered source track");
    shot("keyboard-track-menu");QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(200);
  }

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
    click("lyricSearchButton");QTest::keyClick(w,Qt::Key_A,Qt::ControlModifier);for(char c:QByteArray("absent"))QTest::keyClick(w,c);QTest::qWait(150);check(results&&results->property("count").toInt()==0,"lyric search has an empty result state");QTest::keyClick(w,Qt::Key_Up);check(results&&results->property("currentIndex").toInt()==-1,"empty lyric search keeps no keyboard selection");QTest::keyClick(w,Qt::Key_Escape);check(w->property("side").toString()=="lyrics","Escape closes lyric search without closing the panel");}
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
  auto folderPath=findItem(w->contentItem(),"musicFolderPath");check(until([&]{return folderPath&&folderPath->hasActiveFocus();},2000),"folder path entry opens with keyboard focus");
  const auto musicDir=dir+"/Music #100%";QDir().mkpath(musicDir+"/Artist/Album");QFile::copy(audioPath,musicDir+"/Artist/Album/Folder song.wav");
  if(folderPath)folderPath->setProperty("text","relative/path");
  click("confirmMusicFolderButton");
  auto pathError=findItem(w->contentItem(),"musicFolderPathError");check(pathError&&pathError->isVisible()&&!pathError->property("text").toString().isEmpty(),"invalid folder path stays open with inline error");shot("folder-path-error");
  if(folderPath)folderPath->setProperty("text",musicDir);
  click("browseMusicFolderButton");
  auto dialogs=w->property("fileDialogs").value<QObject*>();auto folderPicker=dialogs?dialogs->property("folderPicker").value<QObject*>():nullptr;
  check(folderPicker&&folderPicker->property("visible").toBool(),"optional native folder picker opens");
  if(folderPicker)QMetaObject::invokeMethod(folderPicker,"reject");
  QTest::qWait(300);check(folderPath&&folderPath->isVisible()&&folderPath->property("text")==musicDir,"canceling native picker restores typed path");shot("folder-path-entry");
  click("confirmMusicFolderButton");
  check(until([&]{return !b->importingLocal();},10000)&&b->musicFolders().contains(musicDir),"typed folder path imports and remembers root without native selection");
  check(b->results()->count()==2,"typed folder import adds nested audio");
  click("musicFoldersButton");click("addMusicFolderButton");click("browseMusicFolderButton");
  if(folderPicker){folderPicker->setProperty("selectedFolder",QUrl::fromLocalFile(musicDir));QMetaObject::invokeMethod(folderPicker,"accept");}
  QTest::qWait(200);QWindowSystemInterface::handleFocusWindowChanged(nullptr);QWindowSystemInterface::handleFocusWindowChanged(w);
  check(folderPath&&folderPath->isVisible()&&folderPath->property("text").toString().startsWith("file:"),"native selection returns to explicit folder confirmation");click("confirmMusicFolderButton");
  check(until([&]{return !b->importingLocal();},10000)&&b->results()->count()==2,"native folder selection also imports without duplicates");
  click("musicFoldersButton");shot("music-folders");QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(200);
  click("rescanFoldersButton");check(until([&]{return !b->importingLocal();},5000)&&b->results()->count()==2,"Rescan skips unchanged files without duplicate entries");
  b->forgetMusicFolder(musicDir);check(b->results()->count()==2&&QFile::exists(musicDir+"/Artist/Album/Folder song.wav"),"forgetting folder keeps music and original files");
  b->removeLocalFile(local.value("id").toString());check(QFile::exists(audioPath),"removing local library entry preserves original audio");b->resetLyrics();
  b->openPlaylist(id);QTest::qWait(100);w->setProperty("side","");w->setProperty("collectionTools",true);b->collection()->setQuery("");b->collection()->setSortKey("original");
  w->resize(780,580);QTest::qWait(300);click("collectionSortButton");
  auto sortItem=findItem(w->contentItem(),"sort_original");
  if(sortItem){
    auto indicator=qobject_cast<QQuickItem*>(sortItem->property("indicator").value<QObject*>());
    auto label=findItem(sortItem,"menuItemLabel");
    check(indicator&&indicator->isVisible()&&label&&label->x()+label->property("leftPadding").toReal()>=indicator->x()+indicator->width()+8,"Material menu checkmark leaves readable label spacing");
  }else check(false,"sort menu exposes current ordering");
  shot("compact-sort-menu");QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(200);
  click("settingsButton");shot("compact-settings-light");QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(200);
  b->setTheme("dark");QTest::qWait(200);click("settingsButton");shot("compact-settings-dark");QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(200);
  w->resize(1180,900);
  // Hover actions must remain available when keyboard focus moves onto them.
  QQmlComponent cardComponent(qmlEngine(w),QUrl("qrc:/qml/ArtCard.qml"));
  QScopedPointer<QObject> cardObject(cardComponent.create(qmlContext(w)));
  auto card=qobject_cast<QQuickItem*>(cardObject.data());
  check(card,"cover card can be instantiated for focus regression");
  if(card){
    card->setParentItem(w->contentItem());card->setX(120);card->setY(100);card->setZ(100);
    card->setProperty("track",QVariantMap{{"title","Keyboard focus"},{"kind","artist"}});
    QTest::mouseMove(w,QPoint(2,2));auto action=findItem(card,"cardAction");
    card->setProperty("focus",true);
    if(action){action->forceActiveFocus(Qt::TabFocusReason);QTest::qWait(500);check(action->hasActiveFocus()&&action->isVisible()&&action->opacity()>0.99,"card action stays visible when keyboard-focused without hover");}
    else check(false,"card action exists");
  }
  b->stop();b->deletePlaylist(id);b->clearQueue();fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);QCoreApplication::exit(failures?2:0);
}


void runVisualPolishTests(Backend *b, QQuickWindow *w) {
  int failures=0;
  const auto dir=qEnvironmentVariable("SUNG_TEST_OUTPUT");QDir().mkpath(dir);
  auto check=[&](bool ok,const char *label){fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",label);fflush(stdout);if(!ok)++failures;};
  auto shot=[&](const char *name){check(w->grabWindow().save(dir+"/"+name+".png"),name);};
  auto until=[](std::function<bool()> predicate){QElapsedTimer timer;timer.start();while(!predicate()&&timer.elapsed()<8000)QTest::qWait(20);return predicate();};
  QWindowSystemInterface::handleFocusWindowChanged(w);QTest::qWait(50);
  b->setVolume(0);b->setAutoplay(false);b->setMotion(true);b->setTheme("dark");
  w->resize(1180,800);b->home();check(until([&]{return !b->busy();}),"fixture home loads");
  QQmlComponent component(qmlEngine(w),QUrl("qrc:/qml/MBusyIndicator.qml"));
  QScopedPointer<QObject> object(component.create(qmlContext(w)));
  auto spinner=qobject_cast<QQuickItem*>(object.data());
  check(spinner,"Material loading component creates");
  if(spinner){
    spinner->setParentItem(w->contentItem());spinner->setX(600);spinner->setY(360);spinner->setZ(90);spinner->setProperty("running",true);
    QTest::qWait(120);check(spinner->property("animating").toBool(),"visible loading indicator animates");shot("01-loading-dark");
    auto bounds=spinner->mapRectToScene(spinner->boundingRect()).toAlignedRect().adjusted(-12,-12,12,12);
    auto first=w->grabWindow().copy(bounds);QTest::qWait(240);
    const bool samplePixels=!qEnvironmentVariableIsSet("SUNG_TEST_BACKGROUND_ACTIVATION");
    if(samplePixels)check(first!=w->grabWindow().copy(bounds),"loading arc visibly advances");
    else fprintf(stdout,"SKIP animation pixel comparisons on compositor-throttled background workspace; offscreen run covers them\n");
    b->setMotion(false);QTest::qWait(150);check(!spinner->property("animating").toBool(),"reduced motion stops spinner animation");
    first=w->grabWindow().copy(bounds);QTest::qWait(160);if(samplePixels)check(first==w->grabWindow().copy(bounds),"reduced-motion indicator remains visually stable");
    b->setTheme("light");QTest::qWait(50);check(spinner->property("ink").value<QColor>()==QColor("#964829"),"loading indicator follows light palette");shot("02-loading-light");
    b->setMotion(true);spinner->setVisible(false);QTest::qWait(50);check(!spinner->property("animating").toBool(),"hidden loading indicator stops animation");
    spinner->setVisible(true);spinner->setProperty("running",false);QTest::qWait(50);check(!spinner->property("animating").toBool(),"finished loading indicator stops animation");
    spinner->setVisible(false);
  }
  b->setTheme("dark");
  auto play=findItem(w->contentItem(),"playButton");check(play,"playback control exists");
  qputenv("SUNG_BUFFER_FIXTURE","1");
  b->playItem({{"id","spinner0003"},{"videoId","spinner0003"},{"title","Loading playback"},{"artist","Test fixture"},{"kind","song"}});
  QTest::qWait(80);
  check(b->resolving()&&play&&play->property("busy").toBool(),"playback preparation exposes loading state");
  if(play){
    auto ring=findItem(play,"buttonSpinner");check(ring&&ring->isVisible()&&ring->width()==24,"button loading indicator stays inside icon slot");
    check(play->property("tip").toString().startsWith("Pause"),"loading playback retains Pause action");
    shot("03-loading-playback");
    QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,play->mapToScene(QPointF(play->width()/2,play->height()/2)).toPoint());
    check(until([&]{return !b->resolving();})&&!b->playing(),"clicking loading playback cancels preparation");
    play->forceActiveFocus();QTest::keyClick(w,Qt::Key_Tab);QTest::keyClick(w,Qt::Key_Backtab);QTest::qWait(50);auto focus=findItem(play,"buttonFocusRing");
    check(focus&&focus->isVisible()&&focus->width()>play->width(),"filled playback button exposes distinct keyboard focus ring");shot("04-keyboard-focus");
  }
  b->stop();b->dismissError();
  b->search("Test","songs");check(until([&]{return !b->busy();}),"fixture search loads");
  if(auto list=findItem(w->contentItem(),"tracksView")){
    list->forceActiveFocus();QTest::keyClick(w,Qt::Key_End);QTest::qWait(100);
    const int last=list->property("count").toInt()-1;
    check(last>10&&list->property("currentIndex").toInt()==last,"End reaches the last track in a long list");
    QTest::keyClick(w,Qt::Key_Home);QTest::keyClick(w,Qt::Key_PageDown);QTest::qWait(80);
    const int pageRow=list->property("currentIndex").toInt();
    check(pageRow>1&&pageRow<last,"Page Down advances by a visible page of tracks");
    QTest::keyClick(w,Qt::Key_PageUp);check(list->property("currentIndex").toInt()==0,"Page Up returns to the first page");
    QTest::keyClick(w,Qt::Key_End,Qt::ShiftModifier);QTest::qWait(120);
    auto selection=qobject_cast<RowSelection*>(list->property("selection").value<QObject*>());
    check(selection&&selection->count()==last+1&&selection->contains(0)&&selection->contains(last),"Shift+End selects from the focused row through the last track");
    auto row=findItem(list,"trackRow_"+QString::number(last));auto title=row?findItem(row,"trackTitle"):nullptr;
    check(title&&title->property("color").value<QColor>()==QColor("#ffdbcb"),"selected track uses its container foreground color");shot("11-keyboard-range");
    if(selection)selection->clear();
    QTest::keyClick(w,Qt::Key_Home);
  }else check(false,"track list exists for page navigation");
  w->resize(1000,700);w->setProperty("side","queue");QTest::qWait(600);
  auto filters=findItem(w->contentItem(),"searchFilters");check(filters,"search filter container exists");
  bool contained=filters!=nullptr;
  for(const auto &name:{"filter_songs","filter_albums","filter_artists","filter_playlists","filter_videos"}){
    auto item=findItem(w->contentItem(),name);
    if(!item||!filters){contained=false;continue;}
    const auto rect=item->mapRectToItem(filters,item->boundingRect());
    contained=contained&&rect.left()>=-1&&rect.right()<=filters->width()+1&&rect.bottom()<=filters->height()+1;
  }
  check(contained,"all search filters fit beside queue at breakpoint");shot("05-search-with-queue");
  w->setProperty("side","");w->resize(780,580);QTest::qWait(450);
  auto field=findItem(w->contentItem(),"searchField");
  b->rememberSearch("Evening mix");
  if(field){
    field->setProperty("text","");field->forceActiveFocus();QMetaObject::invokeMethod(field,"updateSuggestions");QTest::qWait(350);
    auto labels=findItem(w->contentItem(),"suggestionLabels");
    check(labels&&qAbs(labels->y()+labels->height()/2-labels->parentItem()->height()/2)<1,"recent search text is vertically centered");shot("07-recent-search");
    if(auto suggestion=findItem(w->contentItem(),"suggestion_0")){
      QTest::mouseMove(w,suggestion->mapToScene(QPointF(24,suggestion->height()/2)).toPoint());QTest::qWait(200);
      auto state=findItem(suggestion->parentItem(),"suggestionStateLayer");
      check(suggestion->property("hovered").toBool()&&state&&state->opacity()>0.07,"search suggestion gives visible pointer hover feedback");shot("12-search-hover");
      QTest::keyClick(w,Qt::Key_Down);QTest::keyClick(w,Qt::Key_Delete,Qt::ShiftModifier);QTest::qWait(80);
      check(!b->recentSearches().contains("Evening mix"),"Shift+Delete removes the highlighted recent search");
      QTest::mouseMove(w,QPoint(2,2));
      if(!qEnvironmentVariableIsSet("SUNG_TEST_BACKGROUND_ACTIVATION")){
        QTest::keyClick(w,Qt::Key_Z);QTest::keyClick(w,Qt::Key_Down);
        auto results=findItem(w->contentItem(),"suggestionList");
        check(results&&results->property("count").toInt()==0&&field->property("highlighted").toInt()==-1,"fast arrow navigation cannot choose stale suggestions for a new query");
        field->setProperty("text","");QMetaObject::invokeMethod(field,"updateSuggestions");QTest::qWait(200);
      }
    }else check(false,"recent suggestion exposes its action");
    if(qEnvironmentVariableIsSet("SUNG_TEST_BACKGROUND_ACTIVATION")){
      // A hidden workspace can revoke keyboard focus; set up the native layout
      // sample directly. The offscreen path exercises real key delivery.
      field->setProperty("text","Loading");field->setProperty("dismissed",false);field->forceActiveFocus();
      QMetaObject::invokeMethod(field,"updateSuggestions");
      if(auto suggestions=findItem(w->contentItem(),"suggestionList"))QMetaObject::invokeMethod(suggestions,"forceLayout");
      w->grabWindow();
    }else{for(char c:QByteArray("Loading")){QTest::keyClick(w,c);}}
    QTest::qWait(350);
    labels=findItem(w->contentItem(),"suggestionLabels");
    check(labels&&labels->height()>25&&qAbs(labels->y()+labels->height()/2-labels->parentItem()->height()/2)<1,"two-line song suggestion is vertically centered");shot("08-song-suggestion");
    QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(200);w->contentItem()->forceActiveFocus();
  }else check(false,"search input exists for suggestion alignment");
  emit b->toast(QString("Imported a very long album and playlist name ").repeated(12));QTest::qWait(200);
  auto toast=findItem(w->contentItem(),"toastBar");
  check(toast&&toast->width()<=w->width()-48&&toast->height()>48,"long notification wraps within compact window");shot("06-compact-notification");
  if(auto nav=findItem(w->contentItem(),"nav_library"))QMetaObject::invokeMethod(nav,"clicked");
  QTest::qWait(250);
  auto liked=findItem(w->contentItem(),"likedTab");auto playlists=findItem(w->contentItem(),"playlistsTab");
  auto history=findItem(w->contentItem(),"serverTab");auto tabs=findItem(w->contentItem(),"libraryTabs");
  check(liked&&playlists&&history&&tabs,"library navigation tabs exist");
  if(liked&&playlists&&history&&tabs){
    liked->forceActiveFocus(Qt::TabFocusReason);QTest::keyClick(w,Qt::Key_Right);QTest::qWait(50);
    check(playlists->hasActiveFocus(),"Right arrow moves library tab focus without seeking");
    QTest::keyClick(w,Qt::Key_Space);QTest::qWait(100);
    check(w->property("libraryTab").toString()=="playlists","keyboard activates library destination");
    auto ring=findItem(playlists,"tabFocusRing");check(ring&&ring->isVisible(),"keyboard-focused library tab has a distinct focus outline");
    QTest::keyClick(w,Qt::Key_Tab);QTest::qWait(50);
    check(!w->activeFocusItem()||!w->activeFocusItem()->property("libraryNavigation").toBool(),"Tab exits the tab strip instead of visiting each tab");
    QTest::keyClick(w,Qt::Key_Backtab);QTest::qWait(50);
    check(playlists->hasActiveFocus(),"Shift+Tab re-enters at the selected library tab");
    QQmlProperty(tabs,"Layout.maximumWidth",qmlContext(w)).write(240);w->grabWindow();QTest::qWait(50);
    QTest::keyClick(w,Qt::Key_End);QTest::qWait(80);
    auto rect=history->mapRectToItem(tabs,history->boundingRect());
    check(history->hasActiveFocus()&&tabs->property("contentX").toDouble()>0&&rect.left()>=-1&&rect.right()<=tabs->width()+1,"focused last tab scrolls into compact tab viewport");
    QTest::keyClick(w,Qt::Key_Return);QTest::qWait(100);
    check(w->property("libraryTab").toString()=="server","last library tab remains actionable when scrolled");
    shot("09-library-tabs");
    QQmlProperty(tabs,"Layout.maximumWidth",qmlContext(w)).write(1000);w->grabWindow();QTest::qWait(100);
    check(tabs->property("contentX").toDouble()==0,"expanding the tab strip restores the left edge without blank space");
  }
  if(auto settings=findItem(w->contentItem(),"settingsButton"))QMetaObject::invokeMethod(settings,"clicked");
  QTest::qWait(450);w->grabWindow();
  auto settingsBar=findItem(w->contentItem(),"settingsScrollBar");
  auto thumb=settingsBar?qobject_cast<QQuickItem*>(settingsBar->property("contentItem").value<QObject*>()):nullptr;
  if(settingsBar)fprintf(stdout,"SCROLL_GEOMETRY visible=%d width=%.1f height=%.1f size=%.3f thumb=%.1f opacity=%.1f\n",settingsBar->isVisible(),settingsBar->width(),settingsBar->height(),settingsBar->property("size").toDouble(),thumb?thumb->height():-1,thumb?thumb->opacity():-1);
  check(settingsBar&&settingsBar->isVisible()&&settingsBar->width()>0&&thumb&&thumb->height()>0&&thumb->opacity()>0,"overflowing Settings exposes a visible scroll thumb");shot("10-settings-scroll");
  QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(180);
  QQmlComponent sliderComponent(qmlEngine(w),QUrl("qrc:/qml/SettingSlider.qml"));
  QScopedPointer<QObject> sliderObject(sliderComponent.create(qmlContext(w)));
  auto slider=qobject_cast<QQuickItem*>(sliderObject.data());
  check(slider,"settings slider creates");
  if(slider){
    slider->setParentItem(w->contentItem());slider->setX(140);slider->setY(220);slider->setWidth(300);slider->setZ(90);
    slider->setProperty("value",0.5);QTest::qWait(80);
    auto active=findItem(slider,"sliderActiveTrack"),inactive=findItem(slider,"sliderInactiveTrack");
    const auto center=slider->property("thumbCenter").toDouble();
    check(active&&inactive&&qAbs(active->width()-(center-8))<0.1&&qAbs(inactive->x()-(center+8))<0.1,"slider keeps six-pixel gaps on both sides of handle");
    slider->forceActiveFocus();QTest::keyClick(w,Qt::Key_Right);QTest::qWait(50);
    check(slider->property("value").toDouble()>0.5,"settings slider retains keyboard adjustment");
    slider->setProperty("value",0.0);QTest::qWait(50);check(active&&active->width()==0,"slider minimum has no negative track width");
    slider->setProperty("value",1.0);QTest::qWait(50);check(inactive&&inactive->width()==0,"slider maximum has no negative track width");
  }

  if(slider)slider->setVisible(false);
  b->dismissError();w->setProperty("toastPending",false);w->setProperty("side","");
  QMetaObject::invokeMethod(w,"openServerConnection");QTest::qWait(450);
  auto address=findItem(w->contentItem(),"serverAddress"),username=findItem(w->contentItem(),"serverUsername"),password=findItem(w->contentItem(),"serverPassword");
  check(address&&username&&password,"connection form exposes all inputs");
  if(address&&username&&password){
    check(address->hasActiveFocus(),"connection dialog initially focuses its first empty field");
    address->setProperty("text","https://music.example.test");username->setProperty("text","listener");password->setProperty("text","fixture-only");QTest::qWait(250);
    bool labels=true;
    for(auto field:{address,username,password}){
      auto label=findItem(field,"fieldLabel");
      labels=labels&&label&&label->isVisible()&&!label->property("text").toString().isEmpty()&&field->property("floatingLabel").toBool();
    }
    check(labels,"populated fields retain visible floating labels");
    auto fields=findItem(w->contentItem(),"connectionFields");
    auto addressLabel=findItem(address,"fieldLabel");
    check(fields&&addressLabel&&addressLabel->mapRectToItem(fields,addressLabel->boundingRect()).top()>=0,"first floating label stays inside the scroll content");
    check(password->property("displayText").toString()!=password->property("text").toString(),"password remains masked");
    address->forceActiveFocus();QTest::keyClick(w,Qt::Key_Return);check(username->hasActiveFocus(),"Return advances from address to username");
    QTest::keyClick(w,Qt::Key_Return);check(password->hasActiveFocus(),"Return advances from username to password");
    shot("13-labeled-connection");
    QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(200);
    check(password->property("text").toString().isEmpty(),"dismissed connection form clears its password");
  }
  w->setProperty("menuItem",QVariantMap{{"rating",3}});
  auto rating=w->findChild<QObject*>("serverRatingDialog");
  check(rating,"rating dialog exists");
  if(rating){
    QMetaObject::invokeMethod(rating,"open");QTest::qWait(450);
    auto content=qobject_cast<QQuickItem*>(rating->property("contentItem").value<QObject*>());
    bool fits=content!=nullptr;
    for(int i=1;i<=5;++i){
      auto button=findItem(w->contentItem(),"rating_"+QString::number(i));
      if(!button||!content){fits=false;continue;}
      auto bounds=button->mapRectToItem(content,button->boundingRect());
      fits=fits&&button->width()>=48&&button->height()>=48&&bounds.left()>=0&&bounds.right()<=content->width();
    }
    check(fits,"all five rating targets fit the dialog at compact width");shot("14-rating-dialog");
    QMetaObject::invokeMethod(rating,"close");QTest::qWait(200);
  }
  QQmlComponent menuComponent(qmlEngine(w));
  menuComponent.setData(R"(import QtQuick
import QtQuick.Controls
import "qrc:/qml"
MMenu { Repeater { model: 30; MMenuItem { required property int index; objectName: "longMenu_"+index; text: "Menu choice "+index } } })",QUrl("qrc:/qml/AuditMenu.qml"));
  QScopedPointer<QObject> longMenu(menuComponent.create(qmlContext(w)));
  check(!longMenu.isNull(),"long menu fixture creates");
  if(longMenu){
    longMenu->setProperty("parent",QVariant::fromValue(w->contentItem()));longMenu->setProperty("x",400);longMenu->setProperty("y",24);
    QMetaObject::invokeMethod(longMenu.data(),"open");QTest::qWait(350);
    auto scroll=findItem(w->contentItem(),"menuScrollBar");
    check(scroll&&scroll->isVisible()&&scroll->height()>0,"overflow menu exposes a persistent scroll thumb");
    // Menu keyboard navigation must still scroll its selected item into view.
    for(int i=0;i<30;++i)QTest::keyClick(w,Qt::Key_Down);
    QTest::qWait(100);
    auto content=qobject_cast<QQuickItem*>(longMenu->property("contentItem").value<QObject*>());
    check(content&&content->property("contentY").toReal()>0,"keyboard can reach offscreen menu choices");shot("15-scrollable-menu");
    QMetaObject::invokeMethod(longMenu.data(),"close");QTest::qWait(200);
  }
  b->clearQueue();w->setProperty("toastPending",false);
  b->enqueue({{"id","audit000001"},{"videoId","audit000001"},{"title","Audit song"},{"kind","song"}});
  b->clearQueue();QTest::qWait(6300);
  toast=findItem(w->contentItem(),"toastBar");
  check(toast&&toast->isVisible()&&w->property("toastHasUndo").toBool(),"Undo stays available beyond the old six-second timeout");shot("16-persistent-undo");
  b->browseServer();check(until([&]{return !b->busy();}),"disconnected server state settles");QTest::qWait(180);
  auto error=findItem(w->contentItem(),"errorBar");
  check(error&&error->isVisible()&&toast&&!toast->isVisible(),"error and status snackbars never overlap");
  auto connect=findItem(w->contentItem(),"serverEmptyConnect");
  check(connect&&connect->isVisible(),"disconnected server page exposes direct connection action");
  b->dismissError();QTest::qWait(180);
  check(toast&&toast->isVisible(),"pending Undo returns after an error is dismissed");
  auto undo=findItem(w->contentItem(),"toastUndo");
  if(undo)QMetaObject::invokeMethod(undo,"clicked");
  check(b->queue()->count()==1,"persistent Undo restores removed songs");
  w->setProperty("toastPending",false);shot("17-server-empty");
  b->clearQueue();fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);QCoreApplication::exit(failures?2:0);
}

void runServerTests(Backend *b,QQuickWindow *w) {
  int failures=0;const auto dir=qEnvironmentVariable("SUNG_TEST_OUTPUT");QDir().mkpath(dir);
  auto check=[&](bool ok,const char *label){fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",label);fflush(stdout);if(!ok)++failures;};
  auto until=[](std::function<bool()> predicate){QElapsedTimer time;time.start();while(!predicate()&&time.elapsed()<12000)QTest::qWait(25);return predicate();};
  auto click=[&](const char *name){auto item=findItem(w->contentItem(),name);check(item,qPrintable(QString("find ")+name));if(item){QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());QTest::qWait(200);}};
  auto shot=[&](const char *name){QTest::qWait(400);check(w->grabWindow().save(dir+"/"+name+".png"),name);};
  QWindowSystemInterface::handleFocusWindowChanged(w);w->resize(1180,800);b->setTheme("dark");b->setVolume(0);b->setMotion(true);b->setAutoplay(false);
  QMetaObject::invokeMethod(w,"openServerConnection");QTest::qWait(500);
  auto address=findItem(w->contentItem(),"serverAddress"),user=findItem(w->contentItem(),"serverUsername"),password=findItem(w->contentItem(),"serverPassword");
  check(address&&user&&password,"server connection fields render");
  if(address&&user&&password){address->setProperty("text",qEnvironmentVariable("SUNG_TEST_SERVER"));user->setProperty("text",qEnvironmentVariable("SUNG_TEST_USER"));password->setProperty("text",qEnvironmentVariable("SUNG_TEST_PASSWORD"));}
  // Tests never write a real keyring entry.
  if(auto remember=findItem(w->contentItem(),"rememberServer"))remember->setProperty("checked",false);
  shot("connection");w->resize(780,580);QTest::qWait(450);
  auto connectButton=findItem(w->contentItem(),"connectServerButton");
  check(connectButton&&connectButton->isVisible()&&connectButton->mapRectToScene(connectButton->boundingRect()).bottom()<w->height()-16,"Connect action stays visible in compact dialog footer");
  shot("compact-login");click("connectServerButton");check(until([&]{return b->server()->connected();}),"connect through visible UI");
  QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(450);check(password&&password->property("text").toString().isEmpty(),"password field cleared on close");
  w->resize(1180,800);
  b->browseServer("albums");check(until([&]{return !b->busy();}),"server albums load");shot("albums-dark");
  check(w->property("destination")=="library"&&w->property("libraryTab")=="server","server navigation stays selected");
  auto search=findItem(w->contentItem(),"searchField");check(search&&search->isVisible(),"server search field visible");
  if(search){search->setProperty("text","Fixture");search->forceActiveFocus();QTest::keyClick(w,Qt::Key_Return);}
  check(until([&]{return !b->busy()&&b->results()->count()==100;}),"server search through keyboard");shot("songs-dark");
  click("trackRow_0");check(until([&]{return b->playing();}),"server song plays from rendered row");
  check(until([&]{auto row=findItem(w->contentItem(),"trackRow_0");if(!row)return false;auto art=row->findChild<RoundedArt*>();return art&&art->ready();}),"authenticated cover art renders");
  b->fetchLyrics();check(until([&]{return !b->lyricsBusy();})&&!b->lyricLines().isEmpty(),"server live lyrics load");
  QMetaObject::invokeMethod(w,"activateSide",Q_ARG(QVariant,QVariant("lyrics")));QTest::qWait(500);shot("lyrics-dark");
  auto row=findItem(w->contentItem(),"trackRow_0");check(row&&row->property("selectable").toBool(),"server rows support selection");
  if(row){row->forceActiveFocus();QTest::keyClick(w,Qt::Key_Menu);QTest::qWait(300);shot("song-menu");QTest::keyClick(w,Qt::Key_Escape);}
  b->setTheme("light");w->resize(780,580);QTest::qWait(500);shot("compact-light");QMetaObject::invokeMethod(w,"activateSide",Q_ARG(QVariant,QVariant("lyrics")));QTest::qWait(400);shot("compact-library-light");check(findItem(w->contentItem(),"tracksView")->height()>=64,"compact server view keeps songs visible");check(validIconSizes(w->contentItem()),"server controls preserve icon geometry");
  QMetaObject::invokeMethod(w,"openServerConnection");QTest::qWait(400);shot("compact-connection");
  if(auto dialog=w->findChild<QObject*>("serverConnectionDialog"))check(dialog->property("height").toReal()<=w->height()-48,"connection dialog fits compact window");
  QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(350);
  b->pause();b->server()->disconnectServer();b->browseServer();check(until([&]{return !b->busy();})&&!b->error().isEmpty(),"disconnected view reports actionable error");
  shot("disconnected");fprintf(stdout,"RESULT %d failures\n",failures);QCoreApplication::exit(failures?1:0);
}

void runRemoteServerTest(Backend *b,QQuickWindow *) {
  auto until=[](std::function<bool()> predicate,int ms){QElapsedTimer timer;timer.start();while(!predicate()&&timer.elapsed()<ms)QTest::qWait(50);return predicate();};
  int failures=0;auto check=[&](bool ok,const char *name){fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",name);fflush(stdout);if(!ok)++failures;};
  b->setVolume(0);b->setAutoplay(false);b->server()->setScrobbling(false);
  b->server()->connectServer("https://demo.navidrome.org","demo","demo",false);
  check(until([&]{return !b->server()->connecting();},30000)&&b->server()->connected(),"public Navidrome demo connects over HTTPS");
  if(b->server()->connected()){
    b->browseServer("random");check(until([&]{return !b->busy();},30000)&&b->results()->count()>0,"remote HTTPS catalog returns music");
    if(b->results()->count()>0){b->playResults(0);check(until([&]{return b->playing()&&b->position()>1000;},180000),"remote HTTPS audio buffers and plays");b->stop();}
  }
  b->server()->disconnectServer();fprintf(stdout,"RESULT %d failures\n",failures);QCoreApplication::exit(failures?1:0);
}

void runQolTests(Backend *b,QQuickWindow *w) {
  int failures=0;const auto dir=qEnvironmentVariable("SUNG_TEST_OUTPUT");QDir().mkpath(dir);
  auto check=[&](bool ok,const char *name){fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",name);fflush(stdout);if(!ok)++failures;};
  auto until=[](std::function<bool()> p){QElapsedTimer t;t.start();while(!p()&&t.elapsed()<10000)QTest::qWait(25);return p();};
  auto shot=[&](const char *name){QTest::qWait(400);check(w->grabWindow().save(dir+"/"+name+".png"),name);};
  QWindowSystemInterface::handleFocusWindowChanged(w);w->resize(1180,800);b->setMotion(true);b->setTheme("dark");b->setVolume(0);b->setAutoplay(false);b->setPrepareNext(false);
  b->search("QOL","songs");check(until([&]{return !b->busy();}),"search loads");QTest::qWait(200);
  auto tracks=findItem(w->contentItem(),"tracksView");check(tracks,"song list exists");
  b->collection()->setSortKey("title");b->collection()->setQuery("Track");QTest::qWait(100);
  if(tracks){tracks->setProperty("contentY",700.0);}
  QTest::qWait(100);const auto saved=tracks?tracks->property("contentY").toDouble():0;
  b->library("favorites");QTest::qWait(100);b->search("QOL","songs");check(until([&]{return !b->busy();}),"revisited search loads");QTest::qWait(250);
  check(b->collection()->query()=="Track"&&b->collection()->sortKey()=="title","revisited search restores filter and sort");
  check(tracks&&qAbs(tracks->property("contentY").toDouble()-saved)<5&&saved>300,"revisited search restores scroll position");shot("01-restored-view");
  QMetaObject::invokeMethod(w,"focusSearch");QTest::keyClick(w,Qt::Key_Question);QTest::qWait(80);
  auto help=w->findChild<QObject*>("shortcutHelp");check(help&&!help->property("visible").toBool(),"question mark in search does not open help");
  if(tracks){tracks->forceActiveFocus();}
  QTest::keyClick(w,Qt::Key_F1);QTest::qWait(400);
  check(help&&help->property("visible").toBool(),"F1 opens shortcut reference");shot("02-shortcuts");
  QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(250);check(help&&!help->property("visible").toBool(),"Escape closes shortcut reference");
  if(tracks){tracks->forceActiveFocus();}
  QTest::keyClick(w,Qt::Key_Question);QTest::qWait(300);check(help&&help->property("visible").toBool(),"question mark opens help outside text fields");
  QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(200);
  const QVariantMap one{{"id","qol00000001"},{"videoId","qol00000001"},{"title","First song"},{"kind","song"}};
  const QVariantMap two{{"id","qol00000002"},{"videoId","qol00000002"},{"title","Second song"},{"kind","song"}};
  const auto id=b->createPlaylist("QOL playlist");b->addToPlaylist(id,one);
  w->setProperty("batchItems",QVariantList{one,two});QMetaObject::invokeMethod(w,"addPlaylistSelection",Q_ARG(QVariant,id));QTest::qWait(350);
  auto duplicate=w->findChild<QObject*>("duplicateDialog");check(duplicate&&duplicate->property("visible").toBool(),"mixed additions offer Skip duplicates");shot("03-duplicates");
  if(duplicate){QMetaObject::invokeMethod(duplicate,"reject");}
  QTest::qWait(200);b->openPlaylist(id);check(b->results()->count()==1,"cancel does not add songs");
  w->setProperty("batchItems",QVariantList{one,two});QMetaObject::invokeMethod(w,"addPlaylistSelection",Q_ARG(QVariant,id));QTest::qWait(200);if(duplicate)QMetaObject::invokeMethod(duplicate,"accept");QTest::qWait(250);
  check(b->results()->count()==2&&b->undoMessage().contains("skipped 1"),"Skip duplicates adds only new songs and reports count");b->undo();check(b->results()->count()==1,"playlist addition remains undoable");
  qputenv("SUNG_BUFFER_FIXTURE","1");b->clearQueue();QVariantList queue;for(int i=0;i<40;++i)queue.append(one);b->enqueueItems(queue,false,-1);b->playAt(32);
  check(until([&]{return b->playing();}),"fixture playback starts");
  QTest::keyClick(w,Qt::Key_J,Qt::ControlModifier);QTest::qWait(500);
  auto q=findItem(w->contentItem(),"queueView");check(q&&q->property("currentIndex").toInt()==32&&q->hasActiveFocus(),"Ctrl+J focuses exact playing queue occurrence");
  auto row=findItem(w->contentItem(),"queueRow_32");check(row&&q&&row->mapToItem(q,QPointF()).y()>=0&&row->mapToItem(q,QPointF()).y()+row->height()<=q->height()+1,"playing queue occurrence is visible");shot("04-playing-song");
  w->resize(780,580);QTest::qWait(300);QMetaObject::invokeMethod(w,"revealPlaying");QTest::qWait(250);shot("05-compact-queue");
  QTest::keyClick(w,Qt::Key_F1);QTest::qWait(300);shot("06-compact-shortcuts");
  b->stop();b->deletePlaylist(id);b->clearQueue();qunsetenv("SUNG_BUFFER_FIXTURE");
  fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);QCoreApplication::exit(failures?1:0);
}

void runLibraryQolTests(Backend *b,QQuickWindow *w) {
  int failures=0;
  auto check=[&](bool ok,const char *label){fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",label);if(!ok)++failures;};
  const auto dir=qEnvironmentVariable("SUNG_TEST_OUTPUT");QDir().mkpath(dir);
  auto shot=[&](const char *name){QTest::qWait(400);check(w->grabWindow().save(dir+"/"+name+".png"),name);};
  QWindowSystemInterface::handleFocusWindowChanged(w);w->resize(1180,800);b->setVolume(0);b->setAutoplay(false);b->setPrepareNext(false);b->setTheme("dark");
  auto settings=w->findChild<QObject*>("settingsDialog");check(settings,"settings dialog exists");
  if(settings){QMetaObject::invokeMethod(settings,"open");}QTest::qWait(350);
  auto search=findItem(w->contentItem(),"settingsSearch");check(search,"settings search exists");
  if(search){search->forceActiveFocus();QTest::keyClick(w,Qt::Key_V);QTest::keyClick(w,Qt::Key_O);QTest::keyClick(w,Qt::Key_L);}
  QTest::qWait(120);check(settings&&settings->property("searchQuery").toString()=="vol","settings search accepts typing");
  auto volume=findItem(w->contentItem(),"volumeStepButton");auto notification=findItem(w->contentItem(),"trackNotificationsSwitch");
  check(volume&&volume->isVisible()&&notification&&!notification->isVisible(),"settings filters unrelated controls");shot("01-settings-search");
  if(settings){settings->setProperty("searchQuery","no-such-setting");}QTest::qWait(100);auto empty=findItem(w->contentItem(),"settingsNoResults");check(empty&&empty->isVisible(),"settings no matches state");
  if(settings){QMetaObject::invokeMethod(settings,"close");}QTest::qWait(250);
  auto smart=w->findChild<QObject*>("smartPlaylistDialog");check(smart,"smart playlist dialog exists");
  if(smart){QMetaObject::invokeMethod(smart,"edit",Q_ARG(QVariant,QString()));}QTest::qWait(300);
  auto name=findItem(w->contentItem(),"smartName"),artist=findItem(w->contentItem(),"smartArtist");
  if(name){name->setProperty("text","My artist mix");}if(artist){artist->setProperty("text","Example");}shot("02-smart-playlist");
  w->resize(780,580);QTest::qWait(250);shot("03-compact-smart-playlist");
  auto scroll=w->findChild<QObject*>("smartScroll");auto flick=scroll?qvariant_cast<QObject*>(scroll->property("contentItem")):nullptr;
  if(flick){flick->setProperty("contentY",qMax(0.0,flick->property("contentHeight").toDouble()-flick->property("height").toDouble()));}
  QTest::qWait(200);shot("03b-compact-smart-rules");
  if(smart){QMetaObject::invokeMethod(smart,"accept");}QTest::qWait(250);const auto id=b->libraryId();check(!b->smartPlaylist(id).isEmpty(),"smart playlist saves through dialog");
  const QVariantMap online{{"id","qol00000001"},{"videoId","qol00000001"},{"title","A song"},{"artist","Example Artist"},{"seconds",120},{"kind","song"}};
  auto local=online;local.remove("videoId");local["id"]="local_test";local["localPath"]="/tmp/example/Album/An example track.flac";
  const auto saved=b->createPlaylist("Source fixture");b->addItemsToPlaylist(saved,{online,local});b->openPlaylist(id);check(b->results()->count()==2,"saved music updates smart results");
  auto details=w->findChild<QObject*>("trackDetailsDialog");if(details){QMetaObject::invokeMethod(details,"inspect",Q_ARG(QVariant,local));}QTest::qWait(300);check(details&&details->property("visible").toBool(),"track details open");shot("04-track-details");
  if(details){QMetaObject::invokeMethod(details,"close");}QTest::qWait(200);
  w->resize(1180,800);b->clearQueue();b->enqueueItems({online,local,online},false,-1);w->setProperty("side","queue");QTest::qWait(350);
  auto queue=findItem(w->contentItem(),"queueView");check(queue&&queue->property("count").toInt()==3,"queue grouping preserves row count");shot("05-source-grouping");
  if(queue){auto second=findItem(w->contentItem(),"queueRow_1");if(second){QVariant insertion;const auto y=second->mapToItem(queue,QPointF()).y()-10;QMetaObject::invokeMethod(queue,"insertion",Q_RETURN_ARG(QVariant,insertion),Q_ARG(QVariant,y));check(insertion.toInt()==1,"dropping on section heading selects next song");}}
  b->moveQueueRows({1},0);check(b->queue()->get(0).value("id")=="local_test","grouped queue reorders actual songs");b->undo();check(b->queue()->get(1).value("id")=="local_test","grouped queue reorder undo");
  b->deletePlaylist(id);b->deletePlaylist(saved);b->clearQueue();
  fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);QCoreApplication::exit(failures?1:0);
}

void runVisualDelightTests(Backend *b,QQuickWindow *w) {
  int failures=0;auto check=[&](bool ok,const char *s){fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",s);fflush(stdout);if(!ok)++failures;};
  auto until=[](const std::function<bool()> &p){QElapsedTimer t;t.start();while(!p()&&t.elapsed()<8000)QTest::qWait(25);return p();};
  const auto dir=qEnvironmentVariable("SUNG_TEST_OUTPUT");QDir().mkpath(dir);
  auto shot=[&](const char *name){check(w->grabWindow().save(dir+"/"+name+".png"),name);};
  QWindowSystemInterface::handleFocusWindowChanged(w);w->resize(1180,800);b->setVolume(0);b->setAutoplay(false);b->setPrepareNext(false);b->setLyricsFallback(false);b->setTheme("dark");b->setMotion(true);qputenv("SUNG_BUFFER_FIXTURE","1");
  QVariantList songs;for(int i=0;i<4;++i){QImage art(240,240,QImage::Format_RGB32);art.fill(QColor::fromHsv(i*75,130,200));const auto path=dir+QString("/cover-%1.png").arg(i);art.save(path);songs.append(QVariantMap{{"id",QString("delight000%1").arg(i)},{"videoId",QString("delight000%1").arg(i)},{"title",QString("Song %1").arg(i+1)},{"artist","Example artist"},{"kind","song"},{"seconds",60},{"art",QUrl::fromLocalFile(path).toString()}});}
  const auto id=b->createPlaylist("Evening collection");b->addItemsToPlaylist(id,songs);b->library("playlists");QTest::qWait(400);shot("01-playlist-mosaic");
  b->clearQueue();b->enqueueItems(songs,false,-1);b->playAt(0);check(until([&]{return b->playing();}),"fixture playing");
  QFile lrc(dir+"/preview.lrc");check(lrc.open(QIODevice::WriteOnly),"lyric fixture opens");lrc.write("[00:01] First example line\n[00:20] Another example line\n[00:40] Final example line");lrc.close();b->importLyrics(QUrl::fromLocalFile(lrc.fileName()),songs.first().toMap().value("id").toString());
  check(until([&]{return b->lyricLines().size()==3;}),"timed lyrics available for preview");b->openPlaylist(id);QTest::qWait(300);
  auto seek=findItem(w->contentItem(),"seekBar");if(seek){const auto point=seek->mapToScene(QPointF(seek->width()/2,seek->height()/2));QTest::mouseMove(w,point.toPoint());}QTest::qWait(300);
  auto preview=w->findChild<QObject*>("seekPreview");check(preview&&preview->property("visible").toBool(),"seek preview appears on hover");check(seek&&!seek->property("previewLine").toString().isEmpty(),"seek preview contains timed lyric");shot("02-seek-preview");
  QTest::mouseMove(w,QPoint(10,10));QTest::qWait(80);
  auto playing=findItem(w->contentItem(),"playingIndicator");check(playing&&playing->property("animating").toBool(),"playing bars animate");b->toggle();QTest::qWait(80);check(playing&&!playing->property("animating").toBool(),"playing bars stop when paused");
  w->setProperty("side","queue");QTest::qWait(300);b->moveQueueRows({2},0);QTest::qWait(80);shot("03-queue-moving");QTest::qWait(250);check(b->queue()->get(0).value("id")==songs[2].toMap().value("id"),"animated reorder preserves queue order");b->undo();QTest::qWait(280);check(b->queue()->get(0)==songs[0].toMap(),"animated reorder undo preserves queue order");
  b->removeQueueRows({3});QTest::qWait(250);check(b->queue()->count()==3,"animated removal completes");b->undo();QTest::qWait(250);check(b->queue()->count()==4,"animated removal undo completes");
  b->playAt(1);QTest::qWait(110);shot("04-track-transition");check(until([&]{return b->playing();}),"playback survives metadata animation");QTest::qWait(350);
  QMetaObject::invokeMethod(w,"toggleImmersive");QTest::qWait(110);check(w->property("coverFlying").toBool(),"artwork expansion starts");shot("05-artwork-expanding");QTest::qWait(450);check(!w->property("coverFlying").toBool()&&w->property("immersive").toBool(),"artwork expansion completes");shot("06-immersive");
  QMetaObject::invokeMethod(w,"toggleImmersive");QTest::qWait(110);shot("07-artwork-returning");QTest::qWait(450);auto flight=findItem(w->contentItem(),"flyingArtwork");check(flight&&!flight->isVisible()&&flight->property("url").toString().isEmpty(),"transition artwork released after closing");
  b->setMotion(false);QMetaObject::invokeMethod(w,"toggleImmersive");QTest::qWait(100);check(!w->property("coverFlying").toBool(),"reduced motion skips expansion");QMetaObject::invokeMethod(w,"toggleImmersive");QTest::qWait(150);
  QQmlComponent c(qmlEngine(w),QUrl("qrc:/qml/CatalogSkeleton.qml"));auto skeleton=qobject_cast<QQuickItem*>(c.create());check(skeleton,"skeleton component loads");if(skeleton){skeleton->setParentItem(w->contentItem());skeleton->setWidth(640);skeleton->setHeight(350);skeleton->setX(130);skeleton->setY(180);skeleton->setProperty("loading",true);QTest::qWait(200);check(skeleton->isVisible()&&!skeleton->property("animating").toBool(),"loading placeholders respect reduced motion");skeleton->setProperty("cards",true);shot("08-loading-placeholders");b->setMotion(true);QTest::qWait(50);check(skeleton->property("animating").toBool(),"visible placeholders pulse with motion enabled");skeleton->setProperty("loading",false);check(!skeleton->isVisible()&&!skeleton->property("animating").toBool(),"loading animation stops after loading");delete skeleton;}
  QQmlComponent buttonComponent(qmlEngine(w),QUrl("qrc:/qml/MButton.qml"));auto button=qobject_cast<QQuickItem*>(buttonComponent.create());if(button){button->setParentItem(w->contentItem());QMetaObject::invokeMethod(button,"confirm");check(button->property("confirmed").toBool(),"inline confirmation appears");QTest::qWait(1200);check(!button->property("confirmed").toBool(),"inline confirmation clears");delete button;}else check(false,"confirmation component loads");
  bool rapidSettled=true;
  for(int attempt=0;attempt<3;++attempt){
    QMetaObject::invokeMethod(w,"toggleImmersive");QTest::qWait(80);QMetaObject::invokeMethod(w,"toggleImmersive");
    QElapsedTimer settle;settle.start();
    while(w->property("coverFlying").toBool()&&settle.elapsed()<1500)QTest::qWait(20);
    rapidSettled=rapidSettled&&!w->property("coverFlying").toBool()&&!w->property("immersive").toBool();
  }
  check(rapidSettled,"repeated rapid immersive toggles settle within 1.5 seconds");
  QMetaObject::invokeMethod(w,"toggleImmersive");QTest::qWait(80);b->setMotion(false);QTest::qWait(50);check(!w->property("coverFlying").toBool(),"disabling motion cancels active expansion");QMetaObject::invokeMethod(w,"toggleImmersive");QTest::qWait(200);
  b->setMotion(true);w->hide();QTest::qWait(80);b->playAt(0);QTest::qWait(200);check(!w->isVisible(),"hidden player stays hidden during a track change");w->show();QTest::qWait(300);
  b->stop();b->clearQueue();b->deletePlaylist(id);qunsetenv("SUNG_BUFFER_FIXTURE");fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);QCoreApplication::exit(failures?1:0);
}

void runAudioIndicatorTests(Backend *b,QQuickWindow *w) {
  int failures=0;auto check=[&](bool ok,const char *s){fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",s);fflush(stdout);if(!ok)++failures;};
  const auto dir=qEnvironmentVariable("SUNG_TEST_OUTPUT");QDir().mkpath(dir);
  auto until=[](const std::function<bool()> &p){QElapsedTimer t;t.start();while(!p()&&t.elapsed()<5000)QTest::qWait(20);return p();};
  const auto path=dir+"/tone.wav";QProcess encode;encode.start("ffmpeg",{"-nostdin","-v","error","-f","lavfi","-i","sine=frequency=350:sample_rate=48000:duration=12","-c:a","pcm_s16le",path});check(encode.waitForFinished(10000)&&encode.exitCode()==0,"test tone generated");
  QWindowSystemInterface::handleFocusWindowChanged(w);w->resize(1180,800);b->setVolume(0);b->setMotion(true);b->setUiActive(true);b->setAutoplay(false);b->setPrepareNext(false);b->clearQueue();
  b->playItem({{"id","local_meter"},{"localPath",path},{"kind","song"},{"title","Audio-reactive bars"},{"artist","350 Hz fixture"}});w->setProperty("side","queue");
  check(until([&]{return b->playing()&&b->audioLevels()[1].toDouble()>.2;}),"audio buffers drive frequency levels");QTest::qWait(150);
  auto indicator=findItem(w->contentItem(),"playingIndicator");check(indicator,"playing indicator exists");
  for(int i=0;i<5;++i)check(indicator&&findItem(indicator,QString("audioBar_%1").arg(i)),qPrintable(QString("bar %1 exists").arg(i+1)));
  check(b->audioLevels()[1].toDouble()>b->audioLevels()[4].toDouble(),"bass-mid tone is stronger than treble");check(w->grabWindow().save(dir+"/01-playing.png"),"playing screenshot");
  b->pause();QTest::qWait(100);check(b->audioLevels()==QVariantList({0.,0.,0.,0.,0.}),"pause clears measured levels");check(w->grabWindow().save(dir+"/02-paused.png"),"paused screenshot");
  b->toggle();check(until([&]{return b->audioLevels()[1].toDouble()>.2;}),"resume restores audio response");b->setMotion(false);QTest::qWait(100);check(b->audioLevels()==QVariantList({0.,0.,0.,0.,0.})&&indicator&&!indicator->property("animating").toBool(),"reduced motion disables analyzer and bars");b->setMotion(true);
  b->stop();b->clearQueue();fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);QCoreApplication::exit(failures?1:0);
}

void runInteractionTests(Backend *b,QQuickWindow *w) {
  int failures=0;auto check=[&](bool ok,const char *s){fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",s);fflush(stdout);if(!ok)++failures;};
  auto until=[](const std::function<bool()> &p){QElapsedTimer t;t.start();while(!p()&&t.elapsed()<8000)QTest::qWait(25);return p();};
  const auto dir=qEnvironmentVariable("SUNG_TEST_OUTPUT");QDir().mkpath(dir);
  auto shot=[&](const char *name){check(w->grabWindow().save(dir+"/"+name+".png"),name);};
  QWindowSystemInterface::handleFocusWindowChanged(w);w->resize(1180,800);b->setVolume(0);b->setAutoplay(false);b->setPrepareNext(false);b->setLyricsFallback(false);b->setMotion(true);b->setTheme("dark");qputenv("SUNG_BUFFER_FIXTURE","1");
  QVariantList songs;for(int i=0;i<30;++i)songs.append(QVariantMap{{"id",QString("polish%1").arg(i,5,10,QChar('0'))},{"videoId",QString("polish%1").arg(i,5,10,QChar('0'))},{"title",QString("Aurora & <night> %1 — A very long recording title that should remain readable on deliberate hover or keyboard focus").arg(i)},{"artist","Example artist"},{"kind","song"},{"seconds",60}});
  for(int i=0;i<3;++i){QImage cover(120,120,QImage::Format_RGB32);cover.fill(QColor::fromHsv(i*95,140,200));const auto path=dir+QString("/cover-%1.png").arg(i);cover.save(path);auto item=songs[i].toMap();item["art"]=QUrl::fromLocalFile(path).toString();songs[i]=item;}
  const auto id=b->createPlaylist("Evening collection");b->addItemsToPlaylist(id,songs);b->openPlaylist(id);QTest::qWait(300);
  auto list=findItem(w->contentItem(),"tracksView");auto title=findItem(w->contentItem(),"collectionHeaderTitle");check(list&&title,"collection and header exist");
  if(list&&title){check(until([&]{return title->property("font").value<QFont>().pixelSize()==28;}),"expanded header settles");const auto size=title->property("font").value<QFont>().pixelSize();list->setProperty("contentY",220);QTest::qWait(450);check(title->property("font").value<QFont>().pixelSize()<size,"header shrinks while scrolling");shot("01-compact-header");list->setProperty("contentY",0);QTest::qWait(450);check(until([&]{return title->property("font").value<QFont>().pixelSize()==size;}),"header expands at top");}
  b->collection()->setQuery("Aurora night");QTest::qWait(250);auto row=findItem(w->contentItem(),"trackRow_0");auto label=row?findItem(row,"trackTitle"):nullptr;
  check(label&&label->property("text").toString().contains("<b>Aurora")&&label->property("text").toString().contains("&lt;"),"matches highlighted while metadata markup is escaped");shot("02-search-matches");
  if(label){QTest::mouseMove(w,label->mapToScene(QPointF(40,label->height()/2)).toPoint());QTest::qWait(750);auto tip=label->findChild<QObject*>("fullTitleTip");check(tip&&tip->property("visible").toBool(),"truncated title reveals on hover");shot("03-full-title");}
  if(label){
    QTest::mouseMove(w,QPoint(1100,80));QTest::qWait(450);
    check(!label->findChild<QObject*>("fullTitleTip"),"closed title tooltip releases its objects");
    QTest::mouseMove(w,label->mapToScene(QPointF(40,label->height()/2)).toPoint());QTest::qWait(100);
    QTest::mouseMove(w,QPoint(1100,80));QTest::qWait(750);
    check(!label->findChild<QObject*>("fullTitleTip"),"short hover cancels delayed title tooltip");
  }
  QPointer<QQuickItem> focusBeforeTooltip=w->activeFocusItem();
  QQmlComponent buttonComponent(qmlEngine(w),QUrl("qrc:/qml/MButton.qml"));
  auto tooltipButton=qobject_cast<QQuickItem*>(buttonComponent.create());
  check(tooltipButton,"tooltip test button loads");
  if(tooltipButton){
    tooltipButton->setParentItem(w->contentItem());tooltipButton->setPosition(QPointF(950,180));tooltipButton->setZ(100);
    tooltipButton->setProperty("symbol","play");tooltipButton->setProperty("tip","Play fixture");
    tooltipButton->setSize(QSizeF(48,48));w->grabWindow();
    auto findTip=[&]()->QObject*{return tooltipButton->findChild<QObject*>("buttonTip");};
    check(!findTip(),"button tooltip is not allocated before use");
    QTest::mouseMove(w,tooltipButton->mapToScene(tooltipButton->boundingRect().center()).toPoint());QTest::qWait(250);
    check(findTip()&&!findTip()->property("visible").toBool(),"button tooltip preserves delay");
    QTest::qWait(500);
    check(findTip()&&findTip()->property("visible").toBool()&&findTip()->property("parent").value<QQuickItem*>()==tooltipButton,"button tooltip appears anchored to button");
    QTest::mouseMove(w,QPoint(1100,80));QTest::qWait(450);
    check(!findTip(),"button tooltip releases after closing");
    tooltipButton->forceActiveFocus(Qt::TabFocusReason);QTest::qWait(750);
    check(findTip()&&findTip()->property("visible").toBool(),"keyboard focus still reveals button tooltip");
    delete tooltipButton;
    if(focusBeforeTooltip)focusBeforeTooltip->forceActiveFocus(Qt::OtherFocusReason);
  }
  if(qEnvironmentVariableIsSet("SUNG_TOOLTIP_PROBE")){
    b->stop();b->clearQueue();b->deletePlaylist(id);qunsetenv("SUNG_BUFFER_FIXTURE");
    fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);QCoreApplication::exit(failures?1:0);return;
  }
  b->collection()->setQuery("not present");QTest::qWait(250);auto action=findItem(w->contentItem(),"emptyStateAction");check(action&&action->isVisible(),"empty filtered view offers an action");if(action)QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,action->mapToScene(action->boundingRect().center()).toPoint());QTest::qWait(200);check(b->collection()->count()==30,"clear filters restores songs");
  QQmlComponent cardComponent(qmlEngine(w),QUrl("qrc:/qml/ArtCard.qml"));auto card=qobject_cast<QQuickItem*>(cardComponent.create());check(card,"cover component loads");if(card){card->setParentItem(w->contentItem());card->setX(350);card->setY(240);card->setZ(100);card->setProperty("track",QVariantMap{{"kind","local"},{"id",id},{"title","Evening collection"}});QTest::mouseMove(w,QPoint(400,300));QTest::qWait(250);auto play=findItem(card,"cardAction");check(play&&play->property("symbol")=="play"&&play->isVisible(),"collection hover exposes play action");shot("04-cover-play");if(play)QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,play->mapToScene(play->boundingRect().center()).toPoint());check(b->buffering() || b->playing(),"play request exposes loading or ready state");check(until([&]{return b->playing();}),"cover action begins playback");check(until([&]{return !b->buffering();}),"loading feedback clears when audio is ready");check(b->queue()->count()==30&&b->page()=="local"&&b->libraryId()==id,"cover playback preserves page and plays whole local playlist");delete card;}
  b->playCover({{"kind","album"},{"id","fixture-album"}});check(!b->coverPlayId().isEmpty(),"remote cover request exposes loading state");check(until([&]{return b->coverPlayId().isEmpty()&&b->queue()->count()==2;}),"remote cover plays collection without navigation");check(b->libraryId()==id,"remote cover preserves collection page");
  b->playCover({{"kind","album"},{"id","cancel-album"}});b->stop();QTest::qWait(400);check(b->coverPlayId().isEmpty()&&!b->playing(),"stop cancels pending cover playback");
  if(list){list->setProperty("contentY",0);if(auto selection=qobject_cast<RowSelection*>(list->property("selection").value<QObject*>())){selection->select(0,0);selection->select(1,Qt::ControlModifier);selection->select(2,Qt::ControlModifier);}QTest::qWait(250);row=findItem(w->contentItem(),"trackRow_0");auto destination=findItem(w->contentItem(),"trackRow_3");if(row&&destination){auto a=row->mapToScene(QPointF(140,36)).toPoint(),z=destination->mapToScene(QPointF(140,60)).toPoint();QTest::mousePress(w,Qt::LeftButton,Qt::NoModifier,a);QTest::mouseMove(w,a+QPoint(0,30),40);QTest::mouseMove(w,z,40);QTest::qWait(200);auto preview=findItem(w->contentItem(),"trackDragPreview");check(preview&&preview->isVisible(),"artwork stack appears during drag");check(list->property("dropIndex").toInt()>=0,"destination opens insertion gap");shot("05-drag-preview");QTest::keyClick(w,Qt::Key_Escape);QTest::mouseRelease(w,Qt::LeftButton,Qt::NoModifier,z);QTest::qWait(250);check(b->results()->rows==songs,"cancel drag preserves order");check(list->property("dropIndex").toInt()==-1,"cancel closes drop gap");}}
  b->setMotion(false);if(list){if(auto selection=qobject_cast<RowSelection*>(list->property("selection").value<QObject*>()))selection->clear();list->setProperty("contentY",220);}QTest::qWait(50);
  bool offscreenTip=false;for(auto tip:w->findChildren<QObject*>("fullTitleTip"))if(tip->property("visible").toBool())offscreenTip=true;check(!offscreenTip,"scrolling hides offscreen title tooltips");shot("06-reduced-motion");w->resize(900,650);QTest::qWait(300);shot("07-narrow-layout");
  b->stop();b->clearQueue();b->deletePlaylist(id);qunsetenv("SUNG_BUFFER_FIXTURE");fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);QCoreApplication::exit(failures?1:0);
}

void runFolderImportTests(Backend *b,QQuickWindow *w) {
  int failures=0;auto check=[&](bool ok,const char *label){fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",label);fflush(stdout);if(!ok)++failures;};
  auto until=[](const std::function<bool()> &p){QElapsedTimer t;t.start();while(!p()&&t.elapsed()<8000)QTest::qWait(25);return p();};
  const auto dir=qEnvironmentVariable("SUNG_TEST_OUTPUT");QDir().mkpath(dir);
  auto click=[&](const char *name){auto item=findItem(w->contentItem(),name);check(item&&item->isVisible(),name);if(item){QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,item->mapToScene(item->boundingRect().center()).toPoint());QTest::qWait(450);}};
  auto shot=[&](const char *name){check(w->grabWindow().save(dir+"/"+name+".png"),name);};
  QWindowSystemInterface::handleFocusWindowChanged(w);w->resize(1000,750);b->setVolume(0);b->setAutoplay(false);b->setTheme("dark");b->library("files");QTest::qWait(450);
  const auto root=dir+"/Music #100% ü";QDir().mkpath(root+"/Artist/Album");const auto song=root+"/Artist/Album/Example.wav";
  QProcess encode;encode.start("ffmpeg",{"-nostdin","-v","error","-f","lavfi","-i","anullsrc=r=8000:cl=mono","-t","12",song});check(encode.waitForFinished(10000)&&encode.exitCode()==0,"nested audio fixture created");
  click("musicFoldersButton");click("addMusicFolderButton");auto path=findItem(w->contentItem(),"musicFolderPath");
  check(until([&]{return path&&path->hasActiveFocus();}),"folder field receives focus");
  if(path)path->setProperty("text","relative/path");
  click("confirmMusicFolderButton");auto error=findItem(w->contentItem(),"musicFolderPathError");check(error&&error->isVisible(),"invalid path remains editable with inline error");shot("01-invalid-path");
  if(path)path->setProperty("text",root);
  click("browseMusicFolderButton");auto dialogs=w->property("fileDialogs").value<QObject*>();auto picker=dialogs?dialogs->property("folderPicker").value<QObject*>():nullptr;
  check(picker&&picker->property("visible").toBool(),"optional picker opens");if(picker)QMetaObject::invokeMethod(picker,"reject");
  // The offscreen plugin has no compositor to restore focus after a native dialog.
  if(QGuiApplication::platformName()=="offscreen")QWindowSystemInterface::handleFocusWindowChanged(w);
  check(until([&]{return path&&path->hasActiveFocus();})&&path->property("text")==root,"cancel restores path and focus");QTest::qWait(300);shot("02-path-entry");
  auto button=findItem(w->contentItem(),"confirmMusicFolderButton");auto dialog=w->findChild<QObject*>("musicFolderEntry");
  if(button&&dialog){const auto bottom=button->mapRectToScene(button->boundingRect()).bottom();const auto dialogBottom=dialog->property("y").toReal()+dialog->property("height").toReal();check(dialogBottom-bottom>=20,"confirmation button has bottom spacing");}
  click("confirmMusicFolderButton");check(until([&]{return !b->importingLocal();})&&b->musicFolders().contains(root)&&b->results()->count()==1,"manual path recursively imports nested song");shot("03-imported");
  b->playCollection(0);check(until([&]{return b->playing();}),"imported nested song plays");b->pause();
  click("musicFoldersButton");click("addMusicFolderButton");click("browseMusicFolderButton");if(picker){picker->setProperty("selectedFolder",QUrl::fromLocalFile(root));QMetaObject::invokeMethod(picker,"accept");}
  check(until([&]{return path&&path->isVisible();}),"selected folder returns to confirmation");QTest::qWait(450);click("confirmMusicFolderButton");check(until([&]{return !b->importingLocal();})&&b->results()->count()==1,"confirming same folder does not duplicate songs");
  b->stop();b->forgetMusicFolder(root);fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);QCoreApplication::exit(failures?1:0);
}

void runLocalArtworkTests(Backend *b,QQuickWindow *w) {
  int failures=0;auto check=[&](bool ok,const char *label){fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",label);fflush(stdout);if(!ok)++failures;};
  auto until=[](const std::function<bool()> &p,int timeout=8000){QElapsedTimer t;t.start();while(!p()&&t.elapsed()<timeout)QTest::qWait(25);return p();};
  const auto dir=qEnvironmentVariable("SUNG_TEST_OUTPUT");QDir().mkpath(dir);
  auto shot=[&](const char *name){check(w->grabWindow().save(dir+"/"+name+".png"),name);};
  auto click=[&](const char *name,Qt::KeyboardModifiers mods=Qt::NoModifier){w->grabWindow();auto item=findItem(w->contentItem(),name);check(item&&item->isVisible(),name);if(item){QTest::mouseClick(w,Qt::LeftButton,mods,item->mapToScene(QPointF(qMin(120.0,item->width()/2),item->height()/2)).toPoint());QTest::qWait(300);}};
  auto encode=[&](const QStringList &args){QProcess ff;ff.start("ffmpeg",QStringList{"-nostdin","-v","error"}+args);check(ff.waitForFinished(10000)&&ff.exitCode()==0,"generated media fixture");};
  QWindowSystemInterface::handleFocusWindowChanged(w);w->resize(1180,800);b->setVolume(0);b->setAutoplay(false);b->setMotion(true);b->setAnimatedArtwork(true);if(qEnvironmentVariableIsSet("SUNG_TEST_DARK"))b->setTheme("dark");b->library("files");
  const auto root=dir+"/Music #100% ü";QDir().mkpath(root+"/Artist A/Album");QDir().mkpath(root+"/Artist B/Album");
  const auto first=root+"/Artist A/Album/2.wav",second=root+"/Artist A/Album/10.wav",other=root+"/Artist B/Album/1.wav";
  for(const auto &path:{second,other,first})encode({"-f","lavfi","-i","anullsrc=r=8000:cl=mono","-t","120",path});
  const auto format=qEnvironmentVariable("SUNG_ART_FORMAT","gif");
  const auto cover=root+"/Artist A/Album/cover."+format;
  QStringList coverArgs={"-f","lavfi","-i","testsrc2=size=256x256:rate=12:duration=1","-threads","1"};if(format=="gif")coverArgs<<"-loop"<<"0";coverArgs<<cover;encode(coverArgs);
  b->importMusicFolderPath(root);check(until([&]{return !b->importingLocal();},20000)&&b->results()->count()==3,"folder imports all songs with sidecars");
  const auto original=b->results()->rows;click("collectionToolsButton");click("collectionSortButton");click("sort_folder");
  auto view=findItem(w->contentItem(),"tracksView");
  check(b->collection()->sortKey()=="folder" && view && view->property("groupFolders").toBool(),"Folder menu enables grouping");
  check(b->collection()->get(0).value("localPath")==first && b->collection()->get(1).value("localPath")==second && b->collection()->get(2).value("localPath")==other,"natural filename order within distinct folder paths");
  check(b->results()->rows==original,"folder sorting preserves saved order");
  w->grabWindow();QList<QQuickItem*> headings;
  std::function<void(QQuickItem*)> inspect=[&](QQuickItem *item){if(item->objectName()=="folderHeading"&&item->isVisible())headings.append(item);for(auto child:item->childItems())inspect(child);};inspect(w->contentItem());
  check(headings.size()==2,"same-named albums have separate folder headings");
  for(auto heading:headings){const auto rect=heading->mapRectToScene(heading->boundingRect());for(int i=0;i<3;++i){auto row=findItem(w->contentItem(),"trackRow_"+QString::number(i));if(row)check(!rect.intersects(row->mapRectToScene(row->boundingRect())),"folder heading does not overlap song row");}}
  shot("01-folder-groups");
  click("trackRow_0",Qt::ControlModifier);click("trackRow_1",Qt::ControlModifier);
  auto selection=view?qobject_cast<RowSelection*>(view->property("selection").value<QObject*>()):nullptr;
  check(selection&&selection->count()==2&&selection->items().first().toMap().value("localPath")==first,"multi-selection follows folder sort");
  b->clearQueue();b->enqueueItems({b->collection()->get(2)});w->setProperty("side","queue");QTest::qWait(500);w->grabWindow();
  auto from=findItem(w->contentItem(),"trackRow_0"),to=findItem(w->contentItem(),"queueRow_0");
  check(from&&to,"grouped drag endpoints exist");
  if(from&&to){const auto start=from->mapToScene(QPointF(120,from->height()/2)).toPoint(),end=to->mapToScene(QPointF(120,to->height()-4)).toPoint();QTest::mousePress(w,Qt::LeftButton,Qt::NoModifier,start);for(int i=1;i<=16;++i)QTest::mouseMove(w,start+(end-start)*i/16,20);QTest::mouseRelease(w,Qt::LeftButton,Qt::NoModifier,end);QTest::qWait(300);}
  check(b->queue()->count()==3&&b->queue()->get(1).value("localPath")==first&&b->queue()->get(2).value("localPath")==second,"drag selected folder songs into queue preserves display order");b->undo();check(b->queue()->count()==1,"grouped drop supports Undo");
  w->setProperty("side","");b->collection()->setQuery("Artist B/Album");QTest::qWait(250);check(b->collection()->count()==1&&b->collection()->get(0).value("localPath")==other,"folder paths are searchable");
  check(!selection||selection->count()==0,"filter changes clear stale selection");b->collection()->setQuery("");b->library("favorites");b->library("files");QTest::qWait(250);check(b->collection()->sortKey()=="folder","folder view remembered across navigation");
  b->playCollection(0);check(until([&]{return b->playing();}),"playback uses first sorted song");
  auto motion=qmlContext(w)->contextProperty("motionArtwork").value<MotionArtwork*>();check(motion,"shared artwork controller exists");
  if(motion){
    QSignalSpy frames(motion,&MotionArtwork::frameChanged);check(until([&]{return frames.count()>4&&!motion->frame().isNull();}),"current cover animates while audio plays");
    check(!b->current().value("motionArt").toString().isEmpty(),"imported motion URL reaches current track");shot("02-animated-player");
    b->pause();QTest::qWait(200);const auto paused=frames.count();QTest::qWait(400);check(frames.count()==paused&&!motion->running(),"pausing audio freezes cover");
    b->toggle();check(until([&]{return frames.count()>paused+2;}),"resuming audio resumes cover");
    b->setMotion(false);check(until([&]{return motion->source().isEmpty()&&motion->frame().isNull();}),"reduced motion releases decoder and shows poster");check(b->playing(),"motion setting does not stop audio");b->setMotion(true);
    b->setAnimatedArtwork(false);check(until([&]{return motion->source().isEmpty();}),"artwork preference disables animation independently");b->setAnimatedArtwork(true);check(until([&]{return !motion->frame().isNull();}),"artwork preference restores animation");
    click("settingsButton");auto settingsSearch=findItem(w->contentItem(),"settingsSearch");if(settingsSearch){settingsSearch->setProperty("text","Animated album artwork");QMetaObject::invokeMethod(settingsSearch,"textEdited");}QTest::qWait(250);
    click("animatedArtworkSwitch");check(!b->animatedArtwork()&&motion->source().isEmpty(),"Settings switch disables artwork");click("animatedArtworkSwitch");check(b->animatedArtwork(),"Settings switch enables artwork");shot("04-artwork-setting");QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(300);
    QMetaObject::invokeMethod(w,"toggleImmersive");QTest::qWait(500);auto immersive=findItem(w->contentItem(),"immersiveArtwork");check(immersive&&immersive->isVisible()&&!immersive->property("motionUrl").toString().isEmpty(),"immersive artwork uses animated cover");shot("03-immersive-cover");QMetaObject::invokeMethod(w,"toggleImmersive");QTest::qWait(400);
    w->hide();check(until([&]{return motion->source().isEmpty()&&!motion->running();}),"hidden player releases artwork decoder");check(b->playing(),"hidden player continues audio");w->show();check(until([&]{return !motion->frame().isNull();}),"showing player restores animated cover");
    if(QGuiApplication::platformName()=="offscreen"){
      QMetaObject::invokeMethod(w,"openMiniPlayer");QTest::qWait(500);auto mini=w->property("miniPlayer").value<QQuickWindow*>();check(mini&&mini->isVisible()&&motion->running(),"mini player retains shared animation");if(mini)check(mini->grabWindow().save(dir+"/04-mini-cover.png"),"mini capture");QMetaObject::invokeMethod(w,"restorePlayer");QTest::qWait(300);
    }
    QFile::remove(cover);b->rescanMusicFolders();check(until([&]{return !b->importingLocal();},15000),"rescan completes after sidecar removal");check(b->current().value("motionArt").toString().isEmpty()&&motion->source().isEmpty(),"rescan removes stale animation from playing song");check(b->playing(),"artwork rescan preserves ongoing playback");
    encode(coverArgs);b->rescanMusicFolders();check(until([&]{return !b->importingLocal()&&!motion->frame().isNull();},15000),"rescan discovers restored artwork during playback");
    b->playCollection(2);check(until([&]{return b->playing();})&&motion->source().isEmpty(),"next song without artwork releases previous animation");
  }
  b->stop();b->collection()->setSortKey("title");QTest::qWait(250);check(view&&!view->property("groupFolders").toBool(),"other sort modes remove folder headings");
  b->collection()->setSortKey("original");check(b->collection()->items()==b->results()->rows,"original order remains available");
  fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);QCoreApplication::exit(failures?1:0);
}

void runOnlineArtworkTests(Backend *b,QQuickWindow *w) {
  int failures=0;auto check=[&](bool ok,const char *label){fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",label);fflush(stdout);if(!ok)++failures;};
  auto until=[](const std::function<bool()> &p,int timeout=8000){QElapsedTimer t;t.start();while(!p()&&t.elapsed()<timeout)QTest::qWait(25);return p();};
  const auto dir=qEnvironmentVariable("SUNG_TEST_OUTPUT");QDir().mkpath(dir);
  if(!qEnvironmentVariableIsSet("SUNG_MOTION_FIXTURE")){
    QProcess ff;ff.start("ffmpeg",{"-nostdin","-v","error","-f","lavfi","-i","testsrc2=size=256x256:rate=15:duration=1","-threads","1","-c:v","libx264",dir+"/cover.mp4"});
    check(ff.waitForFinished(10000)&&ff.exitCode()==0,"generated silent cover fixture");qputenv("SUNG_MOTION_FIXTURE",(dir+"/cover.mp4").toUtf8());
  }
  qputenv("SUNG_BUFFER_FIXTURE","1");QWindowSystemInterface::handleFocusWindowChanged(w);w->resize(1180,800);
  b->setVolume(0);b->setAutoplay(false);b->setPrepareNext(false);b->setTheme("dark");b->setMotion(true);b->setAnimatedArtwork(true);b->setOnlineArtwork(true);
  QVariantMap song{{"id","motion00001"},{"videoId","motion00001"},{"title","Blinding Lights"},{"artist","The Weeknd"},{"album","After Hours"},{"kind","song"}};
  b->playItem(song);check(until([&]{return b->playing();}),"YouTube transport fixture starts audio");
  auto motion=qmlContext(w)->contextProperty("motionArtwork").value<MotionArtwork*>();check(motion,"shared decoder available");
  if(motion){
    QSignalSpy frames(motion,&MotionArtwork::frameChanged);
    check(until([&]{return frames.count()>10&&!motion->frame().isNull();}),"asynchronous online cover renders changing frames");
    check(!b->onlineMotionArt().isEmpty()&&b->current().value("motionArt").toString().isEmpty(),"online cover does not alter saved track metadata");
    check(w->grabWindow().save(dir+"/01-online-player.png"),"player capture");
    QMetaObject::invokeMethod(w,"toggleImmersive");QTest::qWait(500);
    auto cover=findItem(w->contentItem(),"immersiveArtwork");check(cover&&cover->property("motionUrl").toString()==b->onlineMotionArt(),"immersive view shares the same online cover");
    check(w->grabWindow().save(dir+"/02-online-immersive.png"),"immersive capture");
    b->pause();QTest::qWait(250);const auto count=frames.count();QTest::qWait(400);check(frames.count()==count&&!motion->running(),"pause freezes online cover");
    b->toggle();check(until([&]{return frames.count()>count+3;}),"resume restarts online cover");
    b->setOnlineArtwork(false);check(until([&]{return motion->source().isEmpty()&&motion->frame().isNull();}),"online preference releases decoder");check(b->playing(),"artwork preference preserves audio playback");
    b->setOnlineArtwork(true);check(until([&]{return !motion->frame().isNull();}),"online preference restores artwork");
    w->hide();check(until([&]{return motion->source().isEmpty();}),"hidden window releases online decoder");check(b->playing(),"hidden window preserves audio playback");w->show();check(until([&]{return !motion->frame().isNull();}),"show restores cached online cover");
    QMetaObject::invokeMethod(w,"toggleImmersive");QTest::qWait(400);
    if(QGuiApplication::platformName()=="offscreen"){
      QMetaObject::invokeMethod(w,"openMiniPlayer");QTest::qWait(500);auto mini=w->property("miniPlayer").value<QQuickWindow*>();check(mini&&mini->isVisible()&&!motion->frame().isNull(),"mini player shares online decoder");if(mini)mini->grabWindow().save(dir+"/03-online-mini.png");QMetaObject::invokeMethod(w,"restorePlayer");QTest::qWait(300);
    }
    song["id"]="motion00002";song["videoId"]="motion00002";song["title"]="missing motion";b->playItem(song);
    check(until([&]{return b->playing();}),"next song starts normally");QTest::qWait(1600);
    check(motion->source().isEmpty()&&b->onlineMotionArt().isEmpty()&&b->error().isEmpty(),"missing artwork falls back without retaining previous cover or showing an error");
  }
  b->stop();fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);QCoreApplication::exit(failures?1:0);
}

void runOnlineArtworkLiveTests(Backend *b,QQuickWindow *w) {
  int failures=0;auto check=[&](bool ok,const QString &label){fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",label.toUtf8().constData());fflush(stdout);if(!ok)++failures;};
  auto until=[](const std::function<bool()> &p,int timeout){QElapsedTimer t;t.start();while(!p()&&t.elapsed()<timeout)QTest::qWait(50);return p();};
  const auto dir=qEnvironmentVariable("SUNG_TEST_OUTPUT");QDir().mkpath(dir);
  QFile input(qEnvironmentVariable("SUNG_LIVE_ARTWORK_TRACKS"));check(input.open(QIODevice::ReadOnly),"live track input opens");
  const auto tracks=QJsonDocument::fromJson(input.readAll()).array().toVariantList();check(!tracks.isEmpty(),"live track set is nonempty");
  auto motion=qmlContext(w)->contextProperty("motionArtwork").value<MotionArtwork*>();check(motion,"shared native decoder exists");
  b->setVolume(0);b->setAutoplay(false);b->setPrepareNext(false);b->setMotion(true);b->setAnimatedArtwork(true);b->setOnlineArtwork(true);b->setTheme("dark");
  QVariantList results;int index=0;
  for(const auto &value:tracks){
    if(!motion)break;
    const auto track=value.toMap();const auto label=track.value("artist").toString()+" — "+track.value("title").toString();
    fprintf(stdout,"START %s\n",label.toUtf8().constData());fflush(stdout);
    b->playItem(track);QSignalSpy frames(motion,&MotionArtwork::frameChanged);
    const bool audio=until([&]{return b->playing();},90000);check(audio,label+": real YouTube audio starts");
    QElapsedTimer artworkTime;artworkTime.start();
    const bool cover=audio&&until([&]{return !b->onlineMotionArt().isEmpty()&&!motion->frame().isNull()&&frames.count()>5;},90000);
    check(cover,label+": real lookup reaches native animated cover");
    QVariantMap result{{"title",label},{"audio",audio},{"artwork",cover},{"artworkWaitMs",artworkTime.elapsed()}};
    if(cover){
      result["source"]=b->onlineMotionArt();const auto first=motion->frame().copy();
      check(until([&]{return motion->frame()!=first;},5000),label+": pixels visibly change");
      check(w->grabWindow().save(dir+"/cover-"+QString::number(index)+".png"),label+": window capture");
      b->pause();QTest::qWait(300);const auto count=frames.count();QTest::qWait(400);check(frames.count()==count,label+": pause freezes frames");
      b->toggle();check(until([&]{return frames.count()>count+3;},3000),label+": resume advances frames");
      w->hide();check(until([&]{return motion->source().isEmpty();},2000),label+": hiding releases decoder");w->show();
      check(until([&]{return !motion->frame().isNull();},5000),label+": showing restores cached cover");
      if(index==0){
        for(int step=0;step<4;++step){const auto before=frames.count();QTest::qWait(10000);check(frames.count()>before+20,"actual cover continues across loop interval "+QString::number(step+1));}
        QMetaObject::invokeMethod(w,"toggleImmersive");QTest::qWait(600);auto immersive=findItem(w->contentItem(),"immersiveArtwork");check(immersive&&immersive->property("motionUrl").toString()==b->onlineMotionArt(),"live cover appears in immersive view");w->grabWindow().save(dir+"/immersive.png");QMetaObject::invokeMethod(w,"toggleImmersive");QTest::qWait(500);
        const auto previous=b->onlineMotionArt();b->playAt(b->currentIndex());
        check(until([&]{return b->playing();},90000),"replaying real song starts audio");QElapsedTimer cached;cached.start();
        check(until([&]{return b->onlineMotionArt()==previous&&!motion->frame().isNull();},5000),"replay restores same cached cover");result["replayArtworkWaitMs"]=cached.elapsed();
      }
      check(b->playing()&&b->error().isEmpty(),label+": artwork controls leave audio healthy");
    }
    results.append(result);b->stop();++index;
  }
  QFile output(dir+"/results.json");if(output.open(QIODevice::WriteOnly))output.write(QJsonDocument::fromVariant(results).toJson());
  fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);QCoreApplication::exit(failures?1:0);
}

void runProductPolishTests(Backend *b,QQuickWindow *w) {
  int failures=0;const auto dir=qEnvironmentVariable("SUNG_TEST_OUTPUT");QDir().mkpath(dir);
  const auto check=[&](bool ok,const char *name){fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",name);fflush(stdout);if(!ok)++failures;};
  const auto until=[](std::function<bool()> predicate,int timeout=6000){QElapsedTimer t;t.start();while(!predicate()&&t.elapsed()<timeout)QTest::qWait(30);return predicate();};
  const auto click=[&](QString name){auto item=findItem(w->contentItem(),name);check(item&&item->isVisible(),qPrintable("visible "+name));if(item)QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());QTest::qWait(300);};
  const auto shot=[&](QString name){QTest::qWait(450);check(w->grabWindow().save(dir+'/'+name+".png"),qPrintable("capture "+name));};
  QImage cover(256,256,QImage::Format_RGB32);cover.fill(QColor("#4964ad"));cover.save(dir+"/album.png");qputenv("SUNG_ALBUM_FIXTURE",QUrl::fromLocalFile(dir+"/album.png").toString().toUtf8());
  const auto motionFile=dir+"/fixture.mp4";QProcess encoder;
  encoder.start("ffmpeg",{"-nostdin","-v","error","-f","lavfi","-i","testsrc2=s=96x96:r=12","-t","1","-c:v","libx264","-threads","1","-pix_fmt","yuv420p","-an","-y",motionFile});
  check(encoder.waitForFinished(10000)&&encoder.exitCode()==0,"animated cover fixture encoded");qputenv("SUNG_MOTION_FIXTURE",motionFile.toUtf8());
  w->resize(1280,850);b->setWatchMusicFolders(false);b->setMotion(true);b->setTheme("dark");
  b->open({{"kind","album"},{"browseId","fixture-album"},{"title","Album"}});
  check(until([&]{return !b->busy();}) && b->albumInfo().contains("summary"),"album metadata loaded");
  auto summary=findItem(w->contentItem(),"albumSummary");check(summary&&summary->isVisible(),"album metadata visible");shot("album");
  w->setProperty("side","lyrics");QTest::qWait(550);
  auto grip=findItem(w->contentItem(),"panelResizeHandle"),panel=findItem(w->contentItem(),"sidePanel");
  check(grip&&grip->isVisible()&&panel,"resize grip visible");
  if(grip&&panel){const auto before=panel->width();const auto point=grip->mapToScene(QPointF(grip->width()/2,grip->height()/2)).toPoint();
    QTest::mousePress(w,Qt::LeftButton,Qt::NoModifier,point);QTest::mouseMove(w,point-QPoint(100,0),80);QTest::mouseRelease(w,Qt::LeftButton,Qt::NoModifier,point-QPoint(100,0));QTest::qWait(450);
    check(panel->width()>before+60,"drag grows panel");const auto size=panel->width();w->setProperty("side","");QTest::qWait(400);w->setProperty("side","queue");QTest::qWait(450);check(qAbs(panel->width()-size)<2,"panel width remembered across views");
  }
  shot("resized-panel");w->resize(800,650);QTest::qWait(450);check(panel&&panel->width()<=w->width()&&!grip->isVisible(),"narrow window uses full panel without grip");shot("narrow-panel");
  w->resize(1280,850);w->setProperty("side","");QTest::qWait(450);
  const auto playlist=b->createPlaylist("Evening test mix");
  QTest::keyClick(w,Qt::Key_P,Qt::ControlModifier|Qt::ShiftModifier);QTest::qWait(400);
  auto field=findItem(w->contentItem(),"commandSearch");check(field&&field->hasActiveFocus(),"command shortcut focuses search");
  if(field){field->setProperty("text","Evening test mix");}
  QTest::qWait(100);shot("command-palette");QTest::keyClick(w,Qt::Key_Return);QTest::qWait(450);check(b->libraryId()==playlist,"command opens selected playlist");
  QTest::keyClick(w,Qt::Key_P,Qt::ControlModifier|Qt::ShiftModifier);QTest::qWait(350);if(field)field->setProperty("text","zzzzz-no-command");QTest::keyClick(w,Qt::Key_Return);QTest::qWait(100);check(w->property("modalOpen").toBool(),"empty command results do not dismiss or execute");QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(350);check(!w->property("modalOpen").toBool(),"escape dismisses command palette");
  QVariantMap song{{"id","motion00001"},{"videoId","motion00001"},{"kind","song"},{"title","Cover test"},{"artist","Test artist"}};
  b->setVolume(0);b->setAutoplay(false);b->setPrepareNext(false);b->setAnimatedArtwork(true);b->setOnlineArtwork(true);b->setUiActive(true);b->playItem(song);
  check(until([&]{return b->playing()&&!b->currentMotionArt().isEmpty();}),"animated cover ready");
  QTest::keyClick(w,Qt::Key_P,Qt::ControlModifier|Qt::ShiftModifier);QTest::qWait(350);if(field)field->setProperty("text","Change animated cover");QTest::keyClick(w,Qt::Key_Return);QTest::qWait(400);
  check(findItem(w->contentItem(),"coverPreview")!=nullptr,"cover preview opens through command palette");shot("artwork-controls");
  click("rejectArtworkButton");check(b->currentMotionArt().isEmpty(),"disable current cover");
  click("resetArtworkButton");check(until([&]{return !b->currentMotionArt().isEmpty();}),"automatic cover restores");
  b->chooseArtwork(QUrl::fromLocalFile(motionFile),song.value("id").toString());
  check(until([&]{return b->artworkStatus()=="Your selected cover";}),"local animated file validated and selected");
  const auto chosen=b->currentMotionArt();b->chooseArtwork(QUrl::fromLocalFile(dir+"/missing.mp4"),song.value("id").toString());QTest::qWait(500);
  check(b->currentMotionArt()==chosen,"invalid local cover preserves existing choice");
  b->chooseArtwork(QUrl::fromLocalFile(motionFile),"different-song");QTest::qWait(200);check(b->currentMotionArt()==chosen,"stale song picker rejected");
  check(b->playing()&&b->error().isEmpty(),"artwork controls preserve playback");
  QTest::keyClick(w,Qt::Key_Escape);b->stop();b->deletePlaylist(playlist);
  fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);QCoreApplication::exit(failures?1:0);
}

void runLibraryPolishTests(Backend *b,QQuickWindow *w) {
  int failures=0;const auto dir=qEnvironmentVariable("SUNG_TEST_OUTPUT");QDir().mkpath(dir);
  const auto check=[&](bool ok,const char *name){fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",name);fflush(stdout);if(!ok)++failures;};
  const auto until=[](std::function<bool()> predicate,int timeout=6000){QElapsedTimer t;t.start();while(!predicate()&&t.elapsed()<timeout)QTest::qWait(30);return predicate();};
  const auto shot=[&](QString name){QTest::qWait(450);check(w->grabWindow().save(dir+'/'+name+".png"),qPrintable("capture "+name));};
  w->resize(1280,850);b->setWatchMusicFolders(false);b->setTheme("dark");b->setVolume(0);b->setAutoplay(false);b->setPrepareNext(false);b->setShuffle(false);b->setRepeat(0);
  QImage image(800,400,QImage::Format_RGB32);image.fill(QColor("#dd537c"));const auto art=dir+"/cover.png";image.save(art);
  QVariantList songs;
  for(int i=0;i<4;++i){const auto file=dir+QString("/track%1.wav").arg(i);QProcess encode;
    encode.start("ffmpeg",{"-nostdin","-v","error","-f","lavfi","-i","sine=frequency=220:sample_rate=8000","-t","120","-metadata","title=Song "+QString::number(i+1),"-metadata",i<2?"album=First album":"album=Second album","-metadata","artist=Test artist","-metadata","album_artist=Test artist","-metadata","track="+QString::number(i+1),"-threads","1","-y",file});
    check(encode.waitForFinished(10000)&&encode.exitCode()==0,"audio fixture encoded");b->importLocalFiles({QUrl::fromLocalFile(file)});check(until([&]{return !b->importingLocal();}),"local song imported");
  }
  b->library("local-albums");QTest::qWait(400);check(b->results()->count()==2,"album grid groups imports");
  auto grid=findItem(w->contentItem(),"localGroups");check(grid&&grid->isVisible(),"album grid visible");shot("local-albums");
  auto search=findItem(w->contentItem(),"localGroupSearch");check(search&&search->isVisible(),"album search visible");b->collection()->setQuery("Second");QTest::qWait(100);check(b->collection()->count()==1,"album search filters");b->collection()->setQuery("");
  const auto album=b->results()->get(0);b->open(album);check(b->results()->count()==2&&b->page()=="local-album","album opens tracks");songs=b->results()->rows;shot("local-album-tracks");
  b->back();check(b->page()=="library"&&b->libraryId()=="local-albums","back returns to album grid");
  b->library("local-artists");check(b->results()->count()==1,"artist grid groups imports");b->open(b->results()->get(0));check(b->results()->count()==4,"artist opens songs");
  const auto id=b->createPlaylist("After hours");b->addItemsToPlaylist(id,songs);b->openPlaylist(id);
  auto dialog=w->findChild<QObject*>("playlistCoverDialog");check(dialog!=nullptr,"cover dialog exists");
  if(dialog){dialog->setProperty("playlistId",id);dialog->setProperty("preview",b->preparePlaylistCover(QUrl::fromLocalFile(art)));QMetaObject::invokeMethod(dialog,"open");QTest::qWait(400);shot("playlist-cover-crop");
    auto save=findItem(w->contentItem(),"savePlaylistCover");check(save&&save->isVisible()&&save->isEnabled(),"cover save visible and enabled");
    if(save)QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,save->mapToScene(QPointF(save->width()/2,save->height()/2)).toPoint());
    QTest::qWait(400);check(!b->cover().isEmpty(),"cover saved through UI");shot("playlist-cover");
  }
  auto song=songs.first().toMap();song["art"]=QUrl::fromLocalFile(art).toString(); // normalized below for bounded artwork loader
  song["art"]=b->cover();b->playItem(song);check(until([&]{return b->playing();}),"local playback starts");b->enqueue(songs.last().toMap());
  b->setArtworkAccent(true);QTest::qWait(500);auto sampler=findItem(w->contentItem(),"accentSample");check(sampler&&sampler->property("ready").toBool(),"artwork accent sample ready");
  w->setProperty("side","queue");QTest::qWait(500);auto end=findItem(w->contentItem(),"queueEndLabel");check(end&&end->isVisible()&&!b->queueEnd().isEmpty(),"finish time directly visible");shot("accent-and-queue");
  b->setPlaybackRate(2);QTest::qWait(100);check(b->queueRemainingMs()<125000,"queue timing follows playback speed");b->pause();QTest::qWait(150);check(end&&!end->isVisible(),"paused finish time hidden");b->play();
  b->setRepeat(1);check(b->queueEnd().isEmpty(),"repeat hides finish estimate");b->setRepeat(0);b->setAutoplay(true);check(b->queueEnd().isEmpty(),"autoplay hides finish estimate");b->setAutoplay(false);
  b->setTheme("light");QTest::qWait(400);shot("light-accent");
  const auto evaluate=[&](const QString &code){QQmlExpression expr(qmlContext(w),w,code);const auto value=expr.evaluate();check(!expr.hasError(),"palette expression valid");return value;};
  for(const auto &mode:{QString("dark"),QString("light")}){
    b->setTheme(mode);const auto surface=evaluate("Theme.surface");
    for(int hue=0;hue<360;hue+=30){evaluate(QString("Theme.artworkSeed=Qt.hsla(%1,0.75,0.5,1)").arg(hue/360.0));
      check(evaluate("[Theme.background,Theme.surface,Theme.container,Theme.high].every(c=>Theme.contrast(Theme.primary,c)>=4.5)").toBool(),"accent text meets contrast on all surfaces");
      check(evaluate("Theme.contrast(Theme.primary,Theme.primaryText)>=4.5 && Theme.contrast(Theme.primaryContainer,Theme.containerText)>=4.5").toBool(),"accent role pairs meet contrast");
      check(evaluate("Theme.surface")==surface,"artwork accent preserves surfaces");
    }
  }
  evaluate("Theme.artworkSeed=Qt.rgba(0,0,0,0)");QTest::qWait(300);check(!evaluate("Theme.useArtwork").toBool(),"monochrome cover falls back to theme");
  b->setArtworkAccent(false);QTest::qWait(200);check(sampler&&sampler->property("source").toUrl().isEmpty(),"disabled accent releases sample");
  w->setProperty("side","");w->resize(800,600);b->library("local-albums");shot("narrow-albums");
  check(b->playing()&&b->error().isEmpty(),"new views preserve playback");b->stop();b->deletePlaylist(id);
  fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);QCoreApplication::exit(failures?1:0);
}

void runPlaybackPolishTests(Backend *b,QQuickWindow *w){
  int failures=0;const auto dir=qEnvironmentVariable("SUNG_TEST_OUTPUT");QDir().mkpath(dir);
  const auto check=[&](bool ok,const char *name){fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",name);fflush(stdout);if(!ok)++failures;};
  const auto until=[](std::function<bool()> p,int ms=6000){QElapsedTimer t;t.start();while(!p()&&t.elapsed()<ms)QTest::qWait(30);return p();};
  const auto shot=[&](QString name){QTest::qWait(400);check(w->grabWindow().save(dir+'/'+name+".png"),qPrintable("capture "+name));};
  const auto click=[&](QString name){auto i=findItem(w->contentItem(),name);check(i&&i->isVisible(),qPrintable("visible "+name));if(i)QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,i->mapToScene(QPointF(i->width()/2,i->height()/2)).toPoint());QTest::qWait(300);};
  w->resize(1280,850);b->setVolume(0);b->setWatchMusicFolders(false);b->setAutoplay(false);b->setPrepareNext(false);b->setTheme("dark");b->setMotion(true);
  const auto path=dir+"/audio.wav";QProcess encode;encode.start("ffmpeg",{"-nostdin","-v","error","-f","lavfi","-i","sine=frequency=330:sample_rate=44100","-t","90","-y",path});check(encode.waitForFinished(10000)&&encode.exitCode()==0,"audio encoded");
  QImage image(1600,800,QImage::Format_RGB32);image.fill(QColor("#d752a0"));{QPainter p(&image);p.fillRect(800,0,800,800,QColor("#2ca4ad"));}const auto art=dir+"/wide.jpg";image.save(art);
  QVariantMap song{{"id","local_polish"},{"kind","song"},{"localPath",path},{"title","Wide artwork"},{"artist","Test artist"},{"album","Test album"},{"seconds",90},{"art",QUrl::fromLocalFile(art).toString()}};
  b->playItem(song);check(until([&]{return b->playing();}),"local audio starts");b->seek(10000);b->pause();
  auto session=w->findChild<QObject*>("sessionsDialog");check(session,"sessions dialog available");if(session){QMetaObject::invokeMethod(session,"open");QTest::qWait(300);auto input=findItem(w->contentItem(),"sessionName");if(input)input->setProperty("text","Evening session");click("saveSessionButton");check(b->sessions().size()==1,"save session through UI");shot("sessions");click("resumeSessionButton");check(w->findChild<QObject*>("sessionConfirm")->property("visible").toBool(),"resume asks before replacing queue");QMetaObject::invokeMethod(w->findChild<QObject*>("sessionConfirm"),"accept");}
  check(until([&]{return b->playing()&&b->position()>=10000;}),"session resumes saved position");
  auto controls=w->findChild<QObject*>("artworkControls");QMetaObject::invokeMethod(controls,"open");QTest::qWait(300);click("artworkFit");check(b->currentArtworkFit(),"fit saved through control");shot("fit-controls");QMetaObject::invokeMethod(controls,"close");QTest::qWait(300);
  w->setProperty("immersive",true);QTest::qWait(600);auto immersive=findItem(w->contentItem(),"immersiveArtwork");check(immersive&&immersive->property("fit").toBool(),"immersive cover uses fit");check(immersive&&immersive->property("highResolution").toBool(),"immersive cover requests display resolution");shot("immersive-fit");
  b->setCurrentArtworkFit(false);QTest::qWait(250);check(immersive&&!immersive->property("fit").toBool(),"fill updates without restarting playback");shot("immersive-fill");w->setProperty("immersive",false);QTest::qWait(400);
  b->setArtworkAccent(true);QTest::qWait(500);QQmlExpression start(qmlContext(w),w,"Theme.artworkSeed");const auto initial=start.evaluate().value<QColor>();QQmlExpression change(qmlContext(w),w,"Theme.artworkSeed=Qt.rgba(0.1,0.8,0.3,1)");change.evaluate();QTest::qWait(70);const auto middle=start.evaluate().value<QColor>();QTest::qWait(300);const auto end=start.evaluate().value<QColor>();check(initial!=middle&&middle!=end,"accent transitions through intermediate colors");
  b->setMotion(false);QQmlExpression immediate(qmlContext(w),w,"Theme.artworkSeed=Qt.rgba(0.8,0.2,0.4,1)");immediate.evaluate();QTest::qWait(10);check(qAbs(start.evaluate().value<QColor>().redF()-0.8)<0.01,"reduced motion applies color immediately");b->setMotion(true);
  auto details=w->findChild<QObject*>("trackDetailsDialog");QMetaObject::invokeMethod(details,"inspect",Q_ARG(QVariant,QVariant(song)));QTest::qWait(300);const auto rows=b->trackDetails(song);bool sample=false;for(const auto &v:rows)if(v.toMap().value("label")=="Decoded sample rate")sample=true;check(sample,"actual decoded sample rate shown");auto scroll=findItem(w->contentItem(),"detailsScroll");if(scroll){auto content=scroll->property("contentItem").value<QObject*>();if(content)content->setProperty("contentY",content->property("contentHeight").toDouble()-scroll->height());}shot("quality-details");QMetaObject::invokeMethod(details,"close");
  check(b->playing()&&b->error().isEmpty(),"controls preserve playback");b->setArtworkAccent(false);b->stop();fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);QCoreApplication::exit(failures?1:0);
}
