#pragma once
#include <QAbstractListModel>
#include <QAudioOutput>
#include <QAudioBufferOutput>
#include "audiolevels.h"
#include <QMediaPlayer>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <QTemporaryDir>
#include <QVariantMap>
#include <functional>
#include <memory>
#include <QMediaDevices>
#include <QAudioDevice>
#include "collectionview.h"
#include "playbacknotifier.h"
#include "musicserver.h"
#include <QElapsedTimer>
#include <QFileSystemWatcher>

class Entries : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
  using QAbstractListModel::QAbstractListModel;
  int rowCount(const QModelIndex &p = {}) const override {
    return p.isValid() ? 0 : rows.size();
  }
  int count() const { return rows.size(); }
  QVariant data(const QModelIndex &i, int role) const override {
    if (!i.isValid() || i.row()<0 || i.row()>=rows.size()) return {};
    if(role==Qt::UserRole) return rows[i.row()];
    if(role==Qt::UserRole+4){const auto origin=rows[i.row()].toMap().value("_queueOrigin").toString();if(origin=="manual")return "Added by you";if(origin=="autoplay")return "Autoplay";if(origin=="collection")return "Collection";return data(i,Qt::UserRole+1);}
    if(role==Qt::UserRole+3)return QString("Disc %1").arg(qMax(1,rows[i.row()].toMap().value("discNumber",1).toInt()));
    if(role==Qt::UserRole+2) return CollectionView::folder(rows[i.row()].toMap());
    if(role==Qt::UserRole+1) {
      const auto t=rows[i.row()].toMap();
      return isServerSource(t.value("source")) ? "Music server" : !t.value("localPath").toString().isEmpty() ? "Local files" : "YouTube Music";
    }
    return {};
  }
  QHash<int, QByteArray> roleNames() const override {
    return {{Qt::UserRole, "entry"},{Qt::UserRole+1,"musicSource"},{Qt::UserRole+2,"musicFolder"},{Qt::UserRole+3,"musicDisc"},{Qt::UserRole+4,"queueOrigin"}};
  }
  void assign(const QVariantList &v) {
    beginResetModel();
    rows = v;
    endResetModel();
    emit countChanged();
  }
  // Preserve delegates during small queue edits. Large replacements stay bounded.
  void reconcile(const QVariantList &v) {
    if(rows.size()>512 || v.size()>512){assign(v);return;}
    for(int i=0;i<v.size();++i){
      if(i<rows.size() && rows[i]==v[i])continue;
      int found=-1;for(int j=i+1;j<rows.size();++j)if(rows[j]==v[i]){found=j;break;}
      if(found>=0){beginMoveRows({},found,found,{},i);rows.move(found,i);endMoveRows();}
      else {beginInsertRows({},i,i);rows.insert(i,v[i]);endInsertRows();}
    }
    if(rows.size()>v.size()){beginRemoveRows({},v.size(),rows.size()-1);rows.erase(rows.begin()+v.size(),rows.end());endRemoveRows();}
    emit countChanged();
  }
  Q_INVOKABLE QVariantMap get(int i) const {
    return i >= 0 && i < rows.size() ? rows[i].toMap() : QVariantMap();
  }
  void append(const QVariantList &v) {
    if(v.isEmpty()) return;
    beginInsertRows({}, rows.size(), rows.size()+v.size()-1);
    rows.append(v); endInsertRows(); emit countChanged();
  }
  QVariantList rows;
signals:
  void countChanged();
};

