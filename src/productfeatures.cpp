#include "backend.h"
#include "artworkurl.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QDateTime>
#include <QHash>
#include <QLocale>
#include <algorithm>
#include <functional>
#include <limits>

QVariantMap Backend::albumInfo() const {
  if(m_page!="album" && m_page!="local-album" && !(m_page=="server" && m_request.value("mode")=="album"))return {};
  QString artist=m_request.value("artist").toString();
  if(artist.isEmpty() && !m_results.rows.isEmpty())artist=m_results.get(0).value("artist").toString();
  qint64 seconds=0;bool complete=true;QSet<int> discs;
  for(const auto &v:m_results.rows){const auto t=v.toMap();auto duration=t.value("seconds").toLongLong();
    if(duration<=0){const auto parts=t.value("duration").toString().split(':');for(const auto &part:parts)duration=duration*60+part.toInt();}
    if(duration<=0)complete=false;else seconds+=duration;
    discs.insert(qMax(1,t.value("discNumber",1).toInt()));
  }
  QStringList details;const auto year=m_request.value("year").toString();if(!year.isEmpty())details<<year;
  details<<QString("%1 %2").arg(m_results.count()).arg(m_results.count()==1?"track":"tracks");
  if(complete && seconds>0)details<<(seconds>=3600?QString("%1 hr %2 min").arg(seconds/3600).arg(seconds%3600/60):QString("%1 min").arg(qMax(qint64(1),seconds/60)));
  return {{"artist",artist},{"summary",details.join(" · ")},{"multipleDiscs",discs.size()>1}};
}

QVariantMap Backend::artistInfo() const {
  const bool serverArtist=m_page=="server" && m_request.value("mode")=="artist";
  if(m_page!="artist" && m_page!="local-artist" && !serverArtist)return {};
  QSet<QString> albums;qint64 seconds=0;bool complete=!m_results.rows.isEmpty();
  for(const auto &v:m_results.rows){const auto t=v.toMap();
    const auto album=t.value("album").toString();
    if(!album.isEmpty())albums.insert(album.toCaseFolded());
    auto duration=t.value("seconds").toLongLong();
    if(duration<=0){const auto parts=t.value("duration").toString().split(':');for(const auto &part:parts)duration=duration*60+part.toInt();}
    if(duration<=0)complete=false;else seconds+=duration;
  }
  // An online artist page is a set of shelves rather than a track list, so the
  // counts come out of the shelves it did return.
  int tracks=m_results.count();
  if(tracks==0)for(const auto &v:m_sections){const auto section=v.toMap();tracks+=section.value("items").toList().size();}
  QStringList details;
  if(!albums.isEmpty())details<<QString("%1 %2").arg(albums.size()).arg(albums.size()==1?"album":"albums");
  if(tracks>0)details<<QString("%1 %2").arg(tracks).arg(tracks==1?"song":"songs");
  if(complete && seconds>0)details<<(seconds>=3600?QString("%1 hr %2 min").arg(seconds/3600).arg(seconds%3600/60):QString("%1 min").arg(qMax(qint64(1),seconds/60)));
  return {{"name",m_title},{"summary",details.join(" · ")},{"albums",albums.size()},{"tracks",tracks},{"seconds",seconds}};
}

