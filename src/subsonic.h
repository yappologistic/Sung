#pragma once
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QUrlQuery>
#include <functional>

// One configured account. Track identity includes server and user, never
// credentials.
class Subsonic : public QObject {
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
public:
  using Reply = std::function<void(const QVariantMap &, const QString &)>;
  using Params = QList<QPair<QString, QString>>;
  explicit Subsonic(QObject *parent = nullptr);
  ~Subsonic() override;
  bool connected() const { return m_connected; }
  bool connecting() const { return m_connecting; }
  QString address() const { return m_address; }
  QString username() const { return m_username; }
  QString identity() const { return m_identity; }
  QString error() const { return m_error; }
  QVariantList playlists() const { return m_playlists; }
  QVariantList folders() const { return m_folders; }
  QString folder() const {
    return m_settings.value("subsonic/folder").toString();
  }
  void setFolder(const QString &id);
  bool scrobbling() const {
    return m_settings.value("subsonic/scrobble", true).toBool();
  }
  void setScrobbling(bool value);
  int bitrate() const {
    return m_settings.value("subsonic/bitrate", 0).toInt();
  }
  void setBitrate(int value);
  bool keyringAvailable() const;
  Q_INVOKABLE void connectServer(const QString &address, const QString &user,
                                 const QString &password, bool remember = true);
  Q_INVOKABLE void disconnectServer();
  Q_INVOKABLE void reloadPlaylists();
  Q_INVOKABLE void createPlaylist(const QString &name);
  void call(const QString &method, const Params &params, Reply callback,
            const QString &channel = {});
  void cancel(const QString &channel);
  void browse(const QVariantMap &request, Reply callback, const QString &channel = "catalog");
  void cover(const QVariantMap &track, const QString &path, Reply callback);
  void lyrics(const QVariantMap &track, Reply callback);
  void download(const QVariantMap &track, const QString &path, Reply callback);
  void star(const QVariantMap &track, bool starred, Reply callback);
  void rate(const QVariantMap &track, int rating, Reply callback);
  void editPlaylist(const QString &id, const Params &params, Reply callback);
  void removePlaylist(const QString &id, Reply callback);
  bool owns(const QVariantMap &track) const;
  bool isStarred(const QString &id) const { return m_stars.contains(id); }
  QVariantMap item(const QVariantMap &raw, const QString &kind) const;
  QUrl artworkUrl(const QUrl &reference) const;
  void scrobble(const QVariantMap &track, bool submission, qint64 timestamp,
                Reply callback);
signals:
  void changed();
  void accountChanged();
  void message(const QString &text);

private:
  QUrl url(const QString &method, const Params &params) const;
  void secret(const QStringList &arguments, const QByteArray &input,
              std::function<void(bool, QByteArray)> callback);
  void discover();
  QVariantList items(const QVariant &rows, const QString &kind) const;
  void stopRequests();
  QNetworkAccessManager m_network;
  QSettings m_settings;
  QString m_address, m_username, m_password, m_identity, m_error;
  bool m_connected = false, m_connecting = false, m_lyricsExtension = false,
       m_formPost = false, m_restoring = false;
  quint64 m_generation = 0;
  QHash<QString, QPointer<QNetworkReply>> m_channels;
  QSet<QString> m_stars;
  QVariantList m_playlists, m_folders;
};
