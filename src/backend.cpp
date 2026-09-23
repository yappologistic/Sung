#include "backend.h"
#include "artworkurl.h"
#include "freedmemory.h"
#include "librarydata.h"
#include <QLocale>
#include <QMediaMetaData>
#include "lrc.h"
#include "m3color.h"
#include "m3motion.h"
#include "m3shape.h"
#include <QClipboard>
#include <QSet>
#include <cmath>
#include <QColor>
#include <algorithm>
#include <queue>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrlQuery>
#include <QUuid>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>

static QString dataPath() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}
static std::shared_ptr<QTemporaryDir> audioDirectory() {
  const auto root=QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  if(!QDir().mkpath(root))return {};
  // Use the application cache filesystem instead of /tmp, which is commonly
  // tmpfs on CachyOS. QTemporaryDir still removes each file on normal teardown.
  return std::make_shared<QTemporaryDir>(root+"/audio-XXXXXX");
}
static QString itemId(const QVariant &v) {
  return v.toMap().value("id").toString();
}
Backend::Backend(QObject *parent) : QObject(parent) {
  // Both decks report everything; only the one being heard is listened to.
  const auto eachDeck=[this](const std::function<void(QMediaPlayer *)> &wire){wire(&m_deckA);wire(&m_deckB);};
  m_userVolume=qBound(0.0,m_settings.value("volume",0.65).toDouble(),1.0);
  m_offline.setBudget(qint64(keepPlayedMb())*1024*1024);
  m_videoCovers=m_settings.value("videoCovers").toMap();
  m_audioA.setVolume(m_userVolume);
  m_audioB.setVolume(m_userVolume);
  m_deckA.setAudioOutput(&m_audioA);
  m_deckB.setAudioOutput(&m_audioB);
  m_deckA.setAudioBufferOutput(&m_visualAudio);
  setPlaybackRate(m_settings.value("playbackRate",1.0).toDouble());
  setPreservePitch(m_settings.value("preservePitch",true).toBool());
  eachDeck([this](QMediaPlayer *deck){connect(deck,&QMediaPlayer::playbackRateChanged,this,[this,deck]{if(isActive(*deck))emit settingsChanged();});});
  m_collection.setSourceModel(&m_results);
  connect(&m_queue,&Entries::countChanged,this,[this]{
    m_queueSuffix.fill(0,m_queue.count()+1);
    for(int i=m_queue.count()-1;i>=0;--i){
      const auto seconds=m_queue.get(i).value("seconds").toLongLong();
      m_queueSuffix[i]=seconds>0&&seconds<=604800&&m_queueSuffix[i+1]>=0 ? seconds*1000+m_queueSuffix[i+1] : -1;
    }
    emit queueInfoChanged();
  });
  connect(this,&Backend::trackChanged,this,&Backend::queueInfoChanged);
  connect(this,&Backend::trackChanged,this,&Backend::artworkFitChanged);
  connect(this,&Backend::positionChanged,this,&Backend::queueInfoChanged);
  connect(this,&Backend::playbackChanged,this,&Backend::queueInfoChanged);
  connect(this,&Backend::settingsChanged,this,&Backend::queueInfoChanged);
  connect(&m_devices,&QMediaDevices::audioOutputsChanged,this,[this]{outputsChanged();});
  applyAudioDevice();setupDisconnectMonitor();
  eachDeck([this](QMediaPlayer *deck){
    connect(deck,&QMediaPlayer::metaDataChanged,this,[this,deck]{if(isActive(*deck))emit qualityChanged();});
    connect(deck,&QMediaPlayer::sourceChanged,this,[this,deck]{if(!isActive(*deck))return;m_decodeRate=0;m_decodeChannels=0;emit qualityChanged();});
  });
  m_saveTimer.setSingleShot(true);
  m_saveTimer.setInterval(600);
  connect(&m_saveTimer, &QTimer::timeout, this, &Backend::saveInBackground);
  m_saver.setMaxThreadCount(1);
  m_sleepFadeStart.setSingleShot(true);
  m_sleepFadeTick.setInterval(100);
  connect(&m_sleepFadeStart,&QTimer::timeout,this,[this]{updateSleepGain();m_sleepFadeTick.start();});
  connect(&m_sleepFadeTick,&QTimer::timeout,this,&Backend::updateSleepGain);
  eachDeck([this](QMediaPlayer *deck){
    connect(deck,&QMediaPlayer::positionChanged,this,[this,deck]{if(!isActive(*deck))return;if(m_sleepAtEnd)updateSleepGain();considerCrossfade();considerScrobble();});
    connect(deck,&QMediaPlayer::playbackRateChanged,this,[this,deck]{if(isActive(*deck)&&m_sleepAtEnd)updateSleepGain();});
  });
  m_sleepTick.setInterval(60000);
  connect(&m_sleepTick, &QTimer::timeout, this, &Backend::settingsChanged);
  m_sleepTimer.setSingleShot(true);
  m_sleepTimer.setTimerType(Qt::PreciseTimer);
  connect(&m_sleepTimer, &QTimer::timeout, this, [this] {
    pause();
    setSleep(0);
    emit toast("Sleep timer ended");
  });
  // Only invalidate lyric delegates at a timestamp boundary. Position still updates
  // at the original cadence, and the getter remains exact for immediate seeks.
  const auto updateLyricIndex=[this]{
    const auto index=lyricIndex();
    if(index!=m_notifiedLyricIndex){m_notifiedLyricIndex=index;emit lyricIndexChanged();}
  };
  connect(this,&Backend::positionChanged,this,updateLyricIndex);
  connect(this,&Backend::lyricsChanged,this,updateLyricIndex);
  // Throttle visible progress to four updates per second, with no idle timer.
  m_crossfadeTick.setInterval(40);
  connect(&m_crossfadeTick,&QTimer::timeout,this,&Backend::stepCrossfade);
  m_positionTick.setInterval(250);
  connect(&m_positionTick,&QTimer::timeout,this,[this]{emit positionChanged();});
  m_levelIdle.setSingleShot(true);
  connect(&m_levelIdle,&QTimer::timeout,this,&Backend::resetAudioLevels);
  // Only the deck being heard drives the meters and the loudness measurement;
  // a deck warming up in the background must not colour either.
  connect(&m_visualAudio,&QAudioBufferOutput::audioBufferReceived,this,[this](const QAudioBuffer &buffer){
    if(buffer.isValid() && (m_decodeRate!=buffer.format().sampleRate() || m_decodeChannels!=buffer.format().channelCount())){m_decodeRate=buffer.format().sampleRate();m_decodeChannels=buffer.format().channelCount();emit qualityChanged();}
    // Levelling measures every recording, including while the meters are idle.
    if(buffer.isValid() && playing())m_loudness.process(buffer);
    if(!m_uiActive || !motion() || !playing())return;
    if(!buffer.isValid()){resetAudioLevels();return;}
    m_levelAnalyzer.process(buffer);
    m_levelIdle.start(qBound(180,int(buffer.duration()/1000/std::max(0.25,playbackRate()))+100,600));
    if(m_levelPublish.isValid() && m_levelPublish.elapsed()<33)return;
    m_levelPublish.restart();const auto levels=m_levelAnalyzer.takeLevels();
    if(levels!=m_audioLevels){m_audioLevels=levels;emit audioLevelsChanged();}
  });
  eachDeck([this](QMediaPlayer *deck){connect(deck,&QMediaPlayer::sourceChanged,this,[this,deck]{if(isActive(*deck))resetAudioLevels();});});
  connect(this,&Backend::trackChanged,this,&Backend::refreshRecentlyPlayed);
  connect(this,&Backend::trackChanged,this,&Backend::updateNormalization);
  connect(this,&Backend::seeked,this,&Backend::resetAudioLevels);
  connect(this,&Backend::settingsChanged,this,[this]{if(!motion())resetAudioLevels();});
  eachDeck([this](QMediaPlayer *deck){connect(deck,&QMediaPlayer::durationChanged,this,[this,deck]{if(isActive(*deck))emit playbackChanged();});});
  eachDeck([this](QMediaPlayer *deck){connect(deck,&QMediaPlayer::playbackStateChanged,this,
          [this,deck] {
            if(!isActive(*deck))return;
            if(playing()) {m_stopped=false;if(m_uiActive)m_positionTick.start();recordHistory();notifyTrack();QTimer::singleShot(0,this,&Backend::restorePlaybackPosition);} else {m_positionTick.stop();resetAudioLevels();}
            emit positionChanged(); emit playbackChanged();
          });});
  eachDeck([this](QMediaPlayer *deck){connect(deck,&QMediaPlayer::seekableChanged,this,[this,deck](bool seekable){if(isActive(*deck)&&seekable)QTimer::singleShot(0,this,&Backend::restorePlaybackPosition);});});
  connect(&m_server,&MusicServer::accountChanged,this,&Backend::cancelCoverPlay);
  eachDeck([this](QMediaPlayer *deck){connect(deck,&QMediaPlayer::mediaStatusChanged,this,
          [this,deck](QMediaPlayer::MediaStatus s) {
            // A spare deck that runs out while waiting is simply finished.
            if(!isActive(*deck))return;
            emit playbackChanged();
            if (s == QMediaPlayer::LoadedMedia ||
                s == QMediaPlayer::BufferedMedia) {
              m_recovering=false;
              QTimer::singleShot(0,this,&Backend::restorePlaybackPosition);
              if (m_resolving && m_restorePosition<=0) {
                m_resolving = false;
                emit playbackChanged();
              }
            }
            if (s == QMediaPlayer::EndOfMedia) {
              // A finished recording has nothing left to return to.
              clearResumePosition(current().value("id").toString());
              if(m_sleepAtEnd){endCrossfade(false);clearSpare();setSleep(0);pause();emit toast("Sleep timer ended");return;}
              // Stop instead of advancing, autoplaying or wrapping around.
              if(m_sleepAtQueueEnd&&!shuffle()&&repeat()==0&&m_index+1>=m_queue.count()){
                endCrossfade(false);clearSpare();setSleep(0);pause();emit toast("Sleep timer ended");return;}
              // An overlap runs right up to this moment. The song that was
              // leaving has now left, so the fade is simply over.
              if(m_crossfading){endCrossfade(true);return;}
              if (repeat() == 2) {
                m_serverListenToken=0;
                m_media().setPosition(0);
                m_media().play();
              } else if (!finishGapless())
                next();
            }
          });});
  eachDeck([this](QMediaPlayer *deck){connect(deck,&QMediaPlayer::errorOccurred,this,
          [this,deck](QMediaPlayer::Error err, const QString &) {
            // A spare that cannot load simply forfeits its head start.
            if(!isActive(*deck)){if(err!=QMediaPlayer::NoError)clearSpare();return;}
            if (err == QMediaPlayer::NoError || m_recovering)
              return;
            if(!current().value("localPath").toString().isEmpty()){
              m_resolving=false;m_wantPlay=false;notifyError("Could not play this local file. Check that it is readable and supported.","play");emit playbackChanged();return;
            }
            if (!current().isEmpty() && m_wantPlay && m_recoveryAttempts<2) {
              recoverStream(); return;
            }
            m_resolving=false; m_wantPlay=false;
            emit playbackChanged();
            notifyError("The audio stream was interrupted. Retry to reconnect.","play");
          });});
  m_onlineArtworkTimer.setSingleShot(true);
  m_onlineArtworkTimer.setTimerType(Qt::PreciseTimer);
  m_onlineArtworkTimer.setInterval(1000);
  connect(&m_onlineArtworkTimer,&QTimer::timeout,this,&Backend::fetchOnlineArtwork);
  connect(this,&Backend::trackChanged,this,[this]{updateOnlineArtwork();emit onlineArtworkChanged();});
  connect(this,&Backend::playbackChanged,this,&Backend::updateOnlineArtwork);
  connect(this,&Backend::settingsChanged,this,&Backend::updateOnlineArtwork);
  m_prepareTimer.setSingleShot(true); m_prepareUpdate.setSingleShot(true);
  connect(&m_prepareTimer,&QTimer::timeout,this,&Backend::updatePreparation);
  connect(&m_prepareUpdate,&QTimer::timeout,this,&Backend::updatePreparation);
  const auto schedule=[this]{m_prepareUpdate.start(0);};
  connect(this,&Backend::trackChanged,this,schedule);
  connect(this,&Backend::playbackChanged,this,schedule);
  connect(this,&Backend::settingsChanged,this,schedule);
  connect(this,&Backend::seeked,this,schedule);
  connect(&m_queue,&Entries::countChanged,this,schedule);
  connect(this,&Backend::libraryChanged,this,&Backend::updateLocalView);
  connect(this,&Backend::libraryChanged,this,[this]{if(m_page=="local" && !smartPlaylist(m_libraryId).isEmpty()){for(const auto &p:m_playlists)if(p.toMap().value("id")==m_libraryId){const auto rows=playlistRows(p.toMap());if(rows!=m_results.rows)m_results.assign(rows);}}if(m_page=="library"&&(m_libraryId.startsWith("mix-")||m_libraryId=="files")){const auto rows=libraryRows(m_libraryId);if(rows!=m_results.rows)m_results.assign(rows);}});
  connect(this,&Backend::catalogChanged,this,&Backend::presentationChanged);
  load();
  setupServer();
  setupFolderWatching();
}
Backend::~Backend() {
  m_portMonitor.kill();m_portProbe.kill();m_portMonitor.waitForFinished(500);m_portProbe.waitForFinished(500);
  m_notifier.clear();
  storeResumePosition();
  storeMeasuredLoudness();
  save();
  for (const auto &key : m_processes.keys())
    cancel(key);
}
void Backend::cancel(const QString &channel) {
  if(channel=="catalog"||channel=="lyrics")m_server.cancel(channel);
  if(channel=="play")m_server.cancel("audio");
  auto p = m_processes.take(channel);
  if (!p)
    return;
  p->disconnect(this);
  for(auto timer:p->findChildren<QTimer*>()) timer->stop();
  connect(p,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),p,&QObject::deleteLater);
  if(p->processId()>0) ::kill(-p->processId(),SIGKILL);
  if(p->state()==QProcess::Starting) connect(p,&QProcess::started,p,[p]{if(p->processId()>0)::kill(-p->processId(),SIGKILL);p->kill();});
  p->kill();
  if(p->state()==QProcess::NotRunning)p->deleteLater();
}
void Backend::request(const QString &channel, QVariantMap args, Callback done, std::shared_ptr<QTemporaryDir> lifetime) {
  cancel(channel);
  auto p = new QProcess(this);
  // Keep the unique directory alive until the worker has actually exited, even
  // when cancel() disconnects the backend callback while killing its process group.
  if(lifetime) connect(p,&QObject::destroyed,[lifetime]{});
  m_processes.insert(channel, p);
  p->setChildProcessModifier([] { ::setsid(); });
  QString helper = qEnvironmentVariable("SUNG_HELPER");
  if (helper.isEmpty())
    helper = QCoreApplication::applicationDirPath() + "/../helper/catalog.py";
  if (!QFile::exists(helper))
    helper = QCoreApplication::applicationDirPath() + "/../lib/sung/catalog.py";
  QString python = qEnvironmentVariable("SUNG_PYTHON");
  if (python.isEmpty()) {
    auto bundled =
        QCoreApplication::applicationDirPath() + "/../runtime/bin/python";
    if (!QFile::exists(bundled))
      bundled = QCoreApplication::applicationDirPath() +
                "/../lib/sung/runtime/bin/python";
    python = QFile::exists(bundled) ? bundled : QStringLiteral("python3");
  }
  auto timer = new QTimer(p);
  timer->setSingleShot(true);
  timer->setInterval((channel == "play" || channel == "prepare") ? 75000 : 45000);
  connect(timer, &QTimer::timeout, this, [this, channel, done] {
    cancel(channel);
    done({{"ok", false}, {"error", "Connection timed out. Try again."}});
  });
  connect(p, &QProcess::errorOccurred, this,
          [this, p, channel, done](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart)
              return;
            m_processes.remove(channel);
            done({{"ok", false},
                  {"error",
                   "YouTube helper could not start. Run scripts/setup.sh."}});
            p->deleteLater();
          });
  connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this, p, channel, done](int, QProcess::ExitStatus) {
            if (m_processes.value(channel) != p)
              return;
            m_processes.remove(channel);
            const auto bytes = p->readAllStandardOutput();
            const auto obj = QJsonDocument::fromJson(bytes).object();
            QVariantMap result = obj.toVariantMap();
            if (result.isEmpty())
              result = {{"ok", false},
                        {"error", "YouTube helper returned no data. Check the "
                                  "installation and connection."}};
            p->deleteLater();
            done(result);
          });
  p->start(python, {helper});
  p->write(QJsonDocument::fromVariant(args).toJson(QJsonDocument::Compact));
  p->closeWriteChannel();
  timer->start();
}
void Backend::notifyError(const QString &message, const QString &retryTarget) {
  m_retryTarget = retryTarget;
  m_error = message;
  m_error.remove(QRegularExpression("\\x1b\\[[0-9;]*m"));
  if (m_error.contains("Unable to download") ||
      m_error.contains("ConnectionError") || m_error.contains("timed out"))
    m_error =
        "Couldn’t connect to YouTube. Check your connection and try again.";
  else if (m_error.contains("Sign in") || m_error.contains("sign in") ||
           m_error.contains("bot"))
    m_error =
        "YouTube requires sign-in for this track. Import cookies in Settings.";
  else if (m_error.contains("not available") ||
           m_error.contains("Video unavailable"))
    m_error = "This track isn’t available. Try another upload.";
  m_error = m_error.left(350);
  emit errorChanged();
}
QVariantMap Backend::snapshot() const {
  return {{"viewKey",m_viewKey}, {"page", m_page},          {"title", m_title},
          {"cover", m_cover},        {"sections", m_sections},
          {"items", m_results.rows}, {"request", m_request},
          {"more", m_more},          {"library", m_libraryId},
          {"collectionQuery",m_collection.query()},{"collectionSort",m_collection.sortKey()}};
}
void Backend::restore(const QVariantMap &s) {
  beginView(s.value("viewKey",s.value("page")).toString());
  m_collection.setQuery(s.value("collectionQuery").toString());
  m_collection.setSortKey(s.value("collectionSort","original").toString());
  m_page = s.value("page").toString();
  m_title = s.value("title").toString();
  m_cover = s.value("cover").toString();
  m_sections = s.value("sections").toList();
  m_results.assign(s.value("items").toList());
  m_request = s.value("request").toMap();
  m_more = s.value("more").toBool();
  m_libraryId = s.value("library").toString();
  m_busy = false;
  emit catalogChanged();
}
void Backend::beginView(const QString &key) {
  if(key==m_viewKey)return;
  emit viewAboutToChange();
  m_viewOptions[m_viewKey]={{"query",m_collection.query()},{"sort",m_collection.sortKey()}};
  m_viewOrder.removeAll(m_viewKey);m_viewOrder.append(m_viewKey);
  while(m_viewOrder.size()>32)m_viewOptions.remove(m_viewOrder.takeFirst());
  m_viewKey=key;
  const auto options=m_viewOptions.value(key);
  m_collection.setQuery(options.value("query").toString());
  m_collection.setSortKey(options.value("sort","original").toString());
}
void Backend::navigate(const QString &page, const QString &title, bool push, const QString &key) {
  cancel("catalog");
  if (push) {
    m_back.append(snapshot());
    if (m_back.size() > 12)
      m_back.removeFirst();
  }
  beginView(key.isEmpty()?page:key);
  m_page = page;
  m_title = title;
  m_cover.clear();
  m_results.assign({});
  m_sections.clear();
  m_request.clear();
  m_more = false;
  m_busy = false;
  m_libraryId.clear();
  dismissError();
  emit catalogChanged();
}
void Backend::back() {
  if (m_back.isEmpty())
    return;
  cancel("catalog");
  auto last = m_back.takeLast().toMap();
  restore(last);
  dismissError();
  if(m_page=="local") {
    bool found=false;
    for(const auto &v:m_playlists)if(v.toMap().value("id")==m_libraryId){found=true;m_title=v.toMap().value("title").toString();m_cover=v.toMap().value("customCover").toString();m_results.assign(playlistRows(v.toMap()));}
    if(!found)library("playlists");
  } else if(m_page=="library") m_results.assign(libraryRows(m_libraryId));
  else if(m_page=="local-album" || m_page=="local-artist")updateLocalView();
  emit catalogChanged();
}
void Backend::startSearch() {
  if (m_page != "search")
    navigate("search", "Search");
}
void Backend::retry() {
  auto target = m_retryTarget;
  dismissError();
  if (target == "play") {
    m_wantPlay=true; m_stopped=false; m_recoveryAttempts=1;
    m_restorePosition=position(); m_streams.remove(current().value("videoId").toString());
    resolveCurrent(true);
  }
  else if (target == "catalog")
    refresh();
}
void Backend::home() { browseRequest({{"op", "home"}}, m_page != "home"); }
void Backend::search(const QString &query, const QString &filter) {
  if (query.trimmed().isEmpty())
    return;
  if (query.trimmed().startsWith("https://") || query.trimmed().startsWith("http://")) {
    openLink(query.trimmed());
    return;
  }
  rememberSearch(query);
  browseRequest({{"op", "search"},
                 {"query", query.trimmed()},
                 {"filter", filter},
                 {"limit", 30}},
                m_page != "search");
}
void Backend::browseRequest(QVariantMap req, bool push) {
  auto op = req.value("op").toString();
  QString label =
      req.value("title",
                op == "home" ? "Listen" : req.value("query", "Loading…"))
          .toString();
  const bool extend=!push && op==m_page && req.value("limit").toInt()>m_request.value("limit").toInt();
  const auto key=op=="search" ? "search:"+req.value("query").toString()+":"+req.value("filter").toString() : op+":"+req.value("id",req.value("url")).toString();
  if(push || op!=m_page) navigate(op,label,push,key);
  else {beginView(key);cancel("catalog");dismissError();}
  m_request = req;
  m_busy = true;
  emit catalogChanged();
  request("catalog", req, [this, op, extend](const QVariantMap &data) {
    m_busy = false;
    if (!data.value("ok").toBool()) {
      notifyError(data.value("error").toString(), "catalog");
      emit catalogChanged();
      return;
    }
    const auto incoming=data.value("items").toList();
    bool prefix=extend && incoming.size()>=m_results.count();
    for(int i=0;prefix && i<m_results.count();++i) prefix=itemId(incoming[i])==itemId(m_results.rows[i]);
    if(prefix)m_results.append(incoming.mid(m_results.count())); else m_results.assign(incoming);
    m_sections = data.value("sections").toList();
    if (data.contains("title"))
      m_title = data.value("title").toString();
    m_cover = data.value("art").toString();
    if(op=="album"){m_request["artist"]=data.value("artist");m_request["year"]=data.value("year");}
    const auto limit = m_request.value("limit", 30).toInt();
    m_more = (op == "search" && m_results.count() >= limit && limit < 200) ||
             (op == "playlist" &&
              data.value("total").toInt() > m_results.count() && limit < 5000);
    emit catalogChanged();
  });
}
void Backend::more() {
  if(m_page=="server"){if(m_more&&!m_busy){auto req=m_request;req["offset"]=m_results.count();serverBrowseRequest(req,false,true);}return;}
  if (!m_more || m_busy)
    return;
  auto req = m_request;
  req["limit"] =
      req.value("limit", 30).toInt() + (m_page == "search" ? 30 : 100);
  browseRequest(req, false);
}
void Backend::refresh() {
  if(m_page=="local-album" || m_page=="local-artist"){updateLocalView();return;}
  if(m_page=="server"){auto req=m_request;req["offset"]=0;serverBrowseRequest(req,false);return;}
  if (!m_request.isEmpty())
    browseRequest(m_request, false);
  else if (m_page == "library")
    library(m_libraryId);
}
void Backend::clearListPane() {
  if(m_listPaneId.isEmpty() && m_listPane.rows.isEmpty())return;
  m_listPaneId.clear();m_listPaneTitle.clear();m_listPane.assign({});emit listPaneChanged();
}
void Backend::rememberListPane(const QString &title,const QString &id,const QVariantList &rows) {
  if(rows.isEmpty()){clearListPane();return;}
  m_listPaneTitle=title;m_listPaneId=id;m_listPane.assign(rows);emit listPaneChanged();
}
void Backend::open(const QVariantMap &item) {
  // A detail opened out of a library grid keeps that grid beside it. The grid
  // it came from is whatever is on screen now, which the one collection is
  // about to stop holding.
  const auto kindOpening=item.value("kind").toString();
  const bool fromGrid=(m_page=="library" && (m_libraryId=="local-albums" || m_libraryId=="local-artists"))
                      && (kindOpening=="local-album" || kindOpening=="local-artist");
  // Choosing another entry out of the pane is browsing within that list, not
  // leaving it, so the pane stays where it is.
  const bool withinPane=!m_listPaneId.isEmpty() &&
      ((m_listPaneId=="local-albums" && kindOpening=="local-album") ||
       (m_listPaneId=="local-artists" && kindOpening=="local-artist"));
  if(fromGrid)rememberListPane(m_libraryId=="local-albums"?"Albums":"Artists",m_libraryId,m_results.rows);
  else if(!withinPane && kindOpening!="song" && kindOpening!="video")clearListPane();
  if(item.value("kind")=="local-album" || item.value("kind")=="local-artist"){openLocalGroup(item);return;}

  if(isServerSource(item.value("source"))){
    if(item.value("kind")=="song"){playItem(item);return;}
    if(!m_server.owns(item)){notifyError("Connect to this item’s server in Settings.");return;}
    serverBrowseRequest({{"mode",item.value("kind")},{"remoteId",item.value("remoteId")},{"genre",item.value("title")},{"title",item.value("title")},{"art",item.value("art")},{"editable",item.value("editable")}});return;
  }
  auto kind = item.value("kind").toString();
  if(kind=="smart"){library(item.value("id").toString());return;}
  if(kind=="local"){openPlaylist(item.value("id").toString());return;}
  if (kind == "song" || kind == "video") {
    playItem(item);
    return;
  }
  auto op = kind == "artist"  ? "artist"
            : kind == "album" ? "album"
                              : "playlist";
  auto id = item.value("browseId", item.value("id")).toString();
  if (id.isEmpty())
    return;
  browseRequest(
      {{"op", op}, {"id", id}, {"title", item.value("title")}, {"limit", 100}});
}
void Backend::openLink(const QString &url) {
  browseRequest(
      {{"op", "link"}, {"url", url.trimmed()}, {"title", "YouTube Music"}});
}
// The song playing now is at the head of history; what the queue panel looks
// back over is everything before it.
void Backend::refreshRecentlyPlayed() {
  const auto playingId=current().value("id").toString();
  QVariantList recent;
  for(const auto &v:m_history){
    if(!playingId.isEmpty() && itemId(v)==playingId)continue;
    recent.append(v);
    if(recent.size()>=60)break;
  }
  m_recent.reconcile(recent);
}
QVariantList Backend::playable(const QVariantList &items) {
  QVariantList r;
  for (const auto &i : items) {
    const auto t = i.toMap();
    if (((isServerSource(t.value("source")) && t.value("kind")=="song" && !t.value("remoteId").toString().isEmpty() && !t.value("server").toString().isEmpty()) || !t.value("videoId").toString().isEmpty() || (t.value("id").toString().startsWith("local_") && QDir::isAbsolutePath(t.value("localPath").toString()))) &&
        t.value("available", true).toBool())
      r.append(t);
  }
  return r;
}
void Backend::playResults(int index) {
  if(index<0 || index>=m_results.count())return;
  invalidateUndo("queue");
  const auto target = m_results.get(index);
  auto items = playable(m_results.rows);
  if (items.isEmpty())
    return;
  int actual = 0;
  for (int i = 0; i < items.size(); ++i)
    if (itemId(items[i]) == target.value("id").toString()) {
      actual = i;
      break;
    }
  m_queue.reconcile(queueWithOrigin(items,"collection"));
  playAt(actual);
}
void Backend::cancelCoverPlay() {
  ++m_coverPlayToken;cancel("coverplay");m_server.cancel("coverplay");
  if(!m_coverPlayId.isEmpty()){m_coverPlayId.clear();emit playbackChanged();}
}
void Backend::playCover(const QVariantMap &item) {
  if(!playable({item}).isEmpty()){playItem(item);return;}
  const auto kind=item.value("kind").toString();
  if(!QStringList{"album","playlist","local","local-album","local-artist"}.contains(kind))return;
  cancelCoverPlay();
  const auto token=m_coverPlayToken;
  m_coverPlayId=item.value("browseId",item.value("id")).toString();
  if(m_coverPlayId.isEmpty())return;
  emit playbackChanged();
  auto finish=[this,token](const QVariantList &rows,const QString &error) {
    if(token!=m_coverPlayToken)return;
    m_coverPlayId.clear();emit playbackChanged();
    if(!error.isEmpty()){notifyError(error);return;}
    const auto songs=playable(rows);
    if(songs.isEmpty()){emit toast("No playable songs in this collection");return;}
    invalidateUndo("queue");m_queue.reconcile(queueWithOrigin(songs,"collection"));playAt(0);
  };
  if(kind=="local-album" || kind=="local-artist"){finish(localGroupRows(item),{});return;}
  if(kind=="local"){
    for(const auto &p:m_playlists)if(p.toMap().value("id")==item.value("id")){finish(playlistRows(p.toMap()),{});return;}
    finish({},"This playlist is no longer available.");return;
  }
  if(isServerSource(item.value("source"))){
    if(!m_server.owns(item)){finish({},"Connect to this item’s server in Settings.");return;}
    m_server.browse({{"mode",kind},{"remoteId",item.value("remoteId")}},[finish](const QVariantMap &data,const QString &error){finish(data.value("items").toList(),error);},"coverplay");return;
  }
  request("coverplay",{{"op",kind},{"id",m_coverPlayId},{"limit",5000}},[finish](const QVariantMap &data){finish(data.value("items").toList(),data.value("ok").toBool()?QString():data.value("error","Could not load this collection.").toString());});
}
void Backend::playItem(const QVariantMap &item) {
  if (playable({item}).isEmpty())
    return;
  invalidateUndo("queue");
  m_queue.reconcile(queueWithOrigin({item},"manual"));
  playAt(0);
}
void Backend::playAt(int index, int direction) {
  cancelCoverPlay();
  if (index < 0 || index >= m_queue.count())
    return;
  // Steering by hand cancels any handover already under way.
  endCrossfade(false);
  clearSpare();
  storeResumePosition();
  invalidateUndo("queue-order");
  if(m_sleepAtEnd)setSleep(0);
  m_wantPlay = true; m_stopped=false; m_recoveryAttempts=0; m_recovering=false; ++m_trackToken; m_savedPosition=0;
  cancel("play");
  cancel("linkplay");
  cancel("lyrics");
  cancel("radio");
  m_media().stop();
  m_media().setSource(QUrl());
  m_playbackDirection=direction? (direction<0?-1:1):(index<m_index?-1:1);
  m_index = index;
  if(resumeLongTracks()&&longRecording(current()))m_savedPosition=resumePosition(current().value("id").toString());
  dismissError();
  m_retry = false;
  m_restorePosition = 0;
  m_lyricsLoaded=false;m_lyricsSource.clear();m_lyrics.clear();m_lyricLines.clear();
  m_lyricsBusy = false;
  emit lyricsChanged();
  emit trackChanged();
  emit libraryChanged();
  resolveCurrent();
  m_saveTimer.start();
}
void Backend::restorePlaybackPosition() {
  if(m_restorePosition<=0 || !m_media().isSeekable() || m_media().playbackState()==QMediaPlayer::StoppedState)return;
  const auto target=m_restorePosition;
  m_restorePosition=0;
  m_savedPosition=target;
  m_media().setPosition(target);
  m_resolving=false;
  emit positionChanged();emit playbackChanged();
}
void Backend::recoverStream() {
  if(m_recovering)return;
  m_recovering=true;
  m_restorePosition=qMax(m_restorePosition,position());
  m_resolving=true;
  emit playbackChanged();
  ++m_recoveryAttempts;
  m_streams.remove(current().value("videoId").toString());
  // A kept copy that will not decode has to go with it. The retry below asks
  // the store for nothing, so without this the same bad file would be handed
  // back the next time the song came round.
  const auto failed=current();
  m_offline.forget(isServerSource(failed.value("source"))
                       ? OfflineStore::keyFor(failed,QString::number(m_server.bitrate()))
                       : OfflineStore::keyFor(failed,streamingQuality()));
  // Defer resetting the media source until the decoder's error callback unwinds.
  const auto token=m_trackToken;
  QTimer::singleShot(0,this,[this,token]{if(m_wantPlay && token==m_trackToken){m_media().stop();m_media().setSource({});resolveCurrent(true);}});
}
void Backend::resolveCurrent(bool retry) {
  if(isServerSource(current().value("source"))){
    cancelPreparation();cancel("play");m_recovering=false;m_resolving=true;
    if(!retry&&m_savedPosition>0)m_restorePosition=m_savedPosition;
    // A song that was kept the last time it played is a local file now: no
    // server, no network, no wait for a buffer.
    const auto key=OfflineStore::keyFor(current(),QString::number(m_server.bitrate()));
    if(!retry){
      const auto kept=m_offline.take(key);
      if(!kept.isEmpty()){
        m_audioCache.reset();m_resolving=false;
        m_media().setSource(QUrl::fromLocalFile(kept));
        if(m_wantPlay)m_media().play();
        emit playbackChanged();return;
      }
    }
    m_audioCache=audioDirectory();
    if(!m_audioCache||!m_audioCache->isValid()){m_resolving=false;m_wantPlay=false;notifyError("Could not create the audio buffer.");emit playbackChanged();return;}
    const auto token=m_trackToken;const auto directory=m_audioCache;
    m_server.download(current(),directory->path()+"/song",[this,token,directory,key](const QVariantMap &data,const QString &error){
      if(token!=m_trackToken)return;
      if(!error.isEmpty()){m_resolving=false;m_wantPlay=false;notifyError(error,"play");emit playbackChanged();return;}
      auto file=data.value("file").toString();
      const auto kept=m_offline.keep(key,file);
      if(!kept.isEmpty())file=kept;
      m_media().setSource(QUrl::fromLocalFile(file));if(m_wantPlay)m_media().play();emit playbackChanged();
    });emit playbackChanged();return;
  }
  if(!current().value("localPath").toString().isEmpty()) {
    cancelPreparation();m_audioCache.reset();m_recovering=false;m_resolving=false;
    const QFileInfo file(current().value("localPath").toString());
    if(!file.isFile()||!file.isReadable()){m_wantPlay=false;notifyError("Local file is missing or unreadable. Locate it from the song menu.","play");emit playbackChanged();return;}
    if(!retry&&m_savedPosition>0)m_restorePosition=m_savedPosition;
    m_media().setSource(QUrl::fromLocalFile(file.absoluteFilePath()));
    if(m_wantPlay)m_media().play();
    emit playbackChanged();return;
  }
  auto id = current().value("videoId").toString();
  if (id.isEmpty())
    return;
  const auto offlineKey = OfflineStore::keyFor(current(), streamingQuality());
  if (!retry) {
    const auto kept = m_offline.take(offlineKey);
    if (!kept.isEmpty()) {
      cancelPreparation();
      m_recovering = false;
      m_resolving = false;
      m_stopped = false;
      m_recoveryAttempts = 0;
      if (m_media().source().isEmpty() && m_savedPosition > 0)
        m_restorePosition = m_savedPosition;
      m_media().setSource(QUrl::fromLocalFile(kept));
      if (m_wantPlay)
        m_media().play();
      emit playbackChanged();
      return;
    }
  }
  m_retry = retry;
  m_stopped=false;
  m_resolving = true;
  emit playbackChanged();
  auto apply = [this, id, offlineKey](const QVariantMap &data) {
    if (current().value("videoId").toString() != id)
      return;
    if (!data.value("ok").toBool()) {
      m_recovering=false;
      if(m_wantPlay && m_recoveryAttempts==1){recoverStream();return;}
      m_resolving = false;
      m_wantPlay = false;
      notifyError(data.value("error").toString(), "play");
      emit playbackChanged();
      return;
    }
    if(!data.contains("file"))m_streams[id] = data;
    if (m_streams.size() > 5) {
      auto keys = m_streams.keys();
      for (const auto &k : keys)
        if (k != id) {
          m_streams.remove(k);
          break;
        }
    }
    auto url = QUrl(data.value("url").toString());
    auto file=data.value("file").toString();
    if(!file.isEmpty() && m_audioCache && QFileInfo(file).isFile() && QFileInfo(file).canonicalPath()==QFileInfo(m_audioCache->path()).canonicalFilePath()){
      // The buffer that was about to be thrown away is moved into the store
      // instead, and played from there. Failing to keep it is not a reason to
      // fail to play it, so the buffered file stands in.
      const auto kept=m_offline.keep(offlineKey,file);
      if(!kept.isEmpty())file=kept;
      url=QUrl::fromLocalFile(file);
    }
    if (url.scheme() != "https" && !url.isLocalFile()) {
      m_resolving = false;
      notifyError("The stream URL is invalid.");
      emit playbackChanged();
      return;
    }
    if(m_media().source()==url)m_media().setSource({});
    m_media().setSource(url);
    if (m_wantPlay)
      m_media().play();
  };
  if(!retry && m_preparedId==id && !m_preparedData.isEmpty()) {
    m_audioCache=std::move(m_preparedDirectory);const auto data=m_preparedData;
    m_preparedData.clear();m_preparedId.clear();apply(data);return;
  }
  cancelPreparation();
  auto cached = m_streams.value(id);
  if (!retry && !cached.isEmpty()) {
    const auto expire = QUrlQuery(QUrl(cached.value("url").toString()))
                            .queryItemValue("expire")
                            .toLongLong();
    if (expire > QDateTime::currentSecsSinceEpoch() + 120) {
      apply(cached);
      return;
    }
  }
  if(!retry && m_media().source().isEmpty() && m_savedPosition>0)m_restorePosition=m_savedPosition;
  m_audioCache=audioDirectory();
  if(!m_audioCache||!m_audioCache->isValid()){m_resolving=false;m_wantPlay=false;notifyError("Could not create the audio buffer.");emit playbackChanged();return;}
  request("play", {{"op", "buffer"}, {"id", id}, {"cookies", cookies()}, {"quality", streamingQuality()},
                    {"fallback",m_recoveryAttempts>0},{"directory",m_audioCache->path()}},
          apply,m_audioCache);
}
void Backend::enqueue(const QVariantMap &item, bool next) {
  if (playable({item}).isEmpty())
    return;
  invalidateUndo("queue");
  auto q = m_queue.rows;
  q.insert(next ? qBound(0, m_index + 1, int(q.size())) : q.size(), queueWithOrigin({item},"manual").first());
  m_queue.reconcile(q);
  m_saveTimer.start();
  emit toast(next ? "Playing next" : "Added to queue");
}
void Backend::enqueueResults() {
  invalidateUndo("queue");
  auto q = m_queue.rows;
  q.append(queueWithOrigin(playable(m_results.rows),"manual"));
  m_queue.reconcile(q);
  m_saveTimer.start();
  emit toast("Added to queue");
}
void Backend::moveQueue(int from, int to) {
  if (from < 0 || to < 0 || from >= m_queue.count() || to >= m_queue.count() ||
      from == to)
    return;
  invalidateUndo("queue");
  auto q = m_queue.rows;
  q.move(from, to);
  if (m_index == from)
    m_index = to;
  else if (from < m_index && to >= m_index)
    --m_index;
  else if (from > m_index && to <= m_index)
    ++m_index;
  m_queue.reconcile(q);
  emit trackChanged();
  m_saveTimer.start();
}
void Backend::removeQueue(int i) {
  if (i < 0 || i >= m_queue.count())
    return;
  m_undoType="queue"; m_undoRows=m_queue.rows; m_undoIndex=m_index; m_undoMessage="Removed from queue";
  auto q = m_queue.rows;
  q.removeAt(i);
  bool currentRemoved = i == m_index;
  bool wasPlaying = playing() || m_resolving;
  if (i < m_index)
    --m_index;
  m_queue.reconcile(q);
  if (currentRemoved) {
    stop();
    ++m_trackToken; clearLyrics();
    m_index = q.isEmpty() ? -1 : qMin(i, int(q.size() - 1));
    if (wasPlaying && m_index >= 0)
      playAt(m_index);
  }
  emit trackChanged();
  emit libraryChanged();
  m_saveTimer.start();
  emit toast(m_undoMessage);
}
void Backend::clearQueue() {
  if(m_queue.count()==0)return;
  if (m_queue.count() > 0) {
    m_undoType = "queue";
    m_undoRows = m_queue.rows;
    m_undoIndex = m_index;
    m_undoMessage = "Queue cleared";
  }
  stop();
  m_index = -1;
  clearLyrics();
  m_queue.reconcile({});
  emit trackChanged();
  emit libraryChanged();
  m_saveTimer.start();
  if (!m_undoMessage.isEmpty()) emit toast(m_undoMessage);
}
void Backend::smartShuffleQueue() {
  const int first=qMax(0,m_index+1);if(m_queue.count()-first<2)return;
  auto artistKey=[](const QVariantMap &t){const auto artist=t.value("artist").toString().simplified().toCaseFolded();return artist.isEmpty()?t.value("id").toString():artist;};
  QHash<QString,QVariantList> artists;
  for(int i=first;i<m_queue.count();++i){const auto t=m_queue.get(i);artists[artistKey(t)].append(t);}
  auto keys=artists.keys();std::shuffle(keys.begin(),keys.end(),*QRandomGenerator::global());
  std::priority_queue<std::pair<int,int>> pending;
  for(int i=0;i<keys.size();++i){auto &items=artists[keys[i]];std::shuffle(items.begin(),items.end(),*QRandomGenerator::global());pending.emplace(items.size(),i);}
  auto ordered=m_queue.rows.mid(0,first);auto previous=artistKey(current());
  while(!pending.empty()){
    auto chosen=pending.top();pending.pop();
    if(keys[chosen.second]==previous&&!pending.empty()){auto other=pending.top();pending.pop();pending.push(chosen);chosen=other;}
    auto &items=artists[keys[chosen.second]];ordered.append(items.takeLast());previous=keys[chosen.second];
    if(!items.isEmpty())pending.emplace(items.size(),chosen.second);
  }
  m_undoType="queue-order";m_undoRows=m_queue.rows;m_undoIndex=m_index;m_undoShuffle=shuffle();m_undoMessage="Upcoming songs shuffled";
  setShuffle(false);m_queue.reconcile(ordered);m_saveTimer.start();emit libraryChanged();emit toast(m_undoMessage);
}
void Backend::next() {
  if (m_queue.count() == 0)
    return;
  if (shuffle() && m_queue.count() > 1) {
    int i = m_index;
    while (i == m_index)
      i = QRandomGenerator::global()->bounded(m_queue.count());
    playAt(i,1);
    return;
  }
  if (m_index + 1 < m_queue.count()) {
    playAt(m_index + 1,1);
    return;
  }
  if (repeat() == 1) {
    playAt(0,1);
    return;
  }
  if (autoplay() && !current().value("videoId").toString().isEmpty()) {
    m_resolving = true;
    emit playbackChanged();
    const QString id = current().value("id").toString();
    request("radio", {{"op", "radio"}, {"id", id}},
            [this, id](const QVariantMap &data) {
              m_resolving = false;
              emit playbackChanged();
              if (id != current().value("id").toString())
                return;
              if (!data.value("ok").toBool()) {
                notifyError(data.value("error").toString());
                return;
              }
              auto q = m_queue.rows;
              auto rows = playable(data.value("items").toList());
              for (const auto &t : rows) {
                bool exists = false;
                for (const auto &old : q)
                  if (itemId(old) == itemId(t)) {
                    exists = true;
                    break;
                  }
                if (!exists)
                  q.append(queueWithOrigin({t},"autoplay").first());
              }
              m_queue.reconcile(q);
              if (m_index + 1 < q.size())
                playAt(m_index + 1,1);
              else
                pause();
            });
  } else
    pause();
}
void Backend::previous() {
  if (position() > 3000) {
    seek(0);
    return;
  }
  if (m_index > 0)
    playAt(m_index - 1,-1);
  else
    seek(0);
}
void Backend::toggle() {
  if (playing() || m_resolving)
    pause();
  else
    play();
}
void Backend::play() {
  if (m_queue.count() == 0)
    return;
  m_wantPlay = true; m_stopped=false;
  if (m_queue.count() == 0)
    return;
  if (m_index < 0)
    playAt(0);
  else if (m_media().source().isEmpty() ||
           m_media().error() != QMediaPlayer::NoError)
    resolveCurrent();
  else
    m_media().play();
}
void Backend::pause() {
  cancelCoverPlay();
  cancel("linkplay");
  m_recovering=false;
  m_savedPosition=position();
  storeResumePosition();
  m_wantPlay = false;
  if (m_resolving) {
    cancel("play");
    cancel("radio");
    m_resolving = false;
    emit playbackChanged();
  }
  m_media().pause();
}
void Backend::stop() {
  cancelCoverPlay();
  endCrossfade(false);
  clearSpare();
  if(m_sleepAtEnd)setSleep(0);
  cancel("linkplay");
  m_recovering=false;
  m_stopped=true; m_savedPosition=0; m_restorePosition=0;
  clearLyrics();
  m_wantPlay = false;
  cancel("play");
  cancel("radio");
  m_resolving = false;
  m_media().stop();
  m_media().setSource(QUrl());
  emit playbackChanged();
}
void Backend::seek(qint64 p) {
  const auto target=qBound<qint64>(0,p,duration());
  // Seeking out of the tail takes back the head start the next song was given.
  if(m_crossfading || m_handoffIndex>=0){
    const auto total=duration();
    const qint64 window=qMax(qint64(crossfadeSeconds())*1000,qint64(2500));
    if(total<=0 || total-target>window){endCrossfade(false);clearSpare();}
  }
  if(m_media().source().isEmpty()){m_savedPosition=target;emit positionChanged();}
  else m_media().setPosition(target);
  emit positionChanged();
  emit seeked(target);
}
void Backend::radio(const QVariantMap &item) {
  auto id = item.value("videoId").toString();
  if (id.isEmpty())
    return;
  playItem(item);
  request("radio", {{"op", "radio"}, {"id", id}},
          [this, id](const QVariantMap &data) {
            if (id != current().value("id").toString())
              return;
            if (!data.value("ok").toBool()) {
              notifyError(data.value("error").toString());
              return;
            }
            auto q = m_queue.rows;
            for (const auto &t : playable(data.value("items").toList()))
              if (itemId(t) != id)
                q.append(queueWithOrigin({t},"autoplay").first());
            m_queue.reconcile(q);
            m_saveTimer.start();
            emit toast("Radio started");
          });
}
void Backend::applyLyrics(const QVariantMap &data) {
  m_lyricsBusy=false;m_lyricsLoaded=data.value("ok").toBool();
  m_lyricLines=data.contains("lrc")?Lrc::parse(data.value("lrc").toString(),duration()):data.value("lines").toList();
  m_lyrics=m_lyricLines.isEmpty()?data.value("lyrics").toString():Lrc::plain(m_lyricLines);
  m_lyricsSource=m_lyrics.isEmpty()?QString():data.value("source","YouTube").toString();
  emit positionChanged();emit lyricsChanged();
}
void Backend::fetchLyrics() {
  if(current().isEmpty()||m_lyricsBusy||m_lyricsLoaded)return;
  const auto id=current().value("id").toString();
  static const QRegularExpression valid("^(?:[A-Za-z0-9_-]{11}|(?:local_|sub_|jf_)[a-f0-9]{64})$");
  if(valid.match(id).hasMatch()) {
    QFile file(dataPath()+"/lyrics/"+id+".lrc");
    if(file.size()<=262144&&file.open(QIODevice::ReadOnly)) {
      const auto lrc=QString::fromUtf8(file.readAll());
      if(!Lrc::parse(lrc).isEmpty()){applyLyrics({{"ok",true},{"lrc",lrc},{"source","Imported LRC"}});return;}
    }
  }
  const auto local=current().value("localPath").toString();
  if(!local.isEmpty()) {
    const QFileInfo music(local);QFile sidecar(music.path()+"/"+music.completeBaseName()+".lrc");
    if(sidecar.size()<=262144&&sidecar.open(QIODevice::ReadOnly)){const auto text=QString::fromUtf8(sidecar.read(262145));if(!Lrc::parse(text).isEmpty()){applyLyrics({{"ok",true},{"lrc",text},{"source","Local LRC"}});return;}}
  }
  m_lyricsBusy=true;emit lyricsChanged();const auto token=m_trackToken;
  if(isServerSource(current().value("source"))){
    m_server.lyrics(current(),[this,token](const QVariantMap &data,const QString &error){if(token!=m_trackToken)return;applyLyrics(data);if(!error.isEmpty())notifyError(error);});return;
  }
  request("lyrics",{{"op",local.isEmpty()?"lyrics":"local-lyrics"},{"id",id},{"title",current().value("title")},{"artist",current().value("artist")},{"album",current().value("album")},{"seconds",duration()/1000},{"fallback",lyricsFallback()},{"lyricCache",QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/lyrics"}},[this,id,token](const QVariantMap &data){
    if(id!=current().value("id").toString()||token!=m_trackToken)return;
    applyLyrics(data);if(!data.value("ok").toBool())notifyError(data.value("error").toString());
  });
}
void Backend::reloadLyrics(){clearLyrics();fetchLyrics();}
void Backend::setLyricsFallback(bool enabled){m_settings.setValue("lyricsFallback",enabled);emit settingsChanged();reloadLyrics();}
void Backend::importLyrics(const QUrl &url,const QString &songId){
  static const QRegularExpression valid("^(?:[A-Za-z0-9_-]{11}|(?:local_|sub_|jf_)[a-f0-9]{64})$");
  if(!url.isLocalFile()||!valid.match(songId).hasMatch())return;
  QFile input(url.toLocalFile());
  if(input.size()>262144||!input.open(QIODevice::ReadOnly)){notifyError("Choose an LRC file smaller than 256 KiB.");return;}
  const auto bytes=input.read(262145);
  if(bytes.size()>262144||Lrc::parse(QString::fromUtf8(bytes)).isEmpty()){notifyError("This file has no valid timed lyrics.");return;}
  const QString path=dataPath()+"/lyrics";QDir().mkpath(path);QDir directory(path);
  qint64 size=0;for(const auto &file:directory.entryInfoList({"*.lrc"},QDir::Files))if(file.fileName()!=songId+".lrc")size+=file.size();
  if(size+bytes.size()>8*1024*1024){notifyError("Imported lyrics have reached the 8 MiB limit. Remove an imported lyric first.");return;}
  QSaveFile output(path+"/"+songId+".lrc");
  if(!output.open(QIODevice::WriteOnly)){notifyError("Could not save lyrics.");return;}
  output.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
  if(output.write(bytes)!=bytes.size()||!output.commit()){notifyError("Could not save lyrics.");return;}
  if(current().value("id").toString()==songId)reloadLyrics();
  emit toast("Lyrics imported");
}
void Backend::resetLyrics(){
  const auto id=current().value("id").toString();
  static const QRegularExpression valid("^(?:[A-Za-z0-9_-]{11}|(?:local_|sub_|jf_)[a-f0-9]{64})$");
  if(valid.match(id).hasMatch()) {QFile f(dataPath()+"/lyrics/"+id+".lrc");if(f.exists()&&!f.remove()){notifyError("Could not remove imported lyrics.");return;}}
  reloadLyrics();
}
void Backend::setVolume(double v) {
  v = qBound(0.0, v, 1.0);
  m_userVolume=v;
  applyOutputVolume();
  m_settings.setValue("volume", v);
  emit settingsChanged();
}
void Backend::applyOutputVolume() {
  // The mixer cannot exceed full scale, so a positive normalization gain only
  // reaches quiet recordings while the user keeps headroom of their own.
  // While an overlap is running the fade owns both mixers; it will pick the
  // new level up on its next step.
  if(!m_crossfading)activeAudio().setVolume(settledVolume());
}
// ReplayGain stores the correction directly; R128 stores Q7.8 units against
// -23 LUFS, which this converts to the same -18 dBFS reference.
double Backend::trackGainDb(const QVariantMap &track) {
  bool ok=false;
  for(const auto &key:{"replaygainTrackGain","replaygainAlbumGain"}) {
    const auto raw=track.value(key).toString().trimmed();
    if(raw.isEmpty())continue;
    // Tags are written as "-7.25 dB" or plain "-7.25".
    const double value=raw.split(' ').first().toDouble(&ok);
    if(ok)return value;
  }
  const auto r128=track.value("r128TrackGain").toString().trimmed();
  if(!r128.isEmpty()){const double value=r128.toDouble(&ok);if(ok)return value/256.0+5.0;}
  return qQNaN();
}
double Backend::measuredLoudness(const QString &id) const {
  const auto value=m_settings.value("loudness/"+id);
  return value.isValid()?value.toDouble():qQNaN();
}
// One bounded, settings-backed cache per prefix. An invalid value forgets the
// entry; otherwise the oldest is dropped once the cache is full.
void Backend::rememberBounded(const QString &prefix,const QString &id,const QVariant &value,int limit) {
  if(id.isEmpty())return;
  auto order=m_settings.value(prefix+"Order").toStringList();
  order.removeAll(id);
  if(value.isValid()){order.append(id);m_settings.setValue(prefix+"/"+id,value);}
  else m_settings.remove(prefix+"/"+id);
  while(order.size()>limit)m_settings.remove(prefix+"/"+order.takeFirst());
  m_settings.setValue(prefix+"Order",order);
}
void Backend::storeMeasuredLoudness() {
  if(m_loudnessTrack.isEmpty()||!m_loudness.ready())return;
  rememberBounded("loudness",m_loudnessTrack,m_loudness.levelDb(),2000);
}
double Backend::trackTrim(const QString &id) const {
  if(id.isEmpty())return 0;
  return qBound(-12.0,m_settings.value("trim/"+id,0.0).toDouble(),12.0);
}
void Backend::setTrackTrim(const QString &id,double decibels) {
  if(id.isEmpty())return;
  decibels=qBound(-12.0,decibels,12.0);
  // Rounded so a slider cannot fill the cache with imperceptible neighbours.
  decibels=qRound(decibels*2)/2.0;
  if(qFuzzyCompare(decibels+1,trackTrim(id)+1))return;
  rememberBounded("trim",id,qFuzzyIsNull(decibels)?QVariant():QVariant(decibels),2000);
  if(id==current().value("id").toString())updateNormalization();
  emit libraryChanged();
}
// Long recordings are the ones worth returning to: mixes, sets and live shows.
bool Backend::longRecording(const QVariantMap &track) {
  return track.value("seconds").toInt()>=1200;
}
qint64 Backend::resumePosition(const QString &id) const {
  return id.isEmpty()?0:m_settings.value("resume/"+id,0).toLongLong();
}
void Backend::clearResumePosition(const QString &id) {
  if(id.isEmpty()||!m_settings.contains("resume/"+id))return;
  rememberBounded("resume",id,QVariant(),400);
}
void Backend::storeResumePosition() {
  const auto track=current();const auto id=track.value("id").toString();
  if(id.isEmpty()||!longRecording(track))return;
  const auto at=position(),total=duration();
  // Near either end there is nothing worth returning to.
  if(at<60000||(total>0&&at>total-30000)){clearResumePosition(id);return;}
  if(!resumeLongTracks())return;
  rememberBounded("resume",id,at,400);
}
void Backend::setResumeLongTracks(bool enabled) {
  if(resumeLongTracks()==enabled)return;
  m_settings.setValue("resumeLongTracks",enabled);
  emit settingsChanged();
}
void Backend::updateNormalization() {
  const auto id=current().value("id").toString();
  if(id!=m_loudnessTrack){storeMeasuredLoudness();m_loudness.reset();m_loudnessTrack=id;}
  double gain=0;QString source;
  if(volumeNormalization()&&!id.isEmpty()) {
    const double tag=trackGainDb(current());
    const double measured=measuredLoudness(id);
    if(!qIsNaN(tag)){gain=tag;source="Track tag";}
    else if(!qIsNaN(measured)){gain=-18.0-measured;source="Measured";}
  }
  // Boosting is limited because the mixer cannot amplify past full scale.
  gain=qBound(-15.0,gain,6.0);
  // A hand-set trim applies whether or not levelling is switched on.
  const double trim=trackTrim(id);
  const bool idle=source.isEmpty()&&qFuzzyIsNull(trim);
  const double total=qBound(-15.0,gain+trim,6.0);
  const double factor=idle?1.0:std::pow(10.0,total/20.0);
  if(qFuzzyCompare(factor+1,m_normalizationGain+1)&&source==m_normalizationSource&&qFuzzyCompare(trim+1,m_trim+1))return;
  m_normalizationGain=factor;m_normalizationDb=idle?0.0:total;m_normalizationSource=source;m_trim=trim;
  applyOutputVolume();
  emit normalizationChanged();
}
void Backend::setVolumeNormalization(bool enabled) {
  if(volumeNormalization()==enabled)return;
  m_settings.setValue("volumeNormalization",enabled);
  updateNormalization();
  emit settingsChanged();
}
QStringList Backend::shapeNames() const { return m3::shapeNames(); }
QVariantList Backend::shapeOutline(const QString &name,int steps) const {
  QVariantList out;for(double radius:m3::shapeOutline(name,steps))out.append(radius);return out;
}
QVariantMap Backend::motionSprings(bool expressive) const { return m3::motionScheme(expressive); }
QVariantMap Backend::colorScheme(const QColor &source,bool dark) const {
  if(!source.isValid())return {};
  const auto variant=colorVariant();const double contrast=colorContrast();
  const QPair<QRgb,QString> key{source.rgb(),
    QString::number(dark)+variant+QString::number(contrast,'f',2)};
  // The seed animates between covers, so the same handful of colors comes back
  // many times a second; a small cache keeps the theme off the solver.
  if(const auto found=m_schemes.constFind(key);found!=m_schemes.constEnd())return *found;
  if(m_schemes.size()>256)m_schemes.clear();
  return *m_schemes.insert(key,m3::scheme(source,dark,m3::variantFor(variant),contrast));
}
void Backend::setAccentColor(const QString &value) {
  const QColor color(value.trimmed());
  const auto stored=value.trimmed().isEmpty()?QString():color.isValid()?color.name(QColor::HexRgb):accentColor();
  if(stored==accentColor())return;
  m_settings.setValue("accentColor",stored);
  emit settingsChanged();
}
void Backend::setShuffle(bool v) {
  m_settings.setValue("shuffle", v);
  emit settingsChanged();
}
void Backend::setRepeat(int v) {
  m_settings.setValue("repeat", qBound(0, v, 2));
  emit settingsChanged();
}
void Backend::setAutoplay(bool v) {
  m_settings.setValue("autoplay", v);
  emit settingsChanged();
}
void Backend::setMotion(bool v) {
  m_settings.setValue("motion", v);
  emit settingsChanged();
}
void Backend::setTheme(const QString &v) {
  if (v != "dark" && v != "light" && v != "system")
    return;
  m_settings.setValue("theme", v);
  emit settingsChanged();
}
QString Backend::formatTime(qint64 ms) const {
  auto s = qMax<qint64>(0, ms / 1000);
  return s >= 3600
             ? QString("%1:%2:%3")
                   .arg(s / 3600)
                   .arg(s / 60 % 60, 2, 10, QChar('0'))
                   .arg(s % 60, 2, 10, QChar('0'))
             : QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}
void Backend::setSleep(int minutes) {
  m_sleepAtEnd=minutes==-1 && m_index>=0;
  m_sleepAtQueueEnd=minutes==-2 && m_queue.count()>0;
  m_sleepTimer.stop();
  m_sleepTick.stop();
  m_sleepFadeStart.stop();m_sleepFadeTick.stop();
  m_sleepGain=1; applyOutputVolume();
  if (minutes > 0) {
    m_sleepTimer.start(qMin(minutes,1440) * 60000);
    m_sleepTick.start();
    if(sleepFade())m_sleepFadeStart.start(qMax(0,m_sleepTimer.remainingTime()-30000));
  }
  updateSleepGain();
  emit settingsChanged();
}
QString Backend::sleepLabel() const {
  if(m_sleepAtEnd)return "End of track";
  if(m_sleepAtQueueEnd)return "End of queue";
  return m_sleepTimer.isActive()
             ? QString::number((m_sleepTimer.remainingTime() + 59999) / 60000) +
                   " min"
             : "Off";
}
void Backend::copyLink(const QVariantMap &t) {
  if(isServerSource(t.value("source"))){QGuiApplication::clipboard()->setText(t.value("title").toString()+" — "+t.value("artist").toString());emit toast("Song details copied");return;}
  if(!t.value("localPath").toString().isEmpty()){QGuiApplication::clipboard()->setText(t.value("localPath").toString());emit toast("Path copied");return;}
  QString url = "https://music.youtube.com/";
  if (!t.value("videoId").toString().isEmpty())
    url += "watch?v=" + t.value("videoId").toString();
  else if (t.value("kind") == "playlist")
    url += "playlist?list=" + t.value("id").toString();
  else
    url += "browse/" + t.value("id").toString();
  QGuiApplication::clipboard()->setText(url);
  emit toast("Link copied");
}
void Backend::setCookieFile(const QUrl &url) {
  QFile file(url.toLocalFile());
  if (!file.open(QIODevice::ReadOnly)) {
    notifyError("Cannot read the cookie file.");
    return;
  }
  const auto bytes = file.read(2 * 1024 * 1024);
  if (!bytes.startsWith("# Netscape HTTP Cookie File") &&
      !bytes.startsWith("# HTTP Cookie File")) {
    notifyError("Choose a Netscape format cookies.txt file.");
    return;
  }
  QDir().mkpath(dataPath());
  auto path = dataPath() + "/cookies.txt";
  QSaveFile out(path);
  if (!out.open(QIODevice::WriteOnly)) {
    notifyError("Cannot save the cookie file.");
    return;
  }
  out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  out.write(bytes);
  if (!out.commit()) {
    notifyError("Could not save cookies.");
    return;
  }
  cancelPreparation();m_streams.clear();
  m_settings.setValue("cookies", path);
  emit settingsChanged();
  emit toast("Cookies imported");
}
void Backend::clearCookies() {
  const auto p = cookies();
  if (p == dataPath() + "/cookies.txt")
    QFile::remove(p);
  cancelPreparation();m_streams.clear();
  m_settings.remove("cookies");
  emit settingsChanged();
}
void Backend::setKeepPlayedMb(int megabytes){
  megabytes=qBound(0,megabytes,65536);
  if(megabytes==keepPlayedMb())return;
  m_settings.setValue("keepPlayedMb",megabytes);
  // Lowering the budget gives the disk back now rather than at some later
  // song, so the number in Settings is true the moment it is chosen.
  m_offline.setBudget(qint64(megabytes)*1024*1024);
  emit settingsChanged();
}
QString Backend::keptSongsSize() const {
  const qint64 bytes=m_offline.bytes();
  // Empty means the row that offers to clear it has nothing to offer, so it
  // says nothing rather than saying zero.
  if(bytes<=0)return {};
  if(bytes<1024*1024)return QString("%1 kB").arg(bytes/1024);
  if(bytes<1024LL*1024*1024)return QString("%1 MB").arg(bytes/(1024*1024));
  return QString("%1 GB").arg(double(bytes)/(1024.0*1024*1024),0,'f',1);
}
void Backend::clearKeptSongs(){
  m_offline.clear();
  emit settingsChanged();
  emit toast("Kept songs cleared");
}
void Backend::clearCache() {
  m_onlineArtworkTimer.stop();cancel("motion-artwork");++m_onlineArtworkGeneration;
  m_onlineArtworkAttempted=true;
  m_onlineMotionArt.clear();emit onlineArtworkChanged();
  QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/motion-art").removeRecursively();
  const auto p =
      QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/art";
  QDir(p).removeRecursively();
  m_streams.clear();
  emit artworkCacheCleared();
  emit toast("Cache cleared");
}
void Backend::setUiActive(bool active) {
  if(m_uiActive==active)return;
  m_uiActive=active;
  if(active){
    emit positionChanged();
    if(playing())m_positionTick.start();
  }else {m_positionTick.stop();resetAudioLevels();}
  updateOnlineArtwork();
}
void Backend::localTestSource(const QUrl &url) {
  m_stopped=false;
  m_media().setSource(url);
  m_media().play();
}

void Backend::load() {
  QFile f(dataPath() + "/library.json");
  if (!f.open(QIODevice::ReadOnly))
    return;
  QJsonParseError parse;
  const auto document=QJsonDocument::fromJson(f.readAll(),&parse);
  if(parse.error!=QJsonParseError::NoError || !document.isObject()) {m_storageHealthy=false;notifyError("Your saved library could not be read. The original file has been preserved.");return;}
  auto d = librarydata::read(document.object());
  m_localTracks=playable(d.value("localTracks").toList());
  m_musicFolders=d.value("musicFolders").toStringList().mid(0,64);
  m_favorites = d.value("favorites").toList();
  m_history = d.value("history").toList();
  refreshRecentlyPlayed();
  m_lastPlayed=d.value("lastPlayed").toMap();
  m_plays=d.value("plays").toList();
  m_playlistVersions=d.value("playlistVersions").toMap();
  // Legacy history proves a play, but provides no trustworthy date.
  for(const auto &v:m_history)if(!m_lastPlayed.contains(itemId(v)))m_lastPlayed[itemId(v)]=0;
  m_playlists = d.value("playlists").toList();
  m_sessions=d.value("sessions").toList().mid(0,20);
  m_pins = d.value("pins").toList().mid(0,24);
  m_lyricOffsets=d.value("lyricOffsets").toMap();
  m_queue.assign(playable(d.value("queue").toList()));
  m_index = qBound(-1, d.value("index", -1).toInt(), m_queue.count() - 1);
  m_savedPosition=qMax<qint64>(0,d.value("position").toLongLong());
}
QVariantMap Backend::libraryDocument() const {
  return {{"musicFolders",m_musicFolders},{"localTracks",m_localTracks},{"favorites", m_favorites},
          {"history", m_history},{"lastPlayed",m_lastPlayed},{"plays",m_plays},{"playlistVersions",m_playlistVersions},
          {"sessions",m_sessions},{"playlists", m_playlists},{"pins",m_pins},{"lyricOffsets",m_lyricOffsets},
          {"queue", m_queue.rows},
          {"index", m_index},{"position",position()}};
}
// Safe away from the thread that owns the library: every value in the document
// is implicitly shared, and an edit made meanwhile detaches its own copy.
// https://doc.qt.io/qt-6/threads-modules.html#threads-and-implicitly-shared-classes
static bool writeLibrary(const QString &path, const QVariantMap &document) {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly))
    return false;
  f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  f.write(QJsonDocument::fromVariant(document).toJson(QJsonDocument::Compact));
  const bool written = f.commit();
  // Writing builds the whole document in memory, several times the size of the
  // file, and none of it outlives this function.
  returnFreedMemory();
  return written;
}
void Backend::save() {
  if(!m_storageHealthy)return;
  // Whoever asks by name wants the file now: tests reading it back, and the
  // last save before exit. A background write still under way goes first, so
  // the state taken here is the one that lands last.
  m_saver.waitForDone();
  QDir().mkpath(dataPath());
  if (!writeLibrary(dataPath() + "/library.json", libraryDocument()))
    notifyError("Could not save your library.");
}
void Backend::saveInBackground() {
  if(!m_storageHealthy)return;
  QDir().mkpath(dataPath());
  m_saver.start([this, path = dataPath() + "/library.json", document = libraryDocument()] {
    if (!writeLibrary(path, document))
      QMetaObject::invokeMethod(this, [this] { notifyError("Could not save your library."); }, Qt::QueuedConnection);
  });
}
void Backend::recordHistory() {
  if(m_historyPaused){m_skipHistoryToken=m_trackToken;return;}
  if(m_skipHistoryToken&&m_skipHistoryToken==m_trackToken)return;
  auto t = current();
  if(!t.isEmpty()&&m_recordedToken!=m_trackToken){
    m_recordedToken=m_trackToken;m_lastPlayed[t.value("id").toString()]=QDateTime::currentSecsSinceEpoch();
    // Retain dates only for saved songs and the bounded recent history.
    QSet<QString> keep;for(const auto &v:m_localTracks)keep.insert(itemId(v));for(const auto &v:m_favorites)keep.insert(itemId(v));for(const auto &v:m_history)keep.insert(itemId(v));
    for(const auto &v:m_playlists)for(const auto &row:v.toMap().value("tracks").toList())keep.insert(itemId(row));
    keep.insert(t.value("id").toString());
    for(auto i=m_lastPlayed.begin();i!=m_lastPlayed.end();)if(!keep.contains(i.key()))i=m_lastPlayed.erase(i);else ++i;
    recordPlay(t);
    beginScrobble();
    emit libraryChanged();m_saveTimer.start();
  }
  if (t.isEmpty() || (!m_history.isEmpty() && itemId(m_history.first())==t.value("id").toString()))
    return;
  for (int i = m_history.size() - 1; i >= 0; --i)
    if (itemId(m_history[i]) == t.value("id").toString())
      m_history.removeAt(i);
  invalidateUndo("history");
  m_history.prepend(t);
  while (m_history.size() > 200)
    m_history.removeLast();
  refreshRecentlyPlayed();
  emit libraryChanged();
  m_saveTimer.start();
}
void Backend::notifyTrack() {
  if(m_announcedToken==m_trackToken)return;
  m_announcedToken=m_trackToken;
  if(!trackNotifications()||m_historyPaused)return;
  const auto t=current();m_notifier.show(t.value("title").toString(),t.value("artist").toString());
}
void Backend::setHistoryPaused(bool paused) {
  if(m_historyPaused==paused)return;
  m_historyPaused=paused;
  if(paused){m_skipHistoryToken=m_trackToken;m_notifier.clear();}
  emit settingsChanged();
}
void Backend::setTrackNotifications(bool enabled) {
  m_settings.setValue("trackNotifications",enabled);
  if(!enabled)m_notifier.clear();
  emit settingsChanged();
}
void Backend::setKeepCompletedLyrics(bool enabled) {m_settings.setValue("keepCompletedLyrics",enabled);emit settingsChanged();}
void Backend::setVolumeStep(int percent) {
  if(percent!=1&&percent!=2&&percent!=5&&percent!=10)return;
  m_settings.setValue("volumeStep",percent);emit settingsChanged();
}
bool Backend::isLiked(const QString &id) const {
  if(id.startsWith("sub_") || id.startsWith("jf_"))return m_server.isStarred(id);
  for (const auto &t : m_favorites)
    if (itemId(t) == id)
      return true;
  return false;
}
bool Backend::liked() const {
  return isLiked(current().value("id").toString());
}
void Backend::toggleLike(const QVariantMap &item) {
  if(isServerSource(item.value("source"))){
    m_server.star(item,!m_server.isStarred(item.value("id").toString()),[this](const QVariantMap &,const QString &error){if(!error.isEmpty())notifyError(error);else if(m_page=="server"&&m_request.value("mode")=="favorites")refresh();});return;
  }
  if (playable({item}).isEmpty())
    return;
  bool removed = false;
  for (int i = 0; i < m_favorites.size(); ++i)
    if (itemId(m_favorites[i]) == item.value("id").toString()) {
      m_favorites.removeAt(i);
      removed = true;
      break;
    }
  if (!removed)
    m_favorites.prepend(item);
  emit libraryChanged();
  if (m_page == "library" && m_libraryId == "favorites")
    m_results.assign(m_favorites);
  m_saveTimer.start();
  emit toast(removed ? "Removed from liked songs" : "Added to liked songs");
}
QVariantList Backend::playlists() const {
  QVariantList result;
  for (const auto &v : m_playlists) {
    auto p = v.toMap();
    p["smart"] = p.contains("rules");
    p["count"] = p.contains("rules") ? -1 : p.value("tracks").toList().size();
    QStringList artwork;QSet<QString> seenArt;int inspected=0;
    for(const auto &t:p.value("tracks").toList()){if(++inspected>64)break;const auto url=t.toMap().value("art").toString();if(!url.isEmpty() && !seenArt.contains(url)){artwork.append(url);seenArt.insert(url);}if(artwork.size()==4)break;}
    if(!p.value("customCover").toString().isEmpty())artwork={p.value("customCover").toString()};
    p["artworks"]=artwork;
    p.remove("tracks");
    result.append(p);
  }
  return result;
}
void Backend::library(const QString &kind) {
  // The list pane is the grid a detail was opened from. Choosing a library
  // tab leaves that detail, so the pane goes with it rather than standing
  // beside Mixes or History, which it has nothing to do with.
  clearListPane();
  if(kind=="local-albums" || kind=="local-artists"){
    navigate("library",kind=="local-albums"?"Albums":"Artists",m_page!="library","library:"+kind);
    m_libraryId=kind;m_results.assign(localGroups(kind));emit catalogChanged();return;
  }
  if(kind=="server"){browseServer();return;}
  navigate("library",
           kind == "files" ? "Local files" : kind == "mixes" ? "Mixes" : kind=="mix-recent" ? "Recently liked" : kind=="mix-rediscover" ? "Rediscover" : kind=="mix-unplayed" ? "Unplayed" :
           kind == "history"     ? "Recently played"
           : kind == "playlists" ? "Playlists"
                                 : "Liked songs",
           m_page != "library", "library:"+kind);
  m_libraryId = kind;
  m_results.assign(libraryRows(kind));
  emit catalogChanged();
}
void Backend::clearHistory() {
  m_undoType = "history";
  m_undoRows = m_history;m_undoLastPlayed=m_lastPlayed;m_lastPlayed.clear();
  m_undoMessage = "History cleared";
  m_history.clear();
  refreshRecentlyPlayed();
  if (m_page == "library" && m_libraryId == "history")
    m_results.assign({});
  emit libraryChanged();
  m_saveTimer.start();
  if (!m_undoMessage.isEmpty()) emit toast(m_undoMessage);
}
QString Backend::createPlaylist(const QString &name) {
  invalidateUndo("playlists");
  if (name.trimmed().isEmpty())
    return {};
  auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  m_playlists.append(QVariantMap{{"id", id},
                                 {"title", name.trimmed().left(120)},
                                 {"tracks", QVariantList{}}});
  emit libraryChanged();
  m_saveTimer.start();
  return id;
}
void Backend::openPlaylist(const QString &id) {
  for (const auto &v : m_playlists) {
    auto p = v.toMap();
    if (p.value("id") == id) {
      navigate("local", p.value("title").toString(),true,"local:"+id);
      m_libraryId = id;
      m_cover=p.value("customCover").toString();
      m_results.assign(playlistRows(p));
      emit catalogChanged();
      return;
    }
  }
}
void Backend::renamePlaylist(const QString &id, const QString &name) {
  invalidateUndo("playlists");
  if (name.trimmed().isEmpty())
    return;
  for (int i = 0; i < m_playlists.size(); ++i) {
    auto p = m_playlists[i].toMap();
    if (p.value("id") == id) {
      p["title"] = name.trimmed().left(120);
      m_playlists[i] = p;
      if (m_libraryId == id) {
        m_title = p.value("title").toString();
        emit catalogChanged();
      }
      emit libraryChanged();
      m_saveTimer.start();
      return;
    }
  }
}
void Backend::deletePlaylist(const QString &id) {
  for (int i = 0; i < m_playlists.size(); ++i)
    if (m_playlists[i].toMap().value("id") == id) {
      m_undoType = "playlists";
      m_undoRows = m_playlists;
      m_undoMessage = "Playlist deleted";
      m_playlistVersions.remove(id);
      m_playlists.removeAt(i);
      if (m_libraryId == id)
        library();
      emit libraryChanged();
      m_saveTimer.start();
      if (!m_undoMessage.isEmpty()) emit toast(m_undoMessage);
      return;
    }
}
void Backend::addToPlaylist(const QString &id, const QVariantMap &item) {
  addItemsToPlaylist(id,{item});
}
void Backend::removeFromPlaylist(const QString &id, int index) {
  if(!smartPlaylist(id).isEmpty())return;
  for (int i = 0; i < m_playlists.size(); ++i) {
    auto p = m_playlists[i].toMap();
    if (p.value("id") == id) {
      auto tracks = p.value("tracks").toList();
      if (index < 0 || index >= tracks.size())
        return;
      snapshotPlaylist(id);
      m_undoType="playlists"; m_undoRows=m_playlists; m_undoMessage="Removed from playlist";
      tracks.removeAt(index);
      p["tracks"] = tracks;
      m_playlists[i] = p;
      if (m_libraryId == id)
        m_results.assign(tracks);
      emit libraryChanged();
      m_saveTimer.start();
      emit toast(m_undoMessage);
      return;
    }
  }
}