QString Backend::artworkChoice() const {return m_settings.value("artworkChoices").toMap().value(current().value("id").toString()).toString();}
QString Backend::currentMotionArt() const {
  const auto choice=artworkChoice();if(choice=="disabled")return {};
  if(!choice.isEmpty()){const QUrl url(choice);if(url.isLocalFile() && QFileInfo::exists(url.toLocalFile()))return choice;return {};}
  const auto local=current().value("motionArt").toString();return local.isEmpty()?m_onlineMotionArt:local;
}
QString Backend::artworkStatus() const {
  if(artworkChoice()=="disabled")return "Animation disabled for this song";
  if(!artworkChoice().isEmpty())return currentMotionArt().isEmpty()?"Selected cover is missing":"Your selected cover";
  if(!current().value("motionArt").toString().isEmpty())return "Local animated cover";
  if(!motion() || !animatedArtwork())return "Enable animations in Settings";
  if(!onlineArtwork())return "Online covers disabled";
  if(!m_artworkStatus.isEmpty())return m_artworkStatus;
  return current().value("videoId").toString().isEmpty()?"Choose a local animated cover":"Play this song to look for a cover";
}
void Backend::saveArtworkChoice(const QString &value){
  const auto id=current().value("id").toString();if(id.isEmpty())return;
  auto choices=m_settings.value("artworkChoices").toMap();
  if(value.isEmpty())choices.remove(id);else {if(choices.size()>=1000 && !choices.contains(id)){emit toast("Artwork choice limit reached");return;}choices[id]=value;}
  m_settings.setValue("artworkChoices",choices);updateOnlineArtwork();emit onlineArtworkChanged();
}
void Backend::rejectArtwork(){saveArtworkChoice("disabled");}
void Backend::resetArtworkChoice(){saveArtworkChoice({});m_onlineArtworkAttempted=false;m_onlineArtworkRetries=0;updateOnlineArtwork();}
void Backend::retryArtwork(){
  if(current().value("videoId").toString().isEmpty())return;
  forgetVideoCover(current().value("videoId").toString());
  resetArtworkChoice();m_artworkForce=true;
  if(playing())updateOnlineArtwork();
}
// Covers found for video frames are kept by video id, so every surface that
// draws the frame can swap the cover in without a lookup of its own. An empty
// entry records that nothing matched; "Find cover again" clears it. The map
// is a cache with a ceiling, not a library: past 2,000 songs it starts over
// and refills one song at a time as they play.
void Backend::rememberVideoCover(const QString &videoId,const QString &cover){
  if(videoId.isEmpty())return;
  if(m_videoCovers.contains(videoId) && m_videoCovers.value(videoId).toString()==cover)return;
  if(m_videoCovers.size()>=2000 && !m_videoCovers.contains(videoId))m_videoCovers.clear();
  m_videoCovers[videoId]=cover;m_settings.setValue("videoCovers",m_videoCovers);emit videoCoversChanged();
}
void Backend::forgetVideoCover(const QString &videoId){
  if(!m_videoCovers.contains(videoId))return;
  m_videoCovers.remove(videoId);m_settings.setValue("videoCovers",m_videoCovers);emit videoCoversChanged();
}
void Backend::setAlbumCovers(bool value){
  if(albumCovers()==value)return;
  m_settings.setValue("albumCovers",value);emit settingsChanged();emit videoCoversChanged();
}
QUrl Backend::albumCoverFor(const QUrl &frame) const {
  if(!albumCovers())return {};
  const auto id=artworkurl::videoId(frame);if(id.isEmpty())return {};
  const QUrl cover(m_videoCovers.value(id).toString());
  return artworkurl::isAlbumCover(cover)?cover:QUrl();
}
QString Backend::displayArt(const QVariantMap &track) const {
  const QUrl art(track.value("art").toString());const auto cover=albumCoverFor(art);
  return artworkurl::sized(cover.isEmpty()?art:cover,600).toString();
}
void Backend::chooseArtwork(const QUrl &url,const QString &songId){
  if(songId.isEmpty() || songId!=current().value("id").toString() || !url.isLocalFile())return;
  request("choose-artwork",{{"op","choose-artwork"},{"path",url.toLocalFile()},{"artDirectory",QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/local-art"}},[this,songId](const QVariantMap &data){
    if(songId!=current().value("id").toString())return;
    const auto motion=data.value("motionArt").toString();
    if(!data.value("ok").toBool() || motion.isEmpty()){emit toast("Choose a readable GIF, animated WebP, MP4 or WebM cover");return;}
    saveArtworkChoice(motion);
  });
}

void Backend::setupFolderWatching(){
  m_folderChangeTimer.setSingleShot(true);
  const auto changed=[this](const QString &){if(watchMusicFolders()){m_folderDirty=true;m_folderChangeTimer.start(1500);}};
  connect(&m_folderWatcher,&QFileSystemWatcher::directoryChanged,this,changed);
  connect(&m_folderWatcher,&QFileSystemWatcher::fileChanged,this,changed);
  connect(&m_folderChangeTimer,&QTimer::timeout,this,[this]{
    if(!watchMusicFolders() || m_musicFolders.isEmpty() || !m_folderDirty)return;
    if(importingLocal())return;
    m_folderDirty=false;m_quietFolderScan=true;rescanMusicFolders();
  });
  connect(this,&Backend::localImportChanged,this,[this]{if(!importingLocal() && m_folderDirty && watchMusicFolders())m_folderChangeTimer.start(1500);});
  if(watchMusicFolders() && !m_musicFolders.isEmpty()){m_folderDirty=true;m_folderChangeTimer.start(5000);}
}
void Backend::setWatchMusicFolders(bool value){
  if(watchMusicFolders()==value)return;
  m_settings.setValue("watchMusicFolders",value);emit settingsChanged();
  if(value){m_folderDirty=true;m_folderChangeTimer.start(1500);}
  else {m_folderChangeTimer.stop();m_folderDirty=false;updateFolderWatches({});}
}
void Backend::updateFolderWatches(const QStringList &paths){
  QSet<QString> desired;
  if(watchMusicFolders())for(const auto &p:paths){
    bool allowed=false;for(const auto &root:m_musicFolders)if(p==root || p.startsWith(root+'/') || root.startsWith(p+'/')){allowed=true;break;}
    if(allowed && QFileInfo::exists(p) && desired.size()<14160)desired.insert(p);
  }
  const auto existing=m_folderWatcher.directories()+m_folderWatcher.files();
  QStringList remove;for(const auto &p:existing)if(!desired.remove(p))remove<<p;
  if(!remove.isEmpty())m_folderWatcher.removePaths(remove);
  if(!desired.isEmpty())m_folderWatcher.addPaths(desired.values());
}

#include <QImageReader>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QUuid>
#include <cmath>
#include <QLocale>
#include <algorithm>
#include <functional>
#include <limits>

// Group by album artist (where tagged), not the individual song performer.
static QString groupArtist(const QVariantMap &t) {
  return t.value("albumArtist").toString().trimmed().isEmpty()?t.value("artist").toString().trimmed():t.value("albumArtist").toString().trimmed();
}
static QString localGroupKey(const QVariantMap &t,bool album) {
  const auto artist=album?groupArtist(t):t.value("artist").toString().trimmed();
  return artist.toCaseFolded()+QChar(0x1f)+(album?t.value("album").toString().trimmed().toCaseFolded():QString());
}
// Whether two tracks are parts of one album, which is what a transition has to
// know before it blends them. The library's own grouping answers it: a
// catalogue or server track by its album id, an imported file by the key the
// local album browser files it under, so two records called "Greatest Hits" by
// different artists stay apart.
//
// An imported file needs a named album to count. A folder of untagged files
// groups under "Unknown album" in the browser, but it is not a record, and
// treating it as one would quietly drop the overlap between unrelated songs.
bool Backend::sameAlbum(const QVariantMap &a,const QVariantMap &b) {
  const bool localA=!a.value("localPath").toString().isEmpty();
  const bool localB=!b.value("localPath").toString().isEmpty();
  if(localA || localB){
    if(localA!=localB || a.value("album").toString().trimmed().isEmpty())return false;
    return localGroupKey(a,true)==localGroupKey(b,true);
  }
  const auto id=a.value("albumId").toString();
  return !id.isEmpty() && id==b.value("albumId").toString()
      && a.value("source")==b.value("source") && a.value("server")==b.value("server");
}
QVariantList Backend::localGroups(const QString &kind) const {
  const bool album=kind=="local-albums";QMap<QString,QVariantMap> groups;
  for(const auto &v:m_localTracks){const auto t=v.toMap();const auto key=localGroupKey(t,album);
    auto &g=groups[key];if(g.isEmpty()){
      const auto title=album?t.value("album").toString().trimmed():t.value("artist").toString().trimmed();
      g={{"id",QString("local-group:")+QString::fromLatin1(QCryptographicHash::hash((kind+key).toUtf8(),QCryptographicHash::Sha256).toHex())},
         {"kind",album?"local-album":"local-artist"},{"groupKey",key},{"title",title.isEmpty()?(album?"Unknown album":"Unknown artist"):title},
         {"artist",album?groupArtist(t):QString()},{"art",t.value("art")},{"year",t.value("year")},{"count",0}};
    }
    g["count"]=g.value("count").toInt()+1;
    if(g.value("art").toString().isEmpty())g["art"]=t.value("art");
  }
  QVariantList result;for(const auto &g:groups)result<<g;
  std::stable_sort(result.begin(),result.end(),[](const QVariant &a,const QVariant &b){const auto x=a.toMap(),y=b.toMap();const int c=QString::localeAwareCompare(x.value("title").toString(),y.value("title").toString());return c?c<0:QString::localeAwareCompare(x.value("artist").toString(),y.value("artist").toString())<0;});
  return result;
}
QVariantList Backend::localGroupRows(const QVariantMap &item) const {
  const bool album=item.value("kind")=="local-album";QVariantList rows;
  for(const auto &v:m_localTracks)if(localGroupKey(v.toMap(),album)==item.value("groupKey").toString())rows<<v;
  std::stable_sort(rows.begin(),rows.end(),[album](const QVariant &a,const QVariant &b){const auto x=a.toMap(),y=b.toMap();
    if(!album){const int c=QString::localeAwareCompare(x.value("album").toString(),y.value("album").toString());if(c)return c<0;}
    for(const auto key:{"discNumber","trackNumber"}){const int l=x.value(key,1).toInt(),r=y.value(key,1).toInt();if(l!=r)return l<r;}
    return QString::localeAwareCompare(x.value("title").toString(),y.value("title").toString())<0;
  });return rows;
}
QVariantMap Backend::relatedCollection(const QVariantMap &item,const QString &kind) const {
  if(kind!="album" && kind!="artist")return {};
  if(!item.value("localPath").toString().isEmpty()){
    const auto key=localGroupKey(item,kind=="album");
    const bool album=kind=="album";
    for(const auto &value:m_localTracks){
      const auto track=value.toMap();
      if(localGroupKey(track,album)!=key)continue;
      const auto category=album?QString("local-albums"):QString("local-artists");
      const auto title=track.value(kind).toString().trimmed();
      return {{"id",QString("local-group:")+QString::fromLatin1(QCryptographicHash::hash((category+key).toUtf8(),QCryptographicHash::Sha256).toHex())},
              {"kind","local-"+kind},{"groupKey",key},{"title",title.isEmpty()?(album?"Unknown album":"Unknown artist"):title},
              {"artist",album?groupArtist(track):QString()},{"art",track.value("art")}};
    }
    return {};
  }
  const auto id=item.value(kind+"Id").toString();
  if(id.isEmpty())return {};
  return {{"kind",kind},{"title",item.value(kind)},{"browseId",id},{"remoteId",id},
          {"source",item.value("source")},{"server",item.value("server")},{"art",item.value("art")}};
}
void Backend::openLocalGroup(const QVariantMap &item){
  navigate(item.value("kind").toString(),item.value("title").toString(),true,item.value("id").toString());
  m_request=item;m_cover=item.value("art").toString();m_libraryId=item.value("kind")=="local-album"?"local-albums":"local-artists";
  m_results.assign(localGroupRows(item));emit catalogChanged();
}
void Backend::updateLocalView(){
  QVariantList rows;
  if(m_page=="library" && (m_libraryId=="local-albums" || m_libraryId=="local-artists"))rows=localGroups(m_libraryId);
  else if(m_page=="local-album" || m_page=="local-artist")rows=localGroupRows(m_request);
  else return;
  if(rows==m_results.rows)return;
  if(m_page!="library"){
    m_cover.clear();for(const auto &v:rows){const auto art=v.toMap().value("art").toString();if(!art.isEmpty()){m_cover=art;break;}}
  }
  m_results.assign(rows);emit catalogChanged();
}
QString Backend::preparePlaylistCover(const QUrl &url){
  if(!url.isLocalFile())return {};
  const QFileInfo file(url.toLocalFile());if(!file.isFile() || file.size()>32*1024*1024)return {};
  QImageReader reader(file.absoluteFilePath());reader.setAutoTransform(true);
  const auto size=reader.size();if(!size.isValid() || qint64(size.width())*size.height()>32000000)return {};
  reader.setScaledSize(size.scaled(1024,1024,Qt::KeepAspectRatio));const auto image=reader.read();if(image.isNull())return {};
  const auto dir=QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/cover-edit";if(!QDir().mkpath(dir))return {};
  const auto path=dir+"/preview.jpg";QSaveFile out(path);if(!out.open(QIODevice::WriteOnly)||!image.save(&out,"JPEG",88)||!out.commit())return {};
  QUrl result=QUrl::fromLocalFile(path);result.setQuery(QUuid::createUuid().toString(QUuid::WithoutBraces));return result.toString();
}
bool Backend::setPlaylistCover(const QString &id,const QString &preview,double x,double y,double zoom){
  if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(zoom))return false;
  int index=-1;for(int i=0;i<m_playlists.size();++i)if(m_playlists[i].toMap().value("id")==id){index=i;break;}if(index<0)return false;
  const QUrl url(preview);const auto expected=QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/cover-edit/preview.jpg";
  if(!url.isLocalFile() || url.toLocalFile()!=expected)return false;
  QImage image(expected);if(image.isNull())return false;
  const int side=qMax(1,int(qMin(image.width(),image.height())/qBound(1.0,zoom,3.0)));
  image=image.copy(qRound((image.width()-side)*qBound(0.0,x,1.0)),qRound((image.height()-side)*qBound(0.0,y,1.0)),side,side).scaled(512,512,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
  const auto dir=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/playlist-art";if(!QDir().mkpath(dir))return false;
  const auto path=dir+'/'+QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(),QCryptographicHash::Sha256).toHex())+".jpg";
  QSaveFile out(path);if(!out.open(QIODevice::WriteOnly)||!image.save(&out,"JPEG",85)||!out.commit())return false;
  QUrl cover=QUrl::fromLocalFile(path);cover.setQuery(QUuid::createUuid().toString(QUuid::WithoutBraces));
  auto p=m_playlists[index].toMap();p["customCover"]=cover.toString();m_playlists[index]=p;
  if(m_page=="local" && m_libraryId==id){m_cover=cover.toString();emit catalogChanged();}
  emit libraryChanged();m_saveTimer.start();return true;
}
void Backend::resetPlaylistCover(const QString &id){
  for(auto &v:m_playlists){auto p=v.toMap();if(p.value("id")!=id)continue;p.remove("customCover");v=p;
    if(m_page=="local" && m_libraryId==id){m_cover.clear();emit catalogChanged();}
    emit libraryChanged();m_saveTimer.start();return;}
}

