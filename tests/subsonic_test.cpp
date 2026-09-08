#include "backend.h"
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QtTest>

class SubsonicTest : public QObject {
  Q_OBJECT
  Backend *b = nullptr;
  QVariantList songs;
  QVariantMap response;
  QString failure;
  bool finished = false;
  void call(const QString &method, const Subsonic::Params &params = {}) {
    finished = false;
    response.clear();
    failure.clear();
    b->server()->call(method, params,
                      [this](const QVariantMap &d, const QString &e) {
                        response = d;
                        failure = e;
                        finished = true;
                      });
  }
private slots:
  void initTestCase() {
    QVERIFY2(qEnvironmentVariableIsSet("SUNG_TEST_SERVER"),
             "Run through tests/navidrome_integration.py");
    QCoreApplication::setApplicationName("sung-integration");
    QCoreApplication::setOrganizationName("SungTests");
    b = new Backend;
    b->setVolume(0);
    b->setAutoplay(false);
    b->setPrepareNext(false);
  }
  void authentication() {
    b->server()->connectServer(qEnvironmentVariable("SUNG_TEST_SERVER"),
                               qEnvironmentVariable("SUNG_TEST_USER"), "wrong",
                               false);
    QTRY_VERIFY_WITH_TIMEOUT(!b->server()->connecting(), 10000);
    QVERIFY(!b->server()->connected());
    QVERIFY(!b->server()->error().isEmpty());
    b->server()->connectServer(qEnvironmentVariable("SUNG_TEST_SERVER"),
                               qEnvironmentVariable("SUNG_TEST_USER"),
                               qEnvironmentVariable("SUNG_TEST_PASSWORD"),
                               false);
    QTRY_VERIFY_WITH_TIMEOUT(b->server()->connected(), 10000);
    QTRY_VERIFY(!b->server()->folders().isEmpty());
  }
  void catalogPaginationAndNavigation() {
    b->browseServer("search", "Fixture");
    QTRY_VERIFY(!b->busy());
    QCOMPARE(b->results()->count(), 100);
    QVERIFY(b->canMore());
    const auto first = b->results()->get(0);
    b->more();
    QTRY_VERIFY(!b->busy());
    QCOMPARE(b->results()->count(), 105);
    QVERIFY(!b->canMore());
    QCOMPARE(b->results()->get(0), first);
    songs = b->results()->rows;
    QSet<QString> ids;
    for (const auto &v : songs) {
      auto t = v.toMap();
      ids.insert(t.value("id").toString());
      QVERIFY(t.value("serverSong").toBool());
      QVERIFY(!t.contains("videoId"));
      QVERIFY(!t.value("art").toString().contains("?"));
    }
    QCOMPARE(ids.size(), 105);
    b->browseServer("artists");
    QTRY_VERIFY(!b->busy());
    QCOMPARE(b->results()->count(), 1);
    b->open(b->results()->get(0));
    QTRY_VERIFY(!b->busy());
    QCOMPARE(b->results()->count(), 35);
    b->open(b->results()->get(0));
    QTRY_VERIFY(!b->busy());
    QCOMPARE(b->results()->count(), 3);
    QVERIFY(b->results()->get(0).value("serverSong").toBool());
    b->back();
    QCOMPARE(b->results()->count(), 35);
    b->browseServer("genres");
    QTRY_VERIFY(!b->busy());
    QCOMPARE(b->results()->count(), 1);
    b->open(b->results()->get(0));
    QTRY_VERIFY(!b->busy());
    QCOMPARE(b->results()->count(), 100);
    b->browseServer("search", "does-not-exist");
    QTRY_VERIFY(!b->busy());
    QCOMPARE(b->results()->count(), 0);
    QVERIFY(b->error().isEmpty());
    b->browseServer("albums");
    b->library("files");
    QTest::qWait(200);
    QCOMPARE(b->page(), "library");
    QCOMPARE(b->results()->count(), 0);
  }
  void artwork() {
    const auto song = songs.first().toMap();
    QVERIFY2(!song.value("art").toString().isEmpty(),
             "Fixture must have cover art");
    QNetworkAccessManager network;
    auto reply = network.get(QNetworkRequest(
        b->server()->artworkUrl(QUrl(song.value("art").toString()))));
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 10000);
    QCOMPARE(reply->error(), QNetworkReply::NoError);
    QVERIFY(!QImage::fromData(reply->readAll()).isNull());
    reply->deleteLater();
  }
  void favoritesAndRatings() {
    const auto song = songs.first().toMap();
    b->toggleLike(song);
    QTRY_VERIFY(b->isLiked(song.value("id").toString()));
    call("getStarred2");
    QTRY_VERIFY(finished);
    QVERIFY(failure.isEmpty());
    QCOMPARE(response.value("starred2").toMap().value("song").toList().size(),
             1);
    b->rateServerSong(song, 4);
    QTest::qWait(200);
    call("getSong", {{"id", song.value("remoteId").toString()}});
    QTRY_VERIFY(finished);
    QCOMPARE(response.value("song").toMap().value("userRating").toInt(), 4);
    b->browseServer("favorites");
    QTRY_VERIFY(!b->busy());
    QCOMPARE(b->results()->count(), 1);
    b->toggleLike(song);
    QTRY_VERIFY(!b->isLiked(song.value("id").toString()));
    QTRY_COMPARE(b->results()->count(), 0);
  }
  void remotePlaylistLifecycle() {
    b->server()->createPlaylist("Sung fixture playlist");
    QTRY_COMPARE(b->server()->playlists().size(), 1);
    auto playlist = b->server()->playlists().first().toMap();
    QVERIFY(playlist.value("editable").toBool());
    auto id = playlist.value("remoteId").toString();
    b->addServerPlaylist(id, songs.mid(0, 3));
    QTest::qWait(200);
    b->open(playlist);
    QTRY_VERIFY(!b->busy());
    QCOMPARE(b->results()->count(), 3);
    QVERIFY(b->serverPlaylistEditable());
    const auto first = b->results()->get(0);
    b->moveServerRows({0}, 3);
    QTRY_COMPARE(b->results()->get(2).value("id"), first.value("id"));
    b->removeServerRows({1});
    QTRY_COMPARE(b->results()->count(), 2);
    b->renameServerPlaylist(playlist, "Renamed + café & fixture");
    QTRY_COMPARE(b->title(), QString("Renamed + café & fixture"));
    b->addServerPlaylist(id, {QVariantMap{{"id", "abcdefghijk"},
                                          {"videoId", "abcdefghijk"},
                                          {"kind", "song"}}});
    QVERIFY(b->error().contains("connected server"));
    b->dismissError();
    b->deleteServerPlaylist(playlist);
    QTRY_COMPARE(b->server()->playlists().size(), 0);
    QTRY_VERIFY(!b->busy());
  }
  void coverPlaybackKeepsBrowsingIndependent() {
    b->browseServer("albums");QTRY_VERIFY(!b->busy());QVERIFY(b->results()->count()>0);
    const auto album=b->results()->get(0);
    b->playCover(album);
    b->browseServer("search","Fixture");
    QTRY_VERIFY_WITH_TIMEOUT(b->coverPlayId().isEmpty() && b->playing(),10000);
    QTRY_VERIFY(!b->busy());
    QCOMPARE(b->queue()->count(),3);
    QCOMPARE(b->serverRequest().value("mode").toString(),"search");
    QCOMPARE(b->results()->count(),100);
    b->playCover(album);b->stop();QTest::qWait(250);
    QVERIFY(!b->playing());QVERIFY(b->coverPlayId().isEmpty());
  }
  void playbackLyricsSeekingAndMixedQueue() {
    for (int i = 0; i < 3; ++i) {
      b->playItem(songs[i].toMap());
      QTRY_VERIFY_WITH_TIMEOUT(b->playing(), 10000);
      QVERIFY(b->media()->source().isLocalFile());
      QTRY_VERIFY(!b->serverArtwork().isEmpty());
      QVERIFY(QFile::exists(QUrl(b->serverArtwork()).toLocalFile()));
      QTRY_VERIFY(b->media()->isSeekable());
      QVERIFY(b->duration() > 11000);
      b->seek(5000);
      QTRY_VERIFY(b->position() >= 4900);
      b->pause();
      QVERIFY(!b->playing());
      b->play();
      QTRY_VERIFY(b->playing());
    }
    b->fetchLyrics();
    QTRY_VERIFY(!b->lyricsBusy());
    QCOMPARE(b->lyricLines().size(), 3);
    b->seekLyric(8000);
    QTRY_VERIFY(b->lyricIndex() >= 2);
    b->pause();
    const auto local = b->createPlaylist("Mixed sources");
    b->addItemsToPlaylist(
        local, {songs[0], QVariantMap{{"id", "abcdefghijk"},
                                      {"videoId", "abcdefghijk"},
                                      {"kind", "song"},
                                      {"title", "YouTube fixture"}}});
    b->openPlaylist(local);
    QCOMPARE(b->results()->count(), 2);
    b->server()->setBitrate(128);
    b->playItem(songs[0].toMap());
    QTRY_VERIFY_WITH_TIMEOUT(b->playing(), 10000);
    QVERIFY(b->duration() > 11000);
    b->server()->setBitrate(0);
  }
  void queueAndScrobbling() {
    b->playItem(songs[0].toMap());
    b->enqueue(songs[1].toMap());
    QTRY_VERIFY(b->playing());
    b->seek(2000);
    b->pause();
    b->saveServerQueue();
    QTest::qWait(200);
    b->clearQueue();
    b->restoreServerQueue();
    QTRY_COMPARE(b->queue()->count(), 2);
    QVERIFY(!b->playing());
    QVERIFY(b->position() >= 1900);
    b->playAt(0);
    QTRY_VERIFY(b->playing());
    QTRY_VERIFY_WITH_TIMEOUT(b->m_serverSubmitted, 10000);
    QTest::qWait(200);
    call("getSong", {{"id", songs[0].toMap().value("remoteId").toString()}});
    QTRY_VERIFY(finished);
    QVERIFY(response.value("song").toMap().value("playCount").toInt() > 0);
    b->stop();
  }
  void persistenceAndDisconnect() {
    b->save();
    QFile file(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        "/library.json");
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto bytes = file.readAll();
    QVERIFY(!bytes.contains(qgetenv("SUNG_TEST_PASSWORD")));
    QVERIFY(!bytes.contains("&t="));
    QVERIFY(!bytes.contains("/rest/stream"));
    const auto song = songs[0].toMap();
    auto wrong = song;
    wrong["server"] = "different-account";
    b->playItem(wrong);
    QTRY_VERIFY(!b->resolving());
    QVERIFY(!b->playing());
    QVERIFY(!b->error().isEmpty());
    b->server()->disconnectServer();
    QVERIFY(!b->server()->connected());
    b->playItem(song);
    QTRY_VERIFY(!b->resolving());
    QVERIFY(!b->playing());
    QVERIFY(!b->error().isEmpty());
  }
  void cleanupTestCase() { delete b; }
};
QTEST_MAIN(SubsonicTest)
#include "subsonic_test.moc"
