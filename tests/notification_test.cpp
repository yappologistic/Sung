#include "backend.h"
#include <QtTest>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>

class FakeNotifications : public QObject, protected QDBusContext {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface","org.freedesktop.Notifications")
public:
  QDBusConnection bus{QString("notifications-fixture")};
  QList<QVariantMap> calls;
  QList<uint> closed;
  bool hold=false;
  uint nextId=10;
  QDBusMessage pending;
  uint pendingId=0;
  void release() {bus.send(pending.createReply(QVariantList{pendingId}));hold=false;}
public slots:
  uint Notify(const QString &app,uint replaces,const QString &icon,const QString &title,const QString &body,const QStringList &actions,const QVariantMap &hints,int timeout) {
    calls.append({{"app",app},{"replaces",replaces},{"icon",icon},{"title",title},{"body",body},{"actions",actions},{"hints",hints},{"timeout",timeout}});
    const uint id=replaces?replaces:nextId++;
    if(hold){setDelayedReply(true);pending=message();pendingId=id;}
    return id;
  }
  void CloseNotification(uint id) {closed.append(id);emit NotificationClosed(id,3);}
signals:
  void NotificationClosed(uint id,uint reason);
};

class NotificationTest : public QObject {
  Q_OBJECT
  FakeNotifications server;
  QTemporaryDir storage;
  QUrl audio;
private slots:
  void initTestCase() {
    QVERIFY(storage.isValid());qputenv("XDG_CONFIG_HOME",storage.path().toUtf8());qputenv("XDG_DATA_HOME",storage.path().toUtf8());
    qputenv("SUNG_HELPER",qgetenv("SUNG_FIXTURE_HELPER"));qputenv("SUNG_PYTHON","python3");
    QCoreApplication::setOrganizationName("SungTests");QCoreApplication::setApplicationName("notifications");
    // A separate connection exercises real asynchronous bus delivery, including delayed replies.
    server.bus=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"notifications-fixture");auto bus=server.bus;QVERIFY(bus.registerService("org.freedesktop.Notifications"));
    QVERIFY(bus.registerObject("/org/freedesktop/Notifications",&server,QDBusConnection::ExportAllSlots|QDBusConnection::ExportAllSignals));
    QFile file(storage.filePath("silence.wav"));QVERIFY(file.open(QIODevice::WriteOnly));
    const int bytes=48000*2*8;QDataStream stream(&file);stream.setByteOrder(QDataStream::LittleEndian);
    file.write("RIFF",4);stream<<quint32(36+bytes);file.write("WAVEfmt ",8);stream<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(48000)<<quint32(96000)<<quint16(2)<<quint16(16);file.write("data",4);stream<<quint32(bytes);file.write(QByteArray(bytes,0));file.close();audio=QUrl::fromLocalFile(file.fileName());
  }
  void hiddenPlaybackKeepsAudioAndResumesUi() {
    Backend b;b.setVolume(0);b.localTestSource(audio);QTRY_VERIFY(b.playing());
    QSignalSpy ticks(&b,&Backend::positionChanged);
    QTRY_VERIFY(!ticks.isEmpty());b.setUiActive(false);ticks.clear();
    const auto position=b.position();QTest::qWait(800);
    QVERIFY(b.playing());QVERIFY(b.position()>position+400);QCOMPARE(ticks.count(),0);
    b.setUiActive(true);QCOMPARE(ticks.count(),1);QTRY_VERIFY(ticks.count()>1);
    b.pause();b.setUiActive(false);b.setUiActive(true);ticks.clear();QTest::qWait(350);QCOMPARE(ticks.count(),0);
  }
  void deliveryReplacementAndCancellation() {
    PlaybackNotifier n;n.show("First","A & <B>");QTRY_COMPARE(server.calls.size(),1);QTest::qWait(50);
    const auto call=server.calls.first();QCOMPARE(call.value("app").toString(),"Sung");QCOMPARE(call.value("body").toString(),"A &amp; &lt;B&gt;");
    QVERIFY(call.value("hints").toMap().value("transient").toBool());QVERIFY(call.value("hints").toMap().value("suppress-sound").toBool());
    n.show("Second","Artist");QTRY_COMPARE(server.calls.size(),2);QVERIFY(server.calls.last().value("replaces").toUInt()>0);QTest::qWait(50);
    n.clear();QTRY_VERIFY(!server.closed.isEmpty());
    server.hold=true;n.show("Pending","Artist");QTRY_COMPARE(server.calls.size(),3);n.clear();server.release();QTRY_VERIFY(server.closed.contains(server.pendingId));
    n.show("After cancellation","Artist");QTRY_COMPARE(server.calls.size(),4);QCOMPARE(server.calls.last().value("replaces").toUInt(),0u);QTest::qWait(50);n.clear();
  }
  void realPlaybackHistoryAndDeduplication() {
    QTest::qWait(100);const int before=server.calls.size();Backend b;b.clearQueue();b.clearHistory();b.setVolume(0);b.setTrackNotifications(true);
    auto track=[](const QString &id){return QVariantMap{{"id",id},{"videoId",id},{"title",id},{"artist","Fixture artist"},{"kind","song"},{"seconds",8}};};
    b.playItem(track("public00001"));b.pause();QTRY_VERIFY(!b.resolving());b.localTestSource(audio);QTRY_VERIFY(b.playing());QTRY_COMPARE(server.calls.size(),before+1);
    b.pause();b.play();QTest::qWait(150);QCOMPARE(server.calls.size(),before+1);
    b.library("history");QCOMPARE(b.results()->count(),1);b.clearHistory();b.setHistoryPaused(true);
    b.playItem(track("private0001"));b.pause();QTRY_VERIFY(!b.resolving());b.localTestSource(audio);QTRY_VERIFY(b.playing());QTest::qWait(150);
    b.library("history");QCOMPARE(b.results()->count(),0);QCOMPARE(server.calls.size(),before+1);
    b.pause();b.setHistoryPaused(false);b.play();QTest::qWait(100);b.library("history");QCOMPARE(b.results()->count(),0);QCOMPARE(server.calls.size(),before+1);
    b.playItem(track("public00002"));b.pause();QTRY_VERIFY(!b.resolving());b.localTestSource(audio);QTRY_VERIFY(b.playing());QTRY_COMPARE(server.calls.size(),before+2);b.library("history");QCOMPARE(b.results()->count(),1);
    b.setHistoryPaused(true);b.save();{Backend restored;QVERIFY(!restored.historyPaused());QVERIFY(restored.trackNotifications());}
    b.setTrackNotifications(false);b.stop();b.clearQueue();
  }
};
QTEST_MAIN(NotificationTest)
#include "notification_test.moc"
