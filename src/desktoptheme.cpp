#include "desktoptheme.h"
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

DesktopTheme::DesktopTheme(QObject *parent) : QObject(parent) {
  m_path = qEnvironmentVariable("SUNG_NOCTALIA_COLORS");
  if (m_path.isEmpty())
    m_path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
             + "/color-schemes/noctalia.colors";
  m_debounce.setSingleShot(true);
  m_debounce.setInterval(100);
  connect(&m_debounce, &QTimer::timeout, this, &DesktopTheme::reload);
  connect(&m_watcher, &QFileSystemWatcher::fileChanged, this,
          [this] { m_debounce.start(); });
  connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
          [this] { m_debounce.start(); });
  reload();
}
void DesktopTheme::reload() {
  // Watch the directory as well: Noctalia can replace the file atomically.
  QString dir = QFileInfo(m_path).absolutePath();
  while (!QFileInfo::exists(dir) && dir != "/") dir = QFileInfo(dir).absolutePath();
  if (!m_watcher.directories().contains(dir)) m_watcher.addPath(dir);
  if (!QFileInfo::exists(m_path)) return;
  if (!m_watcher.files().contains(m_path)) m_watcher.addPath(m_path);
  QSettings source(m_path, QSettings::IniFormat);
  const QMap<QString, QString> roles {
    {"background", "Colors:View/BackgroundNormal"},
    {"surface", "Colors:Complementary/BackgroundNormal"},
    {"container", "Colors:Window/BackgroundNormal"},
    {"high", "Colors:Button/BackgroundNormal"},
    {"text", "Colors:View/ForegroundNormal"},
    {"muted", "Colors:View/ForegroundInactive"},
    {"primary", "Colors:Button/ForegroundActive"},
    {"primaryText", "Colors:View/DecorationHover"},
    {"primaryContainer", "Colors:Window/BackgroundAlternate"},
    {"containerText", "Colors:View/DecorationFocus"},
    {"secondary", "Colors:View/ForegroundLink"}
  };
  QVariantMap next;
  for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
    auto rgb = source.value(it.value()).toStringList();
    if (rgb.size() != 3) return; // Ignore partial or malformed writes.
    int channels[3];
    for (int i=0;i<3;++i) {
      bool ok=false; channels[i]=rgb[i].toInt(&ok);
      if (!ok || channels[i]<0 || channels[i]>255) return;
    }
    next[it.key()] = QColor(channels[0],channels[1],channels[2]);
  }
  auto bg=next["background"].value<QColor>();
  auto fg=next["text"].value<QColor>();
  // The generated KDE template omits outline; derive a low-emphasis border.
  next["outline"] = QColor::fromRgbF(bg.redF()*.7+fg.redF()*.3,
      bg.greenF()*.7+fg.greenF()*.3,bg.blueF()*.7+fg.blueF()*.3);
  if (next == m_colors) return;
  m_dark = bg.lightnessF() < .5;
  m_colors = next;
  emit changed();
}