void Backend::undo() {
  if(m_undoType=="queue-order"){
    m_index=m_undoIndex;m_queue.reconcile(m_undoRows);setShuffle(m_undoShuffle);emit trackChanged();
  } else if (m_undoType == "queue") {
    stop();
    m_queue.reconcile(m_undoRows);
    m_index = qBound(-1, m_undoIndex, m_queue.count() - 1);
    emit trackChanged();
  } else if (m_undoType == "history") {
    m_history = m_undoRows;m_lastPlayed=m_undoLastPlayed;m_undoLastPlayed.clear();
    refreshRecentlyPlayed();
    if (m_page == "library" && m_libraryId == "history")
      m_results.assign(m_history);
  } else if (m_undoType == "playlists") {
    m_playlists = m_undoRows;
    if (m_page=="local") {
      bool found=false;
      for(const auto &v:m_playlists)if(v.toMap().value("id")==m_libraryId){found=true;m_title=v.toMap().value("title").toString();m_cover=v.toMap().value("customCover").toString();m_results.assign(playlistRows(v.toMap()));}
      if(!found)library("playlists");else emit catalogChanged();
    }
  } else
    return;
  m_undoRows.clear();
  m_undoType.clear();
  m_undoMessage.clear();
  emit libraryChanged();
  m_saveTimer.start();
  emit toast("Restored");
}

