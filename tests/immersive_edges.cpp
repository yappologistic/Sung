#include "uitest.h"
#include "backend.h"
#include "rowselection.h"
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QPointer>
#include <QProcess>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <qpa/qwindowsysteminterface.h>
#include <functional>

namespace {
QQuickItem *itemNamed(QQuickItem *root,const QString &name) {
  if(!root->isVisible())return nullptr;
  if(root->objectName()==name)return root;
  for(auto child:root->childItems())if(auto found=itemNamed(child,name))return found;
  return nullptr;
}
bool waitUntil(const std::function<bool()> &predicate,int timeout=8000) {
  QElapsedTimer timer;timer.start();
  while(!predicate()&&timer.elapsed()<timeout)QTest::qWait(25);
  return predicate();
}
bool visibleCursors(QQuickItem *root) {
  if(root->cursor().shape()==Qt::BlankCursor)return false;
  for(auto child:root->childItems())if(!visibleCursors(child))return false;
  return true;
}
struct Checks {
  int failures=0;
  void check(bool ok,const char *message) {
    fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",message);fflush(stdout);
    if(!ok)++failures;
  }
  void feature(const char *name,const std::function<void()> &run) {
    const int before=failures;run();
    fprintf(stdout,"FEATURE %s %s\n",name,failures==before?"PASS":"FAIL");fflush(stdout);
  }
  void finish() {fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);QCoreApplication::exit(failures?1:0);}
};
bool requireOffscreen(Checks &c) {
  const bool safe=QGuiApplication::platformName()=="offscreen";
  c.check(safe,"regression runs offscreen");
  if(!safe)c.finish();
  return safe;
}
void focusWindow(QQuickWindow *w) {QWindowSystemInterface::handleFocusWindowChanged(w);QTest::qWait(30);}
void tap(QQuickWindow *w,QQuickItem *item) {
  if(!item)return;
  const auto p=item->mapToScene(item->boundingRect().center()).toPoint();
  QTest::mouseMove(w,p);QTest::qWait(40);QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,p);QTest::qWait(250);
}
}

void runImmersivePreferencesTest(Backend *b,QQuickWindow *w) {
  Checks c;if(!requireOffscreen(c))return;
  b->setMotion(false);b->setTrackNotifications(false);
  w->resize(1180,800);w->setProperty("immersive",true);QTest::qWait(100);focusWindow(w);
  auto player=itemNamed(w->contentItem(),"immersivePlayer");
  c.check(player,"preference test loads immersive player");if(!player){c.finish();return;}
  const auto phase=qEnvironmentVariable("SUNG_PREFERENCE_PHASE");
  if(phase=="seed") {
    c.check(player->property("preferredLayout")=="split"&&!player->property("autoHideControls").toBool(),"fresh profile has safe defaults");
    tap(w,itemNamed(w->contentItem(),"immersiveLayoutButton"));
    tap(w,itemNamed(w->contentItem(),"immersiveLayout_lyrics"));
    tap(w,itemNamed(w->contentItem(),"immersiveLayoutButton"));
    tap(w,itemNamed(w->contentItem(),"immersiveAutoHide"));
    c.check(player->property("preferredLayout")=="lyrics"&&player->property("autoHideControls").toBool(),"menu saves layout and explicit idle-fade preference");
  } else if(phase=="restore") {
    c.check(player->property("preferredLayout")=="lyrics"&&player->property("autoHideControls").toBool(),"new process restores both preferences");
    c.check(player->property("displayedLayout")=="artwork","restart without lyrics preserves artwork fallback");
    tap(w,itemNamed(w->contentItem(),"immersiveLayoutButton"));
    tap(w,itemNamed(w->contentItem(),"immersiveAutoHide"));
    tap(w,itemNamed(w->contentItem(),"immersiveLayoutButton"));
    tap(w,itemNamed(w->contentItem(),"immersiveLayout_artwork"));
  } else if(phase=="reset") {
    c.check(player->property("preferredLayout")=="artwork"&&!player->property("autoHideControls").toBool(),"third process restores changed layout and disabled fading");
  } else c.check(false,"known preference test phase");
  c.finish();
}

