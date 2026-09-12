#include "backend.h"
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
class JellyfinTest : public QObject {
  Q_OBJECT
  Backend *b = nullptr;
  bool duplicatePlaylists = true;
  QVariantList songs;
  QVariantMap playlist;
  QVariantMap response;
  QString failure;
  bool done = false;
  void result(const QVariantMap &d, const QString &e) {
    response = d;
    failure = e;
    done = true;
  }
  Subsonic::Reply callback() {
    done = false;
    failure.clear();
    response.clear();
    return [this](const QVariantMap &d, const QString &e) { result(d, e); };
  }
  void rest(const QString &path, const QByteArray &verb = "GET",
            QJsonObject data = {}) {
    done = false;
    failure.clear();
    auto network = new QNetworkAccessManager(this);
    QNetworkRequest req(QUrl(qEnvironmentVariable("SUNG_TEST_SERVER") + path));
    req.setRawHeader("Authorization", "MediaBrowser Token=\"" +
                                          qgetenv("SUNG_TEST_ADMIN_TOKEN") +
                                          "\"");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto r = network->sendCustomRequest(
        req, verb, verb == "GET" ? QByteArray() : QJsonDocument(data).toJson());
    connect(r, &QNetworkReply::finished, this, [this, r, network] {
      const auto doc = QJsonDocument::fromJson(r->readAll());
      response = doc.isArray()
                     ? QVariantMap{{"Items", doc.array().toVariantList()}}
                     : doc.object().toVariantMap();
      failure =
          r->error() == QNetworkReply::NoError ? QString() : r->errorString();
      done = true;
      network->deleteLater();
    });
  }
  void login(const QString &user = {}) {
    b->server()->connectServer(
        qEnvironmentVariable("SUNG_TEST_SERVER"),
        user.isEmpty() ? qEnvironmentVariable("SUNG_TEST_USER") : user,
        qEnvironmentVariable("SUNG_TEST_PASSWORD"), false);
  }
private slots:
  void initTestCase() {
    QVERIFY(qEnvironmentVariableIsSet("SUNG_TEST_SERVER"));
    QCoreApplication::setOrganizationName("SungTests");
    QCoreApplication::setApplicationName("sung-jellyfin-test");
    b = new Backend;
    b->setVolume(0);
    b->setAutoplay(false);
    b->setPrepareNext(false);
    b->server()->selectProvider("jellyfin");
    rest("/System/Info/Public");
    QTRY_VERIFY(done);
    QVERIFY(failure.isEmpty());
    duplicatePlaylists =
        response.value("Version").toString().section('.', 0, 0).toInt() >= 12;
  }
  void invalidAddress() {
    b->server()->connectServer("file:///tmp/test", "test", "", false);
    QVERIFY(!b->server()->connected());
    QVERIFY(!b->server()->error().isEmpty());
    b->server()->connectServer("https://user:password@localhost", "test", "",
                               false);
    QVERIFY(!b->server()->connecting());
  }
  void wrongPassword() {
    b->server()->connectServer(qEnvironmentVariable("SUNG_TEST_SERVER"),
                               qEnvironmentVariable("SUNG_TEST_USER"), "wrong",
                               false);
    QTRY_VERIFY_WITH_TIMEOUT(!b->server()->connecting(), 15000);
    QVERIFY(!b->server()->connected());
    QVERIFY(!b->server()->error().isEmpty());
  }
  void loginAndLibraries() {
    login();
    QTRY_VERIFY_WITH_TIMEOUT(b->server()->connected(), 15000);
    QTRY_COMPARE(b->server()->folders().size(), 2);
    QVERIFY(b->server()->address().endsWith("/jellyfin"));
    QVERIFY(!b->server()->supportsQueue());
    QVERIFY(!b->server()->supportsRating());
  }
  void searchPagination() {
    b->browseServer("search", "Fixture");
    QTRY_VERIFY(!b->busy());
    QCOMPARE(b->results()->count(), 100);
    QVERIFY(b->canMore());
    b->more();
    QTRY_VERIFY(!b->busy());
    QCOMPARE(b->results()->count(), 105);
    QVERIFY(!b->canMore());
    songs = b->results()->rows;
    QSet<QString> ids;
    for (const auto &v : songs) {
      auto t = v.toMap();
      ids.insert(t.value("id").toString());
      QCOMPARE(t.value("source").toString(), "jellyfin");
      QVERIFY(t.value("serverSong").toBool());
      QVERIFY(!t.contains("videoId"));
      QVERIFY(!t.value("art").toString().contains('?'));
    }
    QCOMPARE(ids.size(), 105);
  }
  void albumsArtistsGenres() {
    b->browseServer("search", "Fixture", "albums");
    QTRY_VERIFY(!b->busy());
    QCOMPARE(b->results()->count(), 3);
    b->open(b->results()->get(0));
    QTRY_VERIFY(!b->busy());
    QCOMPARE(b->results()->count(), 35);
    QVERIFY(b->results()->get(0).value("serverSong").toBool());
    b->back();
    QCOMPARE(b->results()->count(), 3);
    b->browseServer("search", "Sung Test", "artists");
    QTRY_VERIFY(!b->busy());
    QCOMPARE(b->results()->count(), 1);
    b->open(b->results()->get(0));
    QTRY_VERIFY(!b->busy());
    QCOMPARE(b->results()->count(), 3);
    b->browseServer("genres");
    QTRY_VERIFY(!b->busy());
    QVERIFY(b->results()->count() > 0);
    b->open(b->results()->get(0));
    QTRY_VERIFY(!b->busy());
    QCOMPARE(b->results()->count(), 100);
  }
  void init() {
    b->server()->setFolder("");
    b->dismissError();
  }
  void libraryFilter() {
    for (const auto &v : b->server()->folders()) {
      const auto f = v.toMap();
      if (f.value("name") != "Other Music")
        continue;
      b->server()->setFolder(f.value("id").toString());
      b->browseServer("albums");
      QTRY_VERIFY(!b->busy());
      QCOMPARE(b->results()->count(), 1);
      b->browseServer("search", "Fixture");
      QTRY_VERIFY(!b->busy());
      QCOMPARE(b->results()->count(), 0);
    }
    b->server()->setFolder("");
  }
  void emptyAndCancelledSearch() {
    b->browseServer("search", "Fixture");
    b->browseServer("search", "not-a-real-fixture-xyz");
    QTRY_VERIFY(!b->busy());
    QCOMPARE(b->results()->count(), 0);
    QVERIFY(!b->canMore());
    QTest::qWait(200);
    QCOMPARE(b->results()->count(), 0);
  }
  void artwork() {
    QVERIFY(!songs.isEmpty());
    auto song = songs.first().toMap();
    QVERIFY(!song.value("art").toString().isEmpty());
    auto req = b->server()->artworkRequest(QUrl(song.value("art").toString()));
    QVERIFY(!req.url().hasQuery() || !req.url().query().contains("api_key"));
    QVERIFY(req.hasRawHeader("Authorization"));
    QNetworkAccessManager net;
    auto r = net.get(req);
    QTRY_VERIFY(r->isFinished());
    QCOMPARE(r->error(), QNetworkReply::NoError);
    QVERIFY(!QImage::fromData(r->readAll()).isNull());
    r->deleteLater();
    QTemporaryDir dir;
    b->server()->cover(song, dir.path() + "/cover.png", callback());
    QTRY_VERIFY(done);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QVERIFY(!QImage(response.value("file").toString()).isNull());
  }
  void favoriteRoundTrip() {
    auto song = songs.first().toMap();
    b->toggleLike(song);
    QTRY_VERIFY(b->isLiked(song.value("id").toString()));
    rest("/Users/" + qEnvironmentVariable("SUNG_TEST_ADMIN_ID") + "/Items/" +
         song.value("remoteId").toString());
    QTRY_VERIFY(done);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QVERIFY(response.value("UserData").toMap().value("IsFavorite").toBool());
    b->browseServer("favorites");
    QTRY_VERIFY(!b->busy());
    QCOMPARE(b->results()->count(), 1);
    b->toggleLike(song);
    QTRY_VERIFY(!b->isLiked(song.value("id").toString()));
    QTRY_COMPARE(b->results()->count(), 0);
  }
  void createPlaylist() {
    b->server()->createPlaylist("Sung fixture playlist");
    QTRY_COMPARE(b->server()->playlists().size(), 1);
    playlist = b->server()->playlists().first().toMap();
    QVERIFY(playlist.value("editable").toBool());
  }
  void playlistDuplicatesAndReordering() {
    QVERIFY(!playlist.isEmpty());
    b->addServerPlaylist(playlist.value("remoteId").toString(),
                         {songs[0], songs[1], songs[0]});
    QTest::qWait(350);
    b->open(playlist);
    QTRY_VERIFY(!b->busy());
    QCOMPARE(b->results()->count(), duplicatePlaylists ? 3 : 2);
    QVERIFY(b->serverPlaylistEditable());
    if (duplicatePlaylists) {
      QCOMPARE(b->results()->get(0).value("id"),
               b->results()->get(2).value("id"));
      b->moveServerRows({1}, 3);
      QTRY_COMPARE(b->results()->get(2).value("id"),
                   songs[1].toMap().value("id"));
      b->removeServerRows({0});
      QTRY_COMPARE(b->results()->count(), 2);
    } else {
      // Jellyfin 10.11 deliberately removes duplicate playlist additions.
      b->moveServerRows({0}, 2);
      QTRY_COMPARE(b->results()->get(1).value("id"),
                   songs[0].toMap().value("id"));
      b->removeServerRows({0});
      QTRY_COMPARE(b->results()->count(), 1);
    }
    QCOMPARE(b->results()->get(0).value("id"), songs[0].toMap().value("id"));
  }
  void playlistRename() {
    b->renameServerPlaylist(playlist, "Renamed + café & fixture");
    QTRY_VERIFY(!b->server()->playlists().isEmpty() &&
                b->server()->playlists().first().toMap().value("title") ==
                    "Renamed + café & fixture");
    QTRY_COMPARE(b->title(), QString("Renamed + café & fixture"));
  }
  void mixedSourceRejection() {
    b->addServerPlaylist(
        playlist.value("remoteId").toString(),
        {QVariantMap{{"videoId", "abcdefghijk"}, {"kind", "song"}}});
    QVERIFY(b->error().contains("connected server"));
    b->dismissError();
  }
  void playbackFormats() {
    for (int i = 0; i < 3; i++) {
      b->playItem(songs[i].toMap());
      QTRY_VERIFY_WITH_TIMEOUT(b->playing(), 15000);
      QTRY_VERIFY(b->position() > 250);
      QVERIFY(b->media()->source().isLocalFile());
      QVERIFY(b->duration() > 11000);
      QTRY_VERIFY(b->media()->isSeekable());
      b->seek(5000);
      QTRY_VERIFY(b->position() >= 4900);
      b->pause();
      QVERIFY(!b->playing());
      b->play();
      QTRY_VERIFY(b->playing());
    }
    b->pause();
  }
  void synchronizedLyrics() {
    b->playItem(songs[0].toMap());
    QTRY_VERIFY(b->playing());
    b->fetchLyrics();
    QTRY_VERIFY(!b->lyricsBusy());
    QCOMPARE(b->lyricLines().size(), 3);
    b->seekLyric(8000);
    QTRY_VERIFY(b->lyricIndex() >= 2);
    b->pause();
  }
  void importedLyricsOverride() {
    QTemporaryDir dir;
    QFile file(dir.filePath("override.lrc"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("[00:00.00]Imported fixture line\n");
    file.close();
    b->playItem(songs[0].toMap());
    QTRY_VERIFY(b->playing());
    b->importLyrics(QUrl::fromLocalFile(file.fileName()),
                    songs[0].toMap().value("id").toString());
    QTRY_VERIFY(!b->lyricsBusy());
    QCOMPARE(b->lyricLines().size(), 1);
    QVERIFY(b->lyrics().contains("Imported fixture line"));
    b->resetLyrics();
    QTRY_VERIFY(!b->lyricsBusy());
    QCOMPARE(b->lyricLines().size(), 3);
    b->stop();
  }
  void absentLyrics() {
    b->server()->lyrics(songs.last().toMap(), callback());
    QTRY_VERIFY(done);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QCOMPARE(response.value("lines").toList().size(), 0);
  }
  void transcoding() {
    for (int rate : {128, 192, 320}) {
      b->server()->setBitrate(rate);
      b->playItem(songs[0].toMap());
      QTRY_VERIFY_WITH_TIMEOUT(b->playing(), 20000);
      QTRY_VERIFY(b->position() > 100);
      QVERIFY(b->duration() > 11000);
      b->stop();
    }
    b->server()->setBitrate(0);
  }
  void coverPlaybackAndNavigation() {
    b->browseServer("search", "Fixture", "albums");
    QTRY_VERIFY(!b->busy());
    b->playCover(b->results()->get(0));
    b->browseServer("search", "Fixture");
    QTRY_VERIFY_WITH_TIMEOUT(b->coverPlayId().isEmpty() && b->playing(), 15000);
    QCOMPARE(b->queue()->count(), 35);
    QTRY_VERIFY(!b->busy());
    QCOMPARE(b->results()->count(), 100);
    b->stop();
  }
  void mixedLocalQueue() {
    const auto local = b->createPlaylist("Mixed sources");
    b->addItemsToPlaylist(
        local, {songs[0], QVariantMap{{"id", "abcdefghijk"},
                                      {"videoId", "abcdefghijk"},
                                      {"kind", "song"},
                                      {"title", "YouTube fixture"}}});
    b->openPlaylist(local);
    QCOMPARE(b->results()->count(), 2);
    b->playItem(songs[0].toMap());
    QTRY_VERIFY(b->playing());
    b->enqueue(songs[1].toMap());
    b->next();
    QTRY_COMPARE(b->current().value("id"), songs[1].toMap().value("id"));
    QTRY_VERIFY(b->playing());
    b->stop();
  }
  void permissionIsolation() {
    rest("/Playlists/" + playlist.value("remoteId").toString(), "POST",
         {{"IsPublic", true}});
    QTRY_VERIFY(done);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    const auto original = songs[0].toMap();
    b->server()->disconnectServer();
    login(qEnvironmentVariable("SUNG_TEST_READER"));
    QTRY_VERIFY(b->server()->connected());
    QVERIFY(!b->server()->owns(original));
    b->browseServer("playlists");
    QTRY_VERIFY(!b->busy());
    QVERIFY(b->results()->count() > 0);
    b->open(b->results()->get(0));
    QTRY_VERIFY(!b->busy());
    QVERIFY(!b->serverPlaylistEditable());
    b->server()->editPlaylist(playlist.value("remoteId").toString(),
                              {{"name", "Forbidden edit"}}, callback());
    QTRY_VERIFY(done);
    QVERIFY(!failure.isEmpty());
    b->server()->disconnectServer();
    login();
    QTRY_VERIFY(b->server()->connected());
  }
  void sharedPlaylistEditor() {
    const auto id = playlist.value("remoteId").toString();
    rest("/Playlists/" + id + "/Users/" +
             qEnvironmentVariable("SUNG_TEST_READER_ID"),
         "POST", {{"CanEdit", true}});
    QTRY_VERIFY(done);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    b->server()->disconnectServer();
    login(qEnvironmentVariable("SUNG_TEST_READER"));
    QTRY_VERIFY(b->server()->connected());
    QTRY_VERIFY(!b->server()->playlists().isEmpty());
    QVERIFY(
        b->server()->playlists().first().toMap().value("editable").toBool());
    b->browseServer("playlists");
    QTRY_VERIFY(!b->busy());
    QVERIFY(b->results()->get(0).value("editable").toBool());
    b->open(b->results()->get(0));
    QTRY_VERIFY(!b->busy());
    QVERIFY(b->serverPlaylistEditable());
    b->server()->editPlaylist(id, {{"name", "Shared editor rename"}},
                              callback());
    QTRY_VERIFY(done);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    b->server()->disconnectServer();
    login();
    QTRY_VERIFY(b->server()->connected());
  }
  void playbackSessions() {
    b->setHistoryPaused(false);
    b->server()->setScrobbling(true);
    const auto song = songs[5].toMap();
    const auto id = song.value("remoteId").toString();
    auto session = [&] {
      QVariantMap found;
      for (const auto &v : response.value("Items").toList()) {
        const auto x = v.toMap();
        if (x.value("Client") == "Sung" &&
            x.value("NowPlayingItem").toMap().value("Id") == id)
          found = x;
      }
      return found;
    };
    b->playItem(song);
    QTRY_VERIFY(b->playing());
    QTest::qWait(200);
    rest("/Sessions");
    QTRY_VERIFY(done);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QVERIFY(!session().isEmpty());
    b->pause();
    QTest::qWait(150);
    rest("/Sessions");
    QTRY_VERIFY(done);
    QVERIFY(session().value("PlayState").toMap().value("IsPaused").toBool());
    b->seek(6000);
    QTest::qWait(150);
    rest("/Sessions");
    QTRY_VERIFY(done);
    QVERIFY(session()
                .value("PlayState")
                .toMap()
                .value("PositionTicks")
                .toLongLong() >= 59000000);
    b->play();
    QTRY_VERIFY(b->playing());
    b->setHistoryPaused(true);
    QTest::qWait(200);
    rest("/Sessions");
    QTRY_VERIFY(done);
    QVERIFY(session().isEmpty());
    b->setHistoryPaused(false);
    QTest::qWait(200);
    rest("/Sessions");
    QTRY_VERIFY(done);
    QVERIFY(!session().isEmpty());
    b->stop();
    QTest::qWait(200);
    rest("/Sessions");
    QTRY_VERIFY(done);
    QVERIFY(session().isEmpty());
  }
  void localFileAndServerQueue() {
    b->importLocalFiles(
        {QUrl::fromLocalFile(qEnvironmentVariable("SUNG_TEST_LOCAL_FILE"))
             .toString()});
    QTRY_VERIFY(!b->importingLocal());
    b->library("files");
    QTRY_VERIFY(b->results()->count() > 0);
    const auto local = b->results()->get(0);
    b->playItem(local);
    QTRY_VERIFY(b->playing());
    b->enqueue(songs[0].toMap());
    b->next();
    QTRY_COMPARE(b->current().value("id"), songs[0].toMap().value("id"));
    QTRY_VERIFY(b->playing());
    b->enqueue(local);
    b->next();
    QTRY_COMPARE(b->current().value("id"), local.value("id"));
    QTRY_VERIFY(b->playing());
    b->stop();
  }
  void privateListening() {
    b->setHistoryPaused(true);
    const auto song = songs[4].toMap();
    rest("/Users/" + qEnvironmentVariable("SUNG_TEST_ADMIN_ID") + "/Items/" +
         song.value("remoteId").toString());
    QTRY_VERIFY(done);
    const auto count =
        response.value("UserData").toMap().value("PlayCount").toInt();
    b->playItem(song);
    QTRY_VERIFY(b->playing());
    QTest::qWait(1300);
    b->stop();
    QTest::qWait(200);
    rest("/Users/" + qEnvironmentVariable("SUNG_TEST_ADMIN_ID") + "/Items/" +
         song.value("remoteId").toString());
    QTRY_VERIFY(done);
    QCOMPARE(response.value("UserData").toMap().value("PlayCount").toInt(),
             count);
    b->setHistoryPaused(false);
  }
  void deletePlaylist() {
    b->deleteServerPlaylist(playlist);
    QTRY_COMPARE(b->server()->playlists().size(), 0);
  }
  void disconnectedRowsAndPersistence() {
    b->save();
    QFile f(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
            "/library.json");
    QVERIFY(f.open(QIODevice::ReadOnly));
    const auto bytes = f.readAll();
    QVERIFY(!bytes.contains(qgetenv("SUNG_TEST_PASSWORD")));
    QVERIFY(!bytes.contains(qgetenv("SUNG_TEST_ADMIN_TOKEN")));
    QVERIFY(!bytes.contains("api_key"));
    b->server()->disconnectServer();
    QVERIFY(b->server()
                ->artworkUrl(QUrl(songs[0].toMap().value("art").toString()))
                .isEmpty());
    b->playItem(songs[0].toMap());
    QTRY_VERIFY(!b->resolving());
    QVERIFY(!b->playing());
    QVERIFY(!b->error().isEmpty());
  }
  void cleanupTestCase() { delete b; }
};
QTEST_MAIN(JellyfinTest)
#include "jellyfin_test.moc"
