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
#include <QColor>
#include <functional>
#include <memory>
#include <QMediaDevices>
#include <QAudioDevice>
#include "collectionview.h"
#include "playbacknotifier.h"
#include "musicserver.h"
#include "scrobbler.h"
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
  // Nought to one once the files to import are known, and below nought while
  // the folders are still being walked and the total is not.
  Q_PROPERTY(double localImportProgress READ localImportProgress NOTIFY localImportChanged)
  Q_PROPERTY(QStringList recentSearches READ recentSearches NOTIFY recentSearchesChanged)
  Q_PROPERTY(Entries *results READ results CONSTANT)
  // Material's list and detail layout keeps the list a detail was opened from
  // alive beside it. The list pane holds the rows that were on screen at that
  // moment, so the one collection the rest of the view reads can go on to be
  // the detail.
  Q_PROPERTY(Entries *listPane READ listPane CONSTANT)
  Q_PROPERTY(QString listPaneTitle READ listPaneTitle NOTIFY listPaneChanged)
  Q_PROPERTY(QString listPaneId READ listPaneId NOTIFY listPaneChanged)
  Q_PROPERTY(Entries *queue READ queue CONSTANT)
  Q_PROPERTY(CollectionView *collection READ collection CONSTANT)
  // The songs played before this one, newest first, for the queue panel to
  // look back over without leaving it.
  Q_PROPERTY(Entries *recentlyPlayed READ recentlyPlayed CONSTANT)
  Q_PROPERTY(Scrobbler *scrobbler READ scrobbler CONSTANT)
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
  Q_PROPERTY(bool volumeNormalization READ volumeNormalization WRITE setVolumeNormalization NOTIFY settingsChanged)
  Q_PROPERTY(double normalizationGainDb READ normalizationGainDb NOTIFY normalizationChanged)
  Q_PROPERTY(double trackTrimDb READ trackTrimDb NOTIFY normalizationChanged)
  Q_PROPERTY(bool resumeLongTracks READ resumeLongTracks WRITE setResumeLongTracks NOTIFY settingsChanged)
  Q_PROPERTY(QString normalizationSource READ normalizationSource NOTIFY normalizationChanged)
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
  Q_PROPERTY(QString accentColor READ accentColor WRITE setAccentColor NOTIFY settingsChanged)
  // How much of the source colour the interface takes, and how hard the text
  // and boundaries are pushed away from what they sit on.
  Q_PROPERTY(QString colorVariant READ colorVariant WRITE setColorVariant NOTIFY settingsChanged)
  Q_PROPERTY(double colorContrast READ colorContrast WRITE setColorContrast NOTIFY settingsChanged)
  // Material draws components tighter when a precision pointer is present,
  // because the 48dp minimum exists to disambiguate touches.
  Q_PROPERTY(bool precisePointer READ precisePointer WRITE setPrecisePointer NOTIFY settingsChanged)
  Q_PROPERTY(bool ambientBackdrop READ ambientBackdrop WRITE setAmbientBackdrop NOTIFY settingsChanged)
  Q_PROPERTY(bool backdropPulse READ backdropPulse WRITE setBackdropPulse NOTIFY settingsChanged)
  Q_PROPERTY(bool typeAheadJump READ typeAheadJump WRITE setTypeAheadJump NOTIFY settingsChanged)
  Q_PROPERTY(bool onboarded READ onboarded WRITE setOnboarded NOTIFY settingsChanged)
  Q_PROPERTY(QVariantMap albumInfo READ albumInfo NOTIFY catalogChanged)
  Q_PROPERTY(QVariantMap artistInfo READ artistInfo NOTIFY catalogChanged)
  Q_PROPERTY(QString currentMotionArt READ currentMotionArt NOTIFY onlineArtworkChanged)
  Q_PROPERTY(QString artworkStatus READ artworkStatus NOTIFY onlineArtworkChanged)
  Q_PROPERTY(QString artworkPage READ artworkPage NOTIFY onlineArtworkChanged)
  Q_PROPERTY(bool watchMusicFolders READ watchMusicFolders WRITE setWatchMusicFolders NOTIFY settingsChanged)
  Q_PROPERTY(QString onlineMotionArt READ onlineMotionArt NOTIFY onlineArtworkChanged)
  Q_PROPERTY(bool onlineArtwork READ onlineArtwork WRITE setOnlineArtwork NOTIFY settingsChanged)
  Q_PROPERTY(bool albumCovers READ albumCovers WRITE setAlbumCovers NOTIFY settingsChanged)
  Q_PROPERTY(bool animatedArtwork READ animatedArtwork WRITE setAnimatedArtwork NOTIFY settingsChanged)
  Q_PROPERTY(bool motion READ motion WRITE setMotion NOTIFY settingsChanged)
  Q_PROPERTY(bool historyPaused READ historyPaused WRITE setHistoryPaused NOTIFY settingsChanged)
  Q_PROPERTY(bool trackNotifications READ trackNotifications WRITE setTrackNotifications NOTIFY settingsChanged)
  Q_PROPERTY(bool keepCompletedLyrics READ keepCompletedLyrics WRITE setKeepCompletedLyrics NOTIFY settingsChanged)
  Q_PROPERTY(int volumeStep READ volumeStep WRITE setVolumeStep NOTIFY settingsChanged)
  Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY settingsChanged)
  Q_PROPERTY(bool sleepFade READ sleepFade WRITE setSleepFade NOTIFY settingsChanged)
  Q_PROPERTY(QString sleepStatus READ sleepLabel NOTIFY settingsChanged)
  Q_PROPERTY(int crossfadeSeconds READ crossfadeSeconds WRITE setCrossfadeSeconds NOTIFY settingsChanged)
  Q_PROPERTY(bool gapless READ gapless WRITE setGapless NOTIFY settingsChanged)
  // Material's two motion schemes. Expressive overshoots and settles, which the
  // specification recommends for most products; standard eases in without the
  // bounce, for when that reads as fussy.
  Q_PROPERTY(QString motionScheme READ motionScheme WRITE setMotionScheme NOTIFY settingsChanged)
  Q_PROPERTY(bool crossfading READ crossfading NOTIFY playbackChanged)
  Q_PROPERTY(bool prepareNext READ prepareNext WRITE setPrepareNext NOTIFY settingsChanged)
  Q_PROPERTY(bool lyricsFallback READ lyricsFallback WRITE setLyricsFallback NOTIFY settingsChanged)
  Q_PROPERTY(QString lyricsSource READ lyricsSource NOTIFY lyricsChanged)
  Q_PROPERTY(QString lyrics READ lyrics NOTIFY lyricsChanged)
  Q_PROPERTY(QVariantList lyricLines READ lyricLines NOTIFY lyricsChanged)
  Q_PROPERTY(int lyricIndex READ lyricIndex NOTIFY lyricIndexChanged)
  Q_PROPERTY(double lyricProgress READ lyricProgress NOTIFY positionChanged)
  Q_PROPERTY(int lyricSpan READ lyricSpan NOTIFY lyricIndexChanged)
  Q_PROPERTY(bool lyricsBusy READ lyricsBusy NOTIFY lyricsChanged)
  Q_PROPERTY(QVariantList playlists READ playlists NOTIFY libraryChanged)
  Q_PROPERTY(QVariantList pins READ pins NOTIFY libraryChanged)
  Q_PROPERTY(QVariantMap collectionItem READ collectionItem NOTIFY catalogChanged)
  Q_PROPERTY(bool liked READ liked NOTIFY libraryChanged)
  Q_PROPERTY(QString cookies READ cookies NOTIFY settingsChanged)
  Q_PROPERTY(QString streamingQuality READ streamingQuality WRITE setStreamingQuality NOTIFY settingsChanged)
  Q_PROPERTY(bool rememberStreamedAudio READ rememberStreamedAudio WRITE setRememberStreamedAudio NOTIFY settingsChanged)
  Q_PROPERTY(int streamedAudioCacheMb READ streamedAudioCacheMb WRITE setStreamedAudioCacheMb NOTIFY settingsChanged)
  Q_PROPERTY(QStringList musicFolders READ musicFolders NOTIFY libraryChanged)
  Q_PROPERTY(bool cleanupBusy READ cleanupBusy NOTIFY cleanupChanged)
  Q_PROPERTY(QVariantList cleanupItems READ cleanupItems NOTIFY cleanupChanged)