void Backend::invalidateUndo(const QString &type) {
  if(m_undoType!=type && !(type=="queue"&&m_undoType=="queue-order"))return;
  m_undoType.clear();m_undoRows.clear();m_undoLastPlayed.clear();m_undoMessage.clear();emit libraryChanged();
}
void Backend::clearLyrics() {
  cancel("lyrics");m_lyricsLoaded=false;m_lyricsSource.clear();m_lyrics.clear();m_lyricLines.clear();m_lyricsBusy=false;emit lyricsChanged();
}
void Backend::saveQueue(const QString &name) {
  if(m_queue.count()==0)return;
  auto id=createPlaylist(name);
  if(id.isEmpty())return;
  addItemsToPlaylist(id,m_queue.rows);
  emit toast("Queue saved as playlist");
}
void Backend::movePlaylistTrack(const QString &id,int from,int to) {
  if(!smartPlaylist(id).isEmpty())return;
  for(int i=0;i<m_playlists.size();++i){auto p=m_playlists[i].toMap();if(p.value("id")!=id)continue;
    auto rows=p.value("tracks").toList();if(from<0||to<0||from>=rows.size()||to>=rows.size()||from==to)return;
    snapshotPlaylist(id);invalidateUndo("playlists");rows.move(from,to);p["tracks"]=rows;m_playlists[i]=p;
    if(m_page=="local"&&m_libraryId==id)m_results.assign(rows);
    emit libraryChanged();m_saveTimer.start();return;
  }
}
void Backend::exportLibrary(const QUrl &url) {
  if(!url.isLocalFile())return;
  QSaveFile file(url.toLocalFile());if(!file.open(QIODevice::WriteOnly)){notifyError("Couldn’t export the library.");return;}
  file.setPermissions(QFile::ReadOwner|QFile::WriteOwner);
  file.write(QJsonDocument::fromVariant(QVariantMap{{"sung",1},{"musicFolders",m_musicFolders},{"localTracks",m_localTracks},{"favorites",m_favorites},{"playlists",m_playlists},{"pins",pins()},{"lyricOffsets",m_lyricOffsets}}).toJson());
  if(!file.commit())notifyError("Couldn’t export the library.");else emit toast("Library exported");
}
void Backend::importLibrary(const QUrl &url) {
  if(!url.isLocalFile())return;
  QFile file(url.toLocalFile());if(!file.open(QIODevice::ReadOnly)||file.size()>10*1024*1024){notifyError("Choose a Sung library JSON file smaller than 10 MB.");return;}
  auto d=QJsonDocument::fromJson(file.readAll());auto map=d.object().toVariantMap();
  if(!d.isObject()||map.value("sung").toInt()!=1){notifyError("This isn’t a Sung library export.");return;}
  for(const auto &v:playable(map.value("localTracks").toList()))if(!v.toMap().value("localPath").toString().isEmpty())mergeLocalTrack(v.toMap());
  for(const auto &path:map.value("musicFolders").toStringList())if(QDir::isAbsolutePath(path)&&!m_musicFolders.contains(path)&&m_musicFolders.size()<64)m_musicFolders.append(path);
  invalidateUndo("playlists");
  for(const auto &v:playable(map.value("favorites").toList()))if(!isLiked(itemId(v)))m_favorites.append(v);
  for(const auto &v:map.value("playlists").toList()){
    auto p=v.toMap();const auto importedRules=p.value("rules").toMap();const bool importedSmart=p.contains("rules");auto rows=playable(p.value("tracks").toList());auto name=p.value("title").toString().trimmed();if(name.isEmpty())continue;
    int found=-1;for(int i=0;i<m_playlists.size();++i)if(m_playlists[i].toMap().value("id")==p.value("id")){found=i;break;}
    if(importedSmart){if(found<0){const auto newId=saveSmartPlaylist({},name,importedRules);if(!newId.isEmpty() && !p.value("id").toString().isEmpty()){auto created=m_playlists.last().toMap();created["id"]=p.value("id");m_playlists.last()=created;}}else if(!smartPlaylist(p.value("id").toString()).isEmpty())saveSmartPlaylist(p.value("id").toString(),name,importedRules);continue;}
    if(found>=0 && m_playlists[found].toMap().contains("rules"))continue;
    if(found<0){const auto importedId=p.value("id").toString();createPlaylist(name);found=m_playlists.size()-1;p=m_playlists[found].toMap();if(!importedId.isEmpty())p["id"]=importedId;}else p=m_playlists[found].toMap();
    auto existing=p.value("tracks").toList();for(const auto &t:rows){bool seen=false;for(const auto &old:existing)if(itemId(old)==itemId(t)){seen=true;break;}if(!seen)existing.append(t);}
    p["tracks"]=existing;m_playlists[found]=p;
  }
  invalidateUndo("playlists");
  const auto offsets=map.value("lyricOffsets").toMap();
  for(auto it=offsets.cbegin();it!=offsets.cend();++it){bool ok=false;const auto value=it.value().toInt(&ok);if(ok&&!it.key().isEmpty()&&it.key().size()<=128&&value>=-10000&&value<=10000)m_lyricOffsets[it.key()]=value;}
  emit lyricsChanged();emit positionChanged();
  for(const auto &v:map.value("pins").toList())if(!isPinned(v.toMap()))togglePin(v.toMap());
  emit libraryChanged();if(m_page=="library")library(m_libraryId);else if(m_page=="local") {const auto id=m_libraryId;openPlaylist(id);}
  save();emit toast("Library imported");
}
void Backend::playLink(const QString &url) {
  request("linkplay",{{"op","link"},{"url",url}},[this](const QVariantMap &data){
    if(!data.value("ok").toBool()){notifyError(data.value("error").toString());return;}
    auto items=playable(data.value("items").toList());if(items.isEmpty())return;
    invalidateUndo("queue");m_queue.reconcile(queueWithOrigin(items,"collection"));playAt(0);
  });
}

