#include "uitest.h"
#include "backend.h"
#include "m3color.h"
#include "rowselection.h"
#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QQmlContext>
#include <QQmlProperty>
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
void collectItems(QQuickItem *root,const QString &name,QList<QQuickItem*> &found) {
  if(!root)return;
  if(root->objectName()==name)found.append(root);
  for(auto child:root->childItems())collectItems(child,name,found);
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
  // The layout menu is the only vibrant menu in the application, and a
  // vibrant menu is not segmented, so its rows have no container of their own.
  // Ink that assumed one left the chosen row at 1.5:1 against the menu it sits
  // on, which put the rows that were switched on out of reach of the people
  // most likely to be reading them.
  auto menuInkIsReadable=[&]{
    auto menu=w->findChild<QObject*>("immersiveLayoutMenu");
    check(menu,"immersive layout menu");
    if(!menu)return;
    check(!menu->property("segmented").toBool(),
          "the layout menu is a list rather than a run, so it draws no container per row");
    auto frame=menu->property("background").value<QQuickItem*>();
    const auto surface=frame?frame->property("color").value<QColor>():QColor();
    QList<QQuickItem*> inked;
    collectItems(menu->property("contentItem").value<QQuickItem*>(),"menuItemLabel",inked);
    collectItems(menu->property("contentItem").value<QQuickItem*>(),"menuItemLeading",inked);
    check(inked.size()>=6,"the menu shows its rows and their ticks");
    double worst=99;QString worstOn;
    for(auto item:inked){
      if(!item->isVisible())continue;
      const auto ink=item->property("color").isValid()?item->property("color").value<QColor>()
                                                      :item->property("ink").value<QColor>();
      const double ratio=m3::contrastRatio(ink,surface);
      if(ratio<worst){worst=ratio;worstOn=item->property("text").toString();}
    }
    const auto verdict=QString("every row of the vibrant menu clears 4.5:1 (worst %1:1%2)")
                           .arg(worst,0,'f',2)
                           .arg(worstOn.isEmpty()?QString():" on "+worstOn);
    check(worst>=4.5,qPrintable(verdict));
  };
  auto choose=[&](const QString &layout){click("immersiveLayoutButton");
    if(layout=="lyrics"){menuInkIsReadable();shot("layout-menu");}
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
  // --- The immersive view's own proportions ---
  // The complaint about this screen was never a missing feature: it was that
  // the pieces do not line up and the artwork does not use the room it has.
  // These are the measurements that say so.
  choose("artwork");QTest::qWait(500);
  {
    auto top=visibleItem(w->contentItem(),"immersiveTopControls");
    auto bar=visibleItem(w->contentItem(),"immersiveToolbar");
    auto art=visibleItem(w->contentItem(),"immersiveArtwork");
    auto row=visibleItem(w->contentItem(),"immersiveSeekRow");
    check(top&&bar&&art&&row,"the immersive view has its four bands on screen");
    if(top&&bar&&art&&row){
      // Material's floating toolbar is 64dp. The immersive view used to set
      // its own height and quietly made the component taller than the token.
      check(qAbs(bar->height()-64)<0.5,
            qPrintable(QString("the transport is Material's 64dp floating toolbar (%1)")
                       .arg(bar->height(),0,'f',0)));
      // Everything stacked down the middle shares one centre line. It did not:
      // the seek row carried the queue and volume buttons on its end, which
      // pushed the bar itself off centre while the row stayed centred.
      const auto centreOf=[&](QQuickItem *item){return item->mapToScene(QPointF(item->width()/2,0)).x();};
      const double middle=w->width()/2.0;
      check(qAbs(centreOf(art)-middle)<1.5,
            qPrintable(QString("the artwork is centred (%1 of %2)").arg(centreOf(art),0,'f',1).arg(middle)));
      check(qAbs(centreOf(bar)-middle)<1.5,
            qPrintable(QString("so is the transport (%1)").arg(centreOf(bar),0,'f',1)));
      auto seek=visibleItem(w->contentItem(),"immersiveSeek");
      if(seek)
        check(qAbs(centreOf(seek)-middle)<1.5,
              qPrintable(QString("and so is the seek bar itself (%1)").arg(centreOf(seek),0,'f',1)));
      // The margin on one side is the margin on the other.
      const double left=top->mapToScene(QPointF(0,0)).x();
      const double right=w->width()-(top->mapToScene(QPointF(top->width(),0)).x());
      check(qAbs(left-right)<1.5,
            qPrintable(QString("the page margins match (%1 and %2)").arg(left,0,'f',0).arg(right,0,'f',0)));
    }
    // A taller window has to reach the artwork. It was capped by a constant,
    // so past a certain height the view stopped using the room it was given.
    // The stage pins the window so its captures match; lift that to ask the
    // layout what it does with more room, then pin it back.
    // And it stays square and inside the body, rather than growing past it.
    if(art){
      check(qAbs(art->width()-art->height())<1.5,"the artwork stays square");
      auto body=visibleItem(w->contentItem(),"immersiveBody");
      check(body&&art->height()<=body->height()+1,
            qPrintable(QString("and inside the body it sits in (%1 of %2)")
                       .arg(art->height(),0,'f',0).arg(body?body->height():0,0,'f',0)));
    }
    // A snackbar sits above whatever is anchored at the foot of the window
    // rather than over it. In this view that is the transport, and a constant
    // margin tuned for the ordinary player landed straight on it.
    b->toast("Immersive placement");
    if(waitFor([&]{auto t=visibleItem(w->contentItem(),"toastBar");return t&&t->height()>1;})){
      auto toast=visibleItem(w->contentItem(),"toastBar");
      auto bar=visibleItem(w->contentItem(),"immersiveToolbar");
      if(toast&&bar)
        check(toast->mapToScene(QPointF(0,toast->height())).y()<=bar->mapToScene(QPointF(0,0)).y()+0.5,
              qPrintable(QString("the notification clears the transport (ends %1, transport starts %2)")
                         .arg(toast->mapToScene(QPointF(0,toast->height())).y(),0,'f',0)
                         .arg(bar->mapToScene(QPointF(0,0)).y(),0,'f',0)));
      else check(false,"the notification and the transport are both on screen");
    } else check(false,"a notification appears to place");
    // The menu opened from this bar took the tertiary container, which is the
    // source hue rotated and so belongs to no other surface in the window: on
    // a warm cover it came out green beside a sepia interface. MenuTokens puts
    // a menu on surfaceContainer and marks a chosen item with the secondary
    // pair, and every menu in the app answers to the same rule.
    click("immersiveLayoutButton");QTest::qWait(400);
    {
      auto menu=w->findChild<QObject*>("immersiveLayoutMenu");
      auto surface=menu?menu->property("background").value<QQuickItem*>():nullptr;
      check(surface,"the immersive menu has a surface to read");
      if(surface){
        // The floating toolbar in this same view is on surfaceContainer by its
        // own token, so it is the role to measure against without reaching
        // into the theme singleton for it.
        const auto colour=surface->property("color").value<QColor>();
        auto bar=visibleItem(w->contentItem(),"immersiveToolbar");
        const auto onSurfaceContainer=bar?bar->property("color").value<QColor>():QColor();
        check(bar && colour==onSurfaceContainer,
              qPrintable(QString("the menu sits on the same surfaceContainer the transport does (%1 against %2)")
                         .arg(colour.name()).arg(onSurfaceContainer.name())));
      }
      shot("menu");
      QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(300);
    }
    shot("proportions");
  }
  b->setTheme("light");shot("light");b->setMotion(false);
  choose("artwork");check(player->property("displayedLayout")=="artwork","reduced motion applies layout immediately");
  choose("split");b->setTheme("dark");
  w->setProperty("immersive",false);QTest::qWait(150);w->showNormal();w->setMinimumSize({780,580});w->setMaximumSize({780,580});w->resize(780,580);w->setProperty("immersive",true);QTest::qWait(250);
  player=visibleItem(w->contentItem(),"immersivePlayer");shot("compact");
  const auto art=visibleItem(w->contentItem(),"immersiveArtwork");const auto seek=visibleItem(w->contentItem(),"immersiveSeek");
  check(art&&seek&&art->mapToScene({0,art->height()}).y()<seek->mapToScene({0,0}).y(),
        qPrintable(QString("compact artwork stays above transport (%1 against %2)")
                   .arg(art?art->mapToScene(QPointF(0,art->height())).y():-1,0,'f',0)
                   .arg(seek?seek->mapToScene(QPointF(0,0)).y():-1,0,'f',0)));
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
