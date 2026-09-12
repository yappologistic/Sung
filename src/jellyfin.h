#pragma once
#include "subsonic.h"
#include <QJsonObject>
// Jellyfin credentials stay in request headers; persisted rows contain only
// opaque identities.
class Jellyfin : public Subsonic {
public:
  explicit Jellyfin(bool restore = true);
  ~Jellyfin() override;
  bool connected() const { return m_connected; }
  bool connecting() const { return m_connecting; }
  QString address() const { return m_address; }
  QString username() const { return m_username; }
  QString identity() const { return m_identity; }
  QString error() const { return m_error; }
  QVariantList playlists() const { return m_playlists; }
  QVariantList folders() const { return m_folders; }
  QString folder() const {
    return m_settings.value("jellyfin/folder").toString();
  }
  bool scrobbling() const {
    return m_settings.value("jellyfin/scrobble", true).toBool();
  }
  int bitrate() const {
    return m_settings.value("jellyfin/bitrate", 0).toInt();
  }
  void setFolder(const QString &id);
  void setScrobbling(bool value);
  void setBitrate(int value);
  void connectServer(const QString &, const QString &, const QString &,
                     bool remember = true);
  void disconnectServer();
  void reloadPlaylists();
  void createPlaylist(const QString &name);
  void call(const QString &, const Params &, Reply,
            const QString &channel = {});
  void cancel(const QString &channel);
  void browse(const QVariantMap &, Reply, const QString &channel = "catalog");
  void cover(const QVariantMap &, const QString &, Reply);
  void lyrics(const QVariantMap &, Reply);
  void download(const QVariantMap &, const QString &, Reply);
  void star(const QVariantMap &, bool, Reply);
  void rate(const QVariantMap &, int, Reply cb) {
    cb({}, "Jellyfin does not support five-star song ratings.");
  }
  void editPlaylist(const QString &, const Params &, Reply);
  void removePlaylist(const QString &, Reply);
  bool owns(const QVariantMap &) const;
  bool isStarred(const QString &id) const { return m_stars.contains(id); }
  QVariantMap item(const QVariantMap &, const QString &) const;
  QUrl artworkUrl(const QUrl &) const;
  QNetworkRequest artworkRequest(const QUrl &) const;
  void scrobble(const QVariantMap &, bool, qint64, Reply);
  void reportPlayback(const QVariantMap &, qint64, bool, bool);

private:
  QUrl url(const QString &, const Params & = {}) const;
  QNetworkRequest request(const QUrl &) const;
  void json(const QString &, const Params &, const QByteArray &,
            const QJsonObject &, Reply, const QString &channel = {});
  void fetchRows(const QString &, Params, bool, Reply, const QString &);
  void file(const QUrl &, const QString &, qint64, Reply, const QString &);
  void stopRequests();
  void resolvePlaylistAccess(QVariantList, Reply, const QString &);
  void sendPlaybackReport();
  QList<QPair<QString, QJsonObject>> m_reports;
  void discover();
  void acceptLogin(const QVariantMap &, bool);
  void playlistAccess(const QString &, Reply, const QString &channel = {});
  QNetworkAccessManager m_network;
  QSettings m_settings;
  QString m_address, m_username, m_identity, m_error, m_token, m_user, m_device;
  bool m_connected = false, m_connecting = false;
  quint64 m_generation = 0;
  QHash<QString, QPointer<QNetworkReply>> m_channels;
  QVariantList m_playlists, m_folders;
  QSet<QString> m_stars;
  QString m_playing;
};
