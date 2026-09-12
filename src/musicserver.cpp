#include "musicserver.h"
MusicServer::MusicServer(QObject *parent)
    : QObject(parent),
      m_provider(m_settings.value("server/provider", "subsonic").toString()),
      m_sub(nullptr, m_provider != "jellyfin"),
      m_jelly(m_provider == "jellyfin") {
  if (m_provider != "jellyfin")
    m_provider = "subsonic";
  for (auto *s : {&m_sub, static_cast<Subsonic *>(&m_jelly)}) {
    connect(s, &Subsonic::changed, this, &MusicServer::changed);
    connect(s, &Subsonic::accountChanged, this, &MusicServer::accountChanged);
    connect(s, &Subsonic::message, this, &MusicServer::message);
  }
}
void MusicServer::selectProvider(const QString &provider) {
  if (provider == m_provider ||
      (provider != "subsonic" && provider != "jellyfin"))
    return;
  disconnectServer();
  m_provider = provider;
  m_settings.setValue("server/provider", provider);
  emit accountChanged();
  emit changed();
}