#include <QJsonDocument>
#include <QDateTime>
static QString fitKey(const QVariantMap &t){
  const auto album=t.value("album").toString().trimmed();
  const auto key=album.isEmpty()?t.value("id").toString():t.value("source").toString()+"|"+t.value("server").toString()+"|"+groupArtist(t).toCaseFolded()+"|"+album.toCaseFolded();
  return QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(),QCryptographicHash::Sha256).toHex());
}
bool Backend::artworkFits(const QVariantMap &t) const {return m_settings.value("artworkFit").toMap().value(fitKey(t),false).toBool();}
void Backend::setCurrentArtworkFit(bool enabled){
  if(current().isEmpty())return;
  auto choices=m_settings.value("artworkFit").toMap();const auto key=fitKey(current());
  if(enabled){if(choices.size()>=2000&&!choices.contains(key)){emit toast("Artwork preference limit reached");return;}choices[key]=true;}else choices.remove(key);
  m_settings.setValue("artworkFit",choices);emit artworkFitChanged();
}
QVariantList Backend::sessions() const {
  QVariantList result;for(const auto &v:m_sessions){auto s=v.toMap();const auto rows=s.take("queue").toList();s["count"]=rows.size();const auto i=s.value("index").toInt();if(i>=0&&i<rows.size())s["song"]=rows[i].toMap().value("title");result<<s;}return result;
}
bool Backend::saveSession(const QString &name,const QString &id){
  const auto title=name.trimmed().left(80);if(title.isEmpty() || m_queue.count()==0)return false;
  if(m_queue.count()>2000){emit toast("Sessions support up to 2,000 songs");return false;}
  int index=-1;for(int i=0;i<m_sessions.size();++i)if(m_sessions[i].toMap().value("id")==id)index=i;
  if((!id.isEmpty()&&index<0)||(index<0&&m_sessions.size()>=20)){emit toast("Keep up to 20 listening sessions");return false;}
  QVariantMap s{{"id",id.isEmpty()?QUuid::createUuid().toString(QUuid::WithoutBraces):id},{"title",title},{"queue",m_queue.rows},{"index",qMax(0,m_index)},{"position",position()},{"rate",playbackRate()},{"shuffle",shuffle()},{"repeat",repeat()},{"autoplay",autoplay()}};
  auto saved=m_sessions;if(index<0)saved.prepend(s);else saved[index]=s;
  if(QJsonDocument::fromVariant(saved).toJson(QJsonDocument::Compact).size()>8*1024*1024){emit toast("Saved sessions have reached their storage limit");return false;}
  m_sessions=saved;emit sessionsChanged();m_saveTimer.start();return true;
}
void Backend::renameSession(const QString &id,const QString &name){if(name.trimmed().isEmpty())return;for(auto &v:m_sessions){auto s=v.toMap();if(s.value("id")==id){s["title"]=name.trimmed().left(80);v=s;emit sessionsChanged();m_saveTimer.start();return;}}}
void Backend::deleteSession(const QString &id){for(int i=0;i<m_sessions.size();++i)if(m_sessions[i].toMap().value("id")==id){m_sessions.removeAt(i);emit sessionsChanged();m_saveTimer.start();return;}}
bool Backend::restoreSession(const QString &id){
  for(const auto &v:m_sessions){const auto s=v.toMap();if(s.value("id")!=id)continue;
    const auto original=s.value("queue").toList();const auto rows=playable(original);if(rows.isEmpty()){emit toast("This session has no playable songs");return false;}
    const auto oldIndex=qBound(0,s.value("index").toInt(),int(original.size())-1);const bool matched=!playable({original[oldIndex]}).isEmpty();
    const int index=matched?playable(original.mid(0,oldIndex)).size():0;
    stop();dismissError();++m_trackToken;invalidateUndo("queue");m_queue.reconcile(rows);m_index=index;m_savedPosition=matched?qBound<qint64>(qint64(0),s.value("position").toLongLong(),qint64(604800000)):0;
    setPlaybackRate(s.value("rate",1).toDouble());setShuffle(s.value("shuffle").toBool());setRepeat(qBound(0,s.value("repeat").toInt(),2));setAutoplay(s.value("autoplay",true).toBool());
    emit trackChanged();emit positionChanged();play();m_saveTimer.start();return true;
  }return false;
}
void Backend::pauseForDisconnect(){if(pauseOnDisconnect()&&(playing()||m_wantPlay)){pause();emit toast("Output disconnected · playback paused");}}
void Backend::outputsChanged(){
  bool present=m_outputId.isEmpty();for(const auto &d:QMediaDevices::audioOutputs())if(d.id()==m_outputId)present=true;
  if(!present)pauseForDisconnect();
  applyAudioDevice();emit audioDevicesChanged();if(pauseOnDisconnect())m_portDebounce.start();
}
void Backend::setPauseOnDisconnect(bool enabled){
  if(enabled==pauseOnDisconnect())return;
  m_settings.setValue("pauseOnDisconnect",enabled);emit settingsChanged();m_outputPort.clear();
  if(enabled){if(m_portMonitor.state()==QProcess::NotRunning)m_portMonitor.start("pactl",{"subscribe"});m_portDebounce.start();}
  else {m_portDebounce.stop();m_portTimeout.stop();m_portMonitor.kill();m_portProbe.kill();}
}
void Backend::setupDisconnectMonitor(){
  m_portDebounce.setSingleShot(true);m_portDebounce.setInterval(120);m_portTimeout.setSingleShot(true);m_portTimeout.setInterval(2500);
  connect(&m_portTimeout,&QTimer::timeout,this,[this]{m_portProbe.kill();});
  connect(&m_portDebounce,&QTimer::timeout,this,&Backend::refreshOutputPort);
  connect(&m_portMonitor,&QProcess::readyReadStandardOutput,this,[this]{const auto events=m_portMonitor.readAllStandardOutput();if(pauseOnDisconnect()&&(events.contains("sink")||events.contains("card")))m_portDebounce.start();});
  connect(&m_portMonitor,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this]{
    if(pauseOnDisconnect())QTimer::singleShot(1000,this,[this]{if(pauseOnDisconnect()&&m_portMonitor.state()==QProcess::NotRunning){m_portMonitor.start("pactl",{"subscribe"});m_portDebounce.start();}});
  });
  connect(&m_portMonitor,&QProcess::readyReadStandardError,this,[this]{m_portMonitor.readAllStandardError();});
  connect(&m_portProbe,&QProcess::readyReadStandardOutput,this,[this]{if(m_portProbe.bytesAvailable()>1024*1024)m_portProbe.kill();});
  connect(&m_portProbe,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status){
    m_portTimeout.stop();const auto data=m_portProbe.readAllStandardOutput();m_portProbe.readAllStandardError();
    if(pauseOnDisconnect()&&code==0&&status==QProcess::NormalExit)inspectOutputPorts(QJsonDocument::fromJson(data).toVariant().toList());
    if(m_portDirty){m_portDirty=false;m_portDebounce.start();}
  });
  if(pauseOnDisconnect()){m_portMonitor.start("pactl",{"subscribe"});m_portDebounce.start();}
}
void Backend::refreshOutputPort(){if(!pauseOnDisconnect())return;if(m_portProbe.state()!=QProcess::NotRunning){m_portDirty=true;return;}m_portProbe.start("pactl",{"-f","json","list","sinks"});m_portTimeout.start();}
void Backend::inspectOutputPorts(const QVariantList &sinks){
  for(const auto &v:sinks){const auto s=v.toMap();if(s.value("description").toString()!=m_outputDescription && s.value("name").toString()!=QString::fromUtf8(m_outputId))continue;
    const auto port=s.value("active_port").toString();
    if(!m_outputPort.isEmpty() && m_outputPort!=port && (m_outputPort.contains("headphone",Qt::CaseInsensitive)||m_outputPort.contains("headset",Qt::CaseInsensitive)))pauseForDisconnect();
    m_outputPort=port;return;
  }
}