int Backend::lyricIndex() const {
  const auto pos=position()+lyricOffset();int low=0,high=m_lyricLines.size();
  while(low<high){const int mid=(low+high)/2;if(m_lyricLines[mid].toMap().value("start").toLongLong()<=pos)low=mid+1;else high=mid;}
  if(low==0)return -1;
  const auto line=m_lyricLines[low-1].toMap();const auto end=line.value("end").toLongLong();
  return end>0&&pos>=end ? -1 : low-1;
}

// The line currently being sung: where it starts, and how long it is sung for.
//
// A line's end marks when it stops being the current line, which is not the
// same as when it stops being sung. The last line of a song is held until the
// audio runs out, and a solo can leave a line standing for a minute; filling
// across either would creep rather than sing. So a line fills over the gap to
// the next one, and a line with no such gap, or an unreasonably long one,
// borrows the pace the rest of the song is sung at.
QPair<qint64,qint64> Backend::lyricSpanAt(int index) const {
  if(index<0 || index>=m_lyricLines.size())return {0,0};
  const auto startOf=[this](int i){return m_lyricLines[i].toMap().value("start").toLongLong();};
  QList<qint64> gaps;
  for(int i=0;i+1<m_lyricLines.size();++i)
    if(const auto gap=startOf(i+1)-startOf(i);gap>0)gaps.append(gap);
  std::sort(gaps.begin(),gaps.end());
  const qint64 typical=gaps.isEmpty()?4000:gaps[gaps.size()/2];
  const qint64 start=startOf(index);
  const qint64 gap=index+1<m_lyricLines.size()?startOf(index+1)-start:0;
  return {start,qMax(qint64(1),gap>0 && gap<=typical*4?gap:typical)};
}

