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
  // The longest side a published frame may have. 800 serves every cover
  // surface; the Motion layout raises it to the large cover's own size
  // (Backend::motionQuality) so that cover arrives without a second scale
  // on every frame.
  Q_PROPERTY(int maximumSize READ maximumSize WRITE setMaximumSize NOTIFY maximumSizeChanged)
public:
  explicit MotionArtwork(QObject *parent=nullptr);
  ~MotionArtwork() override;
  QUrl source() const { return m_source; }
  void setSource(const QUrl &source);
  bool running() const { return m_running; }
  void setRunning(bool running);
  int maximumSize() const { return m_maximumSize; }
  void setMaximumSize(int size);
  const QImage &frame() const { return m_frame; }
signals:
  void sourceChanged();
  void runningChanged();
  void maximumSizeChanged();
  void frameChanged();
private:
  void clear();
  void publish(QImage frame);
  void publishVideo(const QVideoFrame &frame);
  QUrl m_source;
  QImage m_frame;
  bool m_running=false;
  int m_maximumSize=800;
  bool m_movieFailed=false;
  std::unique_ptr<QMovie> m_movie;
  std::unique_ptr<QVideoSink> m_sink;
  std::unique_ptr<QMediaPlayer> m_player;
};
