#include "uitest.h"
#include "backend.h"
#include "m3color.h"
#include "rowselection.h"
#include <QColor>
#include <QAccessible>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QMap>
#include <QSet>
#include <QImage>
#include <QLoggingCategory>
#include <QPainter>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QProcess>
#include <QPointer>
#include <QStringList>
#include <QTest>
#include <QWheelEvent>
#include <qpa/qwindowsysteminterface.h>
#include <algorithm>
#include <functional>
#include <atomic>

namespace {
struct QmlMessageAudit {
  static std::atomic<QmlMessageAudit*> active;
  QtMessageHandler previous=nullptr;
  std::atomic<int> loops{0},types{0},references{0},assignments{0};
  QmlMessageAudit(){previous=qInstallMessageHandler(&capture);active.store(this);}
  ~QmlMessageAudit(){active.store(nullptr);qInstallMessageHandler(previous);}
  static void capture(QtMsgType type,const QMessageLogContext &context,const QString &message){
    if(auto audit=active.load()){
      if(message.contains("Binding loop detected"))++audit->loops;
      if(message.contains("TypeError:"))++audit->types;
      if(message.contains("ReferenceError:"))++audit->references;
      if(message.contains("Cannot assign"))++audit->assignments;
      if(audit->previous)audit->previous(type,context,message);
    }
  }
  int total()const{return loops.load()+types.load()+references.load()+assignments.load();}
};
std::atomic<QmlMessageAudit*> QmlMessageAudit::active{nullptr};
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
void collectImmersiveControls(QQuickItem *root,QList<QQuickItem*> &found,bool inFlickable=false) {
  if(!root||!root->isVisible()||!root->isEnabled())return;
  const bool scrolling=inFlickable||root->inherits("QQuickFlickable");
  if(!scrolling&&root->inherits("QQuickControl")&&!root->inherits("QQuickScrollBar")&&
     (root->property("pressed").isValid()||root->property("value").isValid()))found.append(root);
  for(auto child:root->childItems())collectImmersiveControls(child,found,scrolling);
}
}