// --- Listening statistics ----------------------------------------------------
//
// The recent-history list keeps one row per song, so it can say what was played
// but not how often. These come from a separate log with one row per play, kept
// only on this device and only while history is being recorded at all.

void Backend::recordPlay(const QVariantMap &track) {
  if(m_historyPaused)return;
  const auto id=track.value("id").toString();
  if(id.isEmpty())return;
  const auto at=QDateTime::currentSecsSinceEpoch();
  m_plays.append(QVariantMap{{"at",at},
                             {"id",id},
                             {"title",track.value("title")},
                             {"artist",track.value("artist")},
                             {"album",track.value("album")},
                             {"seconds",track.value("seconds")}});
  countListeningDay(at,track.value("seconds").toLongLong());
  // Roughly a decade of ordinary listening, then the oldest fall away.
  while(m_plays.size()>20000)m_plays.removeFirst();
}

void Backend::countListeningDay(qint64 at, qint64 seconds) {
  if(seconds<0 || seconds>86400)seconds=0;
  const auto key=QDateTime::fromSecsSinceEpoch(at).date().toString(Qt::ISODate);
  const auto day=m_listeningDays.value(key).toList();
  m_listeningDays.insert(key,QVariantList{day.value(0).toLongLong()+seconds,day.value(1).toInt()+1});
  // Ten years of days. The keys sort as dates, so the first is the oldest.
  while(m_listeningDays.size()>3660)m_listeningDays.remove(m_listeningDays.firstKey());
}