void runImmersiveEdgeTests(Backend *b,QQuickWindow *w) {
  Checks c;if(!requireOffscreen(c))return;
  const auto dir=qEnvironmentVariable("SUNG_TEST_OUTPUT");QDir().mkpath(dir+"/music");
  b->setTrackNotifications(false);b->setMotion(false);b->setVolume(0);b->setAutoplay(false);
  b->setPrepareNext(false);b->setWatchMusicFolders(false);b->setOnlineArtwork(false);b->setLyricsFallback(false);
  w->resize(1180,800);focusWindow(w);
  QProcess encode;encode.start("ffmpeg",{"-nostdin","-v","error","-f","lavfi","-i","anullsrc=r=8000:cl=mono","-t","120","-metadata","album=Regression album","-metadata","artist=Regression artist",dir+"/music/one.flac"});
  c.check(encode.waitForFinished(10000)&&encode.exitCode()==0,"generate silent edge-case fixture");
  QFile::copy(dir+"/music/one.flac",dir+"/music/two.flac");
  b->importMusicFolder(QUrl::fromLocalFile(dir+"/music"));c.check(waitUntil([&]{return !b->importingLocal();}),"import edge-case fixture");
  b->library("files");const auto songs=b->results()->rows;
  c.check(songs.size()==2,"fixture contains two distinct tracks");if(songs.size()!=2){c.finish();return;}
  b->enqueueItems(songs);b->playAt(0);c.check(waitUntil([&]{return b->playing();}),"silent fixture plays");
  QFile lrc(dir+"/edge.lrc");c.check(lrc.open(QIODevice::WriteOnly),"create intro and gap lyric fixture");
  lrc.write("[00:05]First test line\n[00:10]Second test line\n[00:15]\n[00:20]Third test line\n");lrc.close();
  b->importLyrics(QUrl::fromLocalFile(lrc.fileName()),songs.first().toMap().value("id").toString());
  c.check(waitUntil([&]{return b->lyricLines().size()==4;}),"timed intro and gap lyrics load");
  b->pause();w->setProperty("immersive",true);QTest::qWait(100);
  auto player=itemNamed(w->contentItem(),"immersivePlayer");
  c.check(player,"edge-case player loads");if(!player){c.finish();return;}
  auto layout=[&](const char *value){QMetaObject::invokeMethod(player,"layoutRequested",Q_ARG(QString,QString::fromLatin1(value)));};
  c.feature("layouts",[&]{
    b->setMotion(true);
    for(const auto mode:{"artwork","lyrics","split","artwork","lyrics"}){layout(mode);QTest::qWait(25);}
    c.check(waitUntil([&]{return player->property("displayedLayout")=="lyrics";}),"rapid layout changes settle on last choice");
    layout("artwork");QTest::qWait(30);b->setMotion(false);QTest::qWait(30);
    auto body=itemNamed(w->contentItem(),"immersiveBody");
    c.check(body&&body->opacity()==1&&player->property("displayedLayout")=="artwork","disabling motion settles a layout transition");
    layout("invalid-layout");QTest::qWait(30);
    c.check(player->property("displayedLayout")=="artwork","unknown saved layout has safe fallback");
    layout("split");b->setTheme("light");QTest::qWait(50);
    c.check(body&&body->width()>0&&body->height()>0&&body->opacity()==1,"light theme preserves visible layout");b->setTheme("dark");
  });
  c.feature("idle-controls",[&]{
    b->toggle();c.check(waitUntil([&]{return b->playing();}),"resume for idle checks");
    player->forceActiveFocus();QMetaObject::invokeMethod(player,"wake");QTest::qWait(3700);
    c.check(player->property("controlsShown").toBool(),"default controls remain visible after idle timeout");
    QMetaObject::invokeMethod(player,"autoHideRequested",Q_ARG(bool,true));
    QMetaObject::invokeMethod(player,"wake");QTest::qWait(3700);
    c.check(!player->property("controlsShown").toBool(),"explicit idle fading hides controls");
    c.check(visibleCursors(w->contentItem())&&(!QGuiApplication::overrideCursor()||QGuiApplication::overrideCursor()->shape()!=Qt::BlankCursor),"idle fading never hides cursor");
    QWindowSystemInterface::handleFocusWindowChanged(nullptr);QTest::qWait(3700);
    c.check(player->property("controlsShown").toBool(),"losing window focus restores and retains controls");
    focusWindow(w);player->forceActiveFocus();QMetaObject::invokeMethod(player,"wake");QTest::qWait(3700);
    QTest::keyClick(w,Qt::Key_F6);QTest::qWait(30);
    c.check(player->property("controlsShown").toBool(),"keyboard activity restores idle controls");
    QMetaObject::invokeMethod(player,"autoHideRequested",Q_ARG(bool,false));b->pause();
  });
  c.feature("queue",[&]{
    b->clearQueue();QMetaObject::invokeMethod(w,"showQueue");QTest::qWait(100);
    auto sheet=w->findChild<QObject*>("immersiveQueueSheet");
    auto queue=itemNamed(w->contentItem(),"queueView");
    c.check(queue&&queue->property("count").toInt()==0,"empty immersive queue opens safely");
    QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(100);
    c.check(sheet&&!sheet->property("visible").toBool(),"empty queue closes with Escape");
    b->enqueueItems({songs[0],songs[1],songs[0]});
    QMetaObject::invokeMethod(w,"showQueue");QTest::qWait(100);queue=itemNamed(w->contentItem(),"queueView");
    c.check(queue,"duplicate-track queue opens");if(!queue)return;
    auto selection=queue->property("selection").value<RowSelection*>();c.check(selection,"queue has selection model");if(!selection)return;
    queue->forceActiveFocus();QTest::keyClick(w,Qt::Key_A,Qt::ControlModifier);QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(50);
    c.check(selection->count()==0&&sheet->property("visible").toBool(),"first Escape clears selection without closing sheet");
    selection->select(2,0);QTest::keyClick(w,Qt::Key_Delete);QTest::qWait(100);
    c.check(b->queue()->count()==2&&b->queue()->get(0).value("id")==songs[0].toMap().value("id"),"keyboard deletion removes only selected duplicate occurrence");
    b->undo();c.check(b->queue()->count()==3,"queue deletion undo restores duplicate occurrence");
    selection->clear();QPointer<QQuickItem> released=queue;
    QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(100);
    c.check(waitUntil([&]{return released.isNull();}),"closed queue releases its loaded list");
    for(int n=0;n<3;++n){QMetaObject::invokeMethod(w,"showQueue");QTest::qWait(50);QMetaObject::invokeMethod(sheet,"close");QTest::qWait(50);}
    c.check(b->queue()->count()==3&&!itemNamed(w->contentItem(),"queueView"),"repeated sheet toggles preserve queue and release content");
    b->playAt(0);c.check(waitUntil([&]{return b->playing();}),"queue remains playable after reopen cycles");b->pause();
  });
  c.feature("shortcut-hud",[&]{
    focusWindow(w);player->forceActiveFocus();b->setVolume(0.99);
    QTest::keyClick(w,Qt::Key_Up,Qt::ControlModifier);QTest::qWait(30);
    c.check(b->volume()==1,"volume shortcut clamps at 100 percent");
    QTest::keyClick(w,Qt::Key_M);QTest::keyClick(w,Qt::Key_M);QTest::qWait(30);
    c.check(b->volume()==1,"mute toggle restores previous volume");
    b->setVolume(0.01);QTest::keyClick(w,Qt::Key_Down,Qt::ControlModifier);QTest::qWait(30);
    c.check(b->volume()==0,"volume shortcut clamps at zero");
    b->seek(1000);QTest::keyClick(w,Qt::Key_Left);QTest::qWait(50);
    c.check(b->position()==0,"backward seek clamps at beginning");
    auto hud=itemNamed(w->contentItem(),"playbackHud");
    c.check(hud&&hud->property("label")=="0:00","repeated actions replace HUD contents with current feedback");
    QTest::qWait(1200);c.check(hud&&!hud->isVisible()&&hud->opacity()==0,"reduced-motion HUD dismisses fully");
    QMetaObject::invokeMethod(player,"showLyricsSearch");QTest::qWait(50);const auto position=b->position();
    QTest::keyClick(w,Qt::Key_M);QTest::keyClick(w,Qt::Key_Up,Qt::ControlModifier);QTest::keyClick(w,Qt::Key_Right);
    c.check(b->volume()==0&&b->position()==position,"typing in lyrics does not trigger playback shortcuts");
    QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(50);
  });
  c.feature("collection-navigation",[&]{
    layout("split");QTest::qWait(50);const auto id=b->current().value("id");const auto origin=b->viewKey();
    QMetaObject::invokeMethod(w,"openImmersiveCollection",Q_ARG(QVariant,QVariantMap{}));
    c.check(w->property("immersive").toBool()&&b->viewKey()==origin,"missing collection target does not navigate");
    auto artist=itemNamed(w->contentItem(),"immersiveArtistButton");c.check(artist&&artist->isEnabled(),"local artist link is enabled");tap(w,artist);
    c.check(b->page()=="local-artist"&&!w->property("immersive").toBool(),"artist link opens matching collection");
    b->open(b->relatedCollection(b->current(),"album"));QMetaObject::invokeMethod(w,"navigateBack");QTest::qWait(50);
    c.check(!w->property("immersive").toBool()&&b->page()=="local-artist","Back within collection stack preserves browsing");
    QMetaObject::invokeMethod(w,"navigateBack");QTest::qWait(100);focusWindow(w);
    c.check(w->property("immersive").toBool()&&b->viewKey()==origin&&b->current().value("id")==id,"Back at origin restores immersion without replacing track");
    player=itemNamed(w->contentItem(),"immersivePlayer");
  });
  c.feature("mini-lyrics",[&]{
    b->pause();b->seek(0);QMetaObject::invokeMethod(w,"openMiniPlayer");QTest::qWait(150);
    auto mini=qvariant_cast<QQuickWindow*>(w->property("miniPlayer"));c.check(mini,"mini player creates");if(!mini)return;
    auto line=itemNamed(mini->contentItem(),"miniLyricContainer");c.check(line,"timed lyrics reserve a stable row during intro");if(!line)return;
    c.check(line->property("shown").toString().isEmpty()&&mini->height()==216,"intro has no stale lyric and does not resize player");
    b->seek(6000);QTest::qWait(50);c.check(line->property("shown")=="First test line","seeking past intro displays correct lyric");
    b->seek(16000);QTest::qWait(50);c.check(line->property("shown").toString().isEmpty()&&mini->height()==216,"timed gap clears lyric without moving controls");
    b->seek(6000);QTest::qWait(50);b->setMotion(true);b->seek(11000);QTest::qWait(35);mini->hide();QTest::qWait(50);
    c.check(line->property("progress").toDouble()==1&&line->property("previous").toString().isEmpty(),"hiding mini player releases outgoing lyric and settles animation");
    mini->show();b->setMotion(false);b->seek(21000);QTest::qWait(100);
    c.check(line->property("shown")=="Third test line","reopened mini player shows latest lyric");
    b->playAt(1);c.check(waitUntil([&]{return b->lyricLines().isEmpty();}),"switch to track without lyrics completes");QTest::qWait(50);
    c.check(mini->height()==188,"missing lyrics collapse mini player");
    b->playAt(0);c.check(waitUntil([&]{return b->lyricLines().size()==4;}),"switch back restores timed lyrics");QTest::qWait(100);
    c.check(mini->height()==216,"returning to lyrics restores row exactly once");
  });
  b->stop();b->clearQueue();c.finish();
}