// How far playback has travelled across that line, from 0 at its first syllable
// to 1 at its last, or -1 when no line is live.
double Backend::lyricProgress() const {
  const auto [start,span]=lyricSpanAt(lyricIndex());
  if(span<=0)return -1;
  return qBound(0.0,double(position()+lyricOffset()-start)/double(span),1.0);
}

// How long the live line is sung for, so a surface drawing its progress can
// carry on smoothly between position reports instead of stepping with them.
int Backend::lyricSpan() const { return int(lyricSpanAt(lyricIndex()).second); }

QVariantList Backend::audioDevices() const {
  QVariantList result{{QVariantMap{{"id",""},{"name","System default"}}}};
  for(const auto &device:QMediaDevices::audioOutputs())
    result.append(QVariantMap{{"id",QString::fromLatin1(device.id().toBase64())},{"name",device.description()}});
  return result;
}
QString Backend::audioDeviceName() const {
  if(audioDeviceId().isEmpty())return "System default";
  for(const auto &device:QMediaDevices::audioOutputs())if(QString::fromLatin1(device.id().toBase64())==audioDeviceId())return device.description();
  return "System default";
}
void Backend::setAudioDeviceId(const QString &id) {
  if(!id.isEmpty()){
    bool found=false;for(const auto &device:QMediaDevices::audioOutputs())if(QString::fromLatin1(device.id().toBase64())==id){found=true;break;}
    if(!found)return;
  }
  if(audioDeviceId()==id)return;
  m_settings.setValue("audioDevice",id);m_outputPort.clear();applyAudioDevice();if(pauseOnDisconnect())m_portDebounce.start();emit audioDevicesChanged();
}
void Backend::applyAudioDevice() {
  auto chosen=QMediaDevices::defaultAudioOutput();bool found=audioDeviceId().isEmpty();
  for(const auto &device:QMediaDevices::audioOutputs())if(QString::fromLatin1(device.id().toBase64())==audioDeviceId()){chosen=device;found=true;break;}
  // A disconnected device falls back immediately. It cannot steal playback if it reconnects.
  if(!found)m_settings.remove("audioDevice");
  if(activeAudio().device()!=chosen)activeAudio().setDevice(chosen);
  m_outputId=chosen.id();m_outputDescription=chosen.description();
}
void Backend::playCollection(int index) {
  if(index<0||index>=m_collection.count())return;
  const auto all=m_collection.items();const auto target=m_collection.get(index);
  if(playable({target}).isEmpty())return;
  int actual=0;for(int i=0;i<index;++i)if(!playable({all[i]}).isEmpty())++actual;
  invalidateUndo("queue");m_queue.reconcile(queueWithOrigin(playable(all),"collection"));playAt(actual);
}
void Backend::enqueueCollection() {
  auto items=playable(m_collection.items());if(items.isEmpty())return;
  invalidateUndo("queue");m_queue.append(queueWithOrigin(items,"manual"));m_saveTimer.start();emit toast("Added to queue");
}

static QString pinKey(const QVariantMap &item) {
  return item.value("kind").toString()+":"+item.value("browseId",item.value("id")).toString();
}
QVariantList Backend::pins() const {
  QVariantList visible;
  for(const auto &v:m_pins){
    auto item=v.toMap();
    if(item.value("kind")=="local"){
      bool found=false;
      for(const auto &p:m_playlists)if(itemId(p)==item.value("id").toString()){
        const auto list=p.toMap();item["title"]=list.value("title");
        const auto tracks=list.value("tracks").toList();item["art"]=tracks.isEmpty()?QString():tracks.first().toMap().value("art");
        QStringList artwork;int inspected=0;for(const auto &t:tracks){if(++inspected>64)break;const auto url=t.toMap().value("art").toString();if(!url.isEmpty()&&!artwork.contains(url))artwork.append(url);if(artwork.size()==4)break;}if(!list.value("customCover").toString().isEmpty()){artwork={list.value("customCover").toString()};item["art"]=list.value("customCover");}item["artworks"]=artwork;
        found=true;break;
      }
      if(!found)continue;
    }
    visible.append(item);
  }
  return visible;
}
bool Backend::isPinned(const QVariantMap &item) const {
  for(const auto &p:m_pins)if(pinKey(p.toMap())==pinKey(item))return true;
  return false;
}
void Backend::togglePin(const QVariantMap &item) {
  const auto kind=item.value("kind").toString(),id=item.value("browseId",item.value("id")).toString();
  if(!QStringList{"album","playlist","artist","local"}.contains(kind)||id.isEmpty()||item.value("title").toString().trimmed().isEmpty())return;
  for(int i=0;i<m_pins.size();++i)if(pinKey(m_pins[i].toMap())==pinKey(item)){
    m_pins.removeAt(i);emit libraryChanged();m_saveTimer.start();emit toast("Unpinned from Home");return;
  }
  if(m_pins.size()>=24){emit toast("Unpin an item before adding another");return;}
  QVariantMap pin{{"kind",kind},{"id",id},{"title",item.value("title").toString().left(120)}};
  for(const auto &key:{"browseId","art","artist","source","server","remoteId"})if(item.contains(key))pin[key]=item.value(key).toString();
  m_pins.prepend(pin);emit libraryChanged();m_saveTimer.start();emit toast("Pinned to Home");
}
QVariantMap Backend::collectionItem() const {
  if(m_page=="server" && QStringList{"album","artist","playlist"}.contains(m_request.value("mode").toString()))return m_server.item({{"id",m_request.value("remoteId")},{"name",m_title}},m_request.value("mode").toString());
  if(m_page=="local")return {{"id",m_libraryId},{"kind","local"},{"title",m_title},{"art",m_cover}};
  if(!QStringList{"album","artist","playlist"}.contains(m_page)||m_request.value("id").toString().isEmpty())return {};
  return {{"id",m_request.value("id")},{"browseId",m_request.value("id")},{"kind",m_page},{"title",m_title},{"art",m_cover}};
}

void Backend::setPlaybackRate(double rate) {
  if(!std::isfinite(rate)||rate<0.5||rate>2.0)return;
  m_settings.setValue("playbackRate",rate);m_media().setPlaybackRate(rate);
}
void Backend::setLyricTextSize(int size) {
  size=qBound(20,size,32);if(size==lyricTextSize())return;
  m_settings.setValue("lyricTextSize",size);emit settingsChanged();
}
qint64 Backend::queueRemainingMs() const {
  if(m_queue.count()==0)return 0;
  if(shuffle())return -1;
  const int first=qMax(0,m_index);
  if(first>=m_queue.count()||m_queueSuffix.size()!=m_queue.count()+1)return -1;
  qint64 remaining=m_queueSuffix[first];
  if(m_index>=0){
    if(duration()<=0||m_queueSuffix[first+1]<0)return -1;
    remaining=qMax<qint64>(0,duration()-position())+m_queueSuffix[first+1];
  }
  return remaining<0 ? -1 : qint64(std::ceil(remaining/playbackRate()));
}
QString Backend::queueTime() const {
  const auto ms=queueRemainingMs();if(ms<=0)return {};
  const auto minutes=(ms+59999)/60000;
  const auto time=minutes>=60 ? QString("%1 h %2 min").arg(minutes/60).arg(minutes%60) : QString("%1 min").arg(minutes);
  return time+(repeat()||autoplay()?" queued":" left");
}
QString Backend::queueEnd() const {
  const auto ms=queueRemainingMs();
  if(ms<=0||!playing()||resolving()||buffering()||repeat()||autoplay()||m_sleepAtEnd||m_sleepTimer.isActive())return {};
  const auto now=QDateTime::currentDateTime(),end=now.addMSecs(ms);
  return "Ends around "+end.toString(end.date()==now.date()?"h:mm AP":"ddd h:mm AP");
}
bool Backend::pitchAdjustable() const {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
  return m_media().pitchCompensationAvailability()==QMediaPlayer::PitchCompensationAvailability::Available;
#else
  return false;
#endif
}
bool Backend::preservePitch() const {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
  return m_media().pitchCompensation();
#else
  return false;
#endif
}
void Backend::setPreservePitch(bool enabled) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
  if(!pitchAdjustable())return;
  m_media().setPitchCompensation(enabled);m_settings.setValue("preservePitch",enabled);emit settingsChanged();
#else
  Q_UNUSED(enabled);