class Backend : public QObject {
  Q_OBJECT
  Q_PROPERTY(MusicServer *server READ server CONSTANT)
  Q_PROPERTY(bool serverPlaylistEditable READ serverPlaylistEditable NOTIFY catalogChanged)
  Q_PROPERTY(QVariantMap serverRequest READ serverRequest NOTIFY catalogChanged)
  Q_PROPERTY(bool importingLocal READ importingLocal NOTIFY localImportChanged)
  Q_PROPERTY(QString localImportStatus READ localImportStatus NOTIFY localImportChanged)
  Q_PROPERTY(QStringList recentSearches READ recentSearches NOTIFY recentSearchesChanged)
  Q_PROPERTY(Entries *results READ results CONSTANT)
  Q_PROPERTY(Entries *queue READ queue CONSTANT)
  Q_PROPERTY(CollectionView *collection READ collection CONSTANT)
  Q_PROPERTY(QVariantList audioDevices READ audioDevices NOTIFY audioDevicesChanged)
  Q_PROPERTY(QString audioDeviceId READ audioDeviceId WRITE setAudioDeviceId NOTIFY audioDevicesChanged)
  Q_PROPERTY(QString audioDeviceName READ audioDeviceName NOTIFY audioDevicesChanged)
  Q_PROPERTY(QVariantList sections READ sections NOTIFY catalogChanged)
  Q_PROPERTY(QString viewKey READ viewKey NOTIFY catalogChanged)
  Q_PROPERTY(QString page READ page NOTIFY catalogChanged)
  Q_PROPERTY(QString libraryId READ libraryId NOTIFY catalogChanged)
  Q_PROPERTY(QString query READ query NOTIFY catalogChanged)
  Q_PROPERTY(QString searchFilter READ searchFilter NOTIFY catalogChanged)
  Q_PROPERTY(QString undoMessage READ undoMessage NOTIFY libraryChanged)
  Q_PROPERTY(bool canRetry READ canRetry NOTIFY errorChanged)
  Q_PROPERTY(QString title READ title NOTIFY catalogChanged)
  Q_PROPERTY(QString cover READ cover NOTIFY catalogChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY catalogChanged)
  Q_PROPERTY(bool canBack READ canBack NOTIFY catalogChanged)
  Q_PROPERTY(bool canMore READ canMore NOTIFY catalogChanged)
  Q_PROPERTY(QString error READ error NOTIFY errorChanged)
  Q_PROPERTY(QVariantMap current READ current NOTIFY trackChanged)
  Q_PROPERTY(int currentIndex READ currentIndex NOTIFY trackChanged)
  Q_PROPERTY(QVariantList audioLevels READ audioLevels NOTIFY audioLevelsChanged)
  Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
  Q_PROPERTY(bool resolving READ resolving NOTIFY playbackChanged)
  Q_PROPERTY(bool buffering READ buffering NOTIFY playbackChanged)
  Q_PROPERTY(QString coverPlayId READ coverPlayId NOTIFY playbackChanged)
  Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
  Q_PROPERTY(qint64 duration READ duration NOTIFY playbackChanged)
  Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY settingsChanged)
  Q_PROPERTY(double playbackRate READ playbackRate WRITE setPlaybackRate NOTIFY settingsChanged)
  Q_PROPERTY(bool preservePitch READ preservePitch WRITE setPreservePitch NOTIFY settingsChanged)
  Q_PROPERTY(bool pitchAdjustable READ pitchAdjustable CONSTANT)
  Q_PROPERTY(int lyricOffset READ lyricOffset WRITE setLyricOffset NOTIFY lyricsChanged)
  Q_PROPERTY(int lyricTextSize READ lyricTextSize WRITE setLyricTextSize NOTIFY settingsChanged)
  Q_PROPERTY(QString queueTime READ queueTime NOTIFY queueInfoChanged)
  Q_PROPERTY(QString queueEnd READ queueEnd NOTIFY queueInfoChanged)
  Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY settingsChanged)
  Q_PROPERTY(int repeat READ repeat WRITE setRepeat NOTIFY settingsChanged)
  Q_PROPERTY(
      bool autoplay READ autoplay WRITE setAutoplay NOTIFY settingsChanged)
  Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)
  Q_PROPERTY(bool pauseOnDisconnect READ pauseOnDisconnect WRITE setPauseOnDisconnect NOTIFY settingsChanged)
  Q_PROPERTY(bool currentArtworkFit READ currentArtworkFit WRITE setCurrentArtworkFit NOTIFY artworkFitChanged)
  Q_PROPERTY(int playbackDirection READ playbackDirection NOTIFY trackChanged)
  Q_PROPERTY(int lyricGapSeconds READ lyricGapSeconds NOTIFY positionChanged)
  Q_PROPERTY(bool viewCompactDensity READ viewCompactDensity NOTIFY presentationChanged)
  Q_PROPERTY(int viewDensity READ viewDensity WRITE setViewDensity NOTIFY presentationChanged)
  Q_PROPERTY(QString viewMode READ viewMode WRITE setViewMode NOTIFY presentationChanged)
  Q_PROPERTY(bool viewSupportsGrid READ viewSupportsGrid NOTIFY presentationChanged)
  Q_PROPERTY(bool compactDensity READ compactDensity WRITE setCompactDensity NOTIFY presentationChanged)
  Q_PROPERTY(QString startPage READ startPage WRITE setStartPage NOTIFY presentationChanged)
  Q_PROPERTY(QStringList homeOrder READ homeOrder NOTIFY presentationChanged)
  Q_PROPERTY(QStringList hiddenHomeSections READ hiddenHomeSections NOTIFY presentationChanged)
  Q_PROPERTY(bool artworkAccent READ artworkAccent WRITE setArtworkAccent NOTIFY settingsChanged)
  Q_PROPERTY(QVariantMap albumInfo READ albumInfo NOTIFY catalogChanged)
  Q_PROPERTY(QString currentMotionArt READ currentMotionArt NOTIFY onlineArtworkChanged)
  Q_PROPERTY(QString artworkStatus READ artworkStatus NOTIFY onlineArtworkChanged)
  Q_PROPERTY(QString artworkPage READ artworkPage NOTIFY onlineArtworkChanged)
  Q_PROPERTY(bool watchMusicFolders READ watchMusicFolders WRITE setWatchMusicFolders NOTIFY settingsChanged)
  Q_PROPERTY(QString onlineMotionArt READ onlineMotionArt NOTIFY onlineArtworkChanged)
  Q_PROPERTY(bool onlineArtwork READ onlineArtwork WRITE setOnlineArtwork NOTIFY settingsChanged)
  Q_PROPERTY(bool animatedArtwork READ animatedArtwork WRITE setAnimatedArtwork NOTIFY settingsChanged)
  Q_PROPERTY(bool motion READ motion WRITE setMotion NOTIFY settingsChanged)
  Q_PROPERTY(bool historyPaused READ historyPaused WRITE setHistoryPaused NOTIFY settingsChanged)
  Q_PROPERTY(bool trackNotifications READ trackNotifications WRITE setTrackNotifications NOTIFY settingsChanged)
  Q_PROPERTY(bool keepCompletedLyrics READ keepCompletedLyrics WRITE setKeepCompletedLyrics NOTIFY settingsChanged)
  Q_PROPERTY(int volumeStep READ volumeStep WRITE setVolumeStep NOTIFY settingsChanged)
  Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY settingsChanged)
  Q_PROPERTY(bool sleepFade READ sleepFade WRITE setSleepFade NOTIFY settingsChanged)
  Q_PROPERTY(QString sleepStatus READ sleepLabel NOTIFY settingsChanged)
  Q_PROPERTY(bool prepareNext READ prepareNext WRITE setPrepareNext NOTIFY settingsChanged)
  Q_PROPERTY(bool lyricsFallback READ lyricsFallback WRITE setLyricsFallback NOTIFY settingsChanged)
  Q_PROPERTY(QString lyricsSource READ lyricsSource NOTIFY lyricsChanged)
  Q_PROPERTY(QString lyrics READ lyrics NOTIFY lyricsChanged)
  Q_PROPERTY(QVariantList lyricLines READ lyricLines NOTIFY lyricsChanged)
  Q_PROPERTY(int lyricIndex READ lyricIndex NOTIFY lyricIndexChanged)
  Q_PROPERTY(bool lyricsBusy READ lyricsBusy NOTIFY lyricsChanged)
  Q_PROPERTY(QVariantList playlists READ playlists NOTIFY libraryChanged)
  Q_PROPERTY(QVariantList pins READ pins NOTIFY libraryChanged)
  Q_PROPERTY(QVariantMap collectionItem READ collectionItem NOTIFY catalogChanged)
  Q_PROPERTY(bool liked READ liked NOTIFY libraryChanged)
  Q_PROPERTY(QString cookies READ cookies NOTIFY settingsChanged)
  Q_PROPERTY(QStringList musicFolders READ musicFolders NOTIFY libraryChanged)
  Q_PROPERTY(bool cleanupBusy READ cleanupBusy NOTIFY cleanupChanged)
  Q_PROPERTY(QVariantList cleanupItems READ cleanupItems NOTIFY cleanupChanged)
