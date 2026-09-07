#include "backend.h"
#include "lrc.h"
#include <QStandardPaths>
#include <QDateTime>
#include "rowselection.h"
#include "desktoptheme.h"
#include <QSaveFile>
#include <QClipboard>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

class BackendTest : public QObject {
  Q_OBJECT
  QTemporaryDir storage;
  QVariantMap track(const QString &id) {
    return {{"id", id},
            {"videoId", id},
            {"title", "Track " + id},
            {"kind", "song"}};
  }
private slots:
  void initTestCase() {
    qputenv("XDG_DATA_HOME", storage.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", storage.path().toUtf8());
    QCoreApplication::setApplicationName("sung-test");
    QCoreApplication::setOrganizationName("SungTests");
  }
  void folderImportAndPlaylistCleanup() {
    const auto oldHelper=qgetenv("SUNG_HELPER"),oldPython=qgetenv("SUNG_PYTHON");
    const auto helper=QFileInfo(QString::fromUtf8(qgetenv("SUNG_FIXTURE_HELPER"))).dir().absoluteFilePath("../helper/catalog.py");
    qputenv("SUNG_HELPER",helper.toUtf8());qputenv("SUNG_PYTHON","/usr/bin/python3");
    const auto restore=qScopeGuard([&]{qputenv("SUNG_HELPER",oldHelper);qputenv("SUNG_PYTHON",oldPython);});
    QTemporaryDir music;QDir().mkpath(music.filePath("Album"));
    const auto path=music.filePath("Album/One.wav");
    QProcess encode;encode.start("ffmpeg",{"-nostdin","-v","error","-f","lavfi","-i","anullsrc=r=8000:cl=mono","-t","12",path});QVERIFY(encode.waitForFinished(10000));QCOMPARE(encode.exitCode(),0);
    Backend b;b.m_localTracks.clear();b.m_musicFolders.clear();b.m_playlists.clear();b.clearQueue();b.setVolume(0);b.setAutoplay(false);b.setPrepareNext(false);
    b.importMusicFolder(QUrl::fromLocalFile(music.path()));QTRY_VERIFY_WITH_TIMEOUT(!b.importingLocal(),10000);QCOMPARE(b.m_localTracks.size(),1);QCOMPARE(b.musicFolders().size(),1);
    const auto first=b.m_localTracks.first().toMap();QVERIFY(!first.value("localStamp").toString().isEmpty());
    b.playItem(first);QTRY_VERIFY(b.playing());const auto source=b.media()->source();
    b.rescanMusicFolders();QTRY_VERIFY(!b.importingLocal());QCOMPARE(b.m_importDone,1);QCOMPARE(b.m_localTracks.size(),1);QVERIFY(b.playing());QCOMPARE(b.media()->source(),source);
    const auto secondPath=music.filePath("Two.wav");QVERIFY(QFile::copy(path,secondPath));b.rescanMusicFolders();QTRY_VERIFY(!b.importingLocal());QCOMPARE(b.m_localTracks.size(),2);
    b.save();b.load();QCOMPARE(b.musicFolders(),QStringList{music.path()});
    auto missing=b.m_localTracks.last().toMap();QVERIFY(QFile::remove(secondPath));b.rescanMusicFolders();QTRY_VERIFY(!b.importingLocal());QCOMPARE(b.m_localTracks.size(),2);
    auto youtube=track("abcdefghijk");const auto id=b.createPlaylist("Cleanup fixture");
    const QVariantList original{first,youtube,first,missing,youtube};
    b.m_playlists={QVariantMap{{"id",id},{"title","Cleanup fixture"},{"tracks",original}}};b.openPlaylist(id);
    b.inspectPlaylist(id);QTRY_VERIFY(!b.cleanupBusy());QCOMPARE(b.cleanupItems().size(),3);
    b.applyPlaylistCleanup(true,false);QTRY_VERIFY(!b.cleanupBusy());QCOMPARE(b.results()->count(),3);QVERIFY(b.playing());QCOMPARE(b.media()->source(),source);QVERIFY(QFile::exists(path));
    b.undo();QCOMPARE(b.results()->count(),5);
    b.inspectPlaylist(id);QTRY_VERIFY(!b.cleanupBusy());
    // A missing file restored after preview must survive the commit recheck.
    QVERIFY(QFile::copy(path,secondPath));b.applyPlaylistCleanup(false,true);QTRY_VERIFY(!b.cleanupBusy());QCOMPARE(b.results()->count(),5);
    QVERIFY(QFile::remove(secondPath));b.inspectPlaylist(id);QTRY_VERIFY(!b.cleanupBusy());b.applyPlaylistCleanup(true,true);QTRY_VERIFY(!b.cleanupBusy());QCOMPARE(b.results()->count(),2);b.undo();QCOMPARE(b.results()->count(),5);
    // Never apply stale row indices after an edit or reorder.
    b.inspectPlaylist(id);QTRY_VERIFY(!b.cleanupBusy());b.removePlaylistRows(id,{0});b.applyPlaylistCleanup(true,true);QTRY_VERIFY(!b.cleanupBusy());QCOMPARE(b.results()->count(),4);
    b.closePlaylistCleanup();QVERIFY(b.cleanupItems().isEmpty());
    b.rescanMusicFolders();b.cancelLocalImport();QTest::qWait(100);QVERIFY(!b.importingLocal());
    b.forgetMusicFolder(music.path());QVERIFY(b.musicFolders().isEmpty());QCOMPARE(b.m_localTracks.size(),2);QVERIFY(QFile::exists(path));
    b.stop();b.m_localTracks.clear();b.m_playlists.clear();b.clearQueue();b.setPrepareNext(true);b.setAutoplay(true);
  }
  void localAudioAndLyricSearch() {
    const auto oldHelper=qgetenv("SUNG_HELPER"),oldPython=qgetenv("SUNG_PYTHON");
    const auto helper=QFileInfo(QString::fromUtf8(qgetenv("SUNG_FIXTURE_HELPER"))).dir().absoluteFilePath("../helper/catalog.py");
    qputenv("SUNG_HELPER",helper.toUtf8());qputenv("SUNG_PYTHON","/usr/bin/python3");
    const auto restore=qScopeGuard([&]{qputenv("SUNG_HELPER",oldHelper);qputenv("SUNG_PYTHON",oldPython);});
    QTemporaryDir music;
    for(const auto &name:{"First song.flac","Second song.mp3"}){
      QProcess encode;encode.start("ffmpeg",{"-nostdin","-v","error","-f","lavfi","-i","anullsrc=r=44100:cl=mono","-t","12","-metadata","title=Local melody","-metadata","artist=Fixture artist","-metadata","album=Fixture album",music.filePath(name)});
      QVERIFY(encode.waitForFinished(10000));QCOMPARE(encode.exitCode(),0);
    }
    QFile lyrics(music.filePath("First song.lrc"));QVERIFY(lyrics.open(QIODevice::WriteOnly));lyrics.write("[00:01] A quiet morning\n[00:03] Another line\n[00:05] A QUIET evening");lyrics.close();
    Backend b;b.setVolume(0);b.setLyricsFallback(false);b.clearQueue();b.setAutoplay(false);
    const QVariantList urls{QUrl::fromLocalFile(music.filePath("First song.flac")),QUrl::fromLocalFile(music.filePath("Second song.mp3"))};
    b.importLocalFiles(urls);QTRY_VERIFY_WITH_TIMEOUT(!b.importingLocal(),15000);b.library("files");QCOMPARE(b.results()->count(),2);
    auto first=b.results()->get(0),second=b.results()->get(1);QVERIFY(first.value("videoId").toString().isEmpty());QCOMPARE(first.value("artist").toString(),"Fixture artist");QCOMPARE(first.value("seconds").toInt(),12);
    b.importLocalFiles(urls);QTRY_VERIFY_WITH_TIMEOUT(!b.importingLocal(),10000);QCOMPARE(b.results()->count(),2);
    RowSelection selection;selection.setModel(b.results());selection.selectAll();QCOMPARE(selection.count(),2);
    const auto playlist=b.createPlaylist("Mixed sources");auto youtube=track("abcdefghijk");b.addItemsToPlaylist(playlist,{first,youtube,second});b.openPlaylist(playlist);QCOMPARE(b.results()->count(),3);
    b.playCollection(0);QTRY_VERIFY_WITH_TIMEOUT(b.playing()&&b.duration()>0,5000);QCOMPARE(b.media()->source(),urls[0].toUrl());QVERIFY(!b.m_processes.contains("play"));
    b.fetchLyrics();QCOMPARE(b.lyricsSource(),"Local LRC");auto matches=b.searchLyrics("quiet");QCOMPARE(matches.size(),2);QCOMPARE(matches.last().toMap().value("start").toLongLong(),5000);QVERIFY(b.searchLyrics("missing").isEmpty());
    b.setLyricOffset(250);b.seekLyric(matches.last().toMap().value("start").toLongLong());QCOMPARE(b.position(),4750);
    b.playKeepingQueue(second);QTRY_VERIFY(b.playing()&&b.media()->source()==urls[1].toUrl());QCOMPARE(b.queue()->count(),3);QCOMPARE(b.current().value("id"),second.value("id"));
    b.moveQueueRows({0},3);b.undo();QVERIFY(b.playing());b.pause();b.save();b.load();QCOMPARE(b.m_localTracks.size(),2);
    QFile exported(music.filePath("library.json"));b.exportLibrary(QUrl::fromLocalFile(exported.fileName()));QVERIFY(exported.open(QIODevice::ReadOnly));QVERIFY(exported.readAll().contains("localTracks"));
    b.stop();QVERIFY(QFile::rename(music.filePath("First song.flac"),music.filePath("Moved.flac")));b.playItem(first);QVERIFY(!b.playing());QVERIFY(b.error().contains("missing"));QVERIFY(!b.m_processes.contains("play"));
    b.locateLocalFile(QUrl::fromLocalFile(music.filePath("Moved.flac")),first.value("id").toString());QTRY_VERIFY_WITH_TIMEOUT(!b.importingLocal(),10000);QCOMPARE(b.current().value("id"),first.value("id"));QCOMPARE(b.current().value("localPath").toString(),music.filePath("Moved.flac"));
    b.retry();QTRY_VERIFY(b.playing());b.pause();b.openPlaylist(playlist);QCOMPARE(b.results()->get(0).value("localPath").toString(),music.filePath("Moved.flac"));
    b.importLocalFiles({QUrl::fromLocalFile(music.filePath("Moved.flac"))});QTRY_VERIFY(!b.importingLocal());QCOMPARE(b.m_localTracks.size(),2);
    b.removeLocalFile(first.value("id").toString());QVERIFY(QFile::exists(music.filePath("Moved.flac")));QCOMPARE(b.results()->count(),3);
    b.applyLyrics({{"ok",true},{"lyrics","Quiet text\nAnother quiet line"}});QCOMPARE(b.searchLyrics("quiet").size(),2);QCOMPARE(b.searchLyrics("quiet").first().toMap().value("start").toInt(),-1);
    b.importLocalFiles(urls);b.cancelLocalImport();QTest::qWait(150);QVERIFY(!b.importingLocal());
    b.stop();b.deletePlaylist(playlist);b.m_localTracks.clear();b.clearQueue();b.setLyricsFallback(true);b.setAutoplay(true);
  }
  void listeningFeatures() {
    const auto oldHelper=qgetenv("SUNG_HELPER"),oldPython=qgetenv("SUNG_PYTHON");
    qputenv("SUNG_HELPER",qgetenv("SUNG_FIXTURE_HELPER"));qputenv("SUNG_PYTHON","/usr/bin/python3");qputenv("SUNG_BUFFER_FIXTURE","1");
    const auto restore=qScopeGuard([&]{qputenv("SUNG_HELPER",oldHelper);qputenv("SUNG_PYTHON",oldPython);qunsetenv("SUNG_BUFFER_FIXTURE");});
    auto parsed=Lrc::parse("[offset:100]\n[00:02.12][00:01.500] Test\n[00:01.500] Translation\n[00:04] End",6000);
    QCOMPARE(parsed.size(),3);QCOMPARE(parsed[0].toMap().value("start").toLongLong(),1400);QCOMPARE(parsed[0].toMap().value("text").toString(),"Test\nTranslation");QCOMPARE(parsed[1].toMap().value("start").toLongLong(),2020);
    QVERIFY(Lrc::parse("[ar:Artist] No timing").isEmpty());QVERIFY(Lrc::parse("[00:77] Invalid").isEmpty());QVERIFY(Lrc::parse(QString(262145,'x')).isEmpty());
    Backend b;b.clearQueue();b.setShuffle(false);b.setRepeat(0);b.setAutoplay(false);b.setPrepareNext(true);
    auto a=track("prepare0001"),next=track("prepare0002"),slow=track("prepare0003"),fail=track("prepare0004");
    for(auto t:{a,next,slow,fail})b.enqueue(t);
    b.playAt(0);QTRY_VERIFY_WITH_TIMEOUT(b.playing()&&!b.resolving(),10000);
    const auto source=b.media()->source();QVERIFY(source.isLocalFile());const auto token=b.trackToken();
    QVERIFY(!b.m_prepareTimer.isActive()||b.m_preparedData.isEmpty());
    b.seek(20000);QTRY_VERIFY_WITH_TIMEOUT(!b.m_preparedData.isEmpty(),5000);
    QVERIFY(QFileInfo::exists(source.toLocalFile()));QCOMPARE(b.trackToken(),token);QCOMPARE(b.media()->source(),source);
    const auto prepared=b.m_preparedData.value("file").toString();QVERIFY(QFileInfo::exists(prepared));
    b.next();QTRY_VERIFY_WITH_TIMEOUT(b.playing()&&!b.resolving(),5000);QCOMPARE(b.media()->source(),QUrl::fromLocalFile(prepared));
    // A reordered queue must discard the old target, without touching the decoder.
    b.seek(20000);QTRY_VERIFY_WITH_TIMEOUT(b.m_processes.contains("prepare"),3000);b.moveQueue(3,2);
    QTRY_COMPARE_WITH_TIMEOUT(b.m_preparedId,QString("prepare0004"),3000);QTest::qWait(600);QVERIFY(b.m_preparedData.isEmpty());QVERIFY(b.error().isEmpty());
    QCOMPARE(b.media()->source(),QUrl::fromLocalFile(prepared));QVERIFY(QFileInfo::exists(prepared));
    b.moveQueue(3,2);QTRY_VERIFY_WITH_TIMEOUT(!b.m_preparedData.isEmpty(),5000);b.pause();QTRY_VERIFY(b.m_preparedId.isEmpty());QVERIFY(b.m_preparedData.isEmpty());
    b.play();b.setShuffle(true);QTest::qWait(100);QVERIFY(b.m_preparedId.isEmpty());b.setShuffle(false);b.setSleep(-1);QTest::qWait(100);QVERIFY(b.m_preparedId.isEmpty());b.setSleep(0);
    b.setPrepareNext(false);QTest::qWait(100);QVERIFY(b.m_preparedId.isEmpty());
    // Import is associated with the song captured when the picker opened.
    QTemporaryDir files;QFile lrc(files.filePath("lyrics.lrc"));QVERIFY(lrc.open(QIODevice::WriteOnly));lrc.write("[00:01.00] First\n[00:03.00] Second");lrc.close();
    b.importLyrics(QUrl::fromLocalFile(lrc.fileName()),a.value("id").toString());QVERIFY(b.lyricsSource()!="Imported LRC");
    b.playAt(0);b.fetchLyrics();QCOMPARE(b.lyricsSource(),"Imported LRC");QCOMPARE(b.lyricLines().size(),2);QTRY_VERIFY(b.playing()&&!b.resolving());b.seek(1500);QCOMPARE(b.lyricIndex(),0);
    b.resetLyrics();QTRY_VERIFY(!b.lyricsBusy());QCOMPARE(b.lyricsSource(),"YouTube");
    // Saved tracks deduplicate across mixes and track plays separately from list order.
    b.m_favorites={a,next,slow};b.m_playlists={QVariantMap{{"id","mix-test"},{"tracks",QVariantList{a,fail}}}};
    b.m_lastPlayed={{a.value("id").toString(),QDateTime::currentSecsSinceEpoch()-31*86400},{next.value("id").toString(),QDateTime::currentSecsSinceEpoch()}};
    b.library("mixes");QCOMPARE(b.results()->count(),3);b.open(b.results()->get(0));QCOMPARE(b.title(),"Recently liked");QCOMPARE(b.results()->count(),3);
    b.library("mix-unplayed");QCOMPARE(b.results()->count(),2);b.library("mix-rediscover");QCOMPARE(b.results()->count(),1);QCOMPARE(b.results()->get(0).value("id"),a.value("id"));
    b.clearHistory();QVERIFY(b.m_lastPlayed.isEmpty());b.undo();QVERIFY(!b.m_lastPlayed.isEmpty());
    b.setHistoryPaused(true);b.playAt(2);QTRY_VERIFY(b.playing()&&!b.resolving());QVERIFY(!b.m_lastPlayed.contains(slow.value("id").toString()));b.setHistoryPaused(false);
    b.stop();b.m_favorites.clear();b.m_playlists.clear();b.m_lastPlayed.clear();b.clearQueue();b.setPrepareNext(true);b.setAutoplay(true);
  }
  void searchAndBulkOperations() {
    Backend b;b.clearQueue();for(const auto &q:b.recentSearches())b.removeRecentSearch(q);
    b.rememberSearch("  Blue   sky ");b.rememberSearch("blue sky");QCOMPARE(b.recentSearches().size(),1);
    b.setHistoryPaused(true);b.rememberSearch("private");QCOMPARE(b.recentSearches().size(),1);b.setHistoryPaused(false);
    b.rememberSearch("https://music.youtube.com/watch?v=secret");QCOMPARE(b.recentSearches().size(),1);
    for(int i=0;i<20;++i)b.rememberSearch(QString::number(i));QCOMPARE(b.recentSearches().size(),12);
    b.removeRecentSearch("19");QCOMPARE(b.recentSearches().first(),"18");
    QVariantList songs;for(int i=0;i<6;++i){auto t=track(QString("bulk%1").arg(i,7,10,QChar('0')));t["title"]=QString("Nebula %1").arg(i);t["seconds"]=120;songs.append(t);}
    const auto id=b.createPlaylist("Nebula collection");b.addItemsToPlaylist(id,songs);b.addItemsToPlaylist(id,songs);b.openPlaylist(id);QCOMPARE(b.results()->count(),6);
    b.enqueueItems(songs);b.playAt(2);b.pause();b.seek(23000);const auto current=b.current();const auto token=b.trackToken();
    b.moveQueueRows({0,2,2,-1,999},6);QCOMPARE(b.currentIndex(),5);QCOMPARE(b.current(),current);QCOMPARE(b.position(),23000);QCOMPARE(b.trackToken(),token);
    b.undo();QCOMPARE(b.queue()->rows,songs);QCOMPARE(b.currentIndex(),2);QCOMPARE(b.position(),23000);
    b.removeQueueRows({0,4});QCOMPARE(b.currentIndex(),1);QCOMPARE(b.current(),current);b.undo();QCOMPARE(b.currentIndex(),2);QCOMPARE(b.queue()->rows,songs);
    b.enqueueItems({songs[0],songs[1]},true);QCOMPARE(b.queue()->get(3),songs[0].toMap());QCOMPARE(b.queue()->get(4),songs[1].toMap());QCOMPARE(b.current(),current);b.undo();QCOMPARE(b.queue()->rows,songs);
    b.movePlaylistRows(id,{1,3},6);QCOMPARE(b.results()->get(4),songs[1].toMap());QCOMPARE(b.results()->get(5),songs[3].toMap());b.undo();QCOMPARE(b.results()->rows,songs);
    b.collection()->setSortKey("title");b.movePlaylistRows(id,{0},5);QCOMPARE(b.results()->rows,songs);
    b.collection()->setQuery("Nebula 4");const int source=b.collection()->sourceIndex(0);b.removePlaylistRows(id,{source});QCOMPARE(b.results()->count(),5);b.undo();QCOMPARE(b.results()->rows,songs);
    b.collection()->setQuery("");b.collection()->setSortKey("original");
    auto matches=b.localMatches("nebula");QVERIFY(matches.size()<=8);QCOMPARE(matches.first().toMap().value("kind").toString(),"local");
    auto exact=b.localMatches("nebula 2");QCOMPARE(exact.size(),1);QCOMPARE(exact.first().toMap().value("queueIndex").toInt(),2);
    QVERIFY(b.localMatches("zz-no-match").isEmpty());QVERIFY(b.localMatches("").isEmpty());
    b.removeQueueRows({0,1,2,3,4,5});QCOMPARE(b.currentIndex(),-1);QCOMPARE(b.queue()->count(),0);b.undo();QCOMPARE(b.queue()->rows,songs);
    b.deletePlaylist(id);b.clearQueue();
  }
  void selectionTracksModelChanges() {
    Entries entries;entries.assign({track("one"),track("two"),track("three"),QVariantMap{{"kind","album"}}});
    RowSelection selection;selection.setModel(&entries);selection.select(0);selection.select(2,Qt::ShiftModifier);QCOMPARE(selection.rows(),QVariantList({0,1,2}));
    selection.select(1,Qt::ControlModifier);QCOMPARE(selection.rows(),QVariantList({0,2}));QCOMPARE(selection.items().size(),2);
    selection.selectAll();QCOMPARE(selection.count(),3);entries.assign({track("new")});QCOMPARE(selection.count(),0);
    selection.select(0);entries.append({track("other")});QCOMPARE(selection.count(),0);
    CollectionView proxy;proxy.setSourceModel(&entries);selection.setModel(&proxy);selection.selectAll();proxy.setQuery("other");QCOMPARE(selection.count(),0);
    selection.select(0);QCOMPARE(selection.items().first().toMap().value("id").toString(),"other");
  }
  void homePinsAndPersistence() {
    const QVariantMap album{{"kind","album"},{"id","MPRE-test-pin"},{"title","Pinned album"}};
    QString id;
    {
      Backend b;for(const auto &v:b.pins())b.togglePin(v.toMap());
      b.togglePin(track("not-a-collection"));QVERIFY(b.pins().isEmpty());
      b.togglePin(album);QVERIFY(b.isPinned(album));QCOMPARE(b.pins().size(),1);
      id=b.createPlaylist("Pinned list");b.openPlaylist(id);b.togglePin(b.collectionItem());
      b.renamePlaylist(id,"Renamed pin");QCOMPARE(b.pins().first().toMap().value("title").toString(),"Renamed pin");
      b.deletePlaylist(id);QCOMPARE(b.pins().size(),1);
      b.undo();QCOMPARE(b.pins().size(),2);
      b.open(b.pins().first().toMap());QCOMPARE(b.page(),"local");QCOMPARE(b.libraryId(),id);
      b.save();
    }
    Backend restored;QVERIFY(restored.isPinned(album));QCOMPARE(restored.pins().size(),2);
    QTemporaryDir dir;const QUrl url=QUrl::fromLocalFile(dir.filePath("pins.json"));restored.exportLibrary(url);
    for(const auto &v:restored.pins())restored.togglePin(v.toMap());QVERIFY(restored.pins().isEmpty());
    restored.importLibrary(url);QCOMPARE(restored.pins().size(),2);
    restored.importLibrary(url);QCOMPARE(restored.pins().size(),2);
    for(const auto &v:restored.pins())restored.togglePin(v.toMap());restored.deletePlaylist(id);
  }
  void sleepAfterTrack() {
    Backend b;b.clearQueue();b.setSleep(-1);QCOMPARE(b.sleepLabel(),"Off");
    b.enqueue(track("sleep000001"));b.enqueue(track("sleep000002"));b.playAt(0);b.pause();
    b.setRepeat(2);b.setAutoplay(true);b.setSleep(-1);QCOMPARE(b.sleepLabel(),"End of track");
    b.media()->mediaStatusChanged(QMediaPlayer::EndOfMedia);
    QCOMPARE(b.sleepLabel(),"Off");QCOMPARE(b.currentIndex(),0);QVERIFY(!b.playing());QVERIFY(!b.resolving());
    b.setSleep(-1);b.playAt(1);QCOMPARE(b.sleepLabel(),"Off");b.pause();
    b.setSleep(-1);b.setSleep(15);QCOMPARE(b.sleepLabel(),"15 min");
    b.setSleep(-1);b.clearQueue();QCOMPARE(b.sleepLabel(),"Off");b.setRepeat(0);b.setAutoplay(false);
  }
  void playbackSpeedAndPitch() {
    {Backend b;b.setPlaybackRate(1.25);QCOMPARE(b.playbackRate(),1.25);
      b.setPlaybackRate(0);b.setPlaybackRate(3);b.setPlaybackRate(std::numeric_limits<double>::quiet_NaN());QCOMPARE(b.playbackRate(),1.25);
      if(b.pitchAdjustable()){b.setPreservePitch(false);QVERIFY(!b.preservePitch());b.setPreservePitch(true);QVERIFY(b.preservePitch());}
    }
    Backend restored;QCOMPARE(restored.playbackRate(),1.25);restored.setPlaybackRate(1);
  }
  void lyricTextSizePersistence() {
    {Backend b;b.setLyricTextSize(28);QCOMPARE(b.lyricTextSize(),28);}
    Backend b;QCOMPARE(b.lyricTextSize(),28);b.setLyricTextSize(2);QCOMPARE(b.lyricTextSize(),20);b.setLyricTextSize(100);QCOMPARE(b.lyricTextSize(),32);b.setLyricTextSize(25);
  }
  void listeningPreferences() {
    {Backend b;QVERIFY(!b.historyPaused());b.setHistoryPaused(true);b.setKeepCompletedLyrics(false);b.setVolumeStep(2);b.setVolumeStep(7);QCOMPARE(b.volumeStep(),2);}
    Backend b;QVERIFY(!b.historyPaused());QVERIFY(!b.keepCompletedLyrics());QCOMPARE(b.volumeStep(),2);b.setKeepCompletedLyrics(true);b.setVolumeStep(5);
  }
  void queueTimeAndSmartShuffle() {
    qputenv("SUNG_HELPER",qgetenv("SUNG_FIXTURE_HELPER"));qputenv("SUNG_PYTHON","python3");
    Backend b;b.clearQueue();b.setShuffle(false);b.setRepeat(0);b.setAutoplay(false);b.setPlaybackRate(1);
    for(int i=0;i<7;++i){auto t=track(QString("queue%1").arg(i,6,10,QChar('0')));t["seconds"]=120;t["artist"]=QString(QChar('A'+i%3));b.enqueue(t);}
    b.playAt(0);b.pause();QTRY_VERIFY_WITH_TIMEOUT(!b.resolving(),5000);b.seek(30000);
    QCOMPARE(b.queueRemainingMs(),810000);QCOMPARE(b.queueTime(),"14 min left");QVERIFY(b.queueEnd().isEmpty());
    b.setPlaybackRate(1.5);QCOMPARE(b.queueRemainingMs(),540000);b.setPlaybackRate(1);
    b.enqueue(track("unknown0001"));QCOMPARE(b.queueRemainingMs(),-1);b.removeQueue(7);QCOMPARE(b.queueRemainingMs(),810000);
    const auto original=b.queue()->rows;const auto current=b.current();b.setShuffle(true);QCOMPARE(b.queueRemainingMs(),-1);
    b.smartShuffleQueue();QVERIFY(!b.shuffle());QCOMPARE(b.current(),current);QCOMPARE(b.currentIndex(),0);QCOMPARE(b.position(),30000);
    auto ids=[](const QVariantList &rows){QStringList result;for(const auto &row:rows)result.append(row.toMap().value("id").toString());result.sort();return result;};
    QCOMPARE(ids(b.queue()->rows),ids(original));
    for(int i=1;i<b.queue()->count();++i)QVERIFY(b.queue()->get(i).value("artist")!=b.queue()->get(i-1).value("artist"));
    b.undo();QCOMPARE(b.queue()->rows,original);QVERIFY(b.shuffle());QCOMPARE(b.position(),30000);
    b.smartShuffleQueue();b.next();b.pause();QVERIFY(b.undoMessage().isEmpty());
    b.setRepeat(1);QVERIFY(b.queueTime().endsWith("queued"));QVERIFY(b.queueEnd().isEmpty());
    b.clearQueue();QCOMPARE(b.queueRemainingMs(),0);b.setRepeat(0);b.setShuffle(false);
    qunsetenv("SUNG_HELPER");qunsetenv("SUNG_PYTHON");
  }
  void lyricTimingAndPersistence() {
    qputenv("SUNG_HELPER",qgetenv("SUNG_FIXTURE_HELPER"));
    qputenv("SUNG_PYTHON","python3");
    {
      Backend b;b.clearQueue();auto timed=track("timedlyric1");timed["seconds"]=120;b.enqueue(timed);b.enqueue(track("otherlyrics"));b.playAt(0);b.pause();
      QTRY_VERIFY_WITH_TIMEOUT(!b.resolving(),5000);b.fetchLyrics();
      QTRY_VERIFY_WITH_TIMEOUT(!b.lyricsBusy(),5000);QCOMPARE(b.lyricLines().size(),2);
      b.setLyricOffset(0);b.seek(500);QCOMPARE(b.lyricIndex(),-1);
      b.setLyricOffset(750);QCOMPARE(b.lyricIndex(),0);
      b.seekLyric(3000);QCOMPARE(b.position(),2250);QCOMPARE(b.lyricIndex(),1);
      b.setLyricOffset(-500);b.seekLyric(1000);QCOMPARE(b.position(),1500);QCOMPARE(b.lyricIndex(),0);
      b.playAt(1);b.pause();QCOMPARE(b.lyricOffset(),0);b.playAt(0);b.pause();QCOMPARE(b.lyricOffset(),-500);b.save();
    }
    Backend restored;QCOMPARE(restored.lyricOffset(),-500);
    QTemporaryDir dir;const auto url=QUrl::fromLocalFile(dir.filePath("timing.json"));restored.exportLibrary(url);
    restored.setLyricOffset(0);restored.importLibrary(url);QCOMPARE(restored.lyricOffset(),-500);
    restored.setLyricOffset(99999);QCOMPARE(restored.lyricOffset(),10000);restored.setLyricOffset(0);restored.clearQueue();
    qunsetenv("SUNG_HELPER");qunsetenv("SUNG_PYTHON");
  }
  void immediatePlaybackKeepsQueue() {
    Backend b;b.clearQueue();
    for(const auto &id:{"keepqueue01","keepqueue02","keepqueue03"})b.enqueue(track(id));
    b.playAt(0);b.pause();b.playKeepingQueue(track("keepqueue04"));b.pause();
    QCOMPARE(b.queue()->count(),4);QCOMPARE(b.currentIndex(),1);
    QCOMPARE(b.queue()->get(0).value("id").toString(),"keepqueue01");QCOMPARE(b.queue()->get(2).value("id").toString(),"keepqueue02");
    b.playKeepingQueue(track("keepqueue03"));b.pause();QCOMPARE(b.currentIndex(),2);QCOMPARE(b.queue()->count(),4);
    QCOMPARE(b.queue()->get(3).value("id").toString(),"keepqueue02");
    b.playKeepingQueue(track("keepqueue03"));b.pause();QCOMPARE(b.currentIndex(),2);QCOMPARE(b.queue()->count(),4);
    b.playKeepingQueue({{"kind","album"},{"id","not a song"}});QCOMPARE(b.queue()->count(),4);
    b.clearQueue();b.playKeepingQueue(track("keepqueue01"));b.pause();QCOMPARE(b.queue()->count(),1);QCOMPARE(b.currentIndex(),0);b.clearQueue();
  }
  void queueAndValidation() {
    Backend b;
    b.clearQueue();
    b.enqueue({{"id", "album"}, {"kind", "album"}});
    QCOMPARE(b.queue()->count(), 0);
    b.enqueue(track("aaaaaaaaaaa"));
    b.enqueue(track("bbbbbbbbbbb"));
    b.enqueue(track("ccccccccccc"));
    QCOMPARE(b.queue()->count(), 3);
    b.moveQueue(2, 0);
    QCOMPARE(b.queue()->get(0).value("id").toString(), "ccccccccccc");
    b.removeQueue(-1);
    b.removeQueue(999);
    QCOMPARE(b.queue()->count(), 3);
    b.removeQueue(1);
    QCOMPARE(b.queue()->count(), 2);
    b.clearQueue();
    QCOMPARE(b.currentIndex(), -1);
  }
  void favoritesAndPersistence() {
    QString id;
    {
      Backend b;
      b.clearQueue();
      auto t = track("favorite001");
      if (b.isLiked("favorite001"))
        b.toggleLike(t);
      b.toggleLike(t);
      QVERIFY(b.isLiked("favorite001"));
      id = b.createPlaylist("  Evening  ");
      QVERIFY(!id.isEmpty());
      b.addToPlaylist(id, t);
      b.addToPlaylist(id, t);
      b.openPlaylist(id);
      QCOMPARE(b.results()->count(), 1);
      QCOMPARE(b.title(), "Evening");
      b.renamePlaylist(id, "Night");
      QCOMPARE(b.title(), "Night");
      b.enqueue(t);
      b.save();
    }
    {
      Backend b;
      QVERIFY(b.isLiked("favorite001"));
      b.openPlaylist(id);
      QCOMPARE(b.results()->count(), 1);
      QCOMPARE(b.title(), "Night");
      QCOMPARE(b.queue()->count(), 1);
      b.removeFromPlaylist(id, 0);
      QCOMPARE(b.results()->count(), 0);
      b.deletePlaylist(id);
      b.toggleLike(track("favorite001"));
      QVERIFY(!b.isLiked("favorite001"));
    }
  }
  void settingsAndFormatting() {
    Backend b;
    b.setVolume(2);
    QCOMPARE(b.volume(), 1.0);
    b.setVolume(-1);
    QCOMPARE(b.volume(), 0.0);
    b.setRepeat(10);
    QCOMPARE(b.repeat(), 2);
    b.setTheme("light");
    b.setTheme("garbage");
    QCOMPARE(b.theme(), "light");
    b.setMotion(false);
    QVERIFY(!b.motion());
    QCOMPARE(b.formatTime(65000), "1:05");
    QCOMPARE(b.formatTime(3661000), "1:01:01");
    QCOMPARE(b.formatTime(-1), "0:00");
    b.setSleep(15);
    QCOMPARE(b.sleepLabel(), "15 min");
    b.setSleep(0);
    QCOMPARE(b.sleepLabel(), "Off");
  }
  void undoAndValidation() {
    Backend b;
    b.clearQueue();
    b.enqueue(track("aaaaaaaaaaa"));
    b.enqueue(track("bbbbbbbbbbb"));
    b.clearQueue();
    QCOMPARE(b.queue()->count(),0);
    b.undo();
    QCOMPARE(b.queue()->count(),2);
    QVERIFY(!b.playing());
    QVERIFY(b.createPlaylist("   ").isEmpty());
    auto id=b.createPlaylist("Recover me");
    b.addToPlaylist(id,track("aaaaaaaaaaa"));
    auto count=b.playlists().size();
    b.deletePlaylist(id);
    QCOMPARE(b.playlists().size(),count-1);
    b.undo();
    b.openPlaylist(id);
    QCOMPARE(b.results()->count(),1);
    b.copyLink(track("aaaaaaaaaaa"));
    QCOMPARE(QGuiApplication::clipboard()->text(),"https://music.youtube.com/watch?v=aaaaaaaaaaa");
    b.notifyError("ConnectionError: private details", "catalog");
    QVERIFY(b.canRetry());
    QVERIFY(!b.error().contains("private details"));
    b.dismissError();
    QVERIFY(!b.canRetry());
  }
  void catalogRequestsAndCancellation() {
    const auto helper=qgetenv("SUNG_FIXTURE_HELPER");
    QVERIFY2(!helper.isEmpty(), "CTest must provide the transport fixture path");
    qputenv("SUNG_HELPER",helper);
    qputenv("SUNG_PYTHON","python3");
    Backend b;
    b.home();
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    QCOMPARE(b.sections().size(),1);
    b.search("slow");
    b.search("latest","songs");
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    QCOMPARE(b.query(),"latest");
    QCOMPARE(b.results()->count(),30);
    QVERIFY(b.canMore());
    b.more();
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    QCOMPARE(b.results()->count(),60);
    b.open({{"kind","album"},{"browseId","ALBUM"},{"title","Album"}});
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    QCOMPARE(b.results()->count(),2);
    b.back();
    QCOMPARE(b.query(),"latest");
    QCOMPARE(b.results()->count(),60);
    b.search("error");
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    QVERIFY(b.canRetry());
    b.retry();
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    QVERIFY(b.canRetry());
    b.search("empty");
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    QVERIFY(b.error().isEmpty());
    QCOMPARE(b.results()->count(),0);
    b.openLink("https://music.youtube.com/watch?v=00000000001");
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    QCOMPARE(b.results()->count(),2);
    b.clearQueue();
    b.enqueueResults();
    QCOMPARE(b.queue()->count(),2);
    b.setAutoplay(false);
    b.playAt(0);
    QTRY_VERIFY_WITH_TIMEOUT(!b.resolving(),5000);
    QCOMPARE(b.currentIndex(),0);
    b.next();
    QCOMPARE(b.currentIndex(),1);
    b.previous();
    QCOMPARE(b.currentIndex(),0);
    b.setShuffle(true);
    b.next();
    QCOMPARE(b.currentIndex(),1);
    b.setShuffle(false);
    b.setRepeat(1);
    b.next();
    QCOMPARE(b.currentIndex(),0);
    b.fetchLyrics();
    QTRY_VERIFY_WITH_TIMEOUT(!b.lyricsBusy(),5000);
    QCOMPARE(b.lyrics(),"Test lyrics");
    b.radio(track("00000000001"));
    QTRY_COMPARE_WITH_TIMEOUT(b.queue()->count(),4,5000);
    b.stop();
    qunsetenv("SUNG_HELPER");
    qunsetenv("SUNG_PYTHON");
  }
  void libraryExportMergeAndUndo() {
    Backend b;b.clearQueue();
    b.enqueue(track("aaaaaaaaaaa"));b.enqueue(track("bbbbbbbbbbb"));
    b.removeQueue(0);b.enqueue(track("ccccccccccc"));b.undo();
    QCOMPARE(b.queue()->count(),2);
    QCOMPARE(b.queue()->get(0).value("id").toString(),"bbbbbbbbbbb");
    b.saveQueue("Saved queue");
    const auto id=b.playlists().last().toMap().value("id").toString();
    b.movePlaylistTrack(id,1,0);b.openPlaylist(id);
    QCOMPARE(b.results()->get(0).value("id").toString(),"ccccccccccc");
    QTemporaryDir temp;const auto url=QUrl::fromLocalFile(temp.filePath("library.json"));
    b.exportLibrary(url);QVERIFY(QFile::exists(url.toLocalFile()));
    const auto count=b.playlists().size();b.deletePlaylist(id);
    b.importLibrary(url);QCOMPARE(b.playlists().size(),count);
    b.importLibrary(url);QCOMPARE(b.playlists().size(),count);
    b.openPlaylist(id);QCOMPARE(b.results()->count(),2);
    b.deletePlaylist(id);b.clearQueue();
  }
  void timedLyricsFollowSeekAndClear() {
    qputenv("SUNG_HELPER",qgetenv("SUNG_FIXTURE_HELPER"));qputenv("SUNG_PYTHON","python3");
    Backend b;auto t=track("timedlyric1");t["seconds"]=120;b.playItem(t);
    QTRY_VERIFY_WITH_TIMEOUT(!b.resolving(),5000);b.fetchLyrics();
    QTRY_VERIFY_WITH_TIMEOUT(!b.lyricsBusy(),5000);QCOMPARE(b.lyricLines().size(),2);
    b.seek(500);QCOMPARE(b.lyricIndex(),-1);
    QSignalSpy changes(&b,&Backend::lyricIndexChanged);
    b.seek(1500);QCOMPARE(b.lyricIndex(),0);QCOMPARE(changes.count(),1);
    b.seek(1600);b.seek(1700);QCOMPARE(changes.count(),1);
    b.setLyricOffset(1000);QCOMPARE(b.lyricIndex(),-1);QCOMPARE(changes.count(),2);
    b.setLyricOffset(0);QCOMPARE(b.lyricIndex(),0);QCOMPARE(changes.count(),3);
    b.seek(2500);QCOMPARE(b.lyricIndex(),-1);
    b.seek(3500);QCOMPARE(b.lyricIndex(),1);
    b.seek(5000);QCOMPARE(b.lyricIndex(),-1);
    b.stop();QVERIFY(b.lyricLines().isEmpty());QCOMPARE(b.lyricIndex(),-1);
    qunsetenv("SUNG_HELPER");qunsetenv("SUNG_PYTHON");
  }
  void savedPlaybackPosition() {
    qputenv("SUNG_HELPER",qgetenv("SUNG_FIXTURE_HELPER"));qputenv("SUNG_PYTHON","python3");
    {
      Backend b;b.clearQueue();auto t=track("position001");t["seconds"]=180;
      b.playItem(t);QTRY_VERIFY_WITH_TIMEOUT(!b.resolving(),5000);
      b.seek(60000);b.save();QCOMPARE(b.position(),qint64(60000));
    }
    {
      Backend b;QCOMPARE(b.position(),qint64(60000));QVERIFY(!b.playing());
      b.stop();QCOMPARE(b.position(),qint64(0));QVERIFY(b.stopped());b.clearQueue();
    }
    qunsetenv("SUNG_HELPER");qunsetenv("SUNG_PYTHON");
  }
  void cookieImportAndRemoval() {
    Backend b;
    QTemporaryDir files;
    auto path=files.filePath("cookies.txt");
    QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Netscape HTTP Cookie File\n.youtube.com\tTRUE\t/\tTRUE\t0\ttest\tfixture\n"); f.close();
    b.setCookieFile(QUrl::fromLocalFile(path));
    QVERIFY(!b.cookies().isEmpty());
    const auto saved=b.cookies();
    QVERIFY(QFile::exists(saved));
    QVERIFY(!(QFile::permissions(saved) & (QFile::ReadOther|QFile::ReadGroup)));
    b.clearCookies();
    QVERIFY(b.cookies().isEmpty());
    QVERIFY(!QFile::exists(saved));
    b.setCookieFile(QUrl::fromLocalFile(files.filePath("missing")));
    QVERIFY(!b.error().isEmpty());
  }
  void noctaliaUpdatesAtomically() {
    QTemporaryDir files;
    const auto path=files.filePath("noctalia.colors");
    qputenv("SUNG_NOCTALIA_COLORS",path.toUtf8());
    auto write=[&](bool light){
      QSaveFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
      QByteArray data;
      const QList<QByteArray> groups={"View","Window","Button","Complementary"};
      const QList<QByteArray> keys={"BackgroundNormal","BackgroundAlternate","ForegroundNormal","ForegroundInactive","ForegroundActive","ForegroundLink","DecorationHover","DecorationFocus"};
      for(const auto &g:groups){data+="[Colors:"+g+"]\n";for(const auto &k:keys)data+=k+"="+(light?"240,240,240":"20,20,20")+"\n";}
      file.write(data); QVERIFY(file.commit());
    };
    write(false);
    DesktopTheme t;
    QVERIFY(t.available()); QVERIFY(t.dark());
    QSignalSpy changed(&t,&DesktopTheme::changed);
    write(true);
    QTRY_VERIFY_WITH_TIMEOUT(!t.dark(),3000);
    QVERIFY(changed.count()>0);
    QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly));f.write("partial");f.close();
    QTest::qWait(200);
    QVERIFY(t.available()); QVERIFY(!t.dark());
    write(false);
    QTRY_VERIFY_WITH_TIMEOUT(t.dark(),3000);
    qunsetenv("SUNG_NOCTALIA_COLORS");
  }
  void collectionFilterSortAndPlayback() {
    qputenv("SUNG_HELPER",qgetenv("SUNG_FIXTURE_HELPER"));qputenv("SUNG_PYTHON","python3");
    Backend b;b.clearQueue();
    auto a=track("collection1");a["title"]="Echo 10";a["artist"]="Alpha";a["duration"]="4:05";
    auto c=track("collection2");c["title"]="echo 2";c["artist"]="Beta";c["duration"]="1:05:00";
    auto d=track("collection3");d["title"]="Other";d["artist"]="Alpha";d["seconds"]=80;
    b.results()->assign({a,c,d});auto view=b.collection();
    view->setQuery("ECHO beta");QCOMPARE(view->count(),1);QCOMPARE(view->sourceIndex(0),1);
    QCOMPARE(view->sourceIndex(9),-1);
    view->setQuery("Alpha");view->setSortKey("duration");
    QCOMPARE(view->count(),2);QCOMPARE(view->get(0).value("id"),d.value("id"));
    QCOMPARE(b.results()->get(0).value("id"),a.value("id"));
    b.playCollection(0);QCOMPARE(b.queue()->count(),2);QCOMPARE(b.current().value("id"),d.value("id"));b.stop();
    view->setQuery("");view->setSortKey("title");
    QCOMPARE(view->get(0).value("id"),c.value("id"));
    view->setSortKey("original");QCOMPARE(view->get(0).value("id"),a.value("id"));
    view->setQuery("no matches");QCOMPARE(view->count(),0);b.enqueueCollection();QCOMPARE(b.queue()->count(),2);
    b.library("favorites");QVERIFY(view->query().isEmpty());QCOMPARE(view->sortKey(),"original");
    b.back();QCOMPARE(view->query(),"no matches");b.clearQueue();
    qunsetenv("SUNG_HELPER");qunsetenv("SUNG_PYTHON");
  }
  void audioOutputValidation() {
    Backend b;b.setAudioDeviceId("");
    QVERIFY(!b.audioDevices().isEmpty());QCOMPARE(b.audioDevices().first().toMap().value("name").toString(),"System default");
    b.setAudioDeviceId("missing-output");QVERIFY(b.audioDeviceId().isEmpty());
    QCOMPARE(b.audioDeviceName(),"System default");
  }
  void navigation() {
    Backend b;
    b.library("favorites");
    QCOMPARE(b.page(), "library");
    auto id = b.createPlaylist("Back test");
    b.openPlaylist(id);
    QCOMPARE(b.page(), "local");
    QVERIFY(b.canBack());
    b.back();
    QCOMPARE(b.page(), "library");
    b.deletePlaylist(id);
  }
};
QTEST_MAIN(BackendTest)
#include "backend_test.moc"
