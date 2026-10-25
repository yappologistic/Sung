#pragma once

#include <QObject>
#include <QSettings>
#include <QTranslator>

class Localization : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)

public:
  explicit Localization(QObject *parent = nullptr);
  ~Localization() override;
  QString language() const { return m_language; }
  void setLanguage(const QString &language);

signals:
  void languageChanged();

protected:
  bool eventFilter(QObject *object, QEvent *event) override;

private:
  void applyLanguage();
  QSettings m_settings;
  QTranslator m_qtBase;
  QTranslator m_qtQuick;
  QTranslator m_translator;
  QString m_language;
};
