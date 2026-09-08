#include "mpris.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
void RootAdaptor::Quit() { QCoreApplication::quit(); }
PlayerAdaptor::PlayerAdaptor(Backend *p) : QDBusAbstractAdaptor(p), b(p) {
  connect(p, &Backend::trackChanged, this, &PlayerAdaptor::changed);
  connect(p, &Backend::playbackChanged, this, &PlayerAdaptor::changed);
  connect(p, &Backend::settingsChanged, this, &PlayerAdaptor::changed);
  connect(p, &Backend::seeked, this, [this](qint64 position){emit Seeked(position*1000);});
}
QVariantMap PlayerAdaptor::metadata() const {
  const auto t = b->current();
  if (t.isEmpty())
    return {};
  return {{"mpris:trackid", QVariant::fromValue(QDBusObjectPath(
                                "/org/mpris/MediaPlayer2/track/" +
                                b->trackToken()))},
          {"mpris:length", b->duration() * 1000},
          {"mpris:artUrl", t.value("source")=="subsonic"?QVariant(b->serverArtwork()):t.value("art")},
          {"xesam:title", t.value("title")},
          {"xesam:artist", QStringList{t.value("artist").toString()}},
          {"xesam:album", t.value("album")},
          {"xesam:url", t.value("source")=="subsonic"?QString():t.value("localPath").toString().isEmpty()?"https://music.youtube.com/watch?v="+t.value("videoId").toString():QUrl::fromLocalFile(t.value("localPath").toString()).toString()}};
}
void PlayerAdaptor::SetPosition(const QDBusObjectPath &id, qlonglong pos) {
  if (canSeek() && pos>=0 && pos<=b->duration()*1000 && id.path() ==
      "/org/mpris/MediaPlayer2/track/" + b->trackToken()) {
    b->seek(pos / 1000);
  }
}
void PlayerAdaptor::changed() {
  auto msg = QDBusMessage::createSignal("/org/mpris/MediaPlayer2",
                                        "org.freedesktop.DBus.Properties",
                                        "PropertiesChanged");
  msg << QString("org.mpris.MediaPlayer2.Player")
      << QVariantMap{{"PlaybackStatus", status()}, {"Metadata", metadata()},
                     {"Rate",rate()}, {"Volume", volume()},         {"Shuffle", shuffle()},
                     {"LoopStatus", loop()},       {"CanSeek", canSeek()},
                     {"CanGoNext",canNext()},{"CanGoPrevious",canPrevious()}}
      << QStringList{};
  QDBusConnection::sessionBus().send(msg);
}
void registerMpris(Backend *b) {
  new RootAdaptor(b);
  new PlayerAdaptor(b);
  auto bus = QDBusConnection::sessionBus();
  if (bus.registerService("org.mpris.MediaPlayer2.sung"))
    bus.registerObject("/org/mpris/MediaPlayer2", b,
                       QDBusConnection::ExportAdaptors);
}