void Backend::clearListeningStats() {
  if(m_plays.isEmpty() && m_listeningDays.isEmpty())return;
  m_plays.clear();
  m_listeningDays.clear();
  emit libraryChanged();
  m_saveTimer.start();
  emit toast("Listening statistics cleared");
}

QVariantMap Backend::listeningStats(int days) const {
  const qint64 now=QDateTime::currentSecsSinceEpoch();
  return statsBetween(days>0?now-qint64(days)*86400:0,std::numeric_limits<qint64>::max());
}

QVariantMap Backend::listeningStatsOn(const QString &date) const {
  const auto day=QDate::fromString(date,Qt::ISODate);
  if(!day.isValid())return {};
  auto stats=statsBetween(day.startOfDay().toSecsSinceEpoch(),day.addDays(1).startOfDay().toSecsSinceEpoch());
  // A day older than the play log still has its total: the rankings for it
  // are gone, the time and the count are not.
  const auto total=m_listeningDays.value(day.toString(Qt::ISODate)).toList();
  if(stats.value("plays").toInt()==0 && !total.isEmpty()){
    stats["seconds"]=total.value(0).toLongLong();
    stats["plays"]=total.value(1).toInt();
    stats["totalOnly"]=true;
  }
  return stats;
}

QVariantMap Backend::statsBetween(qint64 from, qint64 to) const {
  struct Tally { qint64 seconds=0; int plays=0; QString subtitle; };
  QHash<QString,Tally> artists,albums,songs;
  QSet<QString> distinctSongs;
  qint64 total=0;
  int plays=0;
  const QChar separator(0x1f);
  for(const auto &v:m_plays){
    const auto play=v.toMap();
    const auto at=play.value("at").toLongLong();
    if(at<from || at>=to)continue;
    auto seconds=play.value("seconds").toLongLong();
    if(seconds<0 || seconds>86400)seconds=0;
    total+=seconds;
    ++plays;
    distinctSongs.insert(play.value("id").toString());
    const auto artist=play.value("artist").toString().trimmed();
    const auto album=play.value("album").toString().trimmed();
    const auto title=play.value("title").toString().trimmed();
    if(!artist.isEmpty()){auto &t=artists[artist];t.seconds+=seconds;++t.plays;}
    if(!album.isEmpty()){auto &t=albums[album];t.seconds+=seconds;++t.plays;if(t.subtitle.isEmpty())t.subtitle=artist;}
    if(!title.isEmpty()){auto &t=songs[title+separator+artist];t.seconds+=seconds;++t.plays;t.subtitle=artist;}
  }
  // Most time first, with the name breaking ties so the order never wobbles.
  const auto rank=[separator](const QHash<QString,Tally> &source,bool joinedKey){
    QVariantList rows;
    for(auto i=source.constBegin();i!=source.constEnd();++i)
      rows.append(QVariantMap{{"name",joinedKey?i.key().section(separator,0,0):i.key()},
                              {"subtitle",i.value().subtitle},
                              {"seconds",i.value().seconds},
                              {"plays",i.value().plays}});
    std::sort(rows.begin(),rows.end(),[](const QVariant &a,const QVariant &b){
      const auto x=a.toMap(),y=b.toMap();
      if(x.value("seconds").toLongLong()!=y.value("seconds").toLongLong())
        return x.value("seconds").toLongLong()>y.value("seconds").toLongLong();
      if(x.value("plays").toInt()!=y.value("plays").toInt())return x.value("plays").toInt()>y.value("plays").toInt();
      return x.value("name").toString()<y.value("name").toString();
    });
    return rows.mid(0,10);
  };
  return {{"plays",plays},
          {"seconds",total},
          {"songs",distinctSongs.size()},
          {"artists",artists.size()},
          {"albums",albums.size()},
          {"topArtists",rank(artists,false)},
          {"topAlbums",rank(albums,false)},
          {"topSongs",rank(songs,true)}};
}