#endif
}
int Backend::lyricOffset() const {
  return qBound(-10000,m_lyricOffsets.value(current().value("id").toString(),0).toInt(),10000);
}
void Backend::setLyricOffset(int value) {
  const auto id=current().value("id").toString();if(id.isEmpty())return;
  value=qBound(-10000,value,10000);if(value==lyricOffset())return;
  if(value==0)m_lyricOffsets.remove(id);else m_lyricOffsets[id]=value;
  emit lyricsChanged();emit positionChanged();m_saveTimer.start();
}
void Backend::seekLyric(qint64 milliseconds) {seek(milliseconds-lyricOffset());}
void Backend::playKeepingQueue(const QVariantMap &item) {
  if(playable({item}).isEmpty())return;
  const auto id=item.value("id").toString();
  if(m_index>=0&&current().value("id").toString()==id){play();return;}
  auto q=m_queue.rows;const int target=qBound(0,m_index+1,int(q.size()));
  int existing=-1;for(int i=target;i<q.size();++i)if(q[i].toMap().value("id").toString()==id){existing=i;break;}
  if(existing>=0)q.insert(target,queueWithOrigin({q.takeAt(existing)},"manual").first());else q.insert(target,queueWithOrigin({item},"manual").first());
  invalidateUndo("queue");m_queue.reconcile(q);playAt(target);emit toast("Playing · queue kept");
}


void Backend::rememberSearch(const QString &value) {
  const auto query=value.simplified().left(256);
  if(query.isEmpty() || query.startsWith("https://") || query.startsWith("http://") || historyPaused())return;
  auto recent=recentSearches();
  recent.removeIf([&](const QString &s){return s.compare(query,Qt::CaseInsensitive)==0;});
  recent.prepend(query);m_settings.setValue("recentSearches",recent.mid(0,12));emit recentSearchesChanged();
}
void Backend::removeRecentSearch(const QString &query) {
  auto recent=recentSearches();recent.removeAll(query);m_settings.setValue("recentSearches",recent);emit recentSearchesChanged();
}
QVariantList Backend::localMatches(const QString &query) const {
  const auto terms=query.simplified().split(' ',Qt::SkipEmptyParts);
  if(terms.isEmpty())return {};
  QVariantList matches;QSet<QString> seen;
  auto add=[&](QVariantMap item,const QString &origin,int queueIndex=-1){
    const auto key=item.value("kind").toString()+":"+item.value("id").toString();
    if(matches.size()>=8 || seen.contains(key))return;
    const auto text=item.value("title").toString()+' '+item.value("artist").toString()+' '+item.value("album").toString();
    for(const auto &term:terms)if(!text.contains(term,Qt::CaseInsensitive))return;
    seen.insert(key);item["origin"]=origin;if(queueIndex>=0)item["queueIndex"]=queueIndex;matches.append(item);
  };
  for(const auto &v:m_playlists){auto p=v.toMap();add({{"kind","local"},{"id",p.value("id")},{"title",p.value("title")}},"Playlist");if(matches.size()>=8)break;}
  for(int i=0;i<m_queue.count() && matches.size()<8;++i)add(m_queue.get(i),"Queue",i);
  for(const auto &v:m_favorites){add(v.toMap(),"Liked songs");if(matches.size()>=8)break;}
  for(const auto &v:m_playlists){for(const auto &t:v.toMap().value("tracks").toList()){add(t.toMap(),v.toMap().value("title").toString());if(matches.size()>=8)break;}if(matches.size()>=8)break;}
  for(const auto &v:m_localTracks){add(v.toMap(),"Local files");if(matches.size()>=8)break;}
  for(const auto &v:m_history){add(v.toMap(),"History");if(matches.size()>=8)break;}
  return matches;
}
static QList<int> validRows(const QVariantList &indices,int count) {
  QSet<int> unique;for(const auto &v:indices){bool ok=false;int i=v.toInt(&ok);if(ok&&i>=0&&i<count)unique.insert(i);}
  auto rows=unique.values();std::sort(rows.begin(),rows.end());return rows;
}
static QList<int> movedOrder(int count,const QList<int> &selected,int before) {
  QSet<int> set(selected.begin(),selected.end());QList<int> order;int insert=0;
  for(int i=0;i<count;++i)if(!set.contains(i)){if(i<before)++insert;order.append(i);}
  for(int i=0;i<selected.size();++i)order.insert(insert+i,selected[i]);
  return order;
}
void Backend::enqueueItems(const QVariantList &items,bool next,int before) {
  const auto songs=queueWithOrigin(playable(items),"manual");if(songs.isEmpty())return;
  m_undoType="queue-order";m_undoRows=m_queue.rows;m_undoIndex=m_index;m_undoShuffle=shuffle();m_undoMessage="Added to queue";
  auto rows=m_queue.rows;int at=before>=0?qBound(0,before,int(rows.size())):next?qBound(0,m_index+1,int(rows.size())):int(rows.size());
  if(m_index>=at)m_index+=songs.size();
  for(int i=0;i<songs.size();++i)rows.insert(at+i,songs[i]);
  m_queue.reconcile(rows);emit trackChanged();emit libraryChanged();m_saveTimer.start();emit toast(m_undoMessage);
}
void Backend::moveQueueRows(const QVariantList &indices,int before) {
  const auto selected=validRows(indices,m_queue.count());if(selected.isEmpty()||before<0||before>m_queue.count())return;
  const auto order=movedOrder(m_queue.count(),selected,before);bool changed=false;for(int i=0;i<order.size();++i)changed|=order[i]!=i;if(!changed)return;
  m_undoType="queue-order";m_undoRows=m_queue.rows;m_undoIndex=m_index;m_undoShuffle=shuffle();m_undoMessage="Queue reordered";
  QVariantList rows;for(int i:order)rows.append(m_queue.rows[i]);m_index=order.indexOf(m_index);
  m_queue.reconcile(rows);emit trackChanged();emit libraryChanged();m_saveTimer.start();emit toast(m_undoMessage);
}
void Backend::removeQueueRows(const QVariantList &indices) {
  const auto selected=validRows(indices,m_queue.count());if(selected.isEmpty())return;
  const bool removed=selected.contains(m_index),resume=playing()||m_resolving;
  m_undoType=removed?"queue":"queue-order";m_undoRows=m_queue.rows;m_undoIndex=m_index;m_undoShuffle=shuffle();m_undoMessage="Removed from queue";
  int before=0;for(int i:selected)if(i<m_index)++before;
  auto rows=m_queue.rows;for(auto it=selected.crbegin();it!=selected.crend();++it)rows.removeAt(*it);
  m_index-=before;m_queue.reconcile(rows);
  if(removed){stop();++m_trackToken;clearLyrics();m_index=rows.isEmpty()?-1:qMin(m_index,int(rows.size()-1));if(resume&&m_index>=0)playAt(m_index);}
  emit trackChanged();emit libraryChanged();m_saveTimer.start();emit toast(m_undoMessage);
}
QVariantMap Backend::playlistAdditionInfo(const QString &id,const QVariantList &items) const {
  for(const auto &v:m_playlists){const auto p=v.toMap();if(p.value("id")!=id)continue;
    QSet<QString> seen;for(const auto &t:p.value("tracks").toList())seen.insert(itemId(t));
    int added=0,duplicates=0;
    for(const auto &t:playable(items)){const auto key=itemId(t);if(seen.contains(key))++duplicates;else {seen.insert(key);++added;}}
    return {{"added",added},{"duplicates",duplicates}};
  }
  return {};
}
void Backend::addItemsToPlaylist(const QString &id,const QVariantList &items) {
  if(!smartPlaylist(id).isEmpty())return;
  const auto songs=playable(items);if(songs.isEmpty())return;
  for(int i=0;i<m_playlists.size();++i){auto p=m_playlists[i].toMap();if(p.value("id")!=id)continue;
    auto rows=p.value("tracks").toList();QSet<QString> seen;for(const auto &t:rows)seen.insert(itemId(t));const int old=rows.size();
    for(const auto &t:songs)if(!seen.contains(itemId(t))){rows.append(t);seen.insert(itemId(t));}
    if(rows.size()==old){emit toast("Already in playlist");return;}
    snapshotPlaylist(id);m_undoType="playlists";m_undoRows=m_playlists;m_undoMessage="Added to playlist";
    const int skipped=songs.size()-(rows.size()-old);
    if(skipped)m_undoMessage=QString("Added %1 · skipped %2 duplicate%3").arg(rows.size()-old).arg(skipped).arg(skipped==1?"":"s");
    p["tracks"]=rows;m_playlists[i]=p;if(m_page=="local"&&m_libraryId==id)m_results.assign(rows);
    emit libraryChanged();m_saveTimer.start();emit toast(m_undoMessage);return;
  }
}
void Backend::removePlaylistRows(const QString &id,const QVariantList &indices) {
  if(!smartPlaylist(id).isEmpty())return;
  for(int i=0;i<m_playlists.size();++i){auto p=m_playlists[i].toMap();if(p.value("id")!=id)continue;
    auto rows=p.value("tracks").toList();const auto selected=validRows(indices,rows.size());if(selected.isEmpty())return;
    snapshotPlaylist(id);m_undoType="playlists";m_undoRows=m_playlists;m_undoMessage="Removed from playlist";
    for(auto it=selected.crbegin();it!=selected.crend();++it)rows.removeAt(*it);
    p["tracks"]=rows;m_playlists[i]=p;if(m_page=="local"&&m_libraryId==id)m_results.assign(rows);
    emit libraryChanged();m_saveTimer.start();emit toast(m_undoMessage);return;
  }
}
void Backend::movePlaylistRows(const QString &id,const QVariantList &indices,int before) {
  if(!smartPlaylist(id).isEmpty())return;
  // Display order must be unambiguous before writing a saved playlist order.
  if(m_page!="local"||m_libraryId!=id||!m_collection.query().isEmpty()||m_collection.sortKey()!="original")return;
  for(int i=0;i<m_playlists.size();++i){auto p=m_playlists[i].toMap();if(p.value("id")!=id)continue;
    auto rows=p.value("tracks").toList();const auto selected=validRows(indices,rows.size());if(selected.isEmpty()||before<0||before>rows.size())return;
    const auto order=movedOrder(rows.size(),selected,before);QVariantList moved;bool changed=false;for(int n=0;n<order.size();++n){moved.append(rows[order[n]]);changed|=order[n]!=n;}if(!changed)return;
    snapshotPlaylist(id);m_undoType="playlists";m_undoRows=m_playlists;m_undoMessage="Playlist reordered";
    p["tracks"]=moved;m_playlists[i]=p;m_results.assign(moved);emit libraryChanged();m_saveTimer.start();emit toast(m_undoMessage);return;
  }
}

QVariantList Backend::libraryRows(const QString &kind) const {
  if(kind=="local-albums" || kind=="local-artists")return localGroups(kind);
  if(kind=="files")return m_localTracks;
  if(kind=="favorites")return m_favorites;
  if(kind=="history")return m_history;
  // These descriptions state the same bounds used by the mix branches below.
  if(kind=="mixes")return {QVariantMap{{"id","mix-recent"},{"kind","smart"},{"title","Recently liked"},{"description","Last 50 liked songs"}},QVariantMap{{"id","mix-rediscover"},{"kind","smart"},{"title","Rediscover"},{"description","Last played over 30 days ago"}},QVariantMap{{"id","mix-unplayed"},{"kind","smart"},{"title","Unplayed"},{"description","Never played"}}};
  if(kind=="mix-recent")return m_favorites.mid(0,50);
  QVariantList rows;QSet<QString> seen;
  const auto cutoff=QDateTime::currentSecsSinceEpoch()-30*86400;
  const auto append=[&](const QVariantList &items){for(const auto &v:items){const auto id=itemId(v);if(id.isEmpty()||seen.contains(id))continue;seen.insert(id);const auto date=m_lastPlayed.value(id).toLongLong();if((kind=="mix-unplayed"&&!m_lastPlayed.contains(id))||(kind=="mix-rediscover"&&date>0&&date<cutoff))rows.append(v);}};
  append(m_favorites);append(m_localTracks);for(const auto &v:m_playlists)append(v.toMap().value("tracks").toList());
  if(kind=="mix-rediscover")std::stable_sort(rows.begin(),rows.end(),[this](const QVariant&a,const QVariant&b){return m_lastPlayed.value(itemId(a)).toLongLong()<m_lastPlayed.value(itemId(b)).toLongLong();});
  return rows;
}
// Wayland deliberately gives a client no way to raise itself; xdg-shell has no
// stacking request at all, so Qt's hint reaches the compositor and is dropped.
// Measured on Hyprland: a window carrying the hint is tiled like any other.
bool Backend::canPinWindows(){
  return !QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
}
void Backend::setPrepareNext(bool enabled){m_settings.setValue("prepareNext",enabled);emit settingsChanged();}
void Backend::cancelPreparation(){
  ++m_preparationGeneration;cancel("prepare");m_prepareTimer.stop();m_preparedData.clear();m_preparedDirectory.reset();m_preparedId.clear();
}
void Backend::updatePreparation(){
  m_prepareTimer.stop();
  QString nextId;
  if(prepareNext()&&playing()&&m_wantPlay&&!m_resolving&&!shuffle()&&repeat()!=2&&!m_sleepAtEnd&&m_index>=0){
    const int next=m_index+1<m_queue.count()?m_index+1:repeat()==1?0:-1;
    // A song already kept needs no head start: it is a local file by the time
    // playback reaches it, and fetching it again would undo the point of
    // keeping it.
    if(next>=0&&next!=m_index&&!m_offline.has(OfflineStore::keyFor(m_queue.get(next),streamingQuality())))
      nextId=m_queue.get(next).value("videoId").toString();
    if(m_sleepTimer.isActive()&&m_sleepTimer.remainingTime()<qMax<qint64>(0,duration()-position())/playbackRate())nextId.clear();
  }
  if(nextId.isEmpty()){cancelPreparation();m_preparationAttempt.clear();return;}
  if(m_preparedId!=nextId){cancelPreparation();m_preparationAttempt.clear();m_preparedId=nextId;}
  if(!m_preparedData.isEmpty()||m_processes.contains("prepare")||m_preparationAttempt==nextId)return;
  const qint64 remaining=(duration()-position())/playbackRate();
  if(duration()<=0)return;
  if(remaining>45000){m_prepareTimer.start(int(qMin<qint64>(remaining-45000,2147483647)));return;}
  m_preparationAttempt=nextId;const auto generation=m_preparationGeneration;
  auto directory=audioDirectory();if(!directory||!directory->isValid())return;
  m_preparedDirectory=directory;
  request("prepare",{{"op","buffer"},{"id",nextId},{"directory",directory->path()},{"cookies",cookies()},{"quality",streamingQuality()}},[this,generation,nextId,directory](const QVariantMap &data){
    if(generation!=m_preparationGeneration||m_preparedId!=nextId)return;
    const QFileInfo file(data.value("file").toString());
    // Preload failures and oversized/direct streams leave normal playback in charge.
    if(data.value("ok").toBool()&&file.isFile()&&file.size()<=32*1024*1024&&file.canonicalPath()==QFileInfo(directory->path()).canonicalFilePath()){
      m_preparedData=data;
      QFile buffered(file.filePath());if(buffered.open(QIODevice::ReadOnly))::posix_fadvise(buffered.handle(),0,0,POSIX_FADV_DONTNEED);
    }
    else m_preparedDirectory.reset();
  },directory);
}