public:
  Q_INVOKABLE QVariantMap smartPlaylist(const QString &id) const;
  Q_INVOKABLE QString saveSmartPlaylist(const QString &id,const QString &name,const QVariantMap &rules);
  Q_INVOKABLE QString previewLyric(qint64 position) const;
  Q_INVOKABLE QVariantList trackDetails(const QVariantMap &track) const;
  // What has been listened to, over the last `days` or over everything when
  // days is zero or less. Counted from a log of plays rather than from the
  // recent-history list, which keeps only one row per song.
  Q_INVOKABLE QVariantMap listeningStats(int days) const;
  Q_INVOKABLE void clearListeningStats();
  // Earlier versions of a playlist, newest first, so an edit can be looked back
  // at and taken back long after the single-step Undo has moved on.
  Q_INVOKABLE QVariantList playlistVersions(const QString &id) const;
  Q_INVOKABLE bool restorePlaylistVersion(const QString &id,int index);
  Q_INVOKABLE void clearPlaylistVersions(const QString &id);
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
  Q_INVOKABLE void exportPlaylistM3u(const QString &id,const QUrl &file);
  Q_INVOKABLE void importPlaylistM3u(const QUrl &file);
  Q_INVOKABLE void inspectPlaylist(const QString &id);
  Q_INVOKABLE void applyPlaylistCleanup(bool duplicates,bool missing);
  Q_INVOKABLE void closePlaylistCleanup();
  explicit Backend(QObject *parent = nullptr);
  bool importingLocal() const {return m_scanningFolders || m_importTotal>0;}
  QString localImportStatus() const;
  double localImportProgress() const {return m_importTotal>0 ? double(m_importDone)/m_importTotal : -1.0;}
  Q_INVOKABLE void importLocalFiles(const QVariantList &urls);
  Q_INVOKABLE void cancelLocalImport();
  Q_INVOKABLE void locateLocalFile(const QUrl &url,const QString &id);
  Q_INVOKABLE void removeLocalFile(const QString &id);
  Q_INVOKABLE QVariantList searchLyrics(const QString &query) const;
  ~Backend() override;
  Entries *results() { return &m_results; }
  Entries *listPane() { return &m_listPane; }
  QString listPaneTitle() const { return m_listPaneTitle; }
  QString listPaneId() const { return m_listPaneId; }
  Q_INVOKABLE void clearListPane();
  // Remembers what is on screen as the list a detail is being opened from.
  void rememberListPane(const QString &title, const QString &id, const QVariantList &rows);
  Entries *queue() { return &m_queue; }
  CollectionView *collection() { return &m_collection; }
  Entries *recentlyPlayed() { return &m_recent; }
  Scrobbler *scrobbler() { return &m_scrobbler; }
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
    return m_media().playbackState() == QMediaPlayer::PlayingState;
  }
  QString coverPlayId() const { return m_coverPlayId; }
  bool resolving() const { return m_resolving; }
  bool buffering() const { return m_wantPlay && (m_resolving || m_media().mediaStatus()==QMediaPlayer::LoadingMedia || m_media().mediaStatus()==QMediaPlayer::StalledMedia || m_media().mediaStatus()==QMediaPlayer::BufferingMedia); }
  qint64 position() const { return m_media().source().isEmpty() ? m_savedPosition : m_media().position(); }
  qint64 duration() const {
    return m_media().duration() > 0
               ? m_media().duration()
               : current().value("seconds").toLongLong() * 1000;
  }
  double volume() const { return m_userVolume; }
  bool volumeNormalization() const {return m_settings.value("volumeNormalization",false).toBool();}
  void setVolumeNormalization(bool);
  double normalizationGainDb() const {return m_normalizationDb;}
  // The level actually handed to the mixer, after levelling and sleep fade.
  double effectiveVolume() const {return settledVolume();}
  QString normalizationSource() const {return m_normalizationSource;}
  Q_INVOKABLE double measuredLoudness(const QString &id) const;
  double trackTrimDb() const {return m_trim;}
  // A correction the listener sets by hand, kept alongside any automatic one.
  Q_INVOKABLE double trackTrim(const QString &id) const;
  Q_INVOKABLE void setTrackTrim(const QString &id,double decibels);
  bool resumeLongTracks() const {return m_settings.value("resumeLongTracks",true).toBool();}
  void setResumeLongTracks(bool enabled);
  Q_INVOKABLE qint64 resumePosition(const QString &id) const;
  bool sleepFade() const { return m_settings.value("sleepFade",true).toBool(); }
  void setSleepFade(bool enabled);
  void setVolume(double);
  double playbackRate() const {return m_media().playbackRate();}
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
  // Material derives every color role from one source color. The result only
  // changes when that color or the theme does, so it is worth remembering.
  Q_INVOKABLE QVariantMap colorScheme(const QColor &source,bool dark) const;
  // Material's shape library. `shapeOutline` hands back the radii the named
  // shape carries at `steps` even angles, which is what the loading indicator
  // morphs between and what masks artwork.
  Q_INVOKABLE QStringList shapeNames() const;
  Q_INVOKABLE QVariantList shapeOutline(const QString &name,int steps) const;
  bool artworkAccent() const {return m_settings.value("artworkAccent",false).toBool();}
  void setArtworkAccent(bool enabled) {if(artworkAccent()==enabled)return;m_settings.setValue("artworkAccent",enabled);emit settingsChanged();}
  // A hand-picked Material source color. Empty keeps the built-in palette.
  bool precisePointer() const {return m_settings.value("precisePointer",false).toBool();}
  void setPrecisePointer(bool precise) {if(precisePointer()==precise)return;m_settings.setValue("precisePointer",precise);emit settingsChanged();}
  QString colorVariant() const {return m_settings.value("colorVariant","tonalSpot").toString();}
  void setColorVariant(const QString &name) {if(colorVariant()==name)return;m_settings.setValue("colorVariant",name);emit settingsChanged();}
  double colorContrast() const {return m_settings.value("colorContrast",0.0).toDouble();}
  void setColorContrast(double level) {const double held=qBound(0.0,level,1.0);if(qFuzzyCompare(colorContrast()+1,held+1))return;m_settings.setValue("colorContrast",held);emit settingsChanged();}
  QString accentColor() const {return m_settings.value("accentColor").toString();}
  void setAccentColor(const QString &value);
  bool ambientBackdrop() const {return m_settings.value("ambientBackdrop",true).toBool();}
  void setAmbientBackdrop(bool enabled) {if(ambientBackdrop()==enabled)return;m_settings.setValue("ambientBackdrop",enabled);emit settingsChanged();}
  // First run shows a short setup flow; isolated sessions never do.
  bool onboarded() const {return m_settings.value("onboarded",false).toBool();}
  void setOnboarded(bool done) {if(onboarded()==done)return;m_settings.setValue("onboarded",done);emit settingsChanged();}
  bool backdropPulse() const {return m_settings.value("backdropPulse",true).toBool();}
  void setBackdropPulse(bool enabled) {if(backdropPulse()==enabled)return;m_settings.setValue("backdropPulse",enabled);emit settingsChanged();}
  bool typeAheadJump() const {return m_settings.value("typeAheadJump",true).toBool();}
  void setTypeAheadJump(bool enabled) {if(typeAheadJump()==enabled)return;m_settings.setValue("typeAheadJump",enabled);emit settingsChanged();}
  Q_INVOKABLE QString preparePlaylistCover(const QUrl &url);
  Q_INVOKABLE bool setPlaylistCover(const QString &id,const QString &preview,double x=0.5,double y=0.5,double zoom=1);
  Q_INVOKABLE void resetPlaylistCover(const QString &id);
  QVariantList localGroups(const QString &kind) const;
  QVariantList localGroupRows(const QVariantMap &item) const;
  void openLocalGroup(const QVariantMap &item);
  void updateLocalView();
  QVariantMap albumInfo() const;
  QVariantMap artistInfo() const;
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
  // Whether a song that only has a video frame shows the album cover Apple
  // Music has for it instead. Independent of the animated cover switches: the
  // lookup runs for either, and each result is applied by its own switch.
  bool albumCovers() const { return m_settings.value("albumCovers",true).toBool(); }
  void setAlbumCovers(bool value);
  // The album cover remembered for one of YouTube's video frames, or an empty
  // URL when the frame is not one, nothing has been found for it, or the
  // setting is off. The art loader asks this for every frame it draws.
  Q_INVOKABLE QUrl albumCoverFor(const QUrl &frame) const;
  // A track's cover as something outside the window should show it, at a
  // size that suits a media widget.
  QString displayArt(const QVariantMap &track) const;
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
  // How much of the network a YouTube song is allowed to cost. Standard takes
  // the best stream YouTube offers, which is Opus at about 130 kbps; data
  // saver caps it, which lands on the Opus rung at about 67 kbps. The whole
  // song is buffered before it plays, so this is the download either way.
  QString streamingQuality() const { const auto v=m_settings.value("streamingQuality","standard").toString();return v=="saver"?v:"standard"; }
  void setStreamingQuality(const QString &value) { if(streamingQuality()==value || (value!="saver" && value!="standard"))return;m_settings.setValue("streamingQuality",value);emit settingsChanged(); }
  // Keep the file already buffered for a YouTube or server song and play it
  // again from disk, up to streamedAudioCacheMb. This is not a library copy.
  bool rememberStreamedAudio() const { return m_settings.value("rememberStreamedAudio", true).toBool(); }
  void setRememberStreamedAudio(bool enabled);
  int streamedAudioCacheMb() const { return qBound(64, m_settings.value("streamedAudioCacheMb", 512).toInt(), 4096); }
  void setStreamedAudioCacheMb(int megabytes);
  // Overlap between one song and the next, in seconds. Zero plays them in turn.
  int crossfadeSeconds() const {return qBound(0,m_settings.value("crossfadeSeconds",0).toInt(),12);}
  void setCrossfadeSeconds(int seconds);
  // Without an overlap, the next song still starts the instant this one ends.
  bool gapless() const {return m_settings.value("gapless",true).toBool();}
  void setGapless(bool enabled);
  QString motionScheme() const {const auto v=m_settings.value("motionScheme","expressive").toString();return v=="standard"?v:"expressive";}
  void setMotionScheme(const QString &value) {
    const auto chosen=value=="standard"?QString("standard"):QString("expressive");
    if(chosen==motionScheme())return;
    m_settings.setValue("motionScheme",chosen);
    emit settingsChanged();
  }
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
  double lyricProgress() const;
  int lyricSpan() const;
  QPair<qint64,qint64> lyricSpanAt(int index) const;
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
  Q_INVOKABLE QVariantMap relatedCollection(const QVariantMap &item, const QString &kind) const;
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
  QMediaPlayer *media() { return &m_media(); }
