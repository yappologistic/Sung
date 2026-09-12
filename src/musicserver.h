#pragma once
#include "jellyfin.h"
#include "subsonic.h"
inline bool isServerSource(const QVariant &source) {
  return source == "subsonic" || source == "jellyfin";
}
class MusicServer : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool connected READ connected NOTIFY changed)
  Q_PROPERTY(bool connecting READ connecting NOTIFY changed)
  Q_PROPERTY(QString address READ address NOTIFY changed)
  Q_PROPERTY(QString username READ username NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QString identity READ identity NOTIFY changed)
  Q_PROPERTY(QVariantList playlists READ playlists NOTIFY changed)
  Q_PROPERTY(QVariantList folders READ folders NOTIFY changed)
  Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY changed)
  Q_PROPERTY(bool scrobbling READ scrobbling WRITE setScrobbling NOTIFY changed)
  Q_PROPERTY(int bitrate READ bitrate WRITE setBitrate NOTIFY changed)
  Q_PROPERTY(bool keyringAvailable READ keyringAvailable CONSTANT)
  Q_PROPERTY(QString provider READ provider NOTIFY changed)
  Q_PROPERTY(bool supportsRating READ supportsRating NOTIFY changed)
  Q_PROPERTY(bool supportsQueue READ supportsQueue NOTIFY changed)
