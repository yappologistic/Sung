#include "localization.h"
#include <QCoreApplication>
#include <QEvent>
#include <QLocale>
#include <QLibraryInfo>

namespace {
bool supported(const QString &language) {
  return language == "system" || language == "en" || language == "ru";
}
}

Localization::Localization(QObject *parent) : QObject(parent) {
  m_language = m_settings.value("interfaceLanguage", "system").toString();
  if (!supported(m_language)) m_language = "system";
  applyLanguage();
  QCoreApplication::instance()->installEventFilter(this);
}

Localization::~Localization() {
  QCoreApplication::removeTranslator(&m_translator);
  QCoreApplication::removeTranslator(&m_qtQuick);
  QCoreApplication::removeTranslator(&m_qtBase);
}

void Localization::setLanguage(const QString &language) {
  if (!supported(language) || language == m_language) return;
  m_language = language;
  m_settings.setValue("interfaceLanguage", language);
  m_settings.sync();
  applyLanguage();
  emit languageChanged();
}

void Localization::applyLanguage() {
  QCoreApplication::removeTranslator(&m_translator);
  QCoreApplication::removeTranslator(&m_qtQuick);
  QCoreApplication::removeTranslator(&m_qtBase);
  const auto locale = m_language == "system" ? QLocale::system() : QLocale(m_language);
  const bool russian = locale.language() == QLocale::Russian;
  if (russian) {
    const auto qtPath = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    if (m_qtBase.load("qtbase_ru", qtPath)) QCoreApplication::installTranslator(&m_qtBase);
    if (m_qtQuick.load("qtdeclarative_ru", qtPath)) QCoreApplication::installTranslator(&m_qtQuick);
  }
  if (m_translator.load(russian ? ":/i18n/sung_ru.qm" : ":/i18n/sung_en.qm"))
    QCoreApplication::installTranslator(&m_translator);
  // English source text is Qt's fallback for missing translations. Do not
  // change QLocale's default: user metadata and stored keys are not UI labels.
}

bool Localization::eventFilter(QObject *object, QEvent *event) {
  if (event->type() == QEvent::LocaleChange && m_language == "system") {
    applyLanguage();
    emit languageChanged();
  }
  return QObject::eventFilter(object, event);
}