signals:
  void onlineArtworkChanged();
  void videoCoversChanged();
  void viewAboutToChange();
  void localImportChanged();
  void listPaneChanged();
  void cleanupChanged();
  void recentSearchesChanged();
  void catalogChanged();
  void errorChanged();
  void trackChanged();
  void playbackChanged();
  void audioLevelsChanged();
  void normalizationChanged();
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
  bool motionLookupWanted() const;
  bool coverLookupWanted() const;
  void rememberVideoCover(const QString &videoId,const QString &cover);
  void forgetVideoCover(const QString &videoId);
  QVariantMap m_videoCovers;
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
  friend class CrossfadeTest;
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
  Entries m_results, m_queue, m_listPane;
  QString m_listPaneTitle, m_listPaneId;
  QList<qint64> m_queueSuffix;
  CollectionView m_collection;
  QMediaDevices m_devices;
  QVariantList m_lyricLines;
  // Keyed by the source colour and by everything else that decides the scheme.
  mutable QHash<QPair<QRgb,QString>,QVariantMap> m_schemes;
  QVariantList m_sections, m_favorites, m_history, m_playlists, m_back, m_pins;
  Entries m_recent;
  void refreshRecentlyPlayed();
  // One row per play, oldest first. A private session records nothing.
  QVariantList m_plays;
  void recordPlay(const QVariantMap &track);
  // Listening history sent onward. A song is reported as it starts, and the
  // listen itself once it has played far enough to count.
  Scrobbler m_scrobbler;
  quint64 m_scrobbleToken = 0;
  qint64 m_scrobbleStartedAt = 0;
  qint64 m_scrobbleThreshold = -1;
  bool m_scrobbleSent = false;
  void beginScrobble();
  void considerScrobble();
  // Playlist id to a list of earlier versions, newest first.
  QVariantMap m_playlistVersions;
  void snapshotPlaylist(const QString &id);
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
  QString streamedAudioKey(const QVariantMap &track) const;
  QString streamedAudioDir(const QString &key) const;
  QString lookupStreamedAudio(const QString &key) const;
  QString storeStreamedAudio(const QString &file, const QString &key);
  QString playableAudioFile(const QString &file, const QString &key);
  void touchStreamedAudio(const QString &path) const;
  void pruneStreamedAudio(const QString &keep = {});
  quint64 m_audioStoreGeneration=0;
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
  // Playback runs on two interchangeable decks. One carries the song being
  // heard; the other holds the next one, already decoded and waiting. During a
  // crossfade both sound at once while their volumes trade places, and at the
  // end the decks swap roles. Nothing is handed from one to the other mid-song,
  // so neither side has a seam to hear.
  QAudioOutput m_audioA, m_audioB;
  // One decoded-audio tap, attached to whichever deck is being heard. Leaving a
  // tap on the idle deck as well starves the active one of buffers, so it moves
  // across when the decks swap rather than sitting on both.
  QAudioBufferOutput m_visualAudio;
  QMediaPlayer m_deckA, m_deckB;
  bool m_usingB = false;
  QMediaPlayer &m_media() { return m_usingB ? m_deckB : m_deckA; }
  const QMediaPlayer &m_media() const { return m_usingB ? m_deckB : m_deckA; }
  QMediaPlayer &spareDeck() { return m_usingB ? m_deckA : m_deckB; }
  bool isActive(const QMediaPlayer &deck) const { return &deck == &m_media(); }
