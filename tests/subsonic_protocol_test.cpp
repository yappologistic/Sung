#include "subsonic.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

class ProtocolTest : public QObject {
  Q_OBJECT
  QTemporaryDir storage;
  QTcpServer http;
  QString mode, received;
  QString base;
private slots:
  void initTestCase() {
    qputenv("XDG_CONFIG_HOME", storage.path().toUtf8());
    QCoreApplication::setOrganizationName("SungTests");
    QCoreApplication::setApplicationName("protocol");
    QVERIFY(http.listen(QHostAddress::LocalHost));
    base = "http://127.0.0.1:" + QString::number(http.serverPort());
    connect(&http, &QTcpServer::newConnection, this, [this] {
      while (http.hasPendingConnections()) {
        auto socket = http.nextPendingConnection();
        connect(socket, &QTcpSocket::disconnected, socket,
                &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
          auto buffer =
              socket->property("request").toByteArray() + socket->readAll();
          socket->setProperty("request", buffer);
          if (!buffer.contains("\r\n\r\n") || socket->property("sent").toBool())
            return;
          socket->setProperty("sent", true);
          received = QString::fromUtf8(buffer);
          if (mode == "hold")
            return;
          QByteArray status = "200 OK",
                     body = "{\"subsonic-response\":{\"status\":\"ok\","
                            "\"version\":\"1.16.1\"}}",
                     extra;
          if (mode == "redirect") {
            status = "302 Found";
            extra = "Location: http://127.0.0.1:1/credential-leak\r\n";
          }
          if (mode == "malformed")
            body = "<html>Not a music server</html>";
          if (mode == "denied")
            body = "{\"subsonic-response\":{\"status\":\"failed\",\"error\":{"
                   "\"code\":50}}}";
          if (mode == "disconnect") {
            socket->abort();
            return;
          }
          if (mode == "oversized")
            body = QByteArray(17 * 1024 * 1024, 'x');
          socket->write("HTTP/1.1 " + status + "\r\n" + extra +
                        "Content-Type: application/json\r\nContent-Length: " +
                        QByteArray::number(body.size()) +
                        "\r\nConnection: close\r\n\r\n" + body);
          socket->disconnectFromHost();
        });
      }
    });
  }
  void validatesAddress() {
    Subsonic client;
    client.connectServer("https://user:secret@example.com", "user", "password",
                         false);
    QVERIFY(!client.connecting());
    QVERIFY(!client.error().isEmpty());
    client.connectServer(base + "/rest", "user", "password", false);
    QVERIFY(!client.connecting());
    QVERIFY(!client.error().isEmpty());
  }
  void rejectsUnsafeAndInvalidResponses_data() {
    QTest::addColumn<QString>("behavior");
    for (const auto &v :
         {"redirect", "malformed", "denied", "disconnect", "oversized"})
      QTest::newRow(v) << QString(v);
  }
  void rejectsUnsafeAndInvalidResponses() {
    QFETCH(QString, behavior);
    mode = behavior;
    Subsonic client;
    client.connectServer(base, "fixture", "test-secret", false);
    QTRY_VERIFY_WITH_TIMEOUT(!client.connecting(), 5000);
    QVERIFY(!client.connected());
    QVERIFY(!client.error().isEmpty());
    QVERIFY(!client.error().contains("test-secret"));
  }
  void encodesSpecialCharactersAndFreshSalt() {
    mode = "hold";
    Subsonic client;
    client.connectServer(base + "/music", "a+b & café", "never-in-url", false);
    QTRY_VERIFY(received.contains("/music/rest/ping.view"));
    QVERIFY(received.contains("a%2Bb%20%26%20caf%C3%A9"));
    QVERIFY(!received.contains("never-in-url"));
    const auto first = received;
    received.clear();
    client.connectServer(base + "/music", "a+b & café", "never-in-url", false);
    QTRY_VERIFY(!received.isEmpty());
    QVERIFY(received != first);
    client.disconnectServer();
    QVERIFY(!client.connecting());
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
      Subsonic client;
      client.connectServer(base, "fixture", "keyring fixture + café", true);
      QTRY_VERIFY(client.connected());
      QTRY_VERIFY(QSettings().value("subsonic/remember").toBool());
      QVERIFY(QFile::exists(secretFile));
      QFile config(QSettings().fileName());
      QSettings().sync();
      QVERIFY(config.open(QIODevice::ReadOnly));
      QVERIFY(!config.readAll().contains("keyring fixture"));
    }
    {
      Subsonic client;
      QTRY_VERIFY(client.connected());
      QCOMPARE(client.username(), QString("fixture"));
      client.disconnectServer();
      QTRY_VERIFY(!QFile::exists(secretFile));
      QVERIFY(!QSettings().contains("subsonic/remember"));
    }
  }
  void cancelAndReconnect() {
    mode = "hold";
    Subsonic client;
    client.connectServer(base, "fixture", "secret", false);
    QTest::qWait(50);
    client.disconnectServer();
    mode = "ok";
    QTest::qWait(50);
    QVERIFY(!client.connected());
    client.connectServer(base, "fixture", "secret", false);
    QTRY_VERIFY(client.connected());
    bool callback = false;
    mode = "hold";
    client.call(
        "search3", {},
        [&](const QVariantMap &, const QString &) { callback = true; },
        "catalog");
    QTest::qWait(50);
    client.cancel("catalog");
    QTest::qWait(50);
    QVERIFY(!callback);
    mode = "ok";
    client.call(
        "search3", {},
        [&](const QVariantMap &, const QString &e) { callback = e.isEmpty(); },
        "catalog");
    QTRY_VERIFY(callback);
  }
};
QTEST_GUILESS_MAIN(ProtocolTest)
#include "subsonic_protocol_test.moc"
