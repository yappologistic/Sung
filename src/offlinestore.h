#pragma once
// Songs that have already been fetched, kept so they do not have to be fetched
// again.
//
// Sung buffers a whole song to disk before playing it, so the bytes that would
// make it playable without a network are already written. They were written
// into a temporary directory and handed back at the end of the track, which
// meant every replay paid for the song a second time and a lost connection
// left a library of songs that could not be played. This keeps them instead,
// under a budget the listener sets, and gives the least recently played ones
// back to the filesystem when that budget is reached.
//
// The directory is the index. A song is filed under a hash of its key, its age
// is the file's modification time, and the size on disk is the size on disk,
// so there is no second record to fall out of step with the first. A file that
// is deleted from underneath the store simply stops being found, and one that
// arrives is picked up on the next sweep.
//
// The store fetches nothing and decides nothing about what is worth having.
// The player hands it a file that has just been buffered and asks it later
// whether it still holds one, which keeps every decision about playback in the
// player.
#include <QObject>
#include <QString>
#include <QVariantMap>

class OfflineStore : public QObject {
  Q_OBJECT
public:
  explicit OfflineStore(QObject *parent = nullptr);

  // What a song is filed under, or an empty string for one that is never kept.
  //
  // An imported file is already on disk and is left alone. Everything else
  // carries the quality it was fetched at, so lowering the setting cannot
  // quietly play back the larger file that was kept at the old one, and a
  // server track carries the server it came from, because two servers' track
  // ids say nothing about each other.
  static QString keyFor(const QVariantMap &track, const QString &quality);

  // Whether a song is held, asked without counting as having played it.
  bool has(const QString &key) const;
  // The kept file for this key, or an empty string. Finding one counts as
  // playing it, so it moves to the back of the queue for eviction.
  QString take(const QString &key);
  // Move a freshly buffered file into the store and return where it now lives.
  // Returns an empty string when it could not be kept, and the caller plays
  // the file it already has: failing to keep a song is not a reason to fail to
  // play it.
  QString keep(const QString &key, const QString &file);
  // Drop one song, for a file that turned out not to be playable. Without
  // this, a truncated or corrupt copy would be handed back on every attempt
  // and the song would never recover.
  void forget(const QString &key);
  // Give the whole store back to the filesystem.
  void clear();

  qint64 bytes() const;
  int count() const;
  // Zero turns the store off and empties it. Lowering it evicts at once, so
  // the setting is a promise about disk rather than about the future.
  qint64 budget() const { return m_budget; }
  void setBudget(qint64 bytes);
  QString root() const { return m_root; }

private:
  // Delete least recently played files until the store is inside its budget.
  void evict();
  QString m_root;
  qint64 m_budget = 0;
};