// The listening graph is GitHub's contribution calendar with time listened in
// place of commits: a column per week, a row per weekday starting where the
// locale starts its week, and each day shaded by which quarter of the listened
// days it falls in, as GitHub shades by quartile. Days before the range that
// share its first week are returned as padding so the columns line up.
QVariantMap Backend::listeningCalendar(int year) const {
  const QDate today=QDate::currentDate();
  const QDate from=year>0?QDate(year,1,1):today.addDays(-364);
  const QDate to=year>0?qMin(QDate(year,12,31),today):today;
  if(!from.isValid() || from>to)return {};
  const int weekStart=int(QLocale::system().firstDayOfWeek());
  const QDate first=from.addDays(-((from.dayOfWeek()-weekStart+7)%7));
  QList<qint64> listened;
  qint64 total=0;
  for(QDate d=from;d<=to;d=d.addDays(1)){
    const qint64 seconds=m_listeningDays.value(d.toString(Qt::ISODate)).toList().value(0).toLongLong();
    if(seconds>0){listened.append(seconds);total+=seconds;}
  }
  std::sort(listened.begin(),listened.end());
  const auto quartile=[&](int q){return listened.isEmpty()?0:listened[qMin(listened.size()-1,listened.size()*q/4)];};
  const qint64 low=quartile(1),middle=quartile(2),high=quartile(3);
  QVariantList days;
  for(QDate d=first;d<=to;d=d.addDays(1)){
    const auto day=d<from?QVariantList{}:m_listeningDays.value(d.toString(Qt::ISODate)).toList();
    const qint64 seconds=day.value(0).toLongLong();
    // Counted from the top, so the busiest days are always the darkest, a
    // single day of music included.
    const int level=seconds<=0?0:seconds>=high?4:seconds>=middle?3:seconds>=low?2:1;
    days.append(QVariantMap{{"date",d.toString(Qt::ISODate)},{"seconds",seconds},{"plays",day.value(1).toInt()},
                            {"level",level},{"padding",d<from}});
  }
  // A streak is still alive until a whole day passes without music, so one
  // that reached yesterday counts while today is still young.
  const auto listenedOn=[this](const QDate &d){return m_listeningDays.value(d.toString(Qt::ISODate)).toList().value(0).toLongLong()>0;};
  int streak=0;
  for(QDate d=listenedOn(today)?today:today.addDays(-1);listenedOn(d);d=d.addDays(-1))++streak;
  int longest=0,run=0;
  QDate previous;
  QSet<int> years;
  for(auto i=m_listeningDays.constBegin();i!=m_listeningDays.constEnd();++i){
    if(i.value().toList().value(0).toLongLong()<=0)continue;
    const auto d=QDate::fromString(i.key(),Qt::ISODate);
    run=previous.isValid() && previous.addDays(1)==d?run+1:1;
    longest=qMax(longest,run);
    previous=d;
    years.insert(d.year());
  }
  years.insert(today.year());
  QList<int> sortedYears(years.begin(),years.end());
  std::sort(sortedYears.begin(),sortedYears.end(),std::greater<int>());
  QVariantList yearList;
  for(int y:sortedYears)yearList.append(y);
  return {{"from",from.toString(Qt::ISODate)},{"to",to.toString(Qt::ISODate)},{"days",days},
          {"seconds",total},{"active",listened.size()},{"streak",streak},{"longest",longest},
          {"years",yearList},{"weekStart",weekStart}};
}

