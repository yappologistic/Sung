#include "backend.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>

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
  resetArtworkChoice();m_artworkForce=true;
  if(playing())updateOnlineArtwork();
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
#include <algorithm>

// Group by album artist (where tagged), not the individual song performer.
static QString groupArtist(const QVariantMap &t) {
  return t.value("albumArtist").toString().trimmed().isEmpty()?t.value("artist").toString().trimmed():t.value("albumArtist").toString().trimmed();
}
static QString localGroupKey(const QVariantMap &t,bool album) {
  const auto artist=album?groupArtist(t):t.value("artist").toString().trimmed();
  return artist.toCaseFolded()+QChar(0x1f)+(album?t.value("album").toString().trimmed().toCaseFolded():QString());
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