QString Backend::localImportStatus() const {return m_scanningFolders ? QString("Scanning folders…") : QString("%1 / %2").arg(m_importDone).arg(m_importTotal);}
void Backend::importLocalFiles(const QVariantList &urls) {
  if(importingLocal()){emit toast("An import is already running");return;}
  QSet<QString> seen;
  for(const auto &v:urls){const QUrl url(v.toString());if(!url.isLocalFile())continue;const auto path=QFileInfo(url.toLocalFile()).absoluteFilePath();if(!seen.contains(path)&&m_importFiles.size()<10000){seen.insert(path);m_importFiles.append(path);}}
  if(m_importFiles.isEmpty())return;
  m_importTotal=m_importFiles.size();m_importDone=0;m_importFailed=0;emit localImportChanged();importNextLocalBatch();
}
void Backend::cancelLocalImport(){m_folderDirty=false;m_quietFolderScan=false;m_folderChangeTimer.stop();cancel("folder-scan");m_scanningFolders=false;m_scanLimited=false;m_scanFailed=0;cancel("local-import");m_importFiles.clear();m_importTotal=0;m_relocateId.clear();emit localImportChanged();}
void Backend::locateLocalFile(const QUrl &url,const QString &id){
  if(importingLocal()||!url.isLocalFile())return;
  bool found=false;for(const auto &v:m_localTracks)if(itemId(v)==id)found=true;
  if(!found)for(const auto &v:m_favorites)if(itemId(v)==id)found=true;
  if(!found)for(const auto &v:m_history)if(itemId(v)==id)found=true;
  if(!found)for(const auto &v:m_queue.rows)if(itemId(v)==id)found=true;
  if(!found)for(const auto &v:m_playlists)for(const auto &t:v.toMap().value("tracks").toList())if(itemId(t)==id)found=true;
  if(!found)return;
  m_relocateId=id;importLocalFiles({url});
}
void Backend::importNextLocalBatch(){
  if(!importingLocal()||m_processes.contains("local-import"))return;
  if(m_importFiles.isEmpty()){
    const auto message=m_scanLimited?QString("Scan limit reached. Add a smaller folder to continue.") : (m_importFailed+m_scanFailed)?QString("Import finished · %1 unreadable files or folders").arg(m_importFailed+m_scanFailed):QString("Import finished");
    m_scanLimited=false;m_scanFailed=0;
    m_importTotal=0;m_relocateId.clear();emit localImportChanged();
    if(!m_m3uName.isEmpty()){m_quietFolderScan=false;finishM3uImport();return;}
    if(!m_quietFolderScan)emit toast(message);m_quietFolderScan=false;return;
  }
  QStringList batch;int bytes=0;
  while(!m_importFiles.isEmpty()&&batch.size()<4&&bytes<16000){batch.append(m_importFiles.takeFirst());bytes+=batch.last().toUtf8().size()*2;}
  request("local-import",{{"op","local-files"},{"files",batch},{"artDirectory",dataPath()+"/local-art"}},[this,count=batch.size()](const QVariantMap &data){
    const auto items=data.value("items").toList();m_importDone+=count;m_importFailed+=count-items.size();
    for(const auto &v:items){auto track=v.toMap();if(!m_relocateId.isEmpty())track["id"]=m_relocateId;mergeLocalTrack(track);}
    emit localImportChanged();m_saveTimer.start();QTimer::singleShot(0,this,&Backend::importNextLocalBatch);
  });
}
// M3U is how playlists travel between players. Only local files can be named
// in one: streaming ids are meaningless elsewhere, and server URLs would carry
// credentials that library exports deliberately leave out.
void Backend::exportPlaylistM3u(const QString &id,const QUrl &file) {
  if(!file.isLocalFile()){notifyError("Choose a location on this computer.");return;}
  QVariantMap playlist;
  for(const auto &v:m_playlists)if(v.toMap().value("id")==id)playlist=v.toMap();
  if(playlist.isEmpty()){notifyError("That playlist is no longer available.");return;}
  const auto rows=playlistRows(playlist);
  QByteArray out="#EXTM3U\n#PLAYLIST:"+playlist.value("title").toString().toUtf8()+"\n";
  int written=0,skipped=0;
  for(const auto &v:rows){
    const auto track=v.toMap();const auto path=track.value("localPath").toString();
    if(path.isEmpty()){++skipped;continue;}
    const auto artist=track.value("artist").toString(),title=track.value("title").toString();
    out+="#EXTINF:"+QByteArray::number(track.value("seconds").toInt())+","
        +(artist.isEmpty()?title:artist+" - "+title).toUtf8()+"\n"+path.toUtf8()+"\n";
    ++written;
  }
  if(!written){notifyError("That playlist has no local files to export.");return;}
  QSaveFile target(file.toLocalFile());
  if(!target.open(QIODevice::WriteOnly)||target.write(out)!=out.size()||!target.commit()){notifyError("Could not save that playlist file.");return;}
  emit toast(skipped?QString("Exported %1 songs · %2 streamed songs skipped").arg(written).arg(skipped)
                    :QString("Exported %1 songs").arg(written));
}
void Backend::importPlaylistM3u(const QUrl &file) {
  if(importingLocal()){emit toast("An import is already running");return;}
  const auto source=file.toLocalFile();
  QFile input(source);
  if(!file.isLocalFile()||!input.open(QIODevice::ReadOnly)||input.size()>4*1024*1024){notifyError("Could not read that playlist file.");return;}
  const auto base=QFileInfo(source).absoluteDir();
  QString name;QStringList entries;QSet<QString> seen;
  while(!input.atEnd()&&entries.size()<5000){
    const auto line=QString::fromUtf8(input.readLine()).trimmed();
    if(line.isEmpty())continue;
    if(line.startsWith('#')){if(line.startsWith("#PLAYLIST:"))name=line.mid(10).trimmed();continue;}
    // Entries pointing at a server or a stream are not this computer's to import.
    if(line.contains("://"))continue;
    const auto resolved=QFileInfo(base.filePath(line)).absoluteFilePath();
    if(!seen.contains(resolved)&&QFileInfo(resolved).isFile()){seen.insert(resolved);entries.append(resolved);}
  }
  input.close();
  if(name.isEmpty())name=QFileInfo(source).completeBaseName();
  if(entries.isEmpty()){notifyError("That playlist lists no audio files on this computer.");return;}
  m_m3uName=name.left(120);m_m3uPaths=entries;
  QVariantList urls;for(const auto &entry:entries)urls.append(QUrl::fromLocalFile(entry).toString());
  importLocalFiles(urls);
  if(!importingLocal())finishM3uImport();
}
void Backend::finishM3uImport() {
  if(m_m3uName.isEmpty())return;
  const auto name=m_m3uName;const auto paths=m_m3uPaths;
  m_m3uName.clear();m_m3uPaths.clear();
  QHash<QString,QVariantMap> byPath;
  for(const auto &v:m_localTracks){const auto track=v.toMap();byPath.insert(track.value("localPath").toString(),track);}
  QVariantList tracks;
  for(const auto &path:paths)if(byPath.contains(path))tracks.append(byPath.value(path));
  if(tracks.isEmpty()){notifyError("None of that playlist's files could be read.");return;}
  invalidateUndo("playlists");
  m_undoType="playlists";m_undoRows=m_playlists;m_undoMessage="Playlist imported";
  const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
  m_playlists.append(QVariantMap{{"id",id},{"title",name},{"tracks",tracks}});
  emit libraryChanged();m_saveTimer.start();
  emit toast(QString("Imported %1 · %2 songs").arg(name).arg(tracks.size()));
  openPlaylist(id);
}
void Backend::mergeLocalTrack(QVariantMap track){
  if(m_relocateId.isEmpty())for(const auto &v:m_localTracks)if(v.toMap().value("localPath")==track.value("localPath")){track["id"]=v.toMap().value("id");break;}
  if(playable({track}).isEmpty()||track.value("localPath").toString().isEmpty())return;
  const auto id=track.value("id").toString();bool known=false;
  const auto replace=[&](QVariantList &rows){bool changed=false;for(auto &v:rows)if(itemId(v)==id){v=track;changed=true;}return changed;};
  known=replace(m_localTracks);if(!known)m_localTracks.append(track);
  replace(m_favorites);replace(m_history);
  if(m_undoType=="playlists"){for(auto &v:m_undoRows){auto list=v.toMap();auto rows=list.value("tracks").toList();if(replace(rows)){list["tracks"]=rows;v=list;}}}
  else replace(m_undoRows);
  for(auto &v:m_playlists){auto list=v.toMap();auto rows=list.value("tracks").toList();if(replace(rows)){list["tracks"]=rows;v=list;}}
  auto queue=m_queue.rows;if(replace(queue)&&queue!=m_queue.rows)m_queue.reconcile(queue);
  auto results=m_results.rows;if(replace(results)&&results!=m_results.rows)m_results.assign(results);
  if(current().value("id").toString()==id)emit trackChanged();
  emit libraryChanged();
}
void Backend::removeLocalFile(const QString &id){
  // Forget the library entry only; playlists and original audio files are untouched.
  for(int i=m_localTracks.size()-1;i>=0;--i)if(itemId(m_localTracks[i])==id)m_localTracks.removeAt(i);
  emit libraryChanged();m_saveTimer.start();emit toast("Removed from local files");
}
QVariantList Backend::searchLyrics(const QString &query) const {
  const auto text=query.simplified();if(text.isEmpty())return {};
  QVariantList rows;
  if(!m_lyricLines.isEmpty()){
    for(int i=0;i<m_lyricLines.size();++i){auto row=m_lyricLines[i].toMap();if(row.value("text").toString().contains(text,Qt::CaseInsensitive)){row["index"]=i;rows.append(row);}}
  }else{int index=0;for(const auto &line:m_lyrics.split('\n')){if(line.contains(text,Qt::CaseInsensitive))rows.append(QVariantMap{{"text",line},{"index",index},{"start",-1}});++index;}}
  return rows;
}

QString Backend::importMusicFolderPath(const QString &input) {
  if(importingLocal())return "Wait for the current import to finish.";
  QString path=input.trimmed();
  if(path.isEmpty())return "Enter a music folder path.";
  if(path=="~")path=QDir::homePath();
  else if(path.startsWith("~/"))path=QDir::homePath()+path.mid(1);
  else if(path.startsWith("file:",Qt::CaseInsensitive)){
    const QUrl url(path,QUrl::StrictMode);
    if(!url.isValid() || !url.isLocalFile() || (!url.host().isEmpty() && url.host()!="localhost"))return "Use a local folder path or a mounted network share.";
    path=url.toLocalFile();
  }
  if(!QDir::isAbsolutePath(path))return "Enter an absolute folder path or a path starting with ~/.";
  const QFileInfo folder(path);
  if(!folder.isDir() || !folder.isReadable() || folder.canonicalFilePath().isEmpty())return "Choose a readable music folder.";
  if(!m_musicFolders.contains(folder.canonicalFilePath()) && m_musicFolders.size()>=64)return "You can save up to 64 music folders.";
  importMusicFolder(QUrl::fromLocalFile(folder.canonicalFilePath()));
  return {};
}

void Backend::importMusicFolder(const QUrl &url) {
  if(importingLocal()||!url.isLocalFile())return;
  const QFileInfo info(url.toLocalFile());const auto path=info.canonicalFilePath();
  if(!info.isDir()||!info.isReadable()||path.isEmpty()){emit toast("Choose a readable music folder");return;}
  if(!m_musicFolders.contains(path)){
    if(m_musicFolders.size()>=64){emit toast("You can save up to 64 music folders");return;}
    m_musicFolders.append(path);emit libraryChanged();m_saveTimer.start();
  }
  scanMusicFolders(m_musicFolders);
}
QString Backend::musicFolderLabel(const QString &path) const {
  QString root;
  for(const auto &candidate:m_musicFolders)
    if((path==candidate || path.startsWith(candidate.endsWith('/')?candidate:candidate+'/')) && candidate.size()>root.size())root=candidate;
  if(!root.isEmpty()){
    const auto name=QFileInfo(root).fileName();bool unique=!name.isEmpty();
    for(const auto &other:m_musicFolders)if(other!=root && QFileInfo(other).fileName()==name)unique=false;
    if(unique)return name+path.mid(root.size());
  }
  const auto home=QDir::homePath();
  return path.startsWith(home+'/')?QStringLiteral("~")+path.mid(home.size()):path;
}
void Backend::rescanMusicFolders(){if(!importingLocal()&&!m_musicFolders.isEmpty())scanMusicFolders(m_musicFolders);}
void Backend::forgetMusicFolder(const QString &path){
  if(importingLocal())return;
  if(m_musicFolders.removeAll(path)){updateFolderWatches({});m_folderDirty=true;m_folderChangeTimer.start(1500);emit libraryChanged();m_saveTimer.start();emit toast("Folder forgotten; songs kept");}
}
void Backend::scanMusicFolders(const QStringList &folders){
  m_scanningFolders=true;m_scanLimited=false;m_scanFailed=0;emit localImportChanged();
  QVariantMap known;for(const auto &v:m_localTracks){const auto t=v.toMap();if(t.value("available",true).toBool() && t.contains("albumArtist"))known[t.value("localPath").toString()]=t.value("localStamp");}
  request("folder-scan",{{"op","scan-folders"},{"folders",folders},{"known",known}},[this](const QVariantMap &data){
    m_scanningFolders=false;
    if(!data.value("ok").toBool()){emit localImportChanged();if(!m_quietFolderScan)emit toast("Could not scan music folders. Try Rescan.");m_quietFolderScan=false;return;}
    if(watchMusicFolders())updateFolderWatches(data.value("watchPaths").toStringList());
    const auto missing=data.value("missing").toStringList();
    if(!missing.isEmpty()){
      for(auto &v:m_localTracks){auto t=v.toMap();if(missing.contains(t.value("localPath").toString())){t["available"]=false;v=t;}}
      emit libraryChanged();m_saveTimer.start();
    }
    m_scanLimited=data.value("limited").toBool();m_scanFailed=data.value("failed").toInt();
    QVariantList urls;for(const auto &v:data.value("files").toList())urls.append(QUrl::fromLocalFile(v.toString()));
    if(urls.isEmpty()){
      emit localImportChanged();if(!m_quietFolderScan)emit toast(m_scanLimited?"Scan limit reached. Choose a smaller folder.":m_scanFailed?"Some folders could not be read; songs kept":"No new or changed audio files");
      m_scanLimited=false;m_scanFailed=0;m_quietFolderScan=false;return;
    }
    importLocalFiles(urls);
  });
}
void Backend::closePlaylistCleanup(){
  cancel("cleanup");m_cleanupBusy=false;m_cleanupId.clear();m_cleanupRows.clear();m_cleanupItems.clear();emit cleanupChanged();
}
void Backend::inspectPlaylist(const QString &id){
  closePlaylistCleanup();
  for(const auto &v:m_playlists){const auto p=v.toMap();if(p.value("id")!=id)continue;
    m_cleanupId=id;m_cleanupRows=p.value("tracks").toList();m_cleanupBusy=true;emit cleanupChanged();
    request("cleanup",{{"op","playlist-cleanup"},{"rows",m_cleanupRows}},[this](const QVariantMap &data){
      m_cleanupBusy=false;
      if(!data.value("ok").toBool()){emit cleanupChanged();emit toast("Could not inspect playlist. Try again.");return;}
      for(const auto &v:data.value("issues").toList()){
        const auto issue=v.toMap();const int i=issue.value("index").toInt();if(i<0||i>=m_cleanupRows.size())continue;
        auto row=m_cleanupRows[i].toMap();row["index"]=i;row["duplicate"]=issue.value("duplicate");row["missing"]=issue.value("missing");m_cleanupItems.append(row);
      }
      emit cleanupChanged();
    });return;
  }
}
void Backend::applyPlaylistCleanup(bool duplicates,bool missing){
  if(m_cleanupBusy||m_cleanupId.isEmpty())return;
  const auto id=m_cleanupId;const auto snapshot=m_cleanupRows;
  QVariantList selected;
  for(const auto &v:m_cleanupItems){const auto issue=v.toMap();if((duplicates&&issue.value("duplicate").toBool())||(missing&&issue.value("missing").toBool()))selected.append(issue.value("index"));}
  if(selected.isEmpty())return;
  m_cleanupBusy=true;emit cleanupChanged();
  // Recheck disk state off the UI thread before removing reviewed entries.
  request("cleanup",{{"op","playlist-cleanup"},{"rows",snapshot}},[this,id,snapshot,selected,duplicates,missing](const QVariantMap &data){
    m_cleanupBusy=false;
    if(!data.value("ok").toBool()){emit cleanupChanged();emit toast("Could not check playlist. Nothing removed.");return;}
    for(const auto &v:m_playlists){const auto p=v.toMap();if(p.value("id")!=id)continue;
      if(p.value("tracks").toList()!=snapshot){inspectPlaylist(id);emit toast("Playlist changed. Review the updated results.");return;}
      QVariantList indices;
      for(const auto &v:data.value("issues").toList()){const auto issue=v.toMap();
        if(selected.contains(issue.value("index"))&&((duplicates&&issue.value("duplicate").toBool())||(missing&&issue.value("missing").toBool())))indices.append(issue.value("index"));
      }
      removePlaylistRows(id,indices);inspectPlaylist(id);return;
    }
    closePlaylistCleanup();emit toast("Playlist no longer exists");
  });
}

QVariantMap Backend::smartPlaylist(const QString &id) const {
  for(const auto &v:m_playlists){const auto p=v.toMap();if(p.value("id")==id && p.contains("rules"))return p;}
  return {};
}
QString Backend::saveSmartPlaylist(const QString &id,const QString &name,const QVariantMap &input) {
  if(name.trimmed().isEmpty())return {};
  int index=-1, count=0;
  for(int i=0;i<m_playlists.size();++i){const auto p=m_playlists[i].toMap();count+=p.contains("rules");if(p.value("id")==id)index=i;}
  if(id.isEmpty() && count>=32){emit toast("You can save up to 32 smart playlists");return {};}
  if(!id.isEmpty() && (index<0 || !m_playlists[index].toMap().contains("rules")))return {};
  auto source=input.value("source").toString();if(!QStringList{"local","youtube","subsonic"}.contains(source))source="any";
  int days=input.value("days").toInt();if(!QList<int>{0,-1,7,30,90,365}.contains(days))days=0;
  // Ranges are stored only when both ends make sense, so an empty field never
  // silently excludes every song that simply has no year or duration tag.
  const int yearFrom=qBound(0,input.value("yearFrom").toInt(),9999);
  const int yearTo=qBound(0,input.value("yearTo").toInt(),9999);
  const int shortest=qBound(0,input.value("minSeconds").toInt(),36000);
  const int longest=qBound(0,input.value("maxSeconds").toInt(),36000);
  const QVariantMap rules{{"artist",input.value("artist").toString().trimmed().left(120)},
    {"title",input.value("title").toString().trimmed().left(120)},
    {"album",input.value("album").toString().trimmed().left(120)},{"source",source},
    {"yearFrom",yearFrom},{"yearTo",yearTo<yearFrom?0:yearTo},
    {"minSeconds",shortest},{"maxSeconds",longest<shortest?0:longest},
    {"likedOnly",input.value("likedOnly").toBool()},{"days",days}};
  const auto key=id.isEmpty()?QUuid::createUuid().toString(QUuid::WithoutBraces):id;
  const QVariantMap p{{"id",key},{"title",name.trimmed().left(120)},{"rules",rules},{"tracks",QVariantList{}}};
  m_undoType="playlists";m_undoRows=m_playlists;m_undoMessage=index<0?"Smart playlist created":"Rules updated";
  if(index<0)m_playlists.append(p);else m_playlists[index]=p;
  if(m_page=="local" && m_libraryId==key){m_title=p.value("title").toString();emit catalogChanged();}
  emit libraryChanged();m_saveTimer.start();emit toast(m_undoMessage);return key;
}
QVariantList Backend::playlistRows(const QVariantMap &p) const {
  if(!p.contains("rules"))return p.value("tracks").toList();
  const auto rules=p.value("rules").toMap();
  QVariantList candidates=m_localTracks;candidates.append(m_favorites);
  for(const auto &v:m_playlists)if(!v.toMap().contains("rules"))candidates.append(v.toMap().value("tracks").toList());
  QVariantList result;QSet<QString> seen;
  QSet<QString> likedIds;for(const auto &v:m_favorites)likedIds.insert(itemId(v));
  const int days=rules.value("days").toInt();const auto cutoff=QDateTime::currentSecsSinceEpoch()-qint64(days)*86400;
  for(const auto &v:candidates){const auto t=v.toMap();const auto id=itemId(v);if(id.isEmpty()||seen.contains(id))continue;seen.insert(id);
    if(!t.value("artist").toString().contains(rules.value("artist").toString(),Qt::CaseInsensitive) || !t.value("title").toString().contains(rules.value("title").toString(),Qt::CaseInsensitive))continue;
    if(!t.value("album").toString().contains(rules.value("album").toString(),Qt::CaseInsensitive))continue;
    const int yearFrom=rules.value("yearFrom").toInt(),yearTo=rules.value("yearTo").toInt();
    if(yearFrom||yearTo){
      // A song with no year cannot satisfy a year rule.
      const int year=t.value("year").toString().left(4).toInt();
      if(!year||(yearFrom&&year<yearFrom)||(yearTo&&year>yearTo))continue;
    }
    const int shortest=rules.value("minSeconds").toInt(),longest=rules.value("maxSeconds").toInt();
    if(shortest||longest){
      const int seconds=t.value("seconds").toInt();
      if(seconds<=0||(shortest&&seconds<shortest)||(longest&&seconds>longest))continue;
    }
    const auto source=isServerSource(t.value("source"))?"subsonic":!t.value("localPath").toString().isEmpty()?"local":"youtube";
    const auto wanted=rules.value("source","any").toString();if(wanted!="any" && wanted!=source)continue;
    if(rules.value("likedOnly").toBool() && !(isServerSource(t.value("source"))?m_server.isStarred(id):likedIds.contains(id)))continue;
    const bool played=m_lastPlayed.contains(id);const auto time=m_lastPlayed.value(id).toLongLong();
    if((days==-1 && played) || (days>0 && (!played || time<=0 || time>=cutoff)))continue;
    result.append(t);
  }
  return result;
}
QVariantList Backend::trackDetails(const QVariantMap &track) const {
  QVariantList result;
  const auto add=[&](const QString &label,const QString &value){if(!value.isEmpty())result.append(QVariantMap{{"label",label},{"value",value}});};
  add("Title",track.value("title").toString());add("Artist",track.value("artist").toString());
  const auto albumArtist=track.value("albumArtist").toString();
  if(!albumArtist.isEmpty() && albumArtist!=track.value("artist").toString())add("Album artist",albumArtist);
  add("Album",track.value("album").toString());
  add("Composer",track.value("composer").toString());
  add("Genre",track.value("genre").toString());
  add("Year",track.value("year").toString());
  const int trackNumber=track.value("trackNumber").toInt(),discNumber=track.value("discNumber").toInt();
  if(trackNumber>0)add("Track",discNumber>0?QString("%1 on disc %2").arg(trackNumber).arg(discNumber):QString::number(trackNumber));
  else if(discNumber>0)add("Disc",QString::number(discNumber));
  const auto path=track.value("localPath").toString();
  add("Source",isServerSource(track.value("source"))?"Music server":path.isEmpty()?"YouTube Music":"Local file");
  const int seconds=track.value("seconds").toInt();if(seconds>0)add("Duration",QString("%1:%2").arg(seconds/60).arg(seconds%60,2,10,QChar('0')));
  if(!path.isEmpty()) {const QFileInfo file(path);add("File",path);add("Format",file.suffix().toUpper());add("Available",file.isFile()?"Yes":"File missing");if(file.isFile())add("Size",QLocale().formattedDataSize(file.size()));}
  if(track.value("id")==current().value("id") && !m_media().source().isEmpty()){
    const auto meta=m_media().metaData();add("Playback codec",meta.stringValue(QMediaMetaData::AudioCodec));
    const auto bitrate=meta.value(QMediaMetaData::AudioBitRate).toInt();if(bitrate>0)add("Playback bitrate",QString("%1 kb/s").arg(bitrate/1000));
    if(m_decodeRate>0)add("Decoded sample rate",QString("%1 kHz").arg(m_decodeRate/1000.0,0,'g',6));
    if(m_decodeChannels>0)add("Decoded channels",QString::number(m_decodeChannels));
  }
  add("File codec",track.value("codec").toString());
  if(volumeNormalization()) {
    const double tag=trackGainDb(track);
    const double measured=measuredLoudness(track.value("id").toString());
    if(!qIsNaN(tag))add("Volume normalization",QString("%1 dB · track tag").arg(qBound(-15.0,tag,6.0),0,'f',1));
    else if(!qIsNaN(measured))add("Volume normalization",QString("%1 dB · measured").arg(qBound(-15.0,-18.0-measured,6.0),0,'f',1));
    else add("Volume normalization","Not measured yet");
  }
  const double trim=trackTrim(track.value("id").toString());
  if(!qFuzzyIsNull(trim))add("Volume trim",QString("%1%2 dB").arg(trim>0?"+":"").arg(trim,0,'f',1));
  if(track.value("sampleRate").toInt()>0)add("File sample rate",QString("%1 kHz").arg(track.value("sampleRate").toInt()/1000.0,0,'g',6));
  if(track.value("bitrate").toInt()>0)add("File bitrate",QString("%1 kb/s").arg(track.value("bitrate").toInt()/1000));
  if(const int depth=track.value("bitDepth").toInt();depth>0)add("File bit depth",QString("%1-bit").arg(depth));
  if(const int channels=track.value("channels").toInt();channels>0)
    add("File channels",channels==1?"Mono":channels==2?"Stereo":QString("%1 channels").arg(channels));
  return result;
}

