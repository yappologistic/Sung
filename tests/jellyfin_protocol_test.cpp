#include "jellyfin.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
class JellyfinProtocolTest : public QObject {
  Q_OBJECT
  QTemporaryDir storage;
  QTcpServer http;
  QString mode = "ok", base;
  QByteArray received;
  int requests = 0;
private slots:
  void initTestCase() {
    qputenv("XDG_CONFIG_HOME", storage.path().toUtf8());
    QCoreApplication::setOrganizationName("SungTests");
    QCoreApplication::setApplicationName("jellyfin-protocol");
    QVERIFY(http.listen(QHostAddress::LocalHost));
    base = "http://127.0.0.1:" + QString::number(http.serverPort()) + "/music";
    connect(&http, &QTcpServer::newConnection, this, [this] {
      while (http.hasPendingConnections()) {
        auto *socket = http.nextPendingConnection();
        connect(socket, &QTcpSocket::disconnected, socket,
                &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
          auto data =
              socket->property("data").toByteArray() + socket->readAll();
          socket->setProperty("data", data);
          const int end = data.indexOf("\r\n\r\n");
          if (end < 0 || socket->property("sent").toBool())
            return;
          int length = 0;
          for (const auto &line : data.left(end).split('\n'))
            if (line.toLower().startsWith("content-length:"))
              length = line.mid(15).trimmed().toInt();
          if (data.size() < end + 4 + length)
            return;
          socket->setProperty("sent", true);
          received = data;
          ++requests;
          if (mode == "hold")
            return;
          QByteArray status = "200 OK",
                     body = "{\"Items\":[],\"TotalRecordCount\":0}", extra;
          if (data.startsWith("POST /music/Users/AuthenticateByName"))
            body = "{\"User\":{\"Id\":\"fixture-user\"},\"AccessToken\":"
                   "\"fixture-token\"}";
          if (data.startsWith("GET /music/Users/Me"))
            body = "{\"Id\":\"fixture-user\"}";
          if (mode == "redirect") {
            status = "302 Found";
            extra = "Location: http://127.0.0.1:1/leak\r\n";
          }
          if (mode == "malformed")
            body = "<html>Wrong server</html>";
          if (mode == "expired")
            status = "401 Unauthorized";
          if (mode == "forbidden")
            status = "403 Forbidden";
          if (mode == "missing")
            status = "404 Not Found";
          if (mode == "oversized")
            body = QByteArray(17 * 1024 * 1024, 'x');
          if (mode == "disconnect") {
            socket->abort();
            return;
          }
          if (mode == "lyrics")
            body = "{\"Metadata\":{\"IsSynced\":true,\"Offset\":10000000},"
                   "\"Lyrics\":[{\"Text\":\"One\",\"Start\":20000000},{"
                   "\"Text\":\"Two\",\"Start\":50000000}]}";
          socket->write("HTTP/1.1 " + status + "\r\n" + extra +
                        "Content-Type: application/json\r\nContent-Length: " +
                        QByteArray::number(body.size()) +
                        "\r\nConnection: close\r\n\r\n" + body);
          socket->disconnectFromHost();
        });
      }
    });
  }
  void init() {
    mode = "ok";
    received.clear();
    requests = 0;
  }
  void rejectsBadResponses_data() {
    QTest::addColumn<QString>("behavior");
    for (auto v : {"redirect", "malformed", "expired", "forbidden", "oversized",
                   "disconnect"})
      QTest::newRow(v) << QString(v);
  }
  void rejectsBadResponses() {
    QFETCH(QString, behavior);
    mode = behavior;
    Jellyfin client(false);
    client.connectServer(base, "fixture", "test-secret", false);
    QTRY_VERIFY_WITH_TIMEOUT(!client.connecting(), 5000);
    QVERIFY(!client.connected());
    QVERIFY(!client.error().isEmpty());
    QVERIFY(!client.error().contains("test-secret"));
  }
  void credentialsAndBasePath() {
    mode = "hold";
    Jellyfin client(false);
    client.connectServer(base, "café + &", "test-secret", false);
    QTRY_VERIFY(!received.isEmpty());
    const auto header = received.left(received.indexOf("\r\n\r\n"));
    QVERIFY(header.startsWith("POST /music/Users/AuthenticateByName "));
    QVERIFY(!header.contains("test-secret"));
    QVERIFY(received.contains("test-secret"));
    client.disconnectServer();
    QVERIFY(!client.connecting());
  }
  void invalidAddresses() {
    Jellyfin client(false);
    for (auto s : {"file:///tmp/server", "http://user:secret@localhost",
                   "http://localhost/?token=x", "http://localhost/#web"}) {
      client.connectServer(s, "a", "", false);
      QVERIFY(!client.connecting());
      QVERIFY(!client.error().isEmpty());
    }
  }
  void keyringSaveRestoreAndForget() {
    mode = "ok";
    const auto oldPath = qgetenv("PATH");
    const auto bin = storage.filePath("bin");
    QDir().mkpath(bin);
    const auto secretFile = storage.filePath("keyring-value");
    QFile tool(bin + "/secret-tool");
    QVERIFY(tool.open(QIODevice::WriteOnly));
    tool.write(R"PY(#!/usr/bin/python3
import os, pathlib, sys
p=pathlib.Path(os.environ['SUNG_TEST_KEYRING'])
if sys.argv[1]=='store':
    p.write_bytes(sys.stdin.buffer.read())
    p.chmod(0o600)
elif sys.argv[1]=='lookup':
    if not p.exists(): sys.exit(1)
    sys.stdout.buffer.write(p.read_bytes()+b'\n')
elif sys.argv[1]=='clear':
    p.unlink(missing_ok=True)
else:
    sys.exit(2)
)PY");
    tool.close();
    QVERIFY(tool.setPermissions(QFileDevice::ReadOwner |
                                QFileDevice::WriteOwner |
                                QFileDevice::ExeOwner));
    qputenv("PATH", bin.toUtf8() + ":" + oldPath);
    qputenv("SUNG_TEST_KEYRING", secretFile.toUtf8());
    const auto restore = qScopeGuard([&] {
      qputenv("PATH", oldPath);
      qunsetenv("SUNG_TEST_KEYRING");
    });
    {
      Jellyfin client;
      client.connectServer(base, "fixture", "keyring fixture + café", true);
      QTRY_VERIFY(client.connected());
      QTRY_VERIFY(QSettings().value("jellyfin/remember").toBool());
      QVERIFY(QFile::exists(secretFile));
      QFile config(QSettings().fileName());
      QSettings().sync();
      QVERIFY(config.open(QIODevice::ReadOnly));
      QVERIFY(!config.readAll().contains("keyring fixture"));
    }
    {
      Jellyfin client;
      QTRY_VERIFY(client.connected());
      QCOMPARE(client.username(), QString("fixture"));
      client.disconnectServer();
      QTRY_VERIFY(!QFile::exists(secretFile));
      QVERIFY(!QSettings().contains("jellyfin/remember"));
    }
  }
  void cancelledLogin() {
    mode = "hold";
    Jellyfin client(false);
    client.connectServer(base, "a", "b", false);
    QTRY_VERIFY(!received.isEmpty());
    client.disconnectServer();
    QTest::qWait(100);
    QVERIFY(!client.connected());
    QVERIFY(client.error().isEmpty());
  }
  void artworkCredentialsAreEphemeral() {
    Jellyfin client(false);
    client.connectServer(base, "a", "b", false);
    QTRY_VERIFY(client.connected());
    auto item = client.item(
        {{"Id", "song"}, {"ImageTags", QVariantMap{{"Primary", "tag"}}}},
        "song");
    QVERIFY(!item.value("art").toString().contains("fixture-token"));
    const auto req = client.artworkRequest(QUrl(item.value("art").toString()));
    QVERIFY(!req.url().toString().contains("fixture-token"));
    QVERIFY(req.rawHeader("Authorization").contains("fixture-token"));
    client.disconnectServer();
    QVERIFY(client.artworkUrl(QUrl(item.value("art").toString())).isEmpty());
  }
  void lyricOffsetAndMissing() {
    Jellyfin client(false);
    client.connectServer(base, "a", "b", false);
    QTRY_VERIFY(client.connected());
    QTest::qWait(100);
    auto song = client.item({{"Id", "song"}}, "song");
    mode = "lyrics";
    bool done = false;
    QVariantMap result;
    QString error;
    client.lyrics(song, [&](const QVariantMap &d, const QString &e) {
      done = true;
      result = d;
      error = e;
    });
    QTRY_VERIFY(done);
    QVERIFY(error.isEmpty());
    QCOMPARE(result.value("lines").toList().size(), 2);
    QCOMPARE(result.value("lines")
                 .toList()
                 .first()
                 .toMap()
                 .value("start")
                 .toLongLong(),
             1000);
    mode = "missing";
    done = false;
    client.lyrics(song, [&](const QVariantMap &d, const QString &e) {
      done = true;
      result = d;
      error = e;
    });
    QTRY_VERIFY(done);
    QVERIFY(error.isEmpty());
    QVERIFY(result.value("lines").toList().isEmpty());
  }
  void failedAudioCleansBuffer() {
    Jellyfin client(false);
    client.connectServer(base, "a", "b", false);
    QTRY_VERIFY(client.connected());
    QTest::qWait(100);
    mode = "forbidden";
    const auto path = storage.filePath("failed-audio");
    bool done = false;
    QString error;
    client.download(client.item({{"Id", "song"}}, "song"), path,
                    [&](const QVariantMap &, const QString &e) {
                      done = true;
                      error = e;
                    });
    QTRY_VERIFY(done);
    QVERIFY(!error.isEmpty());
    QVERIFY(!QFile::exists(path));
  }
  void accountIdentity() {
    Jellyfin a(false), b(false);
    a.connectServer(base, "a", "b", false);
    b.connectServer(base + "/other", "a", "b", false);
    QTRY_VERIFY(a.connected());
    const auto song = a.item({{"Id", "song"}}, "song");
    QVERIFY(!b.owns(song));
    a.disconnectServer();
    QVERIFY(!a.owns(song));
  }
};
QTEST_GUILESS_MAIN(JellyfinProtocolTest)
#include "jellyfin_protocol_test.moc"
