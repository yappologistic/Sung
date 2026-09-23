#include "uitest.h"
#include "backend.h"
#include "m3color.h"
#include "rowselection.h"
#include <QColor>
#include <QAccessible>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QQmlContext>
#include <QQmlExpression>
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
  auto resizeTo=[&](int width,int height){
    w->showNormal();
    w->setMinimumSize({0,0});w->setMaximumSize({16777215,16777215});
    w->resize(width,height);QTest::qWait(180);
    check(w->width()==width&&w->height()==height,
          qPrintable(QString("window measures %1x%2").arg(width).arg(height)));
  };
  // WCAG 1.4.3 measures the actual ink against the background behind it.
  // Read the delegate colour and alpha, then sample the captured backdrop in
  // its left padding so artwork-driven theme colours are included.
  auto lyricContrast=[&](double floor,const QString &context){
    QList<QQuickItem*> labels;collectItems(w->contentItem(),"lyricLabel",labels);
    QQuickItem *rest=nullptr,*active=nullptr;
    for(auto label:labels){
      if(!label->isVisible()||!label->parentItem()||!label->parentItem()->isVisible())continue;
      if(label->parentItem()->property("current").toBool())active=label;
      else if(label->parentItem()->opacity()>0.99 && !rest)rest=label;
    }
    check(rest&&active,qPrintable(context+" has current and resting lyric delegates"));
    if(!rest||!active)return;
    const auto image=w->grabWindow();
    const auto p=rest->mapToScene(QPointF(2,rest->height()/2));
    const int x=qBound(0,qRound(p.x()*image.width()/w->width()),image.width()-1);
    const int y=qBound(0,qRound(p.y()*image.height()/w->height()),image.height()-1);
    const auto backdrop=image.pixelColor(x,y);
    const auto ink=rest->property("color").value<QColor>();
    const double alpha=ink.alphaF()*rest->opacity()*rest->parentItem()->opacity();
    const auto drawn=QColor::fromRgbF(ink.redF()*alpha+backdrop.redF()*(1-alpha),
                                     ink.greenF()*alpha+backdrop.greenF()*(1-alpha),
                                     ink.blueF()*alpha+backdrop.blueF()*(1-alpha));
    const double ratio=m3::contrastRatio(drawn,backdrop);
    check(ratio>=floor,qPrintable(QString("%1 resting lyric contrast %2:1 >= %3:1 (ink %4, backdrop %5)")
          .arg(context).arg(ratio,0,'f',2).arg(floor,0,'f',1).arg(ink.name(),backdrop.name())));
    check(active->property("color").value<QColor>()!=ink && active->scale()>rest->scale(),
          qPrintable(context+" keeps current line distinct by colour and scale"));
  };
  auto singAlongContrast=[&](const QString &context){
    auto rest=visibleItem(w->contentItem(),"singAlongLine");
    auto active=visibleItem(w->contentItem(),"singAlongCurrent");
    check(rest&&active,qPrintable(context+" has sung and unsung lines"));
    if(!rest||!active)return;
    const auto image=w->grabWindow();
    const auto p=rest->mapToScene(QPointF(2,rest->height()/2));
    const int x=qBound(0,qRound(p.x()*image.width()/w->width()),image.width()-1);
    const int y=qBound(0,qRound(p.y()*image.height()/w->height()),image.height()-1);
    const auto backdrop=image.pixelColor(x,y);
    const auto ink=rest->property("color").value<QColor>();
    const double alpha=ink.alphaF()*rest->opacity()*rest->parentItem()->opacity();
    const auto drawn=QColor::fromRgbF(ink.redF()*alpha+backdrop.redF()*(1-alpha),
                                     ink.greenF()*alpha+backdrop.greenF()*(1-alpha),
                                     ink.blueF()*alpha+backdrop.blueF()*(1-alpha));
    const double ratio=m3::contrastRatio(drawn,backdrop);
    check(ratio>=3.0,qPrintable(QString("%1 unsung large text %2:1 >= 3:1")
          .arg(context).arg(ratio,0,'f',2)));
    check(active->property("color").value<QColor>()!=ink&&active->parentItem()->scale()>rest->parentItem()->scale(),
          qPrintable(context+" keeps the sung line distinct by colour and scale"));
  };
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->setMinimumSize({1180,800});w->setMaximumSize({1180,800});w->resize(1180,800);QTest::qWait(700);QWindowSystemInterface::handleFocusWindowChanged(w);b->setTheme("dark");b->setMotion(true);b->setVolume(0);
  b->setAutoplay(false);b->setPrepareNext(false);b->setWatchMusicFolders(false);b->setOnlineArtwork(false);b->setLyricsFallback(false);
  QImage cover(640,640,QImage::Format_RGB32);cover.fill(QColor("#46646b"));
  QPainter paint(&cover);paint.setPen(QPen(QColor("#d9f1dd"),5));
  for(int i=0;i<8;++i)paint.drawEllipse(QPoint(320,320),30+i*32,30+i*32);
  paint.end();cover.save(dir+"/music/cover.png");
  QProcess encode;encode.start("ffmpeg",{"-nostdin","-v","error","-f","lavfi","-i","anullsrc=r=8000:cl=mono","-t","180","-metadata","title=Coming ashore","-metadata","album=Still Water","-metadata","artist=Example Artist",dir+"/music/01.flac"});
  check(encode.waitForFinished(30000)&&encode.exitCode()==0,"generate silent local audio");
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
  lyricContrast(3.0,"dark immersive");
  b->setTheme("light");QTest::qWait(250);shot("lyrics-light");lyricContrast(3.0,"light immersive");
  b->setTheme("dark");QTest::qWait(250);
  // At 1440 the lyric measure is bounded; at 1024 it stays on the same
  // centre line. Split mode is checked separately below.
  for(int width:{1440,1024}){
    resizeTo(width,900);
    auto pane=visibleItem(w->contentItem(),"lyricsView");
    check(pane&&pane->width()>=759.5&&pane->width()<=760.5&&qAbs(pane->mapToScene({pane->width()/2,0}).x()-width/2.0)<2,
          qPrintable(QString("%1px lyric-only column is bounded and centred").arg(width)));
    QList<QQuickItem*> lines;collectItems(w->contentItem(),"lyricLine",lines);
    QQuickItem *current=nullptr;
    for(auto line:lines)if(line->isVisible()&&line->property("current").toBool())current=line;
    check(pane&&current&&qAbs(current->mapToScene({0,current->height()/2}).y()-
          pane->mapToScene({0,pane->height()/2}).y())<30,
          qPrintable(QString("%1px current lyric stays at the vertical reading centre").arg(width)));
    shot(QString("lyrics-%1").arg(width));
  }
  resizeTo(1180,800);
  auto lyricLines=QList<QQuickItem*>{};collectItems(w->contentItem(),"lyricLine",lyricLines);
  QQuickItem *seekLine=nullptr;
  for(auto line:lyricLines)if(line->isVisible()&&!line->property("current").toBool()&&line->property("index").toInt()==2)seekLine=line;
  check(seekLine,"a non-current lyric can be targeted");
  if(seekLine){
    const auto point=seekLine->mapToScene(seekLine->boundingRect().center()).toPoint();
    QTest::mouseMove(w,point);QTest::qWait(80);
    check(seekLine->property("hovered").toBool(),"resting lyric remains hoverable");
    QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,point);QTest::qWait(100);
    check(b->position()>=19500&&b->position()<=20500,"clicking a resting lyric seeks its timestamp");
  }
  b->seek(11000);QTest::qWait(100);
  choose("artwork");check(player->property("displayedLayout")=="artwork","artwork layout applies");shot("artwork");
  choose("split");check(player->property("displayedLayout")=="split","split layout applies");shot("split");
  {
    auto pane=visibleItem(w->contentItem(),"lyricsView");
    auto art=visibleItem(w->contentItem(),"immersiveArtwork");
    check(pane&&art&&pane->mapToScene({0,0}).x()>art->mapToScene({art->width(),0}).x(),
          "split lyrics remain to the right of the artwork");
  }
  b->seek(11000);
  choose("singalong");QTest::qWait(500);shot("singalong-dark");
  singAlongContrast("dark sing along");
  b->setTheme("light");QTest::qWait(300);shot("singalong-light");
  singAlongContrast("light sing along");
  b->setTheme("dark");QTest::qWait(200);
  auto singSeek=visibleItem(w->contentItem(),"immersiveSeek");
  check(singSeek&&b->lyricIndex()==1,"sing along starts on the second timed line");
  if(singSeek){
    const auto point=singSeek->mapToScene({singSeek->width()*0.12,singSeek->height()/2}).toPoint();
    QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,point);
    check(waitFor([&]{return b->lyricIndex()==2;}),"seeking through the UI advances the sung line");
    QList<QQuickItem*> fills;collectItems(w->contentItem(),"singAlongFill",fills);
    QQuickItem *previousFill=nullptr;
    for(auto fill:fills)if(fill->parentItem()&&fill->parentItem()->property("index").toInt()==1)previousFill=fill;
    check(previousFill,"previous sung line keeps a fill to fade");
    if(previousFill){
      const auto effects=b->motionSprings(b->motionScheme()!="standard").value("defaultEffects").toMap();
      const int effectsMs=effects.value("ms").toInt();
      auto fade=qmlContext(previousFill)->objectForName("fillFadeAnimation");
      check(fade&&fade->property("duration").toInt()==effectsMs&&
            QQmlProperty::read(fade,"easing.bezierCurve",qmlContext(fade)).toList()==effects.value("curve").toList(),
            qPrintable(QString("sing-along fill uses DefaultEffects duration and curve (%1/%2, curve %3)")
              .arg(fade?fade->property("duration").toInt():-1).arg(effectsMs)
              .arg(fade ? QQmlProperty::read(fade,"easing.bezierCurve",qmlContext(fade)).toList()==effects.value("curve").toList() : false)));
      bool sawFade=false;
      for(int elapsed=0;elapsed<effectsMs && !sawFade;elapsed+=10){
        const double alpha=previousFill->opacity();
        sawFade=alpha>0.02&&alpha<0.98;
        if(!sawFade)QTest::qWait(10);
      }
      check(sawFade,"completed lyric fill fades instead of cutting away");
      QTest::qWait(effectsMs+80);
      QList<QQuickItem*> resting;collectItems(w->contentItem(),"singAlongLine",resting);
      QQuickItem *previousBody=nullptr;
      for(auto body:resting)if(body->parentItem()&&body->parentItem()->property("index").toInt()==1)previousBody=body;
      QColor mutedColor;bool mutedValid=false;
      if(previousBody){
        QQmlExpression muted(qmlContext(previousBody),previousBody,"Theme.muted");
        mutedColor=muted.evaluate().value<QColor>();mutedValid=!muted.hasError();
      }
      check(previousBody&&mutedValid&&previousBody->property("color").value<QColor>()==mutedColor&&
            previousFill->opacity()<0.01&&!previousFill->isVisible()&&
            qAbs(previousFill->width()-previousBody->width())<1,
            "finished line keeps full-width fill while fading, then reads in onSurfaceVariant");
      int accented=0;
      for(auto fill:fills)if(fill->isVisible()&&fill->opacity()>0.01)++accented;
      check(accented==1,"only the current sung line keeps an accent fill");
      shot("singalong-later-line");
    }
    b->setMotion(false);
    const auto nextPoint=singSeek->mapToScene({singSeek->width()*0.18,singSeek->height()/2}).toPoint();
    QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,nextPoint);
    check(waitFor([&]{return b->lyricIndex()==3;}),"reduced-motion seek advances the sung line");
    QTest::qWait(20);
    QList<QQuickItem*> reducedFills;collectItems(w->contentItem(),"singAlongFill",reducedFills);
    bool previousCleared=false;
    for(auto fill:reducedFills)if(fill->parentItem()&&fill->parentItem()->property("index").toInt()==2)
      previousCleared=fill->opacity()<0.01&&!fill->isVisible();
    check(previousCleared,"reduced motion clears the completed fill immediately");
    b->setMotion(true);
  }
  choose("split");
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
  {
    auto button=visibleItem(w->contentItem(),"immersiveQueueButton");
    auto accessible=button?QAccessible::queryAccessibleInterface(button):nullptr;
    check(accessible&&accessible->text(QAccessible::Name)==QString::fromUtf8("Queue \u00b7 Ctrl+L"),
          "queue button keeps its accessible name while the sheet suppresses its tip");
  }
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
  check(b->position()==before,"text editing does not seek playback");
  for(auto key:{Qt::Key_L,Qt::Key_I,Qt::Key_G,Qt::Key_H,Qt::Key_T})QTest::keyClick(w,key);
  QTest::qWait(180);
  {
    auto results=visibleItem(w->contentItem(),"lyricSearchResults");
    check(results&&results->property("count").toInt()==1&&
          results->property("currentIndex").toInt()==0&&
          !results->property("highlightFollowsCurrentItem").toBool(),
          "search results use a custom spatial highlight for the selected match");
  }
  QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(200);
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
    // MenuDefaults.groupStandardContainerColor reads
    // StandardMenuTokens.ContainerColor, surfaceContainerLow.
    click("immersiveLayoutButton");QTest::qWait(400);
    {
      auto menu=w->findChild<QObject*>("immersiveLayoutMenu");
      auto surface=menu?menu->property("background").value<QQuickItem*>():nullptr;
      check(surface,"the immersive menu has a surface to read");
      if(surface){
        const auto colour=surface->property("color").value<QColor>();
        QQmlExpression standardSurface(qmlContext(menu),menu,"Theme.surfaceLow");
        const auto expected=standardSurface.evaluate().value<QColor>();
        check(!standardSurface.hasError()&&colour==expected,
              qPrintable(QString("the standard menu uses surfaceContainerLow (%1 against %2)")
                         .arg(colour.name()).arg(expected.name())));
      }
      shot("menu");
      QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(300);
    }
    shot("proportions");
  }
  // Qt ListView only exposes a duration for its built-in highlight move. The
  // custom highlights and opacity transitions must carry each Material
  // spring's duration and curve together.
  {
    const auto springs=b->motionSprings(b->motionScheme()!="standard");
    auto springCheck=[&](const char *name,const char *spring){
      auto animation=w->findChild<QObject*>(name);
      const auto pair=springs.value(spring).toMap();
      check(animation&&animation->property("duration").toInt()==pair.value("ms").toInt()&&
            QQmlProperty::read(animation,"easing.bezierCurve",qmlContext(animation)).toList()==pair.value("curve").toList(),
            qPrintable(QString("%1 uses the %2 duration and curve").arg(name,spring)));
    };
    springCheck("lyricHighlightMotion","defaultSpatial");
    springCheck("singAlongHighlightMotion","defaultSpatial");
    springCheck("immersiveFadeOutMotion","fastEffects");
    springCheck("immersiveFadeInMotion","fastEffects");
    springCheck("immersiveDetailFadeMotion","fastEffects");
  }
  // The artwork, transport, and seek bar must share a centre at every width
  // used in the immersive captures, including the compact layout.
  b->setMotion(false);
  for(int width:{1440,1024,840,600}){
    resizeTo(width,width==600?620:900);
    auto artwork=visibleItem(w->contentItem(),"immersiveArtwork");
    auto toolbar=visibleItem(w->contentItem(),"immersiveToolbar");
    auto seek=visibleItem(w->contentItem(),"immersiveSeek");
    const auto centre=[](QQuickItem *item){return item->mapToScene({item->width()/2,0}).x();};
    check(artwork&&toolbar&&seek,qPrintable(QString("%1px artwork, transport and seek exist").arg(width)));
    if(artwork&&toolbar&&seek){
      const double art=centre(artwork),bar=centre(toolbar),wave=centre(seek);
      check(qAbs(art-bar)<=2&&qAbs(art-wave)<=2,
            qPrintable(QString("%1px centres art %2, transport %3, seek %4 differ by <=2px")
                       .arg(width).arg(art,0,'f',1).arg(bar,0,'f',1).arg(wave,0,'f',1)));
    }
    shot(QString("artwork-%1").arg(width));
  }
  resizeTo(480,620);
  click("immersiveLayoutButton");click("immersiveCoverflowToggle");
  auto covers=visibleItem(w->contentItem(),"coverflowView");
  auto highlight=covers?covers->property("highlightItem").value<QQuickItem*>():nullptr;
  auto currentCover=covers?covers->property("currentItem").value<QQuickItem*>():nullptr;
  check(covers&&highlight&&currentCover&&!covers->property("highlightFollowsCurrentItem").toBool()&&
        qAbs(highlight->width()-currentCover->width())<1,
        "coverflow follows a cell-sized custom highlight");
  if(covers){
    b->setMotion(true);
    const auto spatial=b->motionSprings(b->motionScheme()!="standard").value("defaultSpatial").toMap();
    const int spatialMs=spatial.value("ms").toInt();
    auto slide=w->findChild<QObject*>("coverflowHighlightMotion");
    check(slide&&slide->property("duration").toInt()==spatialMs&&
          QQmlProperty::read(slide,"easing.bezierCurve",qmlContext(slide)).toList()==spatial.value("curve").toList(),
          "coverflow highlight uses DefaultSpatial duration and curve");
    const double startX=covers->property("contentX").toDouble();
    auto next=visibleItem(w->contentItem(),"immersiveNextButton");
    check(next,"immersive next button is reachable");
    if(next){
      const auto point=next->mapToScene(next->boundingRect().center()).toPoint();
      QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,point);
      check(waitFor([&]{return b->currentIndex()==1;}),"clicking Next changes the playing cover");
      QTest::qWait(spatialMs/3);
      const double middleX=covers->property("contentX").toDouble();
      QTest::qWait(spatialMs+100);
      const double endX=covers->property("contentX").toDouble();
      check(qAbs(endX-startX)>20&&middleX>qMin(startX,endX)+2&&middleX<qMax(startX,endX)-2,
            qPrintable(QString("coverflow slides through contentX %1 -> %2 -> %3")
              .arg(startX,0,'f',1).arg(middleX,0,'f',1).arg(endX,0,'f',1)));
      auto target=visibleItem(w->contentItem(),"coverflowItem_1");
      check(target&&qAbs(target->mapToScene({target->width()/2,0}).x()-
            covers->mapToScene({covers->width()/2,0}).x())<2,
            "the next cover settles at the carousel centre");
      shot("coverflow-slide-settled");
    }
    b->setMotion(false);
    auto previous=visibleItem(w->contentItem(),"immersivePreviousButton");
    check(previous,"immersive previous button is reachable");
    if(previous){
      const auto point=previous->mapToScene(previous->boundingRect().center()).toPoint();
      QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,point);
      check(waitFor([&]{return b->currentIndex()==0&&b->lyricLines().size()==5;}),
            "clicking Previous restores the lyric fixture");
      QTest::qWait(20);
      auto first=visibleItem(w->contentItem(),"coverflowItem_0");
      check(first&&qAbs(first->mapToScene({first->width()/2,0}).x()-
            covers->mapToScene({covers->width()/2,0}).x())<2,
            "reduced motion centres the previous cover immediately");
    }
  }
  auto title=visibleItem(w->contentItem(),"immersiveTitle");
  check(title&&title->property("lineCount").toInt()==1&&!title->property("truncated").toBool(),
        "480px coverflow title uses the column width and stays on one line");
  shot("coverflow-480");
  for(const auto &pair:{qMakePair("immersiveArtistButton","immersiveArtistFocusRing"),
                        qMakePair("immersiveAlbumButton","immersiveAlbumFocusRing")}){
    auto button=visibleItem(w->contentItem(),pair.first);
    auto ring=w->findChild<QQuickItem*>(pair.second);
    if(button)button->forceActiveFocus(Qt::TabFocusReason);
    QTest::qWait(30);
    check(button&&ring&&ring->isVisible()&&qAbs(ring->x()+3)<0.5&&
          qAbs(ring->width()-button->width()-6)<0.5&&ring->property("radius").toDouble()>12&&
          QQmlProperty::read(ring,"border.width",qmlContext(ring)).toInt()==2,
          qPrintable(QString("%1 uses an external 2px shaped focus ring").arg(pair.first)));
  }
  auto artistLink=visibleItem(w->contentItem(),"immersiveArtistButton");
  auto albumLink=visibleItem(w->contentItem(),"immersiveAlbumButton");
  if(artistLink)artistLink->forceActiveFocus(Qt::TabFocusReason);
  QTest::keyClick(w,Qt::Key_Tab);
  check(albumLink&&w->activeFocusItem()==albumLink,
        "Tab reaches the album after the artist in artwork details");
  shot("focus-ring-480");
  auto waveMotion=w->findChild<QObject*>("seekWaveMotion");
  check(waveMotion&&waveMotion->property("duration").toInt()==1000,
        "the 28px seek wave advances one wavelength per second");
  resizeTo(1180,800);
  b->setMotion(true);
  b->setTheme("light");shot("light");b->setMotion(false);
  choose("artwork");check(player->property("displayedLayout")=="artwork","reduced motion applies layout immediately");
  choose("split");b->setTheme("dark");
  w->setProperty("immersive",false);resizeTo(1440,900);
  if(w->property("side")!="lyrics")QTest::keyClick(w,Qt::Key_Y,Qt::ControlModifier);
  QTest::qWait(300);
  check(w->property("side")=="lyrics"&&visibleItem(w->contentItem(),"lyricsView"),
        "side lyric panel opens at wide width");
  b->setTheme("light");QTest::qWait(180);shot("side-lyrics-light");
  lyricContrast(4.5,"light side pane");
  b->setTheme("dark");QTest::qWait(180);shot("side-lyrics-dark");
  lyricContrast(4.5,"dark side pane");
  QMetaObject::invokeMethod(w,"activateSide",Q_ARG(QString,QString("lyrics")));
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