QString Backend::previewLyric(qint64 at) const {
  at+=lyricOffset();int low=0,high=m_lyricLines.size();
  while(low<high){const int mid=(low+high)/2;if(m_lyricLines[mid].toMap().value("start").toLongLong()<=at)low=mid+1;else high=mid;}
  if(low==0)return {};
  const auto line=m_lyricLines[low-1].toMap();
  return at<line.value("end").toLongLong()?line.value("text").toString():QString();
}

void Backend::resetAudioLevels() {
  m_levelIdle.stop();m_levelPublish.invalidate();m_levelAnalyzer.reset();
  const QVariantList silence{0.0,0.0,0.0,0.0,0.0};
  if(m_audioLevels!=silence){m_audioLevels=silence;emit audioLevelsChanged();}
}


// Two reasons to look a song up on Apple: its animated cover, and, for a song
// whose only cover is a video frame, its still one. Either alone is enough.
bool Backend::motionLookupWanted() const {
  return motion() && animatedArtwork() && onlineArtwork() && artworkChoice().isEmpty() && current().value("motionArt").toString().isEmpty();
}
bool Backend::coverLookupWanted() const {
  return albumCovers() && !artworkurl::videoId(QUrl(current().value("art").toString())).isEmpty();
}
void Backend::updateOnlineArtwork() {
  const auto song=current();const auto id=song.value("id").toString();
  const bool changed=id!=m_onlineArtworkId || m_trackToken!=m_onlineArtworkToken;
  const bool motionWanted=motionLookupWanted(),coverWanted=coverLookupWanted();
  const bool enabled=motionWanted || coverWanted;
  if(changed || !enabled){
    m_onlineArtworkTimer.stop();cancel("motion-artwork");++m_onlineArtworkGeneration;
    m_onlineArtworkId=id;m_onlineArtworkToken=m_trackToken;m_onlineArtworkAttempted=false;m_onlineArtworkRetries=0;
    m_artworkPage.clear();m_artworkStatus.clear();m_onlineMotionArt.clear();emit onlineArtworkChanged();
  }
  const bool eligible=enabled && m_uiActive && playing() && !song.value("videoId").toString().isEmpty()
      && song.value("localPath").toString().isEmpty() && !isServerSource(song.value("source"))
      && !song.value("artist").toString().isEmpty();
  if(!eligible){
    m_onlineArtworkTimer.stop();
    if(m_processes.contains("motion-artwork")){
      cancel("motion-artwork");++m_onlineArtworkGeneration;m_onlineArtworkAttempted=false;
    }
    return;
  }
  // A frame whose cover is already known, found or not, is not asked about again.
  if(!motionWanted && m_videoCovers.contains(song.value("videoId").toString()))return;
  if(!m_onlineArtworkAttempted && !m_onlineArtworkTimer.isActive())m_onlineArtworkTimer.start();
}
void Backend::fetchOnlineArtwork() {
  const bool motionWanted=motionLookupWanted();
  m_onlineArtworkAttempted=true;
  // The status line belongs to the artwork controls, which are about the
  // animated cover. A lookup running only for a still cover says nothing
  // there; the cover itself is the feedback.
  if(motionWanted){m_artworkStatus="Looking for a cover…";emit onlineArtworkChanged();}
  const auto directory=audioDirectory();if(!directory || !directory->isValid())return;
  const auto root=QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/motion-art";
  auto args=current();args["op"]="online-artwork";args["artworkCache"]=root;args["scratch"]=directory->path();args["refresh"]=m_artworkForce;args["motion"]=motionWanted;args["covers"]=coverLookupWanted();m_artworkForce=false;
  const auto generation=++m_onlineArtworkGeneration;
  const auto videoId=current().value("videoId").toString();
  request("motion-artwork",args,[this,generation,root,motionWanted,videoId](const QVariantMap &data){
    if(generation!=m_onlineArtworkGeneration || !m_uiActive)return;
    if(motionWanted){m_artworkStatus="No animated cover found";emit onlineArtworkChanged();}
    if((data.value("status")=="retry" || !data.value("ok").toBool()) && m_onlineArtworkRetries++<1 && playing()){
      if(motionWanted){m_artworkStatus="Waiting to retry";emit onlineArtworkChanged();}
      m_onlineArtworkAttempted=false;
      m_onlineArtworkTimer.start(qBound(1,data.value("retryAfter",30).toInt(),3600)*1000);
      return;
    }
    if(!data.value("ok").toBool())return;
    // The still cover is remembered against the video id, so every surface
    // drawing that frame swaps it in, and remembered when absent too, so the
    // song is not looked up again on every play. Only Apple's own image URLs
    // are kept, which is what the art loader relies on.
    if(coverLookupWanted()){
      const QUrl art(data.value("art").toString());
      rememberVideoCover(videoId,artworkurl::isAlbumCover(art)?art.toString():QString());
    }
    if(!motionWanted || !motionLookupWanted())return;
    const QUrl url(data.value("motionArt").toString());const QFileInfo file(url.toLocalFile());
    if(!url.isLocalFile() || !file.isFile() || file.isSymLink()
        || file.suffix()!="mp4" || file.size()<=0 || file.size()>16*1024*1024
        || file.canonicalPath()!=QFileInfo(root).canonicalFilePath())return;
    m_onlineMotionArt=url.toString();m_artworkPage=data.value("page").toString();m_artworkStatus="Online album cover";emit onlineArtworkChanged();
  },directory);
}

void Backend::setSleepFade(bool enabled) {
  m_settings.setValue("sleepFade",enabled);
  m_sleepFadeStart.stop();m_sleepFadeTick.stop();
  if(enabled && m_sleepTimer.isActive())m_sleepFadeStart.start(qMax(0,m_sleepTimer.remainingTime()-30000));
  updateSleepGain();emit settingsChanged();
}
void Backend::updateSleepGain() {
  qint64 remaining=-1;
  if(sleepFade()) {
    if(m_sleepTimer.isActive())remaining=m_sleepTimer.remainingTime();
    // End of queue fades over the last track, exactly as end of track does.
    else if((m_sleepAtEnd||(m_sleepAtQueueEnd&&!shuffle()&&repeat()==0&&m_index+1>=m_queue.count())) && duration()>0)
      remaining=qMax<qint64>(0,duration()-position())/playbackRate();
  }
  m_sleepGain=remaining<0?1.0:qBound(0.0,remaining/30000.0,1.0);
  applyOutputVolume();
}
void Backend::playGroup(const QString &key,bool folders) {
  QVariantList songs;
  for(const auto &v:m_collection.items()) {
    const auto t=v.toMap();
    const auto group=folders?CollectionView::folder(t):QString("Disc %1").arg(qMax(1,t.value("discNumber",1).toInt()));
    if(group==key)songs.append(t);
  }
  songs=playable(songs);if(songs.isEmpty())return;
  invalidateUndo("queue");m_queue.reconcile(queueWithOrigin(songs,"collection"));playAt(0);
}

// --- Crossfade and gapless handover -----------------------------------------
//
// The spare deck is loaded with the next song before the current one runs out.
// With an overlap configured, both play while their volumes trade places on an
// equal-power curve, so the pair keeps a steady loudness through the change.
// With no overlap, the spare simply starts the instant the other stops, which
// removes the pause the media pipeline would otherwise spend loading.
//
// Either way the decks swap roles. Nothing is handed between them part-way
// through a song, so neither side has a seam.

void Backend::setCrossfadeSeconds(int seconds) {
  seconds=qBound(0,seconds,12);
  if(seconds==crossfadeSeconds())return;
  m_settings.setValue("crossfadeSeconds",seconds);
  if(seconds==0)endCrossfade(false);
  clearSpare();
  emit settingsChanged();
}

void Backend::setGapless(bool enabled) {
  if(gapless()==enabled)return;
  m_settings.setValue("gapless",enabled);
  if(!enabled)clearSpare();
  emit settingsChanged();
}

// Which queue position the handover should aim at, or -1 when there is nothing
// to aim at. Shuffle picks its destination here rather than at the last moment,
// because the deck has to be loaded with a decision already made.
int Backend::handoffTarget() {
  if(m_queue.count()<2 || repeat()==2)return -1;
  if(shuffle()){
    int i=m_index,guard=0;
    while(i==m_index && ++guard<64)i=QRandomGenerator::global()->bounded(m_queue.count());
    return i==m_index?-1:i;
  }
  if(m_index+1<m_queue.count())return m_index+1;
  if(repeat()==1)return 0;
  return -1;
}

// A source the spare deck can start from without going back to the network.
// Anything that would still need resolving simply does not get a head start,
// and the ordinary transition carries it instead.
QUrl Backend::readySource(const QVariantMap &track) const {
  const auto local=track.value("localPath").toString();
  if(!local.isEmpty()){
    const QFileInfo file(local);
    return file.isFile() && file.isReadable() ? QUrl::fromLocalFile(file.absoluteFilePath()) : QUrl();
  }
  const auto id=track.value("videoId").toString();
  if(id.isEmpty() || id!=m_preparedId || m_preparedData.isEmpty())return {};
  const auto file=m_preparedData.value("file").toString();
  if(!file.isEmpty() && QFileInfo(file).isFile())return QUrl::fromLocalFile(file);
  const QUrl url(m_preparedData.value("url").toString());
  return url.scheme()=="https" ? url : QUrl();
}

bool Backend::armHandoff(bool playImmediately,int target) {
  if(target<0)target=handoffTarget();
  if(target<0)return false;
  const auto track=m_queue.get(target);
  const auto source=readySource(track);
  if(!source.isValid() || source.isEmpty())return false;
  auto &deck=spareDeck();
  deck.stop();
  deck.setSource(source);
  deck.setPlaybackRate(m_media().playbackRate());
  spareAudio().setDevice(activeAudio().device());
  spareAudio().setVolume(0);
  m_handoffIndex=target;
  m_handoffPrepared=track.value("localPath").toString().isEmpty();
  if(playImmediately)deck.play();
  return true;
}

void Backend::clearSpare() {
  auto &deck=spareDeck();
  deck.stop();
  deck.setSource(QUrl());
  spareAudio().setVolume(0);
  m_handoffIndex=-1;
  m_handoffPrepared=false;
}

// An overlap is a transition between two separate pieces of music, so two
// pairs are left alone.
//
// Two tracks of one album are a single piece cut in two. A record that runs
// its songs together, a live set and a continuous mix all put the seam exactly
// where the recording says, and blending across it is heard as damage rather
// than as a transition. Second, a song the queue rolled into on its own is not
// a transition the listener arranged, so autoplay is joined rather than mixed
// into.
//
// Neither case falls back to a gap: the seamless handover below carries them,
// which is the same change without the blend. This is the choice Material
// leaves to the product, and it needs no setting because there is no listener
// who wants an album cross-faded with itself.
bool Backend::overlapSuits(int target) const {
  if(target<0 || target>=m_queue.count())return false;
  const auto next=m_queue.get(target);
  if(next.value("_queueOrigin").toString()=="autoplay")return false;
  if(m_index<0 || m_index>=m_queue.count())return true;
  return !sameAlbum(m_queue.get(m_index),next);
}

void Backend::considerCrossfade() {
  if(!playing() || m_resolving)return;
  const auto total=m_media().duration();
  if(total<=0)return;
  const qint64 remaining=total-m_media().position();
  if(remaining<0)return;
  const int seconds=crossfadeSeconds();
  const qint64 window=qint64(seconds)*1000;
  // A recording shorter than two overlaps would be mostly overlap.
  if(seconds>0 && !m_crossfading && total>=window*2 && remaining<=window){
    // The destination is settled once, here, so the pair that is judged is the
    // pair that plays. Asking twice would let shuffle answer differently.
    const int target=m_handoffIndex>=0?m_handoffIndex:handoffTarget();
    if(target>=0 && overlapSuits(target)){beginCrossfade(int(window),target);return;}
  }
  // No overlap: the spare still warms up, so the change is not a pause.
  if(!gapless() || m_handoffIndex>=0 || remaining>2500)return;
  armHandoff(false);
}

void Backend::beginCrossfade(int milliseconds,int target) {
  if(!armHandoff(true,target))return;
  m_crossfading=true;
  m_crossfadeMs=qMax(200,milliseconds);
  m_crossfadeClock.restart();
  m_crossfadeTick.start();
  emit playbackChanged();
}

void Backend::stepCrossfade() {
  if(!m_crossfading)return;
  const double t=qBound(0.0,double(m_crossfadeClock.elapsed())/double(m_crossfadeMs),1.0);
  // Equal power. Two linear ramps would dip in the middle, where both songs are
  // half volume; sine and cosine keep the sum of their powers constant.
  const double settled=settledVolume();
  activeAudio().setVolume(qBound(0.0,settled*std::cos(t*M_PI/2),1.0));
  spareAudio().setVolume(qBound(0.0,settled*std::sin(t*M_PI/2),1.0));
  if(t>=1.0)endCrossfade(true);
}

void Backend::endCrossfade(bool completed) {
  m_crossfadeTick.stop();
  if(!m_crossfading){
    if(!completed)clearSpare();
    return;
  }
  m_crossfading=false;
  const int target=m_handoffIndex;
  if(!completed || target<0){
    clearSpare();
    updateNormalization();
    emit playbackChanged();
    return;
  }
  adoptHandoff(target);
}

// The spare deck has been playing the next song for a moment; make it the deck
// the rest of the application is looking at, and move the queue with it.
void Backend::adoptHandoff(int index) {
  if(index<0 || index>=m_queue.count()){clearSpare();return;}
  // The song that just finished has nothing to return to.
  clearResumePosition(current().value("id").toString());
  const bool wasPrepared=m_handoffPrepared;
  m_handoffIndex=-1;
  m_handoffPrepared=false;

  auto &leaving=m_media();
  m_usingB=!m_usingB;
  leaving.setAudioBufferOutput(nullptr);
  m_media().setAudioBufferOutput(&m_visualAudio);
  leaving.stop();
  leaving.setSource(QUrl());
  spareAudio().setVolume(0);

  invalidateUndo("queue-order");
  cancel("play");cancel("linkplay");cancel("lyrics");cancel("radio");
  m_wantPlay=true;m_stopped=false;m_recoveryAttempts=0;m_recovering=false;
  ++m_trackToken;
  m_savedPosition=0;
  m_playbackDirection=1;
  m_index=index;
  // A prepared stream lives in a temporary directory the player now depends on.
  if(wasPrepared){
    if(m_preparedDirectory)m_audioCache=std::move(m_preparedDirectory);
    m_preparedData.clear();m_preparedId.clear();
  }
  dismissError();
  m_retry=false;
  m_restorePosition=0;
  m_resolving=false;
  m_lyricsLoaded=false;m_lyricsSource.clear();m_lyrics.clear();m_lyricLines.clear();m_lyricsBusy=false;
  resetAudioLevels();
  emit lyricsChanged();
  emit trackChanged();
  emit libraryChanged();
  // Nothing transitioned from stopped to playing, so the bookkeeping that
  // normally rides on that change has to be asked for.
  recordHistory();
  notifyTrack();
  updateNormalization();
  if(m_uiActive)m_positionTick.start();
  emit positionChanged();
  emit playbackChanged();
  m_saveTimer.start();
}

// Called when a song ends and a deck is waiting behind it. Returns true when
// that deck took over, so the ordinary advance is not also run.
//
// An overlap that was running has already been settled by the time this is
// reached, so what arrives here is always a handover: either no overlap was
// configured, or the pair was one an overlap does not suit. Reading the
// crossfade setting here instead would drop the second case back onto the
// ordinary transition, which is the pause the waiting deck exists to avoid.
bool Backend::finishGapless() {
  if(m_handoffIndex<0 || !gapless() || m_crossfading)return false;
  const int target=m_handoffIndex;
  if(target<0 || target>=m_queue.count()){clearSpare();return false;}
  auto &deck=spareDeck();
  if(deck.mediaStatus()==QMediaPlayer::NoMedia || deck.mediaStatus()==QMediaPlayer::InvalidMedia){
    clearSpare();return false;
  }
  spareAudio().setVolume(settledVolume());
  deck.play();
  adoptHandoff(target);
  return true;
}
