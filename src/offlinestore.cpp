#include "offlinestore.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <algorithm>

namespace {
// A name that is a function of the key alone, so the same song is found again
// on the next run without anything having been written down. 128 bits of
// SHA-256 is far more than a few thousand songs need to stay apart.
QString fileNameFor(const QString &key, const QString &suffix) {
  const auto digest = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex().left(32);
  return QString::fromLatin1(digest) + (suffix.isEmpty() ? QString() : '.' + suffix);
}
} // namespace

OfflineStore::OfflineStore(QObject *parent) : QObject(parent) {
  m_root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/songs";
}

QString OfflineStore::keyFor(const QVariantMap &track, const QString &quality) {
  if (!track.value("localPath").toString().isEmpty())
    return {};
  const auto source = track.value("source").toString();
  if (source == "jellyfin" || source == "subsonic") {
    const auto id = track.value("id").toString();
    const auto server = track.value("server").toString();
    if (id.isEmpty() || server.isEmpty())
      return {};
    return source + '|' + server + '|' + id + '|' + quality;
  }
  const auto video = track.value("videoId").toString();
  return video.isEmpty() ? QString() : "yt|" + video + '|' + quality;
}

bool OfflineStore::has(const QString &key) const {
  if (key.isEmpty() || m_budget <= 0)
    return false;
  const auto matches = QDir(m_root).entryInfoList({fileNameFor(key, {}) + ".*"}, QDir::Files);
  return !matches.isEmpty() && matches.first().size() > 0;
}

QString OfflineStore::take(const QString &key) {
  if (!has(key))
    return {};
  const auto matches = QDir(m_root).entryInfoList({fileNameFor(key, {}) + ".*"}, QDir::Files);
  const auto file = matches.first();
  if (file.size() <= 0) {
    QFile::remove(file.absoluteFilePath());
    return {};
  }
  // The modification time is the only record of age the store keeps, so
  // playing a song has to touch it or the ones played most would be the ones
  // evicted first.
  QFile touch(file.absoluteFilePath());
  if (touch.open(QIODevice::ReadOnly))
    touch.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
  return file.absoluteFilePath();
}

QString OfflineStore::keep(const QString &key, const QString &file) {
  if (key.isEmpty() || m_budget <= 0 || file.isEmpty())
    return {};
  const QFileInfo source(file);
  if (!source.isFile() || source.size() <= 0)
    return {};
  // One song larger than the whole budget would evict everything else and then
  // itself, so it is played from where it already is and not kept.
  if (source.size() > m_budget)
    return {};
  if (!QDir().mkpath(m_root))
    return {};
  const auto destination = QDir(m_root).absoluteFilePath(fileNameFor(key, source.suffix()));
  if (source.absoluteFilePath() == destination)
    return destination;
  QFile::remove(destination);
  // A rename, because the buffer and the store are both under the cache
  // location and so on one filesystem: the song is kept without the copy
  // being paid for on the playback path. A rename across filesystems fails
  // rather than copying, and then the song is simply not kept.
  if (!QFile::rename(source.absoluteFilePath(), destination))
    return {};
  evict();
  return QFile::exists(destination) ? destination : QString();
}

void OfflineStore::forget(const QString &key) {
  if (key.isEmpty())
    return;
  for (const auto &file : QDir(m_root).entryInfoList({fileNameFor(key, {}) + ".*"}, QDir::Files))
    QFile::remove(file.absoluteFilePath());
}

void OfflineStore::clear() {
  QDir(m_root).removeRecursively();
}

qint64 OfflineStore::bytes() const {
  qint64 total = 0;
  for (const auto &file : QDir(m_root).entryInfoList(QDir::Files))
    total += file.size();
  return total;
}

int OfflineStore::count() const {
  return int(QDir(m_root).entryInfoList(QDir::Files).size());
}

void OfflineStore::setBudget(qint64 bytes) {
  m_budget = qMax(qint64(0), bytes);
  if (m_budget <= 0) {
    clear();
    return;
  }
  evict();
}

void OfflineStore::evict() {
  auto files = QDir(m_root).entryInfoList(QDir::Files);
  qint64 total = 0;
  for (const auto &file : files)
    total += file.size();
  if (total <= m_budget)
    return;
  // Oldest first, which after take() has touched everything it played is the
  // least recently played first.
  std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
    return a.lastModified() < b.lastModified();
  });
  for (const auto &file : files) {
    if (total <= m_budget)
      return;
    const auto size = file.size();
    if (QFile::remove(file.absoluteFilePath()))
      total -= size;
  }
}