public:
  using Reply = Subsonic::Reply;
  using Params = Subsonic::Params;
  explicit MusicServer(QObject *parent = nullptr);
  QString provider() const { return m_provider; }
  bool supportsRating() const { return m_provider == "subsonic"; }
  bool supportsQueue() const { return m_provider == "subsonic"; }
  Q_INVOKABLE void selectProvider(const QString &provider);
  bool connected() const {
    return m_provider == "jellyfin" ? m_jelly.connected() : m_sub.connected();
  }
  bool connecting() const {
    return m_provider == "jellyfin" ? m_jelly.connecting() : m_sub.connecting();
  }
  QString address() const {
    return m_provider == "jellyfin" ? m_jelly.address() : m_sub.address();
  }
  QString username() const {
    return m_provider == "jellyfin" ? m_jelly.username() : m_sub.username();
  }
  QString identity() const {
    return m_provider == "jellyfin" ? m_jelly.identity() : m_sub.identity();
  }
  QString error() const {
    return m_provider == "jellyfin" ? m_jelly.error() : m_sub.error();
  }
  QVariantList playlists() const {
    return m_provider == "jellyfin" ? m_jelly.playlists() : m_sub.playlists();
  }
  QVariantList folders() const {
    return m_provider == "jellyfin" ? m_jelly.folders() : m_sub.folders();
  }
  QString folder() const {
    return m_provider == "jellyfin" ? m_jelly.folder() : m_sub.folder();
  }
  bool scrobbling() const {
    return m_provider == "jellyfin" ? m_jelly.scrobbling() : m_sub.scrobbling();
  }
  int bitrate() const {
    return m_provider == "jellyfin" ? m_jelly.bitrate() : m_sub.bitrate();
  }
  bool keyringAvailable() const {
    return m_provider == "jellyfin" ? m_jelly.keyringAvailable()
                                    : m_sub.keyringAvailable();
  }
  void setFolder(const QString &id) {
    if (m_provider == "jellyfin")
      m_jelly.setFolder(id);
    else
      m_sub.setFolder(id);
  }
  void setScrobbling(bool value) {
    if (m_provider == "jellyfin")
      m_jelly.setScrobbling(value);
    else
      m_sub.setScrobbling(value);
  }
  void setBitrate(int value) {
    if (m_provider == "jellyfin")
      m_jelly.setBitrate(value);
    else
      m_sub.setBitrate(value);
  }
  Q_INVOKABLE void connectServer(const QString &address, const QString &user,
                                 const QString &password,
                                 bool remember = true) {
    if (m_provider == "jellyfin")
      m_jelly.connectServer(address, user, password, remember);
    else
      m_sub.connectServer(address, user, password, remember);
  }
  Q_INVOKABLE void disconnectServer() {
    if (m_provider == "jellyfin")
      m_jelly.disconnectServer();
    else
      m_sub.disconnectServer();
  }
  Q_INVOKABLE void reloadPlaylists() {
    if (m_provider == "jellyfin")
      m_jelly.reloadPlaylists();
    else
      m_sub.reloadPlaylists();
  }
  Q_INVOKABLE void createPlaylist(const QString &name) {
    if (m_provider == "jellyfin")
      m_jelly.createPlaylist(name);
    else
      m_sub.createPlaylist(name);
  }
  void call(const QString &method, const Params &params, Reply callback,
            const QString &channel = {}) {
    if (m_provider == "jellyfin")
      m_jelly.call(method, params, callback, channel);
    else
      m_sub.call(method, params, callback, channel);
  }
  void cancel(const QString &channel) {
    if (m_provider == "jellyfin")
      m_jelly.cancel(channel);
    else
      m_sub.cancel(channel);
  }
  void browse(const QVariantMap &request, Reply callback,
              const QString &channel = "catalog") {
    if (m_provider == "jellyfin")
      m_jelly.browse(request, callback, channel);
    else
      m_sub.browse(request, callback, channel);
  }
  void cover(const QVariantMap &track, const QString &path, Reply callback) {
    if (m_provider == "jellyfin")
      m_jelly.cover(track, path, callback);
    else
      m_sub.cover(track, path, callback);
  }
  void lyrics(const QVariantMap &track, Reply callback) {
    if (m_provider == "jellyfin")
      m_jelly.lyrics(track, callback);
    else
      m_sub.lyrics(track, callback);
  }
  void download(const QVariantMap &track, const QString &path, Reply callback) {
    if (m_provider == "jellyfin")
      m_jelly.download(track, path, callback);
    else
      m_sub.download(track, path, callback);
  }
  void star(const QVariantMap &track, bool value, Reply callback) {
    if (m_provider == "jellyfin")
      m_jelly.star(track, value, callback);
    else
      m_sub.star(track, value, callback);
  }
  void rate(const QVariantMap &track, int value, Reply callback) {
    if (m_provider == "jellyfin")
      m_jelly.rate(track, value, callback);
    else
      m_sub.rate(track, value, callback);
  }
  void editPlaylist(const QString &id, const Params &params, Reply callback) {
    if (m_provider == "jellyfin")
      m_jelly.editPlaylist(id, params, callback);
    else
      m_sub.editPlaylist(id, params, callback);
  }
  void removePlaylist(const QString &id, Reply callback) {
    if (m_provider == "jellyfin")
      m_jelly.removePlaylist(id, callback);
    else
      m_sub.removePlaylist(id, callback);
  }
  void scrobble(const QVariantMap &track, bool submission, qint64 timestamp,
                Reply callback) {
    if (m_provider == "jellyfin")
      m_jelly.scrobble(track, submission, timestamp, callback);
    else
      m_sub.scrobble(track, submission, timestamp, callback);
  }
  bool owns(const QVariantMap &track) const {
    return m_provider == "jellyfin" ? m_jelly.owns(track) : m_sub.owns(track);
  }
  bool isStarred(const QString &id) const {
    return m_provider == "jellyfin" ? m_jelly.isStarred(id)
                                    : m_sub.isStarred(id);
  }
  QVariantMap item(const QVariantMap &raw, const QString &kind) const {
    return m_provider == "jellyfin" ? m_jelly.item(raw, kind)
                                    : m_sub.item(raw, kind);
  }
  QUrl artworkUrl(const QUrl &reference) const {
    return m_provider == "jellyfin" ? m_jelly.artworkUrl(reference)
                                    : m_sub.artworkUrl(reference);
  }
  QNetworkRequest artworkRequest(const QUrl &reference) const {
    return m_provider == "jellyfin"
               ? m_jelly.artworkRequest(reference)
               : QNetworkRequest(m_sub.artworkUrl(reference));
  }
  void reportPlayback(const QVariantMap &track, qint64 position, bool paused,
                      bool stopped) {
    if (m_provider == "jellyfin")
      m_jelly.reportPlayback(track, position, paused, stopped);
  }
signals:
  void changed();
  void accountChanged();
  void message(const QString &text);

private:
  QSettings m_settings;
  QString m_provider;
  Subsonic m_sub;
  Jellyfin m_jelly;
};