void runImmersivePolishTests(Backend *b,QQuickWindow *w) {
  QmlMessageAudit messages;
  int failures=0;
  auto check=[&](bool ok,const char *name){fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",name);fflush(stdout);if(!ok)++failures;};
  const auto dir=qEnvironmentVariable("SUNG_TEST_OUTPUT");QDir().mkpath(dir+"/music");
  auto click=[&](const QString &name){
    auto item=visibleItem(w->contentItem(),name);check(item,qPrintable(name));
    if(item){const auto point=item->mapToScene(item->boundingRect().center()).toPoint();QTest::mouseMove(w,point);QTest::qWait(60);QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,point);}
  };
  auto shot=[&](const QString &name){QTest::qWait(250);check(w->grabWindow().save(dir+'/'+name+".png"),qPrintable(name));};
  auto resizeTo=[&](int width,int height){
    w->showNormal();
    w->setMinimumSize({0,0});w->setMaximumSize({16777215,16777215});
    w->resize(width,height);
    check(waitFor([&]{
            if(w->width()!=width||w->height()!=height)return false;
            if(!w->property("immersive").toBool())return true;
            auto body=visibleItem(w->contentItem(),"immersiveBody");
            auto layout=visibleItem(w->contentItem(),"immersiveLayoutButton");
            if(!body||!layout||qAbs(body->width()-(width-2*(width<600?16:24)))>1)return false;
            const double actionX=layout->mapToScene(layout->boundingRect().center()).x();
            if(actionX<0||actionX>=width)return false;
            auto player=visibleItem(w->contentItem(),"immersivePlayer");
            // The offscreen window can report its new size a frame before its
            // anchored Loader receives the matching height.
            if(player&&qAbs(player->height()-height)>1)return false;
            if(player&&player->property("displayedLayout")=="lyrics"&&width>=1024){
              auto pane=visibleItem(w->contentItem(),"lyricsView");
              return pane&&qAbs(pane->width()-760)<1&&
                     qAbs(pane->mapToScene({pane->width()/2,0}).x()-width/2.0)<2;
            }
            return true;
          }),qPrintable(QString("window and immersive layout measure %1x%2").arg(width).arg(height)));
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
  auto lyricEdgeFade=[&](const QString &context){
    auto list=visibleItem(w->contentItem(),"liveLyrics");
    QList<QQuickItem*> lines;collectItems(list,"lyricLine",lines);
    const auto viewport=list?list->mapRectToScene(list->boundingRect()):QRectF();
    bool crossed=false,transparent=true,currentOpaque=true;
    for(auto line:lines){
      if(!line->isVisible())continue;
      QList<QQuickItem*> labels;collectItems(line,"lyricLabel",labels);
      if(labels.isEmpty())continue;
      const auto ink=labels.first()->mapRectToScene(labels.first()->boundingRect());
      if(line->property("current").toBool()&&ink.center().y()>viewport.top()&&ink.center().y()<viewport.bottom())
        currentOpaque&=line->opacity()>0.99;
      if(line->property("current").toBool())continue;
      const bool topCut=ink.top()<viewport.top()&&ink.bottom()>viewport.top()+2;
      const bool bottomCut=ink.top()<viewport.bottom()-2&&ink.bottom()>viewport.bottom();
      if(topCut||bottomCut){crossed=true;transparent&=line->opacity()<0.1;}
    }
    check(list&&crossed&&transparent&&currentOpaque,
          qPrintable(QString("%1 fades edge lyrics (crossed %2, transparent %3, current %4)")
            .arg(context).arg(crossed).arg(transparent).arg(currentOpaque)));
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
  w->setProperty("immersive",true);
  check(waitFor([&]{return visibleItem(w->contentItem(),"immersivePlayer");}),"immersive player appears");
  auto player=visibleItem(w->contentItem(),"immersivePlayer");check(player,"immersive player loads");
  if(!player){QCoreApplication::exit(1);return;}
  check(!player->property("autoHideControls").toBool()&&player->property("controlsShown").toBool(),"controls remain visible by default");
  auto menuOpen=[&]{auto menu=w->findChild<QObject*>("immersiveLayoutMenu");return menu&&menu->property("visible").toBool();};
  // BasicTooltip.kt:188-199 dismisses the tooltip Popup when its anchor is
  // pressed, and :262-280 waits for another mouse Enter before showing it.
  auto more=visibleItem(w->contentItem(),"immersiveLayoutButton");
  check(more,"More actions is available for the tooltip check");
  if(more){
    const auto point=more->mapToScene(more->boundingRect().center()).toPoint();
    auto showing=[&]{auto tip=more->findChild<QObject*>("buttonTip");return tip&&tip->property("visible").toBool();};
    QTest::mouseMove(w,point);
    check(waitFor(showing),"hovering More actions shows its tooltip");
    click("immersiveLayoutButton");
    check(waitFor(menuOpen),"More actions opens its menu");
    check(!showing(),"clicking More actions clears the tooltip above its menu");
    shot("menu-after-click");
    QTest::keyClick(w,Qt::Key_Escape);
    check(waitFor([&]{return !menuOpen();}),"the tooltip check closes its menu");
    QTest::mouseMove(w,QPoint(20,20));QTest::qWait(120);
    QTest::mouseMove(w,point);
    check(waitFor(showing),"a new hover shows More actions again");
    QTest::mouseMove(w,QPoint(20,20));QTest::qWait(120);
  }
  click("immersiveLayoutButton");
  check(waitFor(menuOpen),"layout menu opens");
  click("immersiveAutoHide");
  check(waitFor([&]{return player->property("autoHideControls").toBool();}),"idle hiding can be enabled explicitly");
  check(waitFor([&]{return !menuOpen();}),"layout menu closes after auto-hide choice");
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
    check(waitFor(menuOpen),"layout menu opens for choice");
    if(layout=="lyrics"){menuInkIsReadable();shot("layout-menu");}
    click("immersiveLayout_"+layout);
    check(waitFor([&]{auto body=visibleItem(w->contentItem(),"immersiveBody");
                      return player->property("preferredLayout")==layout&&
                             player->property("displayedLayout")==player->property("effectiveLayout")&&
                             body&&qAbs(body->opacity()-1)<0.01&&!menuOpen();}),
          qPrintable(QString("%1 layout settles").arg(layout)));
  };
  choose("lyrics");check(player->property("preferredLayout")=="lyrics"&&player->property("displayedLayout")=="artwork","missing lyrics falls back without losing preference");
  QFile lrc(dir+"/sample.lrc");check(lrc.open(QIODevice::WriteOnly),"create timed lyrics");
  lrc.write("[00:00]The light arrives\n[00:10]Across the still water\n[00:20]A quiet moment\n[00:30]We move with the tide\n[01:00]The evening settles\n");lrc.close();
  b->importLyrics(QUrl::fromLocalFile(lrc.fileName()),song.value("id").toString());
  check(waitFor([&]{return b->lyricLines().size()==5&&player->property("displayedLayout")=="lyrics";}),"available lyrics restore saved layout");
  w->setProperty("toastPending",false);
  check(waitFor([&]{return !visibleItem(w->contentItem(),"toastBar");}),
        "lyric import notice clears before lyric captures");
  b->seek(11000);QTest::qWait(450);shot("lyrics");
  lyricContrast(3.0,"dark immersive");
  b->setTheme("light");QTest::qWait(250);shot("lyrics-light");lyricContrast(3.0,"light immersive");
  b->setTheme("dark");QTest::qWait(250);
  // At 1024, 1440 and 2560 the lyric measure stays bounded on the same
  // centre line. Split mode is checked separately below.
  for(int width:{1024,1440,2560}){
    resizeTo(width,900);
    auto pane=visibleItem(w->contentItem(),"lyricsView");
    check(pane&&pane->width()>=759.5&&pane->width()<=760.5&&qAbs(pane->mapToScene({pane->width()/2,0}).x()-width/2.0)<2,
          qPrintable(QString("%1px lyric-only column is bounded and centred").arg(width)));
    // The ListView can finish its centre move after the window's geometry
    // settles. Poll the actual reading position, retaining the 30px bound.
    check(waitFor([&]{
            QList<QQuickItem*> lines;collectItems(w->contentItem(),"lyricLine",lines);
            for(auto line:lines)if(line->isVisible()&&line->property("current").toBool())
              return pane&&qAbs(line->mapToScene({0,line->height()/2}).y()-
                                pane->mapToScene({0,pane->height()/2}).y())<30;
            return false;
          }),
          qPrintable(QString("%1px current lyric stays at the vertical reading centre").arg(width)));
    shot(QString("lyrics-%1").arg(width));
    lyricEdgeFade(QString("%1px dark immersive").arg(width));
    b->setTheme("light");shot(QString("lyrics-%1-light").arg(width));
    lyricEdgeFade(QString("%1px light immersive").arg(width));
    b->setTheme("dark");
  }
  // The wide measure cannot force the other immersive rows past the window
  // edge on compact widths. These are the same widths as the layout audit.
  for(const QSize size:{QSize(840,800),QSize(600,800),QSize(480,620)}){
    w->showNormal();w->setMinimumSize({0,0});w->setMaximumSize({16777215,16777215});w->resize(size);
    check(waitFor([&]{
            if(w->size()!=size)return false;
            auto body=visibleItem(w->contentItem(),"immersiveBody");
            if(!body)return false;
            const double before=body->width();QTest::qWait(30);
            return qAbs(body->width()-before)<0.5;
          }),qPrintable(QString("%1px narrow layout reaches a stable width").arg(size.width())));
    for(const auto &theme:{"dark","light"}){
      b->setTheme(theme);
      auto pane=visibleItem(w->contentItem(),"lyricsView");
      const int margin=size.width()<600?16:24;
      const double available=size.width()-2*margin;
      const double left=pane?pane->mapToScene({0,0}).x():-1;
      check(pane&&qAbs(pane->width()-available)<2&&left>=margin-2&&
            left+pane->width()<=size.width()-margin+2,
            qPrintable(QString("%1px %2 lyric column fits content width (pane %3 at %4, available %5)")
              .arg(size.width()).arg(theme).arg(pane?pane->width():-1,0,'f',1).arg(left,0,'f',1).arg(available,0,'f',1)));
      QList<QQuickItem*> controls;collectImmersiveControls(player,controls);
      QStringList overflow;
      for(auto control:controls){
        const auto rect=control->mapRectToScene(control->boundingRect());
        if(rect.left()<-1||rect.top()<-1||rect.right()>size.width()+1||rect.bottom()>size.height()+1){
          auto name=control->objectName();if(name.isEmpty())name=control->property("tip").toString();
          overflow.append(QString("%1 [%2,%3]").arg(name).arg(rect.left(),0,'f',1).arg(rect.right(),0,'f',1));
        }
      }
      check(controls.size()>=10&&overflow.isEmpty(),
            qPrintable(QString("%1px %2 immersive controls stay inside window (%3 controls; %4)")
              .arg(size.width()).arg(theme).arg(controls.size()).arg(overflow.join(", "))));
      QMetaObject::invokeMethod(player,"wake");
      shot(QString("lyrics-%1-%2").arg(size.width()).arg(theme));
    }
  }
  b->setTheme("dark");
  resizeTo(1180,800);
  auto lyricLines=QList<QQuickItem*>{};collectItems(w->contentItem(),"lyricLine",lyricLines);
  QQuickItem *seekLine=nullptr;
  for(auto line:lyricLines)if(line->isVisible()&&!line->property("current").toBool()&&line->property("index").toInt()==2)seekLine=line;
  check(seekLine,"a non-current lyric can be targeted");
  if(seekLine){
    check(waitFor([&]{
            const auto before=seekLine->mapToScene(seekLine->boundingRect().center());
            QTest::qWait(60);
            const auto after=seekLine->mapToScene(seekLine->boundingRect().center());
            return qAbs(before.y()-after.y())<0.5&&after.y()>0&&after.y()<w->height();
          }),"resting lyric settles inside the viewport before pointer input");
    const auto point=seekLine->mapToScene(seekLine->boundingRect().center()).toPoint();
    QTest::mouseMove(w,point);
    check(waitFor([&]{return seekLine->property("hovered").toBool();}),
          qPrintable(QString("resting lyric remains hoverable at %1,%2").arg(point.x()).arg(point.y())));
    QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,point);
    check(waitFor([&]{return b->position()>=19500&&b->position()<=20500;}),"clicking a resting lyric seeks its timestamp");
  }
  auto liveList=visibleItem(w->contentItem(),"liveLyrics");
  QList<QQuickItem*> keyboardLines;collectItems(liveList,"lyricLine",keyboardLines);
  bool linesOutsideTab=true;
  for(auto line:keyboardLines)linesOutsideTab&=!line->activeFocusOnTab();
  check(liveList&&liveList->activeFocusOnTab()&&linesOutsideTab,
        "reading lyrics use one Tab stop rather than one per line");
  if(liveList){
    liveList->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(w,Qt::Key_Home);
    check(liveList->property("keyboardIndex").toInt()==0,"Home chooses the first lyric");
    QTest::keyClick(w,Qt::Key_Down);
    check(liveList->property("keyboardIndex").toInt()==1,"Down moves one keyboard lyric");
    QTest::keyClick(w,Qt::Key_End);
    const int last=b->lyricLines().size()-1;
    check(liveList->property("keyboardIndex").toInt()==last,"End chooses the last lyric");
    QTest::keyClick(w,Qt::Key_Up);
    check(liveList->property("keyboardIndex").toInt()==last-1,"Up moves one keyboard lyric");
    QTest::keyClick(w,Qt::Key_End);
    QQuickItem *lastLine=nullptr;
    QList<QQuickItem*> visibleLines;collectItems(liveList,"lyricLine",visibleLines);
    for(auto line:visibleLines)if(line->property("index").toInt()==last)lastLine=line;
    const auto viewport=liveList->mapRectToScene(liveList->boundingRect());
    const auto lineRect=lastLine?lastLine->mapRectToScene(lastLine->boundingRect()):QRectF();
    check(lastLine&&lineRect.top()>=viewport.top()-1&&lineRect.bottom()<=viewport.bottom()+1,
          "keyboard lyric stays fully in the viewport");
    auto accessible=QAccessible::queryAccessibleInterface(liveList);
    check(accessible&&accessible->text(QAccessible::Name).contains(QString("%1 of %2").arg(last+1).arg(last+1))&&
          accessible->text(QAccessible::Name).contains("The evening settles"),
          "focused lyric announces its text and position");
    QTest::keyClick(w,Qt::Key_Return);
    check(waitFor([&]{return b->position()>=59500&&b->position()<=61000;}),
          "Enter seeks the keyboard lyric");
    QTest::keyClick(w,Qt::Key_Home);
    check(liveList->hasActiveFocus()&&liveList->property("keyboardIndex").toInt()==0,
          "lyric list keeps keyboard focus after seeking");
    QTest::keyClick(w,Qt::Key_Space);
    check(waitFor([&]{return b->position()<1500;}),
          qPrintable(QString("Space seeks the first keyboard lyric (position %1, selection %2, focus %3)")
            .arg(b->position()).arg(liveList->property("keyboardIndex").toInt())
            .arg(w->activeFocusItem()?w->activeFocusItem()->objectName():QString("none"))));
    QTest::keyClick(w,Qt::Key_Tab);
    auto focus=w->activeFocusItem();
    check(focus&&focus!=liveList&&!liveList->isAncestorOf(focus),
          qPrintable(QString("Tab leaves the lyric list after one stop (focus %1)")
            .arg(focus?focus->objectName():QString("none"))));
  }
  b->seek(11000);QTest::qWait(100);
  choose("artwork");check(player->property("displayedLayout")=="artwork","artwork layout applies");shot("artwork");
  {
    // The cover alone centres its details as a narrowing stack, a page
    // margin below the cover so the title never meets it.
    auto art=visibleItem(w->contentItem(),"immersiveArtwork");
    auto heading=visibleItem(w->contentItem(),"immersiveTitle");
    const double gap=player->property("coverGap").toDouble();
    check(art&&heading&&gap>=16&&heading->property("horizontalAlignment").toInt()==Qt::AlignHCenter&&
          heading->mapToScene({0,0}).y()-art->mapToScene({0,art->height()}).y()>=gap-1,
          qPrintable(QString("artwork view centres the details %1px below the cover").arg(gap)));
  }
  choose("split");check(player->property("displayedLayout")=="split","split layout applies");shot("split");
  {
    auto pane=visibleItem(w->contentItem(),"lyricsView");
    auto art=visibleItem(w->contentItem(),"immersiveArtwork");
    check(pane&&art&&pane->mapToScene({0,0}).x()>art->mapToScene({art->width(),0}).x(),
          "split lyrics remain to the right of the artwork");
    // Beside lyrics the details stay left-aligned: the link text keeps the
    // title's edge inside its 12dp padded state layer.
    auto heading=visibleItem(w->contentItem(),"immersiveTitle");
    auto artistLink=visibleItem(w->contentItem(),"immersiveArtistButton");
    auto albumLink=visibleItem(w->contentItem(),"immersiveAlbumButton");
    auto artistInk=artistLink?artistLink->property("contentItem").value<QQuickItem*>():nullptr;
    auto albumInk=albumLink?albumLink->property("contentItem").value<QQuickItem*>():nullptr;
    check(heading&&artistInk&&albumInk&&heading->property("horizontalAlignment").toInt()==Qt::AlignLeft&&
          qAbs(heading->mapToScene({0,0}).x()-artistInk->mapToScene({0,0}).x())<1&&
          qAbs(heading->mapToScene({0,0}).x()-albumInk->mapToScene({0,0}).x())<1,
          "split view keeps the details left-aligned on one edge");
    // That edge is the cover's. A short window makes the cover the height it
    // can have and centres it in a wider column; the details used to stay at
    // the column's edge and hang off to the cover's left.
    for(const QSize size:{QSize(1180,800),QSize(1600,1000),QSize(1440,640),QSize(900,560)}){
      resizeTo(size.width(),size.height());QTest::qWait(150);
      auto cover=visibleItem(w->contentItem(),"immersiveArtwork");
      auto title=visibleItem(w->contentItem(),"immersiveTitle");
      auto artist=visibleItem(w->contentItem(),"immersiveArtistButton");
      auto ink=artist?artist->property("contentItem").value<QQuickItem*>():nullptr;
      const double coverLeft=cover?cover->mapToScene({0,0}).x():-1;
      check(cover&&title&&ink&&qAbs(title->mapToScene({0,0}).x()-coverLeft)<1&&
            qAbs(ink->mapToScene({0,0}).x()-coverLeft)<1&&
            title->mapToScene({title->width(),0}).x()<=cover->parentItem()->mapToScene({cover->parentItem()->width(),0}).x()+1,
            qPrintable(QString("%1x%2 split details start at the cover's left edge (cover %3, title %4, artist %5)")
                       .arg(size.width()).arg(size.height()).arg(coverLeft,0,'f',1)
                       .arg(title?title->mapToScene({0,0}).x():-1,0,'f',1).arg(ink?ink->mapToScene({0,0}).x():-1,0,'f',1)));
    }
    shot("split-short-window");
    resizeTo(1180,800);QTest::qWait(150);
    // The line being sung sits level with the cover's middle, where the eye
    // already is, rather than in a band set by the lyric column's own height,
    // which reaches down past the details and put the line well below the
    // picture. It holds from one line to the next and at another window size.
    auto lyricList=visibleItem(w->contentItem(),"liveLyrics");
    const auto level=[&](QString label){
      auto cover=visibleItem(w->contentItem(),"immersiveArtwork");
      auto line=lyricList?lyricList->property("currentItem").value<QQuickItem*>():nullptr;
      double coverMiddle=0,lineMiddle=0;
      const bool settled=waitFor([&]{
        line=lyricList?lyricList->property("currentItem").value<QQuickItem*>():nullptr;
        if(!cover||!line)return false;
        coverMiddle=cover->mapToScene({0,cover->height()/2}).y();
        lineMiddle=line->mapToScene({0,line->height()/2}).y();
        return qAbs(coverMiddle-lineMiddle)<1.5;
      });
      check(settled,qPrintable(QString("%1: the sung line's middle is level with the cover's (%2 and %3)")
                               .arg(label).arg(lineMiddle,0,'f',1).arg(coverMiddle,0,'f',1)));
    };
    const int firstLine=b->lyricIndex();
    level(QString("line %1").arg(firstLine));
    b->seek(b->lyricLines().value(firstLine+2).toMap().value("start").toLongLong());
    check(waitFor([&]{return b->lyricIndex()==firstLine+2;}),"a later line takes over");
    level(QString("line %1").arg(firstLine+2));
    shot("split-reading-level");
    resizeTo(1440,640);QTest::qWait(250);
    level("a short, wide window");
    resizeTo(1180,800);QTest::qWait(250);
  }
  b->seek(11000);
  choose("singalong");QTest::qWait(500);shot("singalong-dark");
  singAlongContrast("dark sing along");
  b->setTheme("light");QTest::qWait(300);shot("singalong-light");
  singAlongContrast("light sing along");
  b->setTheme("dark");
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
      QList<QQuickItem*> resting;collectItems(w->contentItem(),"singAlongLine",resting);
      QQuickItem *previousBody=nullptr;
      for(auto body:resting)if(body->parentItem()&&body->parentItem()->property("index").toInt()==1)previousBody=body;
      QColor mutedColor;bool mutedValid=false;
      if(previousBody){
        QQmlExpression muted(qmlContext(previousBody),previousBody,"Theme.muted");
        mutedColor=muted.evaluate().value<QColor>();mutedValid=!muted.hasError();
      }
      check(previousBody&&mutedValid&&waitFor([&]{return previousBody->property("color").value<QColor>()==mutedColor&&
            previousFill->opacity()<0.01&&!previousFill->isVisible();})&&
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
  auto singSearch=visibleItem(w->contentItem(),"immersiveLyricSearchButton");
  check(singSearch&&singSearch->isEnabled(),"Find in lyrics is available in Sing along");
  if(singSearch){
    click("immersiveLyricSearchButton");
    check(waitFor([&]{auto field=visibleItem(w->contentItem(),"lyricSearchField");
                     return player->property("preferredLayout")=="lyrics"&&
                            player->property("displayedLayout")=="lyrics"&&field&&field->hasActiveFocus();}),
          "clicking Find in Sing along opens Lyrics and focuses search");
    if(visibleItem(w->contentItem(),"lyricSearchField")){
      shot("singalong-search");
      QTest::keyClick(w,Qt::Key_Escape);
      check(waitFor([&]{return !visibleItem(w->contentItem(),"lyricSearchField");}),
            "Escape returns from the lyric search field");
    }
  }
  choose("split");
  const auto playingId=b->current().value("id");
  click("immersiveAlbumButton");
  check(waitFor([&]{return !w->property("immersive").toBool()&&b->page()=="local-album";}),"album link opens local collection");
  check(b->current().value("id")==playingId&&b->playing(),"collection navigation preserves playback");
  QMetaObject::invokeMethod(w,"navigateBack");
  check(waitFor([&]{return w->property("immersive").toBool()&&visibleItem(w->contentItem(),"immersivePlayer");}),
        "Back returns to immersive player");
  QWindowSystemInterface::handleFocusWindowChanged(w);QTest::qWait(50);
  player=visibleItem(w->contentItem(),"immersivePlayer");
  check(player&&player->property("preferredLayout")=="split","layout survives player recreation");
  if(!player){QCoreApplication::exit(1);return;}
  click("immersiveQueueButton");
  auto sheet=w->findChild<QObject*>("immersiveQueueSheet");
  check(sheet&&waitFor([&]{return sheet->property("visible").toBool();})&&w->property("immersive").toBool(),
        "queue opens without leaving immersion");
  check(waitFor([&]{auto q=visibleItem(w->contentItem(),"queueView");return q&&q->property("count").toInt()==3;}),
        "sheet uses complete live queue");
  auto queue=visibleItem(w->contentItem(),"queueView");
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
      QTest::qWait(80);QTest::mouseRelease(w,Qt::LeftButton,Qt::NoModifier,end);
    }
    check(waitFor([&]{return queue->property("count").toInt()==3&&b->queue()->get(1).value("id")==lastId;}),
          "dragging reorders the immersive queue");
    b->undo();
    QMetaObject::invokeMethod(queue,"activate",Q_ARG(int,1),Q_ARG(QVariant,b->queue()->get(1)));
    check(waitFor([&]{return b->currentIndex()==1&&b->playing();}),"sheet activation plays requested track");
    b->removeQueueRows({2});check(waitFor([&]{return queue->property("count").toInt()==2;}),"sheet follows removal");b->undo();
  }
  QTest::qWait(3700);check(player->property("controlsShown").toBool(),"open sheet prevents idle hiding");
  QTest::keyClick(w,Qt::Key_Escape);
  check(sheet&&waitFor([&]{return !sheet->property("visible").toBool();})&&w->property("immersive").toBool(),
        "Escape closes sheet only");
  b->playAt(0);check(waitFor([&]{return b->playing()&&b->lyricLines().size()==5;}),"original track and lyrics restore");
  player->forceActiveFocus();
  auto parkedPlay=visibleItem(w->contentItem(),"immersivePlayButton");
  auto parkedLayout=visibleItem(w->contentItem(),"immersiveLayoutButton");
  auto parkedSeek=visibleItem(w->contentItem(),"immersiveSeek");
  auto parkedQueue=visibleItem(w->contentItem(),"immersiveQueueButton");
  auto parkedVolume=visibleItem(w->contentItem(),"exactVolumeButton");
  auto accessibleTreeHas=[&](QObject *target){
    auto root=QAccessible::queryAccessibleInterface(w);
    std::function<bool(QAccessibleInterface*,int)> find=[&](QAccessibleInterface *node,int depth){
      if(!node||depth>32)return false;
      if(node->object()==target)return true;
      for(int i=0;i<node->childCount();++i)if(find(node->child(i),depth+1))return true;
      return false;
    };
    return find(root,0);
  };
  check(parkedPlay&&parkedLayout&&parkedSeek&&parkedQueue&&parkedVolume&&
        accessibleTreeHas(parkedPlay)&&accessibleTreeHas(parkedLayout)&&
        accessibleTreeHas(parkedSeek)&&accessibleTreeHas(parkedQueue)&&
        accessibleTreeHas(parkedVolume),
        "visible controls are in the accessibility tree");
  const auto parkedPoint=parkedPlay?parkedPlay->mapToScene(parkedPlay->boundingRect().center()).toPoint():QPoint(8,8);
  QTest::mouseMove(w,parkedPoint);QMetaObject::invokeMethod(player,"wake");
  check(waitFor([&]{return !player->property("controlsShown").toBool();}),"idle playback hides controls");
  QTest::qWait(350);
  shot("controls-hidden");
  auto hiddenTop=visibleItem(w->contentItem(),"immersiveTopControls");
  auto hiddenBar=visibleItem(w->contentItem(),"immersiveTransport");
  auto hiddenSeek=visibleItem(w->contentItem(),"immersiveSeekRow");
  check(hiddenTop&&hiddenBar&&hiddenSeek&&
        !hiddenTop->isEnabled()&&!hiddenBar->isEnabled()&&!hiddenSeek->isEnabled(),
        "faded controls leave hit testing and keyboard focus");
  check(hiddenTop&&hiddenBar&&hiddenSeek&&
        QQmlProperty::read(hiddenTop,"Accessible.ignored",qmlContext(hiddenTop)).toBool()&&
        QQmlProperty::read(hiddenBar,"Accessible.ignored",qmlContext(hiddenBar)).toBool()&&
        QQmlProperty::read(hiddenSeek,"Accessible.ignored",qmlContext(hiddenSeek)).toBool()&&
        !accessibleTreeHas(parkedPlay)&&!accessibleTreeHas(parkedLayout)&&
        !accessibleTreeHas(parkedSeek)&&!accessibleTreeHas(parkedQueue)&&
        !accessibleTreeHas(parkedVolume),
        qPrintable(QString("faded controls leave AT: play %1 layout %2 seek %3 queue %4 volume %5")
          .arg(accessibleTreeHas(parkedPlay)).arg(accessibleTreeHas(parkedLayout))
          .arg(accessibleTreeHas(parkedSeek)).arg(accessibleTreeHas(parkedQueue))
          .arg(accessibleTreeHas(parkedVolume))));
  const bool wasPlaying=b->playing();
  QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,parkedPoint);
  check(b->playing()==wasPlaying&&!player->property("controlsShown").toBool(),
        "a parked click cannot activate the hidden play button");
  QTest::keyClick(w,Qt::Key_F6);
  check(waitFor([&]{return player->property("controlsShown").toBool();}),
        "a key wakes hidden controls");
  QTest::mouseMove(w,QPoint(30,30));
  check(waitFor([&]{return player->property("controlsShown").toBool();}),"pointer movement restores controls");
  b->pause();QTest::qWait(3800);check(player->property("controlsShown").toBool(),"paused playback keeps controls visible");
  b->toggle();check(waitFor([&]{return b->playing();}),"playback resumes");
  auto play=visibleItem(w->contentItem(),"immersivePlayButton");if(play)play->forceActiveFocus(Qt::TabFocusReason);
  QTest::qWait(3800);check(player->property("controlsShown").toBool(),"keyboard-focused controls never disappear");
  player->forceActiveFocus();QTest::keyClick(w,Qt::Key_Up,Qt::ControlModifier);
  check(waitFor([&]{return visibleItem(w->contentItem(),"playbackHud")&&b->volume()>0;}),
        "volume shortcut displays feedback");
  auto hud=visibleItem(w->contentItem(),"playbackHud");
  QTest::keyClick(w,Qt::Key_Up,Qt::ControlModifier);
  check(hud&&waitFor([&]{return hud->property("label").toString()==QString::number(qRound(b->volume()*100))+"%";}),
        "repeated volume shortcuts update one HUD");
  QTest::keyClick(w,Qt::Key_M);
  check(hud&&waitFor([&]{return b->volume()==0&&hud->property("symbol")=="mute";}),"mute shortcut restores feedback");
  b->pause();b->seek(1000);QTest::qWait(100);QTest::keyClick(w,Qt::Key_Right);
  check(waitFor([&]{return b->position()>=10500&&b->position()<=11500;}),"seek shortcut applies ten seconds");
  shot("shortcut-feedback");
  check(hud&&waitFor([&]{return !hud->isVisible();}),"HUD dismisses after inactivity");
  click("immersiveLyricSearchButton");
  check(waitFor([&]{auto field=visibleItem(w->contentItem(),"lyricSearchField");return field&&field->hasActiveFocus();}),
        "lyric search field receives focus");
  const auto before=b->position();QTest::keyClick(w,Qt::Key_Left);
  check(b->position()==before,"text editing does not seek playback");
  for(auto key:{Qt::Key_L,Qt::Key_I,Qt::Key_G,Qt::Key_H,Qt::Key_T})QTest::keyClick(w,key);
  check(waitFor([&]{auto results=visibleItem(w->contentItem(),"lyricSearchResults");
                    return results&&results->property("count").toInt()==1&&
                           results->property("currentIndex").toInt()==0&&
                           !results->property("highlightFollowsCurrentItem").toBool();}),
        "search results use a custom spatial highlight for the selected match");
  QTest::keyClick(w,Qt::Key_Escape);
  check(waitFor([&]{return !visibleItem(w->contentItem(),"lyricSearchField");}),"lyric search closes");
  // --- The immersive view's own proportions ---
  // The complaint about this screen was never a missing feature: it was that
  // the pieces do not line up and the artwork does not use the room it has.
  // These are the measurements that say so.
  choose("artwork");
  {
    auto top=visibleItem(w->contentItem(),"immersiveTopControls");
    auto bar=visibleItem(w->contentItem(),"immersiveTransport");
    auto art=visibleItem(w->contentItem(),"immersiveArtwork");
    auto row=visibleItem(w->contentItem(),"immersiveSeekRow");
    check(top&&bar&&art&&row,"the immersive view has its four bands on screen");
    if(top&&bar&&art&&row){
      // Material's own music player (icon button guidelines): previous and
      // next are narrow tonal buttons, play a wide filled one, one size in one
      // standard button group. This window is below the large class, so the
      // size is medium: MediumIconButtonTokens 56dp tall, 12+24+12 narrow,
      // 24+24+24 wide.
      auto previous=visibleItem(w->contentItem(),"immersivePreviousButton");
      auto play=visibleItem(w->contentItem(),"immersivePlayButton");
      auto next=visibleItem(w->contentItem(),"immersiveNextButton");
      auto shuffle=visibleItem(w->contentItem(),"immersiveShuffleButton");
      auto repeat=visibleItem(w->contentItem(),"immersiveRepeatButton");
      auto container=[](QQuickItem *button){return button?button->property("background").value<QQuickItem*>():nullptr;};
      auto pc=container(previous),yc=container(play),nc=container(next);
      check(pc&&yc&&nc&&qAbs(yc->height()-56)<0.5&&qAbs(pc->height()-56)<0.5&&qAbs(pc->width()-48)<0.5&&
            qAbs(yc->width()-72)<0.5&&qAbs(nc->width()-48)<0.5&&previous->property("tonal").toBool()&&
            next->property("tonal").toBool()&&play->property("filled").toBool(),
            qPrintable(QString("the transport is Material's player: tonal %1x%2 skips around a filled %3x%4 play")
                       .arg(pc?pc->width():0,0,'f',0).arg(pc?pc->height():0,0,'f',0)
                       .arg(yc?yc->width():0,0,'f',0).arg(yc?yc->height():0,0,'f',0)));
      // Button groups, Specs: "Standard button group inner padding ... M 8dp".
      // A narrow button's footprint is its own width, so the space between
      // containers is the spec's and not the height-wide footprint's.
      if(pc&&yc&&nc){
        const double left=yc->mapToScene({0,0}).x()-pc->mapToScene({pc->width(),0}).x();
        const double right=nc->mapToScene({0,0}).x()-yc->mapToScene({yc->width(),0}).x();
        check(qAbs(left-8)<0.5&&qAbs(right-8)<0.5,
              qPrintable(QString("medium buttons sit 8dp apart, as the spec asks (%1 and %2)").arg(left,0,'f',1).arg(right,0,'f',1)));
      }
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
      // Shuffle and repeat stand outside the group at one distance on either
      // side and on its vertical centre, so the group is what the centre line
      // runs through.
      auto group=visibleItem(w->contentItem(),"immersiveTransportGroup");
      if(group&&shuffle&&repeat){
        const double gl=group->mapToScene({0,0}).x(),gr=group->mapToScene({group->width(),0}).x();
        const double sr=shuffle->mapToScene({shuffle->width(),0}).x(),rl=repeat->mapToScene({0,0}).x();
        const double gy=group->mapToScene({0,group->height()/2}).y();
        check(qAbs(centreOf(group)-middle)<1&&qAbs((gl-sr)-(rl-gr))<0.5&&qAbs(shuffle->width()-repeat->width())<0.5&&
              qAbs(shuffle->mapToScene({0,shuffle->height()/2}).y()-gy)<0.5&&qAbs(repeat->mapToScene({0,repeat->height()/2}).y()-gy)<0.5,
              qPrintable(QString("the group is centred (%1 of %2) with shuffle and repeat %3 and %4 away on its centre line")
                         .arg(centreOf(group),0,'f',1).arg(middle,0,'f',1).arg(gl-sr,0,'f',1).arg(rl-gr,0,'f',1)));
        // Material asks a selected toggle to change more than its colour.
        // Shuffle has no filled form, so on it is drawn at semibold.
        const bool was=b->shuffle();
        QTest::mouseClick(w,Qt::LeftButton,{},shuffle->mapToScene({shuffle->width()/2,shuffle->height()/2}).toPoint());
        auto heavy=[&]{auto fill=visibleItem(shuffle,"iconFill");return fill&&fill->property("source").toString().contains("/shuffle_semibold/")&&fill->opacity()>0.99;};
        check(waitFor([&]{return b->shuffle()!=was&&(b->shuffle()?heavy():!heavy());}),
              qPrintable(QString("clicking shuffle %1 it, and its glyph is %2").arg(was?"turns off":"turns on").arg(was?"regular again":"semibold")));
        if(!was)shot("transport-shuffle-on");
        QTest::mouseClick(w,Qt::LeftButton,{},shuffle->mapToScene({shuffle->width()/2,shuffle->height()/2}).toPoint());
        check(waitFor([&]{return b->shuffle()==was;}),"clicking again puts shuffle back");
      } else check(false,"the transport group, shuffle and repeat are on screen");
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
      auto bar=visibleItem(w->contentItem(),"immersiveTransport");
      if(toast&&bar)
        check(toast->mapToScene(QPointF(0,toast->height())).y()<=bar->mapToScene(QPointF(0,0)).y()+0.5,
              qPrintable(QString("the notification clears the transport (ends %1, transport starts %2)")
                         .arg(toast->mapToScene(QPointF(0,toast->height())).y(),0,'f',0)
                         .arg(bar->mapToScene(QPointF(0,0)).y(),0,'f',0)));
      else check(false,"the notification and the transport are both on screen");
    } else check(false,"a notification appears to place");
    w->setProperty("toastPending",false);
    check(waitFor([&]{return !visibleItem(w->contentItem(),"toastBar");}),
          "placement notice clears before size captures");
    // MenuDefaults.groupStandardContainerColor reads
    // StandardMenuTokens.ContainerColor, surfaceContainerLow.
    click("immersiveLayoutButton");
    check(waitFor(menuOpen),"layout menu opens for colour check");
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
      auto speed=w->findChild<QObject*>("immersiveSpeed");
      // The glyph must be bundled at all three optical sizes, or the Symbols
      // provider draws nothing where the icon should be.
      check(speed&&speed->property("symbol").toString()=="speed"&&
            speed->property("text").toString().startsWith("Playback speed")&&
            QFile::exists(":/assets/icons/speed.svg")&&QFile::exists(":/assets/icons/speed_20.svg")&&
            QFile::exists(":/assets/icons/speed_40.svg"),
            "playback speed leads with the bundled Material speed glyph");
      shot("menu");
      QTest::keyClick(w,Qt::Key_Escape);
      check(waitFor([&]{return !menuOpen();}),"layout menu closes");
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
    // ButtonGroup.kt:138 animates the press on the fast spatial spring.
    springCheck("buttonGroupPressMotion","fastSpatial");
  }
  // The artwork, transport, and seek bar must share a centre at every width
  // used in the immersive captures, including the compact layout.
  b->setMotion(false);
  for(int width:{480,600,840,1024,1440,2560}){
    resizeTo(width,width<=600?620:900);
    auto artwork=visibleItem(w->contentItem(),"immersiveArtwork");
    auto toolbar=visibleItem(w->contentItem(),"immersiveTransport");
    auto seek=visibleItem(w->contentItem(),"immersiveSeek");
    const auto centre=[](QQuickItem *item){return item->mapToScene({item->width()/2,0}).x();};
    check(artwork&&toolbar&&seek,qPrintable(QString("%1px artwork, transport and seek exist").arg(width)));
    if(artwork&&toolbar&&seek){
      const double art=centre(artwork),bar=centre(toolbar),wave=centre(seek);
      check(qAbs(art-bar)<=2&&qAbs(art-wave)<=2,
            qPrintable(QString("%1px centres art %2, transport %3, seek %4 differ by <=2px")
                       .arg(width).arg(art,0,'f',1).arg(bar,0,'f',1).arg(wave,0,'f',1)));
    }
    for(const auto &theme:{"dark","light"}){
      b->setTheme(theme);
      auto detailTitle=visibleItem(w->contentItem(),"immersiveTitle");
      auto detailArtist=visibleItem(w->contentItem(),"immersiveArtistButton");
      auto detailAlbum=visibleItem(w->contentItem(),"immersiveAlbumButton");
      auto artistInk=detailArtist?detailArtist->property("contentItem").value<QQuickItem*>():nullptr;
      auto albumInk=detailAlbum?detailAlbum->property("contentItem").value<QQuickItem*>():nullptr;
      auto cover=visibleItem(w->contentItem(),"immersiveArtwork");
      // The cover is centred at whatever size fits, so the details share its
      // centre line rather than an edge that moves with the window.
      check(detailTitle&&detailArtist&&detailAlbum&&artistInk&&albumInk&&cover&&
            detailTitle->property("horizontalAlignment").toInt()==Qt::AlignHCenter&&
            qAbs(centre(detailTitle)-centre(cover))<1&&qAbs(centre(artistInk)-centre(cover))<1&&
            qAbs(centre(albumInk)-centre(cover))<1&&
            detailTitle->mapToScene({detailTitle->width(),0}).x()<=width+1,
            qPrintable(QString("%1px %2 details centre under the cover inside the window").arg(width).arg(theme)));
      shot(QString("artwork-%1-%2").arg(width).arg(theme));
    }
    b->setTheme("dark");
  }
  b->setColorContrast(1);
  for(const auto &theme:{"dark","light"}){
    b->setTheme(theme);shot(QString("high-contrast-%1").arg(theme));
  }
  b->setColorContrast(0);b->setTheme("dark");
  // A large window takes the large size (LargeIconButtonTokens): 96dp tall,
  // narrow 16+32+16 and wide 48+32+48. Play is the square shape at rest,
  // CornerExtraLarge, and turns round while the song plays. A real press on
  // it grows it by 15% of its width, half taken from each neighbour
  // (ButtonGroup.kt:474-491), and the group neither widens nor moves.
  resizeTo(1600,1000);QTest::qWait(200);
  {
    auto play=visibleItem(w->contentItem(),"immersivePlayButton");
    auto previous=visibleItem(w->contentItem(),"immersivePreviousButton");
    auto next=visibleItem(w->contentItem(),"immersiveNextButton");
    auto group=visibleItem(w->contentItem(),"immersiveTransportGroup");
    auto container=[](QQuickItem *button){return button?button->property("background").value<QQuickItem*>():nullptr;};
    auto yc=container(play),pc=container(previous),nc=container(next);
    check(yc&&pc&&nc&&group&&qAbs(yc->height()-96)<0.5&&qAbs(yc->width()-128)<0.5&&qAbs(pc->width()-64)<0.5&&qAbs(nc->width()-64)<0.5,
          qPrintable(QString("a 1600dp window uses the large player: play %1x%2, skips %3 wide")
                     .arg(yc?yc->width():0,0,'f',1).arg(yc?yc->height():0,0,'f',1).arg(pc?pc->width():0,0,'f',1)));
    if(yc&&pc&&nc){
      const double left=yc->mapToScene({0,0}).x()-pc->mapToScene({pc->width(),0}).x();
      const double right=nc->mapToScene({0,0}).x()-yc->mapToScene({yc->width(),0}).x();
      check(qAbs(left-8)<0.5&&qAbs(right-8)<0.5,
            qPrintable(QString("large buttons sit 8dp apart, as the spec asks (%1 and %2)").arg(left,0,'f',1).arg(right,0,'f',1)));
    }
    if(yc&&pc&&nc&&group&&play){
      b->pause();check(waitFor([&]{return !b->playing()&&qAbs(yc->property("radius").toDouble()-28)<0.5;}),
                       qPrintable(QString("paused, play is the 28dp square (%1)").arg(yc->property("radius").toDouble(),0,'f',1)));
      b->play();check(waitFor([&]{return b->playing()&&qAbs(yc->property("radius").toDouble()-48)<0.5;}),
                      qPrintable(QString("playing, it turns round (%1)").arg(yc->property("radius").toDouble(),0,'f',1)));
      const double groupWidth=group->width(),groupLeft=group->mapToScene({0,0}).x();
      const QPoint centre=play->mapToScene({play->width()/2,play->height()/2}).toPoint();
      QTest::mousePress(w,Qt::LeftButton,{},centre);QTest::qWait(60);
      check(qAbs(yc->width()-(128+19.2))<0.6&&qAbs(pc->width()-(64-9.6))<0.6&&qAbs(nc->width()-(64-9.6))<0.6&&
            qAbs(yc->property("radius").toDouble()-16)<0.5,
            qPrintable(QString("pressed, play grows to %1 on the 16dp pressed corner while the skips give up %2 and %3")
                       .arg(yc->width(),0,'f',1).arg(64-pc->width(),0,'f',1).arg(64-nc->width(),0,'f',1)));
      check(qAbs(group->width()-groupWidth)<0.6&&qAbs(group->mapToScene({0,0}).x()-groupLeft)<0.6,
            qPrintable(QString("and the group keeps its width and place (%1 to %2)").arg(groupWidth,0,'f',1).arg(group->width(),0,'f',1)));
      shot("transport-pressed");
      QTest::mouseRelease(w,Qt::LeftButton,{},centre);
      check(waitFor([&]{return qAbs(yc->width()-128)<0.5&&qAbs(pc->width()-64)<0.5&&qAbs(nc->width()-64)<0.5;}),
            "released, every button returns to its own width");
      check(waitFor([&]{return !b->playing();}),"and the press was a click that paused the song");
      // The skips are pressed and then dragged off before release, which
      // Qt Quick Controls does not count as a click, so the queue stays put
      // for the checks after this one.
      const int index=b->currentIndex();
      auto skip=[&](QQuickItem *button,const char *name){
        const QPoint at=button->mapToScene({button->width()/2,button->height()/2}).toPoint();
        QTest::mousePress(w,Qt::LeftButton,{},at);QTest::qWait(60);
        check(qAbs(container(button)->width()-(64+9.6))<0.6&&qAbs(yc->width()-(128-9.6))<0.6,
              qPrintable(QString("pressing %1 takes all 9.6dp from play (%2, play %3)").arg(name)
                         .arg(container(button)->width(),0,'f',1).arg(yc->width(),0,'f',1)));
        const QPoint away(at.x(),at.y()-240);
        QTest::mouseMove(w,away);QTest::mouseRelease(w,Qt::LeftButton,{},away);
        check(waitFor([&]{return qAbs(yc->width()-128)<0.5;}),"and gives it back");
      };
      skip(previous,"previous");skip(next,"next");
      check(b->currentIndex()==index,"dragging off a skip before release changes nothing");
      b->play();
    }
  }
  resizeTo(480,780);
  click("immersiveLayoutButton");
  check(waitFor(menuOpen),"layout menu opens for coverflow");
  click("immersiveCoverflowToggle");
  check(waitFor([&]{return visibleItem(w->contentItem(),"coverflowView");}),"coverflow appears");
  auto covers=visibleItem(w->contentItem(),"coverflowView");
  auto flowMeasure=visibleItem(w->contentItem(),"immersiveCoverflow");
  check(flowMeasure&&qAbs(player->property("coverflowReserve").toDouble()-
                          flowMeasure->property("reserved").toDouble())<1,
        "preloaded coverflow reserve matches the component's measured row");
  check(covers&&waitFor([&]{
          auto first=visibleItem(w->contentItem(),"coverflowItem_0");
          if(!first)return false;
          const double before=covers->property("contentX").toDouble();
          QTest::qWait(30);
          return qAbs(covers->property("contentX").toDouble()-before)<0.5&&
                 qAbs(first->mapToScene({first->width()/2,0}).x()-
                      covers->mapToScene({covers->width()/2,0}).x())<2;
        }),"coverflow starts centred before the slide probe");
  auto highlight=covers?covers->property("highlightItem").value<QQuickItem*>():nullptr;
  auto currentCover=covers?covers->property("currentItem").value<QQuickItem*>():nullptr;
  check(covers&&highlight&&currentCover&&!covers->property("highlightFollowsCurrentItem").toBool()&&
        qAbs(highlight->width()-currentCover->width())<1,
        "coverflow follows a cell-sized custom highlight");
  if(covers){
    b->setMotion(true);
    const auto spatial=b->motionSprings(b->motionScheme()!="standard").value("defaultSpatial").toMap();
    const int spatialMs=spatial.value("ms").toInt();
    const double startX=covers->property("contentX").toDouble();
    auto next=visibleItem(w->contentItem(),"immersiveNextButton");
    check(next,"immersive next button is reachable");
    if(next){
      const auto point=next->mapToScene(next->boundingRect().center()).toPoint();
      QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,point);
      check(waitFor([&]{return b->currentIndex()==1;}),"clicking Next changes the playing cover");
      QTest::qWait(spatialMs/3);
      // Behavior.animation is Qt's documented route to the live animation.
      // Read it after a user click has started the lazy highlight motion.
      QObject *slide=nullptr;
      if(highlight)for(auto child:highlight->children())
        if(QByteArray(child->metaObject()->className()).contains("Behavior")){
          auto animation=child->property("animation");
          if(animation.convert(QMetaType::fromType<QObject*>()))slide=animation.value<QObject*>();
        }
      check(slide&&slide->objectName()=="coverflowHighlightMotion"&&
            slide->property("duration").toInt()==spatialMs&&
            QQmlProperty::read(slide,"easing.bezierCurve",qmlContext(slide)).toList()==spatial.value("curve").toList(),
            qPrintable(QString("coverflow highlight uses DefaultSpatial duration and curve (%1/%2)")
              .arg(slide?slide->property("duration").toInt():-1).arg(spatialMs)));
      const double middleX=covers->property("contentX").toDouble();
      check(waitFor([&]{
              auto target=visibleItem(w->contentItem(),"coverflowItem_1");
              if(!target)return false;
              const double before=covers->property("contentX").toDouble();
              QTest::qWait(30);
              return qAbs(covers->property("contentX").toDouble()-before)<0.5&&
                     qAbs(target->mapToScene({target->width()/2,0}).x()-
                          covers->mapToScene({covers->width()/2,0}).x())<2;
            }),"the next cover finishes its spring slide");
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
  if(covers){
    auto flowSlot=covers->parentItem()&&covers->parentItem()->parentItem()
                    ?covers->parentItem()->parentItem()->parentItem():nullptr;
    const auto slotRect=flowSlot?flowSlot->mapRectToScene(flowSlot->boundingRect()):QRectF();
    QPointer<QQuickItem> heldCover=covers;
    check(accessibleTreeHas(covers),"visible coverflow list is in the accessibility tree");
    player->forceActiveFocus();QTest::mouseMove(w,QPoint(8,8));QMetaObject::invokeMethod(player,"wake");
    check(waitFor([&]{return !player->property("controlsShown").toBool();}),
          "coverflow controls auto-hide after inactivity");
    QTest::qWait(350);
    check(flowSlot&&flowSlot->isVisible()&&!visibleItem(w->contentItem(),"coverflowView")&&
          qAbs(flowSlot->mapRectToScene(flowSlot->boundingRect()).top()-slotRect.top())<1&&
          qAbs(flowSlot->height()-slotRect.height())<1&&heldCover.isNull(),
          "hidden coverflow leaves input and accessibility without moving its layout slot");
    shot("coverflow-controls-hidden");
    QTest::keyClick(w,Qt::Key_F6);
    check(waitFor([&]{return visibleItem(w->contentItem(),"coverflowView");}),
          "keyboard input restores coverflow controls");
  }
  auto title=visibleItem(w->contentItem(),"immersiveTitle");
  auto artistLink=visibleItem(w->contentItem(),"immersiveArtistButton");
  auto albumLink=visibleItem(w->contentItem(),"immersiveAlbumButton");
  auto artistText=artistLink?artistLink->property("contentItem").value<QQuickItem*>():nullptr;
  auto albumText=albumLink?albumLink->property("contentItem").value<QQuickItem*>():nullptr;
  // Button.kt:1015,1025 puts 12dp inside a text button on each side. The
  // links hug their labels on the title's centre line, never wider than the
  // title measure plus that padding.
  const auto midline=[](QQuickItem *item){return item->mapToScene({item->width()/2,0}).x();};
  check(title&&artistLink&&albumLink&&artistText&&albumText&&
        qAbs(midline(title)-midline(artistText))<1&&qAbs(midline(title)-midline(albumText))<1&&
        qAbs(artistLink->width()-qMin(title->width()+24,artistText->implicitWidth()+24))<1&&
        qAbs(albumLink->width()-qMin(title->width()+24,albumText->implicitWidth()+24))<1,
        "metadata links hug their labels on the title's centre line");
  check(title&&title->property("wrapMode").toInt()==QQmlExpression(qmlContext(title),title,"Text.WordWrap").evaluate().toInt()&&
        title->property("elide").toInt()==QQmlExpression(qmlContext(title),title,"Text.ElideRight").evaluate().toInt()&&
        title->property("maximumLineCount").toInt()==2,
        "title wraps at words and elides after two lines");
  shot("coverflow-480");
  const int beforeCoverflowMessages=messages.total();
  for(const auto size:{QSize(480,620),QSize(600,800),QSize(840,800),
                       QSize(1024,900),QSize(1440,900),QSize(2560,900)}){
    resizeTo(size.width(),size.height());
    for(const auto &theme:{"dark","light"}){
      b->setTheme(theme);
      QMetaObject::invokeMethod(player,"wake");
      if(player->property("coverflowVisible").toBool()){
        check(waitFor([&]{return visibleItem(w->contentItem(),"immersiveCoverflow");}),
              "visible coverflow loads before its reserve is compared");
        auto row=visibleItem(w->contentItem(),"immersiveCoverflow");
        check(row&&qAbs(player->property("coverflowReserve").toDouble()-row->property("reserved").toDouble())<1,
              qPrintable(QString("%1px %2 coverflow reserve agrees with the component").arg(size.width()).arg(theme)));
      }
      shot(QString("coverflow-%1-%2").arg(size.width()).arg(theme));
    }
  }
  check(messages.total()==beforeCoverflowMessages,
        "coverflow width and theme changes emit no binding loops or QML errors");
  b->setTheme("dark");
  resizeTo(480,620);
  auto shortArt=visibleItem(w->contentItem(),"immersiveArtwork");
  auto shortFlow=visibleItem(w->contentItem(),"coverflowView");
  auto shortQueue=visibleItem(w->contentItem(),"immersiveQueueButton");
  check(shortArt&&shortArt->width()>=160&&!shortFlow&&shortQueue&&shortQueue->isEnabled(),
        qPrintable(QString("short window yields coverflow: window %1, player %2, art %3, flow %4, budget %5, reserve %6, title %7x%8, row %9/%10, queue %11")
          .arg(w->height()).arg(player->height(),0,'f',1)
          .arg(shortArt?shortArt->width():-1,0,'f',1)
          .arg(shortFlow?"visible":"hidden")
          .arg(player->property("coverflowCoverBudget").toDouble(),0,'f',1)
          .arg(shortFlow?shortFlow->parentItem()->property("reserved").toDouble():-1,0,'f',1)
          .arg(title?title->property("lineCount").toInt():-1)
          .arg(title?title->property("lineHeight").toDouble():-1,0,'f',1)
          .arg(visibleItem(w->contentItem(),"immersiveSeekRow")?visibleItem(w->contentItem(),"immersiveSeekRow")->height():-1,0,'f',1)
          .arg(visibleItem(w->contentItem(),"immersiveSeekRow")?visibleItem(w->contentItem(),"immersiveSeekRow")->implicitHeight():-1,0,'f',1)
          .arg(shortQueue?QString("at %1,%2 enabled %3")
                .arg(shortQueue->mapToScene({0,0}).x(),0,'f',1)
                .arg(shortQueue->mapToScene({0,0}).y(),0,'f',1)
                .arg(shortQueue->isEnabled()):"missing")));
  shot("coverflow-yield-480");
  resizeTo(480,780);
  for(const auto &pair:{qMakePair("immersiveArtistButton","immersiveArtistFocusRing"),
                        qMakePair("immersiveAlbumButton","immersiveAlbumFocusRing")}){
    auto button=visibleItem(w->contentItem(),pair.first);
    auto ring=w->findChild<QQuickItem*>(pair.second);
    if(button)button->forceActiveFocus(Qt::TabFocusReason);
    check(button&&ring&&waitFor([&]{return ring->isVisible();})&&qAbs(ring->x()+3)<0.5&&
          qAbs(ring->width()-button->width()-6)<0.5&&ring->property("radius").toDouble()>12&&
          QQmlProperty::read(ring,"border.width",qmlContext(ring)).toInt()==2,
          qPrintable(QString("%1 uses an external 2px shaped focus ring").arg(pair.first)));
    check(button&&button->property("leftPadding").toDouble()==12&&
          button->property("rightPadding").toDouble()==12&&button->height()>=48,
          qPrintable(QString("%1 gives its label Material text-button padding in a 48dp target").arg(pair.first)));
  }
  if(artistLink)artistLink->forceActiveFocus(Qt::TabFocusReason);
  QTest::keyClick(w,Qt::Key_Tab);
  check(artistLink&&albumLink&&artistLink->height()>=48&&albumLink->height()>=48&&
        albumLink->mapToScene({0,0}).y()-artistLink->mapToScene({0,0}).y()<=48.5,
        "collection links keep 48dp targets in a tight stack");
  check(albumLink&&w->activeFocusItem()==albumLink,
        "Tab reaches the album after the artist in artwork details");
  shot("focus-ring-480");
  check(title&&title->property("typeRole")=="titleLarge"&&title->property("emphasized").toBool()&&
        title->property("font").value<QFont>().pixelSize()==22,
        "narrow title uses emphasized 22sp title-large");
  auto waveMotion=w->findChild<QObject*>("seekWaveMotion");
  check(waveMotion&&waveMotion->property("duration").toInt()==1000,
        "the 28px seek wave advances one wavelength per second");
  auto seek480=visibleItem(w->contentItem(),"immersiveSeek");
  check(seek480&&seek480->width()>=300,"480px seek has at least 300px of travel");
  resizeTo(1180,800);
  title=visibleItem(w->contentItem(),"immersiveTitle");
  check(title&&title->property("typeRole")=="headlineLarge"&&title->property("emphasized").toBool()&&
        title->property("font").value<QFont>().pixelSize()==32,
        "wide title uses emphasized 32sp headline-large");
  for(const QSize size:{QSize(480,780),QSize(1440,900)}){
    resizeTo(size.width(),size.height());
    for(const auto &theme:{"dark","light"}){
      b->setTheme(theme);
      auto heading=visibleItem(w->contentItem(),"immersiveTitle");
      for(const auto &kind:{"Artist","Album"}){
        const QString kindName=QString::fromLatin1(kind);
        auto link=visibleItem(w->contentItem(),QString("immersive%1Button").arg(kindName));
        auto label=link?link->property("contentItem").value<QQuickItem*>():nullptr;
        if(link)link->forceActiveFocus(Qt::TabFocusReason);
        check(heading&&link&&label&&link->height()>=48&&
              link->property("leftPadding").toDouble()==12&&
              link->property("rightPadding").toDouble()==12&&
              qAbs(label->mapToScene({label->width()/2,0}).x()-heading->mapToScene({heading->width()/2,0}).x())<1,
              qPrintable(QString("%1px %2 %3 link pads its focus container on the title's centre line")
                .arg(size.width()).arg(theme).arg(kindName)));
        shot(QString("focus-%1-%2-%3").arg(kindName.toLower()).arg(size.width()).arg(theme));
      }
    }
  }
  resizeTo(1180,800);b->setTheme("dark");
  b->setMotion(true);
  b->setTheme("light");shot("light");b->setMotion(false);
  choose("artwork");check(player->property("displayedLayout")=="artwork","reduced motion applies layout immediately");
  choose("split");b->setTheme("dark");
  w->setProperty("immersive",false);resizeTo(1440,900);
  if(w->property("side")!="lyrics")QTest::keyClick(w,Qt::Key_Y,Qt::ControlModifier);
  check(waitFor([&]{return w->property("side")=="lyrics"&&visibleItem(w->contentItem(),"lyricsView");}),
        "side lyric panel opens at wide width");
  b->setTheme("light");QTest::qWait(180);shot("side-lyrics-light");
  lyricContrast(4.5,"light side pane");
  auto sideLyrics=visibleItem(w->contentItem(),"liveLyrics");
  if(sideLyrics){
    sideLyrics->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(w,Qt::Key_End);
    check(waitFor([&]{return sideLyrics->property("keyboardIndex").toInt()==b->lyricLines().size()-1;}),
          "End navigates the side-panel lyric list");
    auto edgeCrosses=[&]{
      QList<QQuickItem*> labels;collectItems(sideLyrics,"lyricLabel",labels);
      const auto viewport=sideLyrics->mapRectToScene(sideLyrics->boundingRect());
      for(auto label:labels){
        const auto ink=label->mapRectToScene(label->boundingRect());
        if((ink.top()<viewport.top()&&ink.bottom()>viewport.top()+2)||
           (ink.top()<viewport.bottom()-2&&ink.bottom()>viewport.bottom()))return true;
      }
      return false;
    };
    const auto wheelPoint=sideLyrics->mapToScene({8,sideLyrics->height()/2});
    QTest::mouseMove(w,wheelPoint.toPoint());
    for(int step=0;step<5&&!edgeCrosses();++step){
      QWheelEvent wheel(wheelPoint,w->mapToGlobal(wheelPoint),QPoint(),QPoint(0,120),
                        Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
      QCoreApplication::sendEvent(w,&wheel);
      QTest::qWait(100);
    }
    check(edgeCrosses(),"wheel scrolling brings a side lyric to the viewport edge");
    lyricEdgeFade("light side pane");
    shot("side-lyrics-edge-light");
  }
  b->setTheme("dark");QTest::qWait(180);shot("side-lyrics-dark");
  lyricContrast(4.5,"dark side pane");
  // --- Poster-style lyrics ---
  // Settings, Appearance: the line being sung set in Google Sans Flex's axes,
  // every word its own weight, width, roundness and slant, each row stretched
  // along the width axis until it meets the measure. It is off unless chosen,
  // so it is turned on the way a person turns it on: Settings, a search, a
  // click. It changes nothing about where anything sits.
  {
    b->setMotion(true);b->seek(11000);
    auto currentLine=[&]()->QQuickItem*{
      QList<QQuickItem*> lines;collectItems(w->contentItem(),"lyricLine",lines);
      for(auto line:lines)if(line->isVisible()&&line->property("current").toBool())return line;
      return nullptr;
    };
    check(waitFor([&]{return currentLine()&&currentLine()->property("modelData").toMap().value("text")=="Across the still water";}),
          "the second line is current before the poster is turned on");
    const double plainHeight=currentLine()?currentLine()->height():-1;
    check(!b->posterLyrics()&&!visibleItem(w->contentItem(),"posterLine"),"poster lyrics are off by default");
    click("settingsButton");
    auto settings=w->findChild<QObject*>("settingsDialog");
    check(waitFor([&]{return settings&&settings->property("opened").toBool();}),"Settings opens");
    click("settingsSearch");for(const char key:QByteArray("poster"))QTest::keyClick(w,key);
    check(waitFor([&]{return visibleItem(w->contentItem(),"posterLyricsSwitch");}),"searching finds Poster-style lyrics");
    QTest::qWait(300);
    click("posterLyricsSwitch");
    check(waitFor([&]{return b->posterLyrics();}),"its switch turns them on");
    QTest::keyClick(w,Qt::Key_Escape);
    check(waitFor([&]{return settings&&!settings->property("visible").toBool();}),"Settings closes");
    // Every row of a poster fills the measure unless its words have reached
    // the widest the width axis goes, and there are as many rows as the plain
    // line wraps to.
    auto posterFills=[&](QQuickItem *poster,const QString &context){
      auto line=poster?poster->parentItem():nullptr;
      while(line&&line->objectName()!="lyricLine")line=line->parentItem();
      auto label=line?visibleItem(line,"lyricLabel"):nullptr;
      if(!label)for(auto child:line?line->childItems():QList<QQuickItem*>{})if(child->objectName()=="lyricLabel")label=child;
      QList<QQuickItem*> words;collectItems(poster,"posterWord",words);
      // A row that fills ends at the margin to the pixel, its gaps taking up
      // what the width grid rounded; one that cannot fill (every word at the
      // widest the axis goes, as large as a poster row may be) ends short of
      // it. None runs past it.
      QMap<QQuickItem*,double> rightEdge;QMap<QQuickItem*,bool> widest;
      // Each word sits in a slot the row lays out, so its row is two up.
      const auto rowOf=[](QQuickItem *word){return word->parentItem()->parentItem();};
      for(auto word:words){
        const double right=word->mapToItem(poster,QPointF(word->width(),0)).x();
        rightEdge[rowOf(word)]=std::max(rightEdge.value(rowOf(word)),right);
        widest[rowOf(word)]=!rowOf(word)->property("fills").toBool();
      }
      const int rows=poster->property("rows").toList().size();
      check(label&&rows==label->property("lineCount").toInt(),
            qPrintable(QString("%1: the poster breaks where the plain line does (%2 rows, %3 lines)")
                       .arg(context).arg(rows).arg(label?label->property("lineCount").toInt():-1)));
      bool flush=!rightEdge.isEmpty();QString edges;
      for(auto row:rightEdge.keys()){
        edges+=QString(" %1").arg(rightEdge.value(row),0,'f',1);
        const int count=std::count_if(words.cbegin(),words.cend(),[&](QQuickItem *w){return rowOf(w)==row;});
        const double slack=count>1?1:poster->width()*0.015;
        if(rightEdge.value(row)>poster->width()+slack||(!widest.value(row)&&qAbs(rightEdge.value(row)-poster->width())>slack))flush=false;
      }
      check(flush,qPrintable(QString("%1: every row meets the %2 measure (%3)").arg(context).arg(poster->width(),0,'f',1).arg(edges)));
      QSet<QString> styles;
      for(auto word:words){
        const auto font=word->property("font").value<QFont>();
        styles.insert(QString("%1/%2").arg(font.weight()).arg(font.variableAxisValue(QFont::Tag("wdth")),0,'f',0));
      }
      check(styles.size()>=std::min<qsizetype>(3,words.size()),
            qPrintable(QString("%1: the words take different weights and widths (%2 styles)").arg(context).arg(styles.size())));
      // It starts where the plain text starts and shares its centre line, and
      // the line is exactly as tall as the poster in it.
      if(label&&line){
        const double posterLeft=poster->mapToItem(label,QPointF(0,0)).x();
        const double posterMiddle=poster->mapToItem(label,QPointF(0,poster->height()/2)).y();
        check(qAbs(posterLeft-label->property("leftPadding").toDouble())<0.5&&qAbs(posterMiddle-label->height()/2)<0.5&&
              qAbs(line->height()-(poster->height()+label->property("topPadding").toDouble()+label->property("bottomPadding").toDouble()+20))<0.5,
              qPrintable(QString("%1: the poster sits where the plain text does (left %2, middle %3 of %4) in a line its own height")
                         .arg(context).arg(posterLeft,0,'f',1).arg(posterMiddle,0,'f',1).arg(label->height()/2,0,'f',1)));
      }
    };
    auto poster=[&]{return visibleItem(w->contentItem(),"posterLine");};
    // Measured once the spring has come to rest: DefaultSpatial passes its
    // target on the way, and the row is only meant to fit at the end.
    auto settled=[&]{auto p=poster();return p&&p->property("progress").toDouble()==1.0;};
    check(waitFor(settled),"the current line in the side panel becomes a poster");
    if(poster())posterFills(poster(),"side panel");
    check(currentLine()&&currentLine()->height()>=plainHeight-0.5,
          qPrintable(QString("the line grows only as much as its poster does (%1 from %2)")
                     .arg(currentLine()?currentLine()->height():-1,0,'f',1).arg(plainHeight,0,'f',1)));
    {
      const auto springs=b->motionSprings(b->motionScheme()!="standard");
      // A list delegate is not a QObject child of the window, so the search
      // starts at the poster.
      auto animation=poster()?poster()->findChild<QObject*>("posterLineMotion"):nullptr;
      const auto pair=springs.value("defaultSpatial").toMap();
      check(animation&&animation->property("duration").toInt()==pair.value("ms").toInt()&&
            QQmlProperty::read(animation,"easing.bezierCurve",qmlContext(animation)).toList()==pair.value("curve").toList(),
            "the words spring into place on DefaultSpatial, the lyric line's own spring");
    }
    shot("poster-side-lyrics");
    // The next line takes the poster over; the last one springs back to the
    // plain words and then lets its poster go.
    b->seek(21000);
    check(waitFor([&]{
            QList<QQuickItem*> all;collectItems(w->contentItem(),"posterLine",all);
            int shown=0;for(auto p:all)if(p->isVisible())++shown;
            auto p=poster();auto line=currentLine();
            return shown==1&&p&&line&&line->isAncestorOf(p)&&p->property("progress").toDouble()>0.999;
          }),"the poster moves to the new line and leaves none behind");
    // Sing along stays as it is.
    w->setProperty("immersive",true);resizeTo(1440,900);
    player=visibleItem(w->contentItem(),"immersivePlayer");
    choose("lyrics");
    check(waitFor(settled),"immersive lyrics show the poster");
    if(poster())posterFills(poster(),"immersive lyrics");
    shot("poster-immersive-lyrics");
    choose("split");
    check(waitFor(settled),"so does the split view");
    if(poster())posterFills(poster(),"immersive split");
    shot("poster-immersive-split");
    choose("singalong");QTest::qWait(300);
    check(!visibleItem(w->contentItem(),"posterLine"),"sing along keeps its own line");
    choose("split");
    b->setMotion(false);b->seek(31000);
    check(waitFor([&]{auto p=poster();return p&&p->property("progress").toDouble()==1;}),
          "with reduced motion the poster arrives set, without springing");
    b->setMotion(true);
    b->setPosterLyrics(false);
    check(waitFor([&]{return !visibleItem(w->contentItem(),"posterLine");}),"turning it off returns the plain line");
    w->setProperty("immersive",false);resizeTo(1440,900);
  }
  QMetaObject::invokeMethod(w,"activateSide",Q_ARG(QString,QString("lyrics")));
  w->setProperty("immersive",false);w->showNormal();w->setMinimumSize({780,580});w->setMaximumSize({780,580});w->resize(780,580);w->setProperty("immersive",true);
  check(waitFor([&]{return visibleItem(w->contentItem(),"immersivePlayer")&&w->width()==780&&w->height()==580;}),
        "compact immersive player opens at the requested size");
  player=visibleItem(w->contentItem(),"immersivePlayer");shot("compact");
  const auto art=visibleItem(w->contentItem(),"immersiveArtwork");const auto seek=visibleItem(w->contentItem(),"immersiveSeek");
  check(art&&art->width()>=160&&!visibleItem(w->contentItem(),"coverflowView"),
        "short split layout keeps a useful cover by yielding coverflow");
  check(art&&seek&&art->mapToScene({0,art->height()}).y()<seek->mapToScene({0,0}).y(),
        qPrintable(QString("compact artwork stays above transport (%1 against %2)")
                   .arg(art?art->mapToScene(QPointF(0,art->height())).y():-1,0,'f',0)
                   .arg(seek?seek->mapToScene(QPointF(0,0)).y():-1,0,'f',0)));
  QMetaObject::invokeMethod(w,"openMiniPlayer");
  check(waitFor([&]{auto window=qvariant_cast<QQuickWindow*>(w->property("miniPlayer"));return window&&window->isVisible();}),
        "mini player opens");
  auto mini=qvariant_cast<QQuickWindow*>(w->property("miniPlayer"));
  if(mini){
    b->setMotion(true);b->seek(1000);
    check(waitFor([&]{return visibleItem(mini->contentItem(),"miniLyricContainer");}),"mini lyric line is visible");
    auto line=visibleItem(mini->contentItem(),"miniLyricContainer");
    const int height=mini->height();b->seek(11000);QTest::qWait(80);
    check(line&&line->property("progress").toDouble()>0&&line->property("progress").toDouble()<1,"mini lyric change crossfades");
    check(line&&waitFor([&]{return line->property("shown")=="Across the still water"&&mini->height()==height;}),
          "line changes keep transport geometry stable");
    b->seek(21000);QTest::qWait(20);b->seek(31000);
    check(line&&waitFor([&]{return line->property("shown")=="We move with the tide";}),"rapid seeking shows latest lyric");
    b->setMotion(false);b->seek(1000);QTest::qWait(30);check(line&&line->property("progress").toDouble()==1&&line->property("shown")=="The light arrives","reduced motion settles mini lyrics immediately");
    check(mini->grabWindow().save(dir+"/mini-lyrics.png"),"mini lyric capture");
    b->playAt(1);check(waitFor([&]{return b->currentIndex()==1&&b->lyricLines().isEmpty();}),"track without lyrics clears line");
    check(waitFor([&]{return mini->height()<height&&!visibleItem(mini->contentItem(),"miniLyricContainer");}),
          "missing lyrics collapses unused mini-player space");
    check(mini->grabWindow().save(dir+"/mini-no-lyrics.png"),"compact mini capture");
    mini->hide();
  }
  QDir().mkpath(dir+"/bare");
  const QString longName="Coming ashore after a long and winding trip across the still water into the early morning light";
  QProcess bareEncode;
  bareEncode.start("ffmpeg",{"-nostdin","-v","error","-f","lavfi","-i","anullsrc=r=8000:cl=mono",
                     "-t","120","-metadata","title="+longName,"-metadata","album=Open Sky",
                     "-metadata","artist=Example Artist",dir+"/bare/long.flac"});
  check(bareEncode.waitForFinished(30000)&&bareEncode.exitCode()==0,"generate long-title track without artwork");
  b->importMusicFolder(QUrl::fromLocalFile(dir+"/bare"));
  check(waitFor([&]{return !b->importingLocal();}),"import artwork-free long-title fixture");
  b->library("files");
  QVariantMap bareSong;
  for(const auto &row:b->results()->rows)if(row.toMap().value("title")==longName)bareSong=row.toMap();
  check(!bareSong.isEmpty()&&bareSong.value("art").toString().isEmpty(),
        "long-title fixture has no artwork source");
  if(!bareSong.isEmpty()){
    b->clearQueue();b->enqueueItems({bareSong});b->playAt(0);
    check(waitFor([&]{return b->playing()&&b->queue()->count()==1;}),"one-song queue plays long title");
    w->setProperty("immersive",true);resizeTo(480,780);
    player=visibleItem(w->contentItem(),"immersivePlayer");
    title=visibleItem(w->contentItem(),"immersiveTitle");
    check(title&&title->property("lineCount").toInt()==2&&title->property("truncated").toBool(),
          "long title uses two lines with an ellipsis");
    check(waitFor([&]{return !visibleItem(w->contentItem(),"toastBar");}),
          "queue notification clears before edge captures");
    shot("long-title-no-artwork-dark");
    b->setTheme("light");shot("long-title-no-artwork-light");b->setTheme("dark");
    resizeTo(480,900);
    QMetaObject::invokeMethod(player,"wake");
    check(waitFor([&]{return visibleItem(w->contentItem(),"coverflowView");}),
          "one-song coverflow wakes for the edge capture");
    auto oneFlow=visibleItem(w->contentItem(),"coverflowView");
    check(player&&player->property("coverflow").toBool()&&oneFlow&&
          oneFlow->property("count").toInt()==1,
          qPrintable(QString("one-song coverflow: selected %1, visible %2, count %3")
            .arg(player?player->property("coverflow").toBool():false)
            .arg(bool(oneFlow)).arg(oneFlow?oneFlow->property("count").toInt():-1)));
    shot("one-song-coverflow");
  }
  QVariantMap plainSong{{"id","12345678901"},{"videoId","12345678901"},
                        {"kind","song"},{"title","Untimed song"},
                        {"artist","Test artist"},{"seconds",120},{"available",true}};
  qputenv("SUNG_BUFFER_FIXTURE","1");
  b->clearQueue();b->enqueueItems({plainSong});b->playAt(0);
  check(waitFor([&]{return b->lyrics()=="Test lyrics"&&b->lyricLines().isEmpty();}),
        "fixture supplies unsynced lyrics without timed lines");
  player=visibleItem(w->contentItem(),"immersivePlayer");
  if(player){
    if(player->property("coverflow").toBool()){
      // The menu toggle was exercised above. Restore the reading-only edge
      // setup without competing with a queue notification over the menu.
      QMetaObject::invokeMethod(player,"coverflowRequested",Q_ARG(bool,false));
      check(waitFor([&]{player=visibleItem(w->contentItem(),"immersivePlayer");
                         return player&&!player->property("coverflow").toBool();}),
            "coverflow is off for the unsynced reading capture");
    }
    choose("lyrics");
    check(player->property("displayedLayout")=="lyrics"&&
          visibleItem(w->contentItem(),"lyricsView"),
          "unsynced lyrics retain a reading layout");
    check(waitFor([&]{return !visibleItem(w->contentItem(),"toastBar");}),
          "fixture queue notification clears before unsynced capture");
    shot("unsynced-lyrics-dark");
    b->setTheme("light");shot("unsynced-lyrics-light");b->setTheme("dark");
  }
  qunsetenv("SUNG_BUFFER_FIXTURE");
  check(messages.total()==0,qPrintable(QString("zero QML diagnostics (loops %1, type %2, reference %3, assignment %4)")
        .arg(messages.loops.load()).arg(messages.types.load()).arg(messages.references.load()).arg(messages.assignments.load())));
  b->stop();b->clearQueue();fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);QCoreApplication::exit(failures?1:0);
}
