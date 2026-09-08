#pragma once
#include <QImage>
#include <QObject>
#include <QUrl>
#include <memory>

class QMovie;
class QMediaPlayer;
class QVideoSink;
class QVideoFrame;

// One decoder for the current cover, shared by all now-playing surfaces.
class MotionArtwork : public QObject {
  Q_OBJECT
  friend class ArtworkTest;
  Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
  Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged)
public:
  explicit MotionArtwork(QObject *parent=nullptr);
  ~MotionArtwork() override;
  QUrl source() const { return m_source; }
  void setSource(const QUrl &source);
  bool running() const { return m_running; }
  void setRunning(bool running);
  const QImage &frame() const { return m_frame; }
signals:
  void sourceChanged();
  void runningChanged();
  void frameChanged();
private:
  void clear();
  void publish(QImage frame);
  void publishVideo(const QVideoFrame &frame);
  QUrl m_source;
  QImage m_frame;
  bool m_running=false;
  std::unique_ptr<QMovie> m_movie;
  std::unique_ptr<QVideoSink> m_sink;
  std::unique_ptr<QMediaPlayer> m_player;
};