public:
  bool crossfading() const { return m_crossfading; }
private:
  QAudioOutput &activeAudio() { return m_usingB ? m_audioB : m_audioA; }
  const QAudioOutput &activeAudio() const { return m_usingB ? m_audioB : m_audioA; }
  QAudioOutput &spareAudio() { return m_usingB ? m_audioA : m_audioB; }
  // The volume the mixer is asked for once nothing is fading.
  double settledVolume() const { return qBound(0.0, m_userVolume * m_sleepGain * m_normalizationGain, 1.0); }
  void swapDecks();
  // Crossfade state. m_fadeGain scales the active deck while a fade runs.
  QTimer m_crossfadeWatch, m_crossfadeTick;
  QElapsedTimer m_crossfadeClock;
  bool m_crossfading = false;
  int m_crossfadeMs = 0;
  double m_fadeGain = 1.0;
  quint64 m_crossfadeToken = 0;
  void considerCrossfade();
  void beginCrossfade(int milliseconds);
  void stepCrossfade();
  void endCrossfade(bool completed);
  void clearSpare();
  // The queue position the spare deck is holding, or -1 when it holds nothing.
  int m_handoffIndex = -1;
  bool m_handoffPrepared = false;
  int handoffTarget();
  QUrl readySource(const QVariantMap &track) const;
  bool armHandoff(bool playImmediately);
  void adoptHandoff(int index);
  bool finishGapless();
  QTimer m_saveTimer, m_sleepTimer, m_sleepTick, m_sleepFadeStart, m_sleepFadeTick;
  double m_userVolume=0.65, m_sleepGain=1.0, m_normalizationGain=1.0, m_normalizationDb=0.0, m_trim=0.0;
  bool m_sleepAtQueueEnd=false;
  QString m_normalizationSource;
  LoudnessMeter m_loudness;
  QString m_loudnessTrack;
  // A playlist file waiting for its songs to finish importing.
  QString m_m3uName;
  QStringList m_m3uPaths;
  void finishM3uImport();
  void updateSleepGain();
  void applyOutputVolume();
  void updateNormalization();
  void storeMeasuredLoudness();
  // Settings-backed caches keyed by track, oldest entry dropped first.
  void rememberBounded(const QString &prefix,const QString &id,const QVariant &value,int limit);
  void storeResumePosition();
  void clearResumePosition(const QString &id);
  static bool longRecording(const QVariantMap &track);
  static double trackGainDb(const QVariantMap &track);
  QHash<QString, QProcess *> m_processes;
  QHash<QString, QVariantMap> m_streams;
  qint64 m_restorePosition = 0, m_savedPosition = 0;
  bool m_uiActive = true;
  int m_notifiedLyricIndex = -1;
  QTimer m_positionTick;
};
