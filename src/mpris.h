#pragma once
#include "backend.h"
#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>

class RootAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
  Q_PROPERTY(bool CanQuit READ yes CONSTANT)
  Q_PROPERTY(bool CanRaise READ yes CONSTANT)
  Q_PROPERTY(bool HasTrackList READ no CONSTANT)
  Q_PROPERTY(QString Identity READ identity CONSTANT)
  Q_PROPERTY(QString DesktopEntry READ desktop CONSTANT)
  Q_PROPERTY(QStringList SupportedUriSchemes READ schemes CONSTANT)
  Q_PROPERTY(QStringList SupportedMimeTypes READ types CONSTANT)
public:
  explicit RootAdaptor(Backend *b) : QDBusAbstractAdaptor(b), b(b) {}
  bool yes() const { return true; }
  bool no() const { return false; }
  QString identity() const { return "Sung"; }
  QString desktop() const { return "sung"; }
  QStringList schemes() const { return {"https"}; }
  QStringList types() const { return {}; }
public slots:
  void Quit();
  void Raise() { emit b->raiseRequested(); }

private:
  Backend *b;
};
class PlayerAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
  Q_PROPERTY(QString PlaybackStatus READ status)
  Q_PROPERTY(QString LoopStatus READ loop WRITE setLoop)
  Q_PROPERTY(double Rate READ rate WRITE setRate)
  Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
  Q_PROPERTY(QVariantMap Metadata READ metadata)
  Q_PROPERTY(double Volume READ volume WRITE setVolume)
  Q_PROPERTY(qlonglong Position READ position)
  Q_PROPERTY(double MinimumRate READ minimumRate CONSTANT)
  Q_PROPERTY(double MaximumRate READ maximumRate CONSTANT)
  Q_PROPERTY(bool CanGoNext READ canNext)
  Q_PROPERTY(bool CanGoPrevious READ canPrevious)
  Q_PROPERTY(bool CanPlay READ canControl)
  Q_PROPERTY(bool CanPause READ canControl)
  Q_PROPERTY(bool CanSeek READ canSeek)
  Q_PROPERTY(bool CanControl READ yes CONSTANT)
public:
  explicit PlayerAdaptor(Backend *);
  QString status() const {
    return b->playing()             ? "Playing"
           : b->stopped() || b->current().isEmpty() ? "Stopped"
                                    : "Paused";
  }
  QString loop() const {
    return b->repeat() == 2 ? "Track" : b->repeat() == 1 ? "Playlist" : "None";
  }
  void setLoop(const QString &v) {
    b->setRepeat(v == "Track" ? 2 : v == "Playlist" ? 1 : 0);
  }
  double rate() const { return b->playbackRate(); }
  double minimumRate() const {return 0.5;}
  double maximumRate() const {return 2.0;}
  void setRate(double value) {if(value==0)b->pause();else b->setPlaybackRate(value);}
  bool shuffle() const { return b->shuffle(); }
  void setShuffle(bool v) { b->setShuffle(v); }
  QVariantMap metadata() const;
  double volume() const { return b->volume(); }
  void setVolume(double v) { b->setVolume(v); }
  qlonglong position() const { return b->position() * 1000; }
  bool canControl() const { return b->queue()->count() > 0; }
  bool canNext() const { return canControl() && (b->currentIndex()+1<b->queue()->count() || b->repeat()==1 || b->shuffle() || (b->autoplay()&&!b->current().value("videoId").toString().isEmpty())); }
  bool canPrevious() const { return canControl() && (b->position()>3000 || b->currentIndex()>0 || b->repeat()==1); }
  bool canSeek() const { return b->media()->isSeekable(); }
  bool yes() const { return true; }
public slots:
  void Next() { if(!canNext())return;const bool paused=!b->playing()&&!b->resolving(), stopped=b->stopped();b->next();if(stopped)b->stop();else if(paused)b->pause(); }
  void Previous() { if(!canPrevious())return;const bool paused=!b->playing()&&!b->resolving(), stopped=b->stopped();b->previous();if(stopped)b->stop();else if(paused)b->pause(); }
  void Pause() { b->pause(); }
  void PlayPause() { b->toggle(); }
  void Stop() { b->stop(); }
  void Play() { b->play(); }
  void Seek(qlonglong delta) {
    if(!canSeek())return;
    const auto target=b->position()+delta/1000;
    if(target>b->duration())Next();else b->seek(target);
  }
  void SetPosition(const QDBusObjectPath &id, qlonglong pos);
  void OpenUri(const QString &url) {
    b->playLink(url);
    emit b->raiseRequested();
  }
signals:
  void Seeked(qlonglong position);

private:
  void changed();
  Backend *b;
};
void registerMpris(Backend *);
