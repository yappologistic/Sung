#include "motionartwork.h"
#include <QFileInfo>
#include <QImageReader>
#include <QMediaPlayer>
#include <QMovie>
#include <QVideoFrame>
#include <QVideoSink>
#include <QTransform>

MotionArtwork::MotionArtwork(QObject *parent):QObject(parent) {}
MotionArtwork::~MotionArtwork() { clear(); }
void MotionArtwork::clear() {
  m_movie.reset();m_player.reset();m_sink.reset();m_frame={};
}
void MotionArtwork::publish(QImage frame) {
  if(frame.isNull())return;
  if(frame.width()>800 || frame.height()>800)frame=frame.scaled(800,800,Qt::KeepAspectRatio,Qt::SmoothTransformation);
  m_frame=std::move(frame);emit frameChanged();
}
void MotionArtwork::publishVideo(const QVideoFrame &frame) {
  if(!m_running || !frame.isValid())return;
  auto image=frame.toImage();
  if(frame.rotation()!=QtVideo::Rotation::None)image=image.transformed(QTransform().rotate(int(frame.rotation())));
  if(frame.mirrored())image=image.transformed(QTransform().scale(-1,1));
  publish(std::move(image));
}
void MotionArtwork::setSource(const QUrl &source) {
  if(m_source==source)return;
  clear();m_source=source;emit sourceChanged();emit frameChanged();
  const QFileInfo file(source.toLocalFile());
  if(!source.isLocalFile() || !file.isFile() || file.size()>128*1024*1024)return;
  const auto suffix=file.suffix().toLower();
  if(suffix=="gif" || suffix=="webp") {
    QImageReader reader(file.absoluteFilePath());const auto size=reader.size();
    if(!size.isValid() || size.width()>4096 || size.height()>4096 || !reader.supportsAnimation())return;
    m_movie=std::make_unique<QMovie>(file.absoluteFilePath());
    m_movie->setCacheMode(QMovie::CacheNone);
    m_movie->setScaledSize(size.scaled(800,800,Qt::KeepAspectRatio));
    connect(m_movie.get(),&QMovie::frameChanged,m_movie.get(),[this]{publish(m_movie->currentImage());});
    connect(m_movie.get(),&QMovie::finished,m_movie.get(),[this]{if(m_running)m_movie->start();});
    connect(m_movie.get(),&QMovie::error,m_movie.get(),[this]{m_frame={};emit frameChanged();});
    if(m_running)m_movie->start();
  } else if(suffix=="mp4" || suffix=="webm") {
    m_sink=std::make_unique<QVideoSink>();m_player=std::make_unique<QMediaPlayer>();
    // No QAudioOutput: artwork never adds an audio stream to music playback.
    m_player->setVideoSink(m_sink.get());m_player->setLoops(QMediaPlayer::Infinite);
    connect(m_player.get(),&QMediaPlayer::tracksChanged,m_player.get(),[this]{m_player->setActiveAudioTrack(-1);});
    connect(m_sink.get(),&QVideoSink::videoFrameChanged,m_sink.get(),[this](const QVideoFrame &frame){publishVideo(frame);});
    connect(m_player.get(),&QMediaPlayer::errorOccurred,m_player.get(),[this]{m_frame={};emit frameChanged();});
    QUrl local=QUrl::fromLocalFile(file.absoluteFilePath());m_player->setSource(local);
    if(m_running)m_player->play();
  }
}
void MotionArtwork::setRunning(bool running) {
  if(m_running==running)return;
  m_running=running;emit runningChanged();
  if(m_movie){if(running && m_movie->state()==QMovie::NotRunning)m_movie->start();else m_movie->setPaused(!running);}
  if(m_player){if(running)m_player->play();else m_player->pause();}
}