public:
  Q_INVOKABLE QVariantMap smartPlaylist(const QString &id) const;
  Q_INVOKABLE QString saveSmartPlaylist(const QString &id,const QString &name,const QVariantMap &rules);
  Q_INVOKABLE QString previewLyric(qint64 position) const;
  Q_INVOKABLE QVariantList trackDetails(const QVariantMap &track) const;
  QVariantList playlistRows(const QVariantMap &playlist) const;
  QVariantList audioLevels() const {return m_audioLevels;}
  MusicServer *server() {return &m_server;}
  QString serverArtwork() const {return m_serverArtwork;}
  bool serverPlaylistEditable() const {return m_page=="server" && m_request.value("mode")=="playlist" && m_request.value("editable").toBool();}
  QVariantMap serverRequest() const {return m_page=="server"?m_request:QVariantMap{};}
  Q_INVOKABLE void browseServer(const QString &mode="albums",const QString &query={},const QString &filter="songs");
  Q_INVOKABLE void addServerPlaylist(const QString &remoteId,const QVariantList &songs);
  Q_INVOKABLE void renameServerPlaylist(const QVariantMap &playlist,const QString &name);
  Q_INVOKABLE void deleteServerPlaylist(const QVariantMap &playlist);
  Q_INVOKABLE void removeServerRows(const QVariantList &indices);
  Q_INVOKABLE void moveServerRows(const QVariantList &indices,int before);
  Q_INVOKABLE void rateServerSong(const QVariantMap &song,int rating);
  Q_INVOKABLE void saveServerQueue();
  Q_INVOKABLE void restoreServerQueue();
  QStringList musicFolders() const { return m_musicFolders; }
  Q_INVOKABLE QString musicFolderLabel(const QString &path) const;
  bool cleanupBusy() const { return m_cleanupBusy; }
  QVariantList cleanupItems() const { return m_cleanupItems; }
  Q_INVOKABLE void importMusicFolder(const QUrl &url);
  Q_INVOKABLE QString importMusicFolderPath(const QString &input);
  Q_INVOKABLE void rescanMusicFolders();
  Q_INVOKABLE void forgetMusicFolder(const QString &path);
  Q_INVOKABLE void inspectPlaylist(const QString &id);
  Q_INVOKABLE void applyPlaylistCleanup(bool duplicates,bool missing);
  Q_INVOKABLE void closePlaylistCleanup();
  explicit Backend(QObject *parent = nullptr);
  bool importingLocal() const {return m_scanningFolders || m_importTotal>0;}
  QString localImportStatus() const;
  Q_INVOKABLE void importLocalFiles(const QVariantList &urls);
  Q_INVOKABLE void cancelLocalImport();
  Q_INVOKABLE void locateLocalFile(const QUrl &url,const QString &id);
  Q_INVOKABLE void removeLocalFile(const QString &id);
  Q_INVOKABLE QVariantList searchLyrics(const QString &query) const;
  ~Backend() override;
  Entries *results() { return &m_results; }
  Entries *queue() { return &m_queue; }
  CollectionView *collection() { return &m_collection; }
  QVariantList audioDevices() const;
  QString audioDeviceId() const {return m_settings.value("audioDevice").toString();}
  QString audioDeviceName() const;
  void setAudioDeviceId(const QString &id);
  QVariantList sections() const { return m_sections; }
  QString page() const { return m_page; }
  QString viewKey() const { return m_viewKey; }
  QString libraryId() const { return m_libraryId; }
  QString query() const { return m_request.value("query").toString(); }
  QString searchFilter() const {
    return m_request.value("filter", "songs").toString();
  }
  QString undoMessage() const { return m_undoMessage; }
  bool canRetry() const { return !m_retryTarget.isEmpty(); }
  Q_INVOKABLE void retry();
  Q_INVOKABLE void undo();
  Q_INVOKABLE void startSearch();
  QString title() const { return m_title; }
  QString cover() const { return m_cover; }
  bool busy() const { return m_busy; }
  bool canBack() const { return !m_back.isEmpty(); }
  bool canMore() const { return m_more; }
  QString error() const { return m_error; }
  QVariantMap current() const { return m_queue.get(m_index); }
  int currentIndex() const { return m_index; }
  bool playing() const {
    return m_media.playbackState() == QMediaPlayer::PlayingState;
  }
  QString coverPlayId() const { return m_coverPlayId; }
  bool resolving() const { return m_resolving; }
  bool buffering() const { return m_wantPlay && (m_resolving || m_media.mediaStatus()==QMediaPlayer::LoadingMedia || m_media.mediaStatus()==QMediaPlayer::StalledMedia || m_media.mediaStatus()==QMediaPlayer::BufferingMedia); }
  qint64 position() const { return m_media.source().isEmpty() ? m_savedPosition : m_media.position(); }
  qint64 duration() const {
    return m_media.duration() > 0
               ? m_media.duration()
               : current().value("seconds").toLongLong() * 1000;
  }
  double volume() const { return m_userVolume; }
  bool sleepFade() const { return m_settings.value("sleepFade",true).toBool(); }
  void setSleepFade(bool enabled);
  void setVolume(double);
  double playbackRate() const {return m_media.playbackRate();}
  void setPlaybackRate(double rate);
  bool pitchAdjustable() const;
  bool preservePitch() const;
  void setPreservePitch(bool enabled);
  int lyricOffset() const;
  int lyricTextSize() const {return qBound(20,m_settings.value("lyricTextSize",25).toInt(),32);}
  void setLyricTextSize(int size);
  qint64 queueRemainingMs() const;
  QString queueTime() const;
  QString queueEnd() const;
  void setLyricOffset(int milliseconds);
  Q_INVOKABLE void seekLyric(qint64 milliseconds);
  Q_INVOKABLE void playKeepingQueue(const QVariantMap &item);
  bool shuffle() const { return m_settings.value("shuffle", false).toBool(); }
  void setShuffle(bool);
  int repeat() const { return m_settings.value("repeat", 0).toInt(); }
  void setRepeat(int);
  bool autoplay() const { return m_settings.value("autoplay", true).toBool(); }
  void setAutoplay(bool);
  QVariantList sessions() const;
  int playbackDirection() const {return m_playbackDirection;}
  int lyricGapSeconds() const;
  bool viewCompactDensity() const {const int value=viewDensity();return value<0?compactDensity():value==1;}
  int viewDensity() const;
  void setViewDensity(int value);
  QString viewMode() const;
  void setViewMode(const QString &value);
  bool viewSupportsGrid() const;
  static QVariantList queueWithOrigin(const QVariantList &items,const QString &origin);
  bool compactDensity() const {return m_settings.value("compactDensity",false).toBool();}
  void setCompactDensity(bool value);
  QString startPage() const {return m_settings.value("startPage","home").toString();}
  void setStartPage(const QString &value);
  QStringList homeOrder() const {return m_settings.value("homeOrder").toStringList();}
  QStringList hiddenHomeSections() const {return m_settings.value("hiddenHomeSections").toStringList();}
  Q_INVOKABLE QVariantList homeSections(bool includeHidden=false) const;
  Q_INVOKABLE void moveHomeSection(const QString &title,int offset);
  Q_INVOKABLE void showHomeSection(const QString &title,bool show);
  Q_INVOKABLE void resetHomeLayout();
  Q_INVOKABLE void openStartPage();
  Q_INVOKABLE bool saveSession(const QString &name,const QString &id=QString());
  Q_INVOKABLE void renameSession(const QString &id,const QString &name);
  Q_INVOKABLE void deleteSession(const QString &id);
  Q_INVOKABLE bool restoreSession(const QString &id);
  bool pauseOnDisconnect() const {return m_settings.value("pauseOnDisconnect",false).toBool();}
  void setPauseOnDisconnect(bool enabled);
  Q_INVOKABLE bool artworkFits(const QVariantMap &track) const;
  bool currentArtworkFit() const {return artworkFits(current());}
  void setCurrentArtworkFit(bool enabled);
  bool artworkAccent() const {return m_settings.value("artworkAccent",false).toBool();}
  void setArtworkAccent(bool enabled) {if(artworkAccent()==enabled)return;m_settings.setValue("artworkAccent",enabled);emit settingsChanged();}
  Q_INVOKABLE QString preparePlaylistCover(const QUrl &url);
  Q_INVOKABLE bool setPlaylistCover(const QString &id,const QString &preview,double x=0.5,double y=0.5,double zoom=1);
  Q_INVOKABLE void resetPlaylistCover(const QString &id);
  QVariantList localGroups(const QString &kind) const;
  QVariantList localGroupRows(const QVariantMap &item) const;
  void openLocalGroup(const QVariantMap &item);
  void updateLocalView();
  QVariantMap albumInfo() const;
  QString currentMotionArt() const;
  QString artworkStatus() const;
  QString artworkPage() const { return m_artworkPage; }
  Q_INVOKABLE void retryArtwork();
  Q_INVOKABLE void rejectArtwork();
  Q_INVOKABLE void resetArtworkChoice();
  Q_INVOKABLE void chooseArtwork(const QUrl &url, const QString &songId);
  bool watchMusicFolders() const { return m_settings.value("watchMusicFolders",true).toBool(); }
  void setWatchMusicFolders(bool value);
  QString onlineMotionArt() const { return m_onlineMotionArt; }
  bool onlineArtwork() const { return m_settings.value("onlineArtwork",true).toBool(); }
  void setOnlineArtwork(bool value) { if(onlineArtwork()==value)return;m_settings.setValue("onlineArtwork",value);emit settingsChanged(); }
  bool animatedArtwork() const { return m_settings.value("animatedArtwork",true).toBool(); }
  void setAnimatedArtwork(bool value) { if(animatedArtwork()==value)return;m_settings.setValue("animatedArtwork",value);emit settingsChanged(); }
  bool motion() const { return m_settings.value("motion", true).toBool(); }
  void setMotion(bool);
  Q_INVOKABLE void setUiActive(bool active);
  bool historyPaused() const {return m_historyPaused;}
  void setHistoryPaused(bool paused);
  bool trackNotifications() const {return m_settings.value("trackNotifications",false).toBool();}
  void setTrackNotifications(bool enabled);
  bool keepCompletedLyrics() const {return m_settings.value("keepCompletedLyrics",true).toBool();}
  void setKeepCompletedLyrics(bool enabled);
  int volumeStep() const {const int value=m_settings.value("volumeStep",5).toInt();return value==1||value==2||value==10?value:5;}
  void setVolumeStep(int percent);
  QString theme() const { return m_settings.value("theme", "system").toString(); }
  void setTheme(const QString &);
  QString cookies() const { return m_settings.value("cookies").toString(); }
  bool prepareNext() const {return m_settings.value("prepareNext",true).toBool();}
  void setPrepareNext(bool enabled);
  bool lyricsFallback() const {return m_settings.value("lyricsFallback",true).toBool();}
  void setLyricsFallback(bool enabled);
  QString lyricsSource() const {return m_lyricsSource;}
  Q_INVOKABLE void importLyrics(const QUrl &url, const QString &songId);
  Q_INVOKABLE void resetLyrics();
  Q_INVOKABLE void reloadLyrics();
  QString lyrics() const { return m_lyrics; }
  QVariantList lyricLines() const { return m_lyricLines; }
  int lyricIndex() const;
  bool lyricsBusy() const { return m_lyricsBusy; }
  QVariantList playlists() const;
  QVariantList pins() const;
  QVariantMap collectionItem() const;
  Q_INVOKABLE bool isPinned(const QVariantMap &item) const;
  Q_INVOKABLE void togglePin(const QVariantMap &item);
  bool liked() const;
  Q_INVOKABLE bool isLiked(const QString &id) const;
  QStringList recentSearches() const {return m_settings.value("recentSearches").toStringList().mid(0,12);}
  Q_INVOKABLE void rememberSearch(const QString &query);
  Q_INVOKABLE void removeRecentSearch(const QString &query);
  Q_INVOKABLE QVariantList localMatches(const QString &query) const;
  Q_INVOKABLE void enqueueItems(const QVariantList &items, bool next=false, int before=-1);
  Q_INVOKABLE QVariantMap playlistAdditionInfo(const QString &id, const QVariantList &items) const;
  Q_INVOKABLE void addItemsToPlaylist(const QString &id,const QVariantList &items);
  Q_INVOKABLE void removePlaylistRows(const QString &id,const QVariantList &indices);
  Q_INVOKABLE void movePlaylistRows(const QString &id,const QVariantList &indices,int before);
  Q_INVOKABLE void removeQueueRows(const QVariantList &indices);
  Q_INVOKABLE void moveQueueRows(const QVariantList &indices,int before);
  Q_INVOKABLE void home();
  Q_INVOKABLE void search(const QString &query,
                          const QString &filter = "songs");
  Q_INVOKABLE void open(const QVariantMap &item);
  Q_INVOKABLE void back();
  Q_INVOKABLE void saveQueue(const QString &name);
  Q_INVOKABLE void movePlaylistTrack(const QString &id,int from,int to);
  Q_INVOKABLE void exportLibrary(const QUrl &url);
  Q_INVOKABLE void importLibrary(const QUrl &url);
  Q_INVOKABLE void playLink(const QString &url);
  Q_INVOKABLE void more();
  Q_INVOKABLE void refresh();
  Q_INVOKABLE void library(const QString &kind = "favorites");
  Q_INVOKABLE void openPlaylist(const QString &id);
  Q_INVOKABLE QString createPlaylist(const QString &name);
  Q_INVOKABLE void renamePlaylist(const QString &id, const QString &name);
  Q_INVOKABLE void deletePlaylist(const QString &id);
  Q_INVOKABLE void addToPlaylist(const QString &id, const QVariantMap &item);
  Q_INVOKABLE void removeFromPlaylist(const QString &id, int index);
  Q_INVOKABLE void toggleLike(const QVariantMap &item);
  Q_INVOKABLE void playAt(int index, int direction=0);
  Q_INVOKABLE void playResults(int index = 0);
  Q_INVOKABLE void playCollection(int index = 0);
  Q_INVOKABLE void enqueueCollection();
  Q_INVOKABLE void playCover(const QVariantMap &item);
  Q_INVOKABLE void playItem(const QVariantMap &item);
  Q_INVOKABLE void enqueue(const QVariantMap &item, bool next = false);
  Q_INVOKABLE void enqueueResults();
  Q_INVOKABLE void moveQueue(int from, int to);
  Q_INVOKABLE void removeQueue(int index);
  Q_INVOKABLE void clearQueue();
  Q_INVOKABLE void smartShuffleQueue();
  Q_INVOKABLE void next();
  Q_INVOKABLE void previous();
  Q_INVOKABLE void toggle();
  Q_INVOKABLE void play();
  Q_INVOKABLE void pause();
  Q_INVOKABLE void stop();
  Q_INVOKABLE void seek(qint64 position);
  Q_INVOKABLE void radio(const QVariantMap &item);
  Q_INVOKABLE void fetchLyrics();
  Q_INVOKABLE void dismissError() {
    m_error.clear();
    m_retryTarget.clear();
    emit errorChanged();
  }
  Q_INVOKABLE void copyLink(const QVariantMap &item);
  Q_INVOKABLE void openLink(const QString &url);
  Q_INVOKABLE void setCookieFile(const QUrl &url);
  Q_INVOKABLE void clearCookies();
  Q_INVOKABLE void clearHistory();
  Q_INVOKABLE void clearCache();
  Q_INVOKABLE QString formatTime(qint64 ms) const;
  Q_INVOKABLE void setSleep(int minutes);
  Q_INVOKABLE QString sleepLabel() const;
  Q_INVOKABLE void playGroup(const QString &key, bool folders);
  Q_INVOKABLE void save();
  void notifyError(const QString &message, const QString &retryTarget = {});
  void localTestSource(const QUrl &url);
  bool stopped() const { return m_stopped; }
  QString trackToken() const { return QString::number(m_trackToken); }
  QMediaPlayer *media() { return &m_media; }