// --- Playlist versions -------------------------------------------------------
//
// Undo takes back the last edit. This takes back an edit from last week: each
// change to a playlist puts the version it replaced aside first, so the shape a
// playlist used to have can be looked at and returned to.
//
// Versions hold whole rows rather than identities, so restoring one brings back
// songs that were removed from everywhere else since. That costs space, so both
// the number of versions and the rows across them are bounded.

void Backend::snapshotPlaylist(const QString &id) {
  if(id.isEmpty())return;
  QVariantList tracks;
  bool found=false;
  for(const auto &v:m_playlists){
    const auto p=v.toMap();
    if(p.value("id").toString()!=id)continue;
    tracks=p.value("tracks").toList();found=true;break;
  }
  if(!found)return;
  auto versions=m_playlistVersions.value(id).toList();
  // An edit that changed nothing is not a version.
  if(!versions.isEmpty() && versions.first().toMap().value("tracks").toList()==tracks)return;
  versions.prepend(QVariantMap{{"at",QDateTime::currentSecsSinceEpoch()},{"tracks",tracks}});
  int rows=0;
  for(int i=0;i<versions.size();++i){
    rows+=versions[i].toMap().value("tracks").toList().size();
    if(i>=12 || rows>5000){versions=versions.mid(0,qMax(1,i));break;}
  }
  m_playlistVersions[id]=versions;
}

