#pragma once
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

class DesktopTheme : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
  Q_PROPERTY(bool available READ available NOTIFY changed)
  Q_PROPERTY(bool dark READ dark NOTIFY changed)
public:
  explicit DesktopTheme(QObject *parent = nullptr);
  QVariantMap colors() const { return m_colors; }
  bool available() const { return !m_colors.isEmpty(); }
  bool dark() const { return m_dark; }
signals:
  void changed();
private:
  void reload();
  QString m_path;
  QVariantMap m_colors;
  bool m_dark = true;
  QFileSystemWatcher m_watcher;
  QTimer m_debounce;
};