signals:
  void onlineArtworkChanged();
  void viewAboutToChange();
  void localImportChanged();
  void cleanupChanged();
  void recentSearchesChanged();
  void catalogChanged();
  void errorChanged();
  void trackChanged();
  void playbackChanged();
  void audioLevelsChanged();
  void positionChanged();
  void lyricIndexChanged();
  void settingsChanged();
  void libraryChanged();
  void lyricsChanged();
  void audioDevicesChanged();
  void queueInfoChanged();
  void sessionsChanged();
  void presentationChanged();
  void qualityChanged();
  void artworkFitChanged();
  void artworkCacheCleared();
  void seeked(qint64 position);
  void toast(const QString &message);
  void raiseRequested();

private:
  void updateOnlineArtwork();
  void fetchOnlineArtwork();
  QString artworkChoice() const;
  void saveArtworkChoice(const QString &value);
  QString m_artworkPage, m_artworkStatus;
  bool m_artworkForce=false;
  QTimer m_onlineArtworkTimer;
  QString m_onlineMotionArt, m_onlineArtworkId;
  quint64 m_onlineArtworkToken=0, m_onlineArtworkGeneration=0;
  bool m_onlineArtworkAttempted=false;
  int m_onlineArtworkRetries=0;
  friend class BackendTest;
  friend class SubsonicTest;
  void serverBrowseRequest(QVariantMap request,bool push=true,bool append=false);
  void setupServer();
  MusicServer m_server;
  QString m_serverArtwork;
  std::shared_ptr<QTemporaryDir> m_serverArtDirectory;
  QTimer m_serverListenTimer;
  QElapsedTimer m_serverElapsed;
  quint64 m_serverListenToken=0;
  qint64 m_serverListened=0,m_serverStarted=0;
  bool m_serverSubmitted=false;

  using Callback = std::function<void(const QVariantMap &)>;
  void invalidateUndo(const QString &type);
  void clearLyrics();
  void recoverStream();
  void restorePlaybackPosition();
  void applyAudioDevice();
  void outputsChanged();
  void setupDisconnectMonitor();
  void refreshOutputPort();
  void inspectOutputPorts(const QVariantList &sinks);
  void pauseForDisconnect();
  QByteArray m_outputId;
  QString m_outputDescription,m_outputPort;
  QProcess m_portMonitor,m_portProbe;
  QTimer m_portDebounce,m_portTimeout;
  bool m_portDirty=false;
  int m_decodeRate=0,m_decodeChannels=0;
  QVariantList m_sessions;
  void request(const QString &channel, QVariantMap args, Callback done, std::shared_ptr<QTemporaryDir> lifetime = {});
  void updatePreparation();
  void importNextLocalBatch();
  void mergeLocalTrack(QVariantMap track);
  void scanMusicFolders(const QStringList &folders);
  void setupFolderWatching();
  void updateFolderWatches(const QStringList &paths);
  QFileSystemWatcher m_folderWatcher;
  QTimer m_folderChangeTimer;
  bool m_folderDirty=false, m_quietFolderScan=false;
  QStringList m_musicFolders;
  bool m_scanningFolders=false,m_scanLimited=false,m_cleanupBusy=false;
  int m_scanFailed=0;
  QString m_cleanupId;
  QVariantList m_cleanupRows,m_cleanupItems;
  QVariantList m_localTracks;
  QStringList m_importFiles;
  QString m_relocateId;
  int m_importTotal=0,m_importDone=0,m_importFailed=0;
  void cancelPreparation();
  void applyLyrics(const QVariantMap &data);
  QVariantList libraryRows(const QString &kind) const;
  void cancel(const QString &channel);
  void navigate(const QString &page, const QString &title, bool push = true, const QString &key = {});
  void beginView(const QString &key);
  void browseRequest(QVariantMap req, bool push = true);
  void load();
  void recordHistory();
  void notifyTrack();
  void resolveCurrent(bool retry = false);
  QVariantMap snapshot() const;
  void restore(const QVariantMap &);
  static QVariantList playable(const QVariantList &);
  QSettings m_settings;
  PlaybackNotifier m_notifier;
  bool m_historyPaused = false;
  quint64 m_skipHistoryToken = 0, m_announcedToken = 0;
  Entries m_results, m_queue;
  QList<qint64> m_queueSuffix;
  CollectionView m_collection;
  QMediaDevices m_devices;
  QVariantList m_lyricLines;
  QVariantList m_sections, m_favorites, m_history, m_playlists, m_back, m_pins;
  int m_playbackDirection=1;
  QString m_viewKey = "home";
  QMap<QString,QVariantMap> m_viewOptions;
  QStringList m_viewOrder;
  QString m_page = "home", m_title = "Listen", m_cover, m_error, m_lyrics,
          m_libraryId;
  QVariantMap m_request, m_lyricOffsets;
  QString m_retryTarget, m_undoType, m_undoMessage;
  QVariantList m_undoRows;
  int m_undoIndex = -1;
  bool m_undoShuffle = false;
  bool m_stopped = true, m_storageHealthy = true;
  quint64 m_trackToken = 0;
  int m_recoveryAttempts = 0;
  bool m_recovering = false;
  bool m_sleepAtEnd = false;
  std::shared_ptr<QTemporaryDir> m_audioCache, m_preparedDirectory;
  QVariantMap m_preparedData;
  QString m_preparedId, m_preparationAttempt;
  quint64 m_preparationGeneration=0;
  QTimer m_prepareTimer, m_prepareUpdate;
  QString m_lyricsSource;
  bool m_lyricsLoaded=false;
  QVariantMap m_lastPlayed, m_undoLastPlayed;
  quint64 m_recordedToken=0;
  QString m_coverPlayId;
  quint64 m_coverPlayToken = 0;
  void cancelCoverPlay();
  bool m_busy = false, m_more = false, m_resolving = false,
       m_lyricsBusy = false, m_retry = false, m_wantPlay = false;
  int m_index = -1;
  void resetAudioLevels();
  AudioLevels m_levelAnalyzer;
  QVariantList m_audioLevels{0.0,0.0,0.0,0.0,0.0};
  QElapsedTimer m_levelPublish;
  QTimer m_levelIdle;
  QAudioOutput m_audio;
  QAudioBufferOutput m_visualAudio;
  QMediaPlayer m_media;
  QTimer m_saveTimer, m_sleepTimer, m_sleepTick, m_sleepFadeStart, m_sleepFadeTick;
  double m_userVolume=0.65, m_sleepGain=1.0;
  void updateSleepGain();
  QHash<QString, QProcess *> m_processes;
  QHash<QString, QVariantMap> m_streams;
  qint64 m_restorePosition = 0, m_savedPosition = 0;
  bool m_uiActive = true;
  int m_notifiedLyricIndex = -1;
  QTimer m_positionTick;
};