QVariantList Backend::playlistVersions(const QString &id) const {
  QVariantList rows;
  for(const auto &v:m_playlistVersions.value(id).toList()){
    const auto version=v.toMap();
    const auto count=version.value("tracks").toList().size();
    rows.append(QVariantMap{{"at",version.value("at")},
                            {"count",count},
                            {"summary",QString("%1 %2").arg(count).arg(count==1?"song":"songs")}});
  }
  return rows;
}

bool Backend::restorePlaylistVersion(const QString &id,int index) {
  const auto versions=m_playlistVersions.value(id).toList();
  if(index<0 || index>=versions.size())return false;
  if(!smartPlaylist(id).isEmpty())return false;
  const auto tracks=playable(versions[index].toMap().value("tracks").toList());
  for(int i=0;i<m_playlists.size();++i){
    auto p=m_playlists[i].toMap();
    if(p.value("id").toString()!=id)continue;
    if(p.value("tracks").toList()==tracks){emit toast("Playlist already matches that version");return false;}
    // Put the version being replaced aside first, so a restore can be undone
    // the same way any other edit can.
    snapshotPlaylist(id);
    m_undoType="playlists";m_undoRows=m_playlists;m_undoMessage="Playlist restored";
    p["tracks"]=tracks;m_playlists[i]=p;
    if(m_page=="local"&&m_libraryId==id)m_results.assign(tracks);
    emit libraryChanged();
    m_saveTimer.start();
    emit toast(m_undoMessage);
    return true;
  }
  return false;
}

void Backend::clearPlaylistVersions(const QString &id) {
  if(!m_playlistVersions.contains(id))return;
  m_playlistVersions.remove(id);
  emit libraryChanged();
  m_saveTimer.start();
}

// --- Listening history sent onward -------------------------------------------
//
// A song is announced as it starts, and reported as listened to once it has
// played far enough to count. A private session reports nothing, the same way
// it records nothing.

void Backend::beginScrobble() {
  m_scrobbleToken=m_trackToken;
  m_scrobbleSent=false;
  m_scrobbleStartedAt=QDateTime::currentSecsSinceEpoch();
  m_scrobbleThreshold=-1;
  if(m_historyPaused)return;
  m_scrobbler.nowPlaying(current());
}

void Backend::considerScrobble() {
  if(m_scrobbleSent || m_historyPaused || m_scrobbleToken!=m_trackToken || !playing())return;
  if(m_scrobbleThreshold<0){
    const auto total=duration();
    if(total<=0)return;
    m_scrobbleThreshold=Scrobbler::thresholdFor(total);
    // A recording too short to count never will be.
    if(m_scrobbleThreshold<0){m_scrobbleSent=true;return;}
  }
  if(position()<m_scrobbleThreshold)return;
  m_scrobbleSent=true;
  m_scrobbler.submit(current(),m_scrobbleStartedAt);
}
