#include "uitest.h"
#include "backend.h"
#include "rowselection.h"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QQuickItem>
#include <QQuickWindow>
#include <QProcess>
#include <QTest>
#include <qpa/qwindowsysteminterface.h>
#include <functional>

namespace {
QQuickItem *visibleItem(QQuickItem *root,const QString &name) {
  if(!root->isVisible())return nullptr;
  if(root->objectName()==name)return root;
  for(auto child:root->childItems())if(auto found=visibleItem(child,name))return found;
  return nullptr;
}
bool waitFor(const std::function<bool()> &predicate) {
  QElapsedTimer timer;timer.start();
  while(!predicate() && timer.elapsed()<8000)QTest::qWait(25);
  return predicate();
}
}

void runImmersivePolishTests(Backend *b,QQuickWindow *w) {
  int failures=0;
  auto check=[&](bool ok,const char *name){fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",name);fflush(stdout);if(!ok)++failures;};
  const auto dir=qEnvironmentVariable("SUNG_TEST_OUTPUT");QDir().mkpath(dir+"/music");
  auto click=[&](const QString &name){
    auto item=visibleItem(w->contentItem(),name);check(item,qPrintable(name));
    if(item){const auto point=item->mapToScene(item->boundingRect().center()).toPoint();QTest::mouseMove(w,point);QTest::qWait(60);QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,point);}
    QTest::qWait(350);
  };
  auto shot=[&](const QString &name){QTest::qWait(250);check(w->grabWindow().save(dir+'/'+name+".png"),qPrintable(name));};
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->setMinimumSize({1180,800});w->setMaximumSize({1180,800});w->resize(1180,800);QTest::qWait(700);QWindowSystemInterface::handleFocusWindowChanged(w);b->setTheme("dark");b->setMotion(true);b->setVolume(0);
  b->setAutoplay(false);b->setPrepareNext(false);b->setWatchMusicFolders(false);b->setOnlineArtwork(false);b->setLyricsFallback(false);
  QImage cover(640,640,QImage::Format_RGB32);cover.fill(QColor("#46646b"));
  QPainter paint(&cover);paint.setPen(QPen(QColor("#d9f1dd"),5));
  for(int i=0;i<8;++i)paint.drawEllipse(QPoint(320,320),30+i*32,30+i*32);
  paint.end();cover.save(dir+"/music/cover.png");
  QProcess encode;encode.start("ffmpeg",{"-nostdin","-v","error","-f","lavfi","-i","anullsrc=r=8000:cl=mono","-t","180","-metadata","album=Still Water","-metadata","artist=Example Artist",dir+"/music/01.flac"});
  check(encode.waitForFinished(10000)&&encode.exitCode()==0,"generate silent local audio");
  QFile::copy(dir+"/music/01.flac",dir+"/music/02.flac");QFile::copy(dir+"/music/01.flac",dir+"/music/03.flac");
  b->importMusicFolder(QUrl::fromLocalFile(dir+"/music"));check(waitFor([&]{return !b->importingLocal();}),"import local fixture");
  b->library("files");b->enqueueItems(b->results()->rows);b->playAt(0);
  check(waitFor([&]{return b->playing();}),"local playback starts");
  const auto song=b->current();
  check(!b->relatedCollection(song,"album").isEmpty()&&!b->relatedCollection(song,"artist").isEmpty(),"local artist and album targets resolve");
  check(b->relatedCollection(song,"song").isEmpty(),"unsupported collection kind is rejected");
  check(b->relatedCollection({{"title","No metadata"}},"album").isEmpty(),"missing provider IDs produce no dead link");
  for(const auto source:{"jellyfin","subsonic",""}){
    QVariantMap remote{{"source",source},{"server","test-server"},{"artistId","artist-1"},{"albumId","album-1"},{"artist","Artist"},{"album","Album"}};
    const auto target=b->relatedCollection(remote,"album");
    check(target.value("remoteId")=="album-1"&&target.value("browseId")=="album-1"&&target.value("server")=="test-server"&&target.value("source")==source,"provider target preserves source and server identity");
  }
  w->setProperty("immersive",true);QTest::qWait(400);
  auto player=visibleItem(w->contentItem(),"immersivePlayer");check(player,"immersive player loads");
  if(!player){QCoreApplication::exit(1);return;}
  check(!player->property("autoHideControls").toBool()&&player->property("controlsShown").toBool(),"controls remain visible by default");
  click("immersiveLayoutButton");click("immersiveAutoHide");
  check(player->property("autoHideControls").toBool(),"idle hiding can be enabled explicitly");
  auto choose=[&](const QString &layout){click("immersiveLayoutButton");
    if(layout=="lyrics")shot("layout-menu");
    click("immersiveLayout_"+layout);QTest::qWait(100);
  };
  choose("lyrics");check(player->property("preferredLayout")=="lyrics"&&player->property("displayedLayout")=="artwork","missing lyrics falls back without losing preference");
  QFile lrc(dir+"/sample.lrc");check(lrc.open(QIODevice::WriteOnly),"create timed lyrics");
  lrc.write("[00:00]The light arrives\n[00:10]Across the still water\n[00:20]A quiet moment\n[00:30]We move with the tide\n[01:00]The evening settles\n");lrc.close();
  b->importLyrics(QUrl::fromLocalFile(lrc.fileName()),song.value("id").toString());
  check(waitFor([&]{return b->lyricLines().size()==5&&player->property("displayedLayout")=="lyrics";}),"available lyrics restore saved layout");
  b->seek(11000);QTest::qWait(450);shot("lyrics");
  choose("artwork");check(player->property("displayedLayout")=="artwork","artwork layout applies");shot("artwork");
  choose("split");check(player->property("displayedLayout")=="split","split layout applies");shot("split");
  const auto playingId=b->current().value("id");
  click("immersiveAlbumButton");check(!w->property("immersive").toBool()&&b->page()=="local-album","album link opens local collection");
  check(b->current().value("id")==playingId&&b->playing(),"collection navigation preserves playback");
  QMetaObject::invokeMethod(w,"navigateBack");QTest::qWait(600);
  check(w->property("immersive").toBool(),"Back returns to immersive player");
  QWindowSystemInterface::handleFocusWindowChanged(w);QTest::qWait(50);
  player=visibleItem(w->contentItem(),"immersivePlayer");
  check(player&&player->property("preferredLayout")=="split","layout survives player recreation");
  if(!player){QCoreApplication::exit(1);return;}
  click("immersiveQueueButton");
  auto sheet=w->findChild<QObject*>("immersiveQueueSheet");
  check(sheet&&sheet->property("visible").toBool()&&w->property("immersive").toBool(),"queue opens without leaving immersion");
  auto queue=visibleItem(w->contentItem(),"queueView");check(queue&&queue->property("count").toInt()==3,"sheet uses complete live queue");
  shot("queue");
  if(queue){
    auto selection=queue->property("selection").value<RowSelection*>();
    check(selection,"queue selection is available");
    if(selection){selection->selectAll();check(selection->count()==3,"sheet supports multi-selection");selection->clear();}
    const auto lastId=b->queue()->get(2).value("id");
    auto from=visibleItem(w->contentItem(),"queueRow_2");
    auto to=visibleItem(w->contentItem(),"queueRow_1");
    if(from&&to){
      const auto start=from->mapToScene(QPointF(100,35)).toPoint();
      const auto end=to->mapToScene(QPointF(100,12)).toPoint();
      QTest::mousePress(w,Qt::LeftButton,Qt::NoModifier,start);
      QTest::mouseMove(w,start-QPoint(0,24),40);QTest::mouseMove(w,end,40);
      QTest::qWait(80);QTest::mouseRelease(w,Qt::LeftButton,Qt::NoModifier,end);QTest::qWait(350);
    }
    check(queue->property("count").toInt()==3&&b->queue()->get(1).value("id")==lastId,"dragging reorders the immersive queue");
    b->undo();
    QMetaObject::invokeMethod(queue,"activate",Q_ARG(int,1),Q_ARG(QVariant,b->queue()->get(1)));
    check(waitFor([&]{return b->currentIndex()==1&&b->playing();}),"sheet activation plays requested track");
    b->removeQueueRows({2});QTest::qWait(200);check(queue->property("count").toInt()==2,"sheet follows removal");b->undo();
  }
  QTest::qWait(3700);check(player->property("controlsShown").toBool(),"open sheet prevents idle hiding");
  QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(300);
  check(sheet&&!sheet->property("visible").toBool()&&w->property("immersive").toBool(),"Escape closes sheet only");
  b->playAt(0);check(waitFor([&]{return b->playing()&&b->lyricLines().size()==5;}),"original track and lyrics restore");
  player->forceActiveFocus();QTest::mouseMove(w,QPoint(8,8));QMetaObject::invokeMethod(player,"wake");
  QTest::qWait(3900);check(!player->property("controlsShown").toBool(),"idle playback hides controls");
  QTest::mouseMove(w,QPoint(30,30));QTest::qWait(250);check(player->property("controlsShown").toBool(),"pointer movement restores controls");
  b->pause();QTest::qWait(3800);check(player->property("controlsShown").toBool(),"paused playback keeps controls visible");
  b->toggle();check(waitFor([&]{return b->playing();}),"playback resumes");
  auto play=visibleItem(w->contentItem(),"immersivePlayButton");if(play)play->forceActiveFocus(Qt::TabFocusReason);
  QTest::qWait(3800);check(player->property("controlsShown").toBool(),"keyboard-focused controls never disappear");
  player->forceActiveFocus();QTest::keyClick(w,Qt::Key_Up,Qt::ControlModifier);QTest::qWait(100);
  auto hud=visibleItem(w->contentItem(),"playbackHud");check(hud&&b->volume()>0,"volume shortcut displays feedback");
  QTest::keyClick(w,Qt::Key_Up,Qt::ControlModifier);QTest::qWait(80);
  check(hud&&hud->property("label").toString()==QString::number(qRound(b->volume()*100))+"%","repeated volume shortcuts update one HUD");
  QTest::keyClick(w,Qt::Key_M);QTest::qWait(100);check(b->volume()==0&&hud&&hud->property("symbol")=="mute","mute shortcut restores feedback");
  b->pause();b->seek(1000);QTest::qWait(100);QTest::keyClick(w,Qt::Key_Right);QTest::qWait(150);
  check(b->position()>=10500&&b->position()<=11500,"seek shortcut applies ten seconds");
  shot("shortcut-feedback");QTest::qWait(1300);check(hud&&!hud->isVisible(),"HUD dismisses after inactivity");
  click("immersiveLyricSearchButton");const auto before=b->position();QTest::keyClick(w,Qt::Key_Left);
  check(b->position()==before,"text editing does not seek playback");QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(200);
  b->setTheme("light");shot("light");b->setMotion(false);
  choose("artwork");check(player->property("displayedLayout")=="artwork","reduced motion applies layout immediately");
  choose("split");b->setTheme("dark");
  w->setProperty("immersive",false);QTest::qWait(150);w->showNormal();w->setMinimumSize({780,580});w->setMaximumSize({780,580});w->resize(780,580);w->setProperty("immersive",true);QTest::qWait(250);
  player=visibleItem(w->contentItem(),"immersivePlayer");shot("compact");
  const auto art=visibleItem(w->contentItem(),"immersiveArtwork");const auto seek=visibleItem(w->contentItem(),"seekBar");
  check(art&&seek&&art->mapToScene({0,art->height()}).y()<seek->mapToScene({0,0}).y(),"compact artwork stays above transport");
  QMetaObject::invokeMethod(w,"openMiniPlayer");QTest::qWait(350);
  auto mini=qvariant_cast<QQuickWindow*>(w->property("miniPlayer"));check(mini&&mini->isVisible(),"mini player opens");
  if(mini){
    b->setMotion(true);b->seek(1000);QTest::qWait(300);
    auto line=visibleItem(mini->contentItem(),"miniLyricContainer");check(line,"mini lyric line is visible");
    const int height=mini->height();b->seek(11000);QTest::qWait(80);
    check(line&&line->property("progress").toDouble()>0&&line->property("progress").toDouble()<1,"mini lyric change crossfades");
    QTest::qWait(250);check(line&&line->property("shown")=="Across the still water"&&mini->height()==height,"line changes keep transport geometry stable");
    b->seek(21000);QTest::qWait(20);b->seek(31000);QTest::qWait(300);check(line&&line->property("shown")=="We move with the tide","rapid seeking shows latest lyric");
    b->setMotion(false);b->seek(1000);QTest::qWait(30);check(line&&line->property("progress").toDouble()==1&&line->property("shown")=="The light arrives","reduced motion settles mini lyrics immediately");
    check(mini->grabWindow().save(dir+"/mini-lyrics.png"),"mini lyric capture");
    b->playAt(1);check(waitFor([&]{return b->currentIndex()==1&&b->lyricLines().isEmpty();}),"track without lyrics clears line");QTest::qWait(200);
    check(mini->height()<height&&!visibleItem(mini->contentItem(),"miniLyricContainer"),"missing lyrics collapses unused mini-player space");
    check(mini->grabWindow().save(dir+"/mini-no-lyrics.png"),"compact mini capture");
  }
  b->stop();b->clearQueue();fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);QCoreApplication::exit(failures?1:0);
}
