#include "librarydata.h"
#include <QJsonDocument>
#include <QtTest>

class LibraryDataTest : public QObject {
  Q_OBJECT
private slots:
  void retainsEveryValue() {
    const auto object = QJsonDocument::fromJson(R"({"localTracks":[
      {"id":"same","title":"Song","seconds":241,"gain":-3.25,"available":true},
      {"id":"same","title":"Song","seconds":241,"gain":-3.25,"available":true},
      {"id":"same","title":"Old title","_queueOrigin":"manual"}],
      "unknown":{"null":null,"false":false,"empty":[],"map":{},"unicode":"日本語 é",
                 "integer":9007199254740993,"negative":-99,"fraction":0.125},
      "sessions":[{"id":"session","queue":[{"id":"same","title":"Song"}]}]})").object();
    const auto expected = object.toVariantMap();
    const auto actual = librarydata::read(object);
    QCOMPARE(actual, expected);
    QCOMPARE(QJsonDocument::fromVariant(actual).toJson(), QJsonDocument::fromVariant(expected).toJson());
    const auto unknown = actual.value("unknown").toMap();
    for (auto it = unknown.cbegin(); it != unknown.cend(); ++it)
      QCOMPARE(it.value().metaType(), expected.value("unknown").toMap().value(it.key()).metaType());
  }
  void sharesCopiesButEditsDetach() {
    const QJsonObject song{{"id","track"},{"title","One song"},{"artist","One artist"}};
    auto library = librarydata::read(QJsonObject{{"queue",QJsonArray{song,song}},
                                               {"favorites",QJsonArray{song}}});
    auto queue = library.value("queue").toList();
    auto first = queue[0].toMap();
    const auto second = queue[1].toMap();
    QVERIFY(first.isSharedWith(second));
    QVERIFY(second.isSharedWith(library.value("favorites").toList()[0].toMap()));
    first["title"] = "Edited";
    queue[0] = first;
    library["queue"] = queue;
    QCOMPARE(library.value("favorites").toList()[0].toMap().value("title").toString(), "One song");
    QCOMPARE(queue[1].toMap().value("title").toString(), "One song");
    QCOMPARE(queue[0].toMap().value("title").toString(), "Edited");
  }
  void repeatedIdsKeepDistinctMetadata() {
    QJsonArray tracks;
    for (int i=0;i<40;++i) tracks.append(QJsonObject{{"id","same"},{"title",QString::number(i)}});
    const QJsonObject source{{"tracks",tracks},{"another",tracks}};
    QCOMPARE(librarydata::read(source),source.toVariantMap());
  }
};
QTEST_GUILESS_MAIN(LibraryDataTest)
#include "librarydata_test.moc"
