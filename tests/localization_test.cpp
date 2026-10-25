#include "localization.h"
#include <QGuiApplication>
#include <QLocale>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class LocalizationTest : public QObject {
  Q_OBJECT
  QTemporaryDir m_profile;

private slots:
  void initTestCase() {
    QVERIFY(m_profile.isValid());
    QCoreApplication::setOrganizationName("SungLocalizationTests");
    QCoreApplication::setApplicationName("language");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_profile.path());
  }
  void init() { QSettings().clear(); }

  void systemAndFallback() {
    Localization language;
    QCOMPARE(language.language(), "system");
    QCOMPARE(QCoreApplication::translate("Navigation", "Home"),
             QLocale::system().language() == QLocale::Russian ? QString("Главная") : QString("Home"));
    language.setLanguage("ru");
    QCOMPARE(QCoreApplication::translate("Navigation", "Home"), "Главная");
    QCOMPARE(QCoreApplication::translate("NotTranslated", "Untranslated message"), "Untranslated message");
    language.setLanguage("en");
    QCOMPARE(QCoreApplication::translate("Navigation", "Home"), "Home");
    language.setLanguage("system");
    QCOMPARE(QCoreApplication::translate("Navigation", "Home"),
             QLocale::system().language() == QLocale::Russian ? QString("Главная") : QString("Home"));
  }

  void persistenceAndInvalidInput() {
    {
      Localization language;
      QSignalSpy changes(&language, &Localization::languageChanged);
      language.setLanguage("ru");
      QCOMPARE(changes.count(), 1);
      language.setLanguage("ru");
      language.setLanguage("../bad");
      QCOMPARE(changes.count(), 1);
      QCOMPARE(QSettings().value("interfaceLanguage").toString(), "ru");
    }
    {
      Localization restored;
      QCOMPARE(restored.language(), "ru");
      QCOMPARE(QCoreApplication::translate("Navigation", "Home"), "Главная");
    }
    QSettings().setValue("interfaceLanguage", "unknown");
    Localization invalid;
    QCOMPARE(invalid.language(), "system");
  }

  void retranslateWithoutRecreatingObjects() {
    Localization language;
    language.setLanguage("en");
    QQmlEngine engine;
    connect(&language, &Localization::languageChanged, &engine, &QQmlEngine::retranslate);
    QQmlComponent component(&engine);
    component.setData("import QtQml\nQtObject { property string label: qsTranslate(\"Navigation\", \"Home\"); property int retainedState: 42 }", QUrl());
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("label").toString(), "Home");
    language.setLanguage("ru");
    QCOMPARE(object->property("label").toString(), "Главная");
    QCOMPARE(object->property("retainedState").toInt(), 42);
    language.setLanguage("en");
    QCOMPARE(object->property("label").toString(), "Home");
  }

  void plurals() {
    Localization language;
    language.setLanguage("ru");
    QCOMPARE(QCoreApplication::translate("Main", "%n track(s)", nullptr, 1), "1 трек");
    QCOMPARE(QCoreApplication::translate("Main", "%n track(s)", nullptr, 2), "2 трека");
    QCOMPARE(QCoreApplication::translate("Main", "%n track(s)", nullptr, 5), "5 треков");
    QCOMPARE(QCoreApplication::translate("CollectionCounts", "%n track(s)", nullptr, 486), "486 тр.");
    QCOMPARE(QCoreApplication::translate("Main", "%n track(s)", nullptr, 21), "21 трек");
    language.setLanguage("en");
    QCOMPARE(QCoreApplication::translate("Main", "%n track(s)", nullptr, 1), "1 track");
    QCOMPARE(QCoreApplication::translate("CollectionCounts", "%n track(s)", nullptr, 1), "1 track");
    QCOMPARE(QCoreApplication::translate("CollectionCounts", "%n track(s)", nullptr, 486), "486 tracks");
    QCOMPARE(QCoreApplication::translate("Main", "%n track(s)", nullptr, 2), "2 tracks");
  }
};

QTEST_MAIN(LocalizationTest)
#include "localization_test.moc"
