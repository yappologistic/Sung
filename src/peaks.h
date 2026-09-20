#pragma once
#include <QAudioBuffer>
#include <QVariantList>
#include <vector>
#include <algorithm>
#include <cmath>

// Bars for the seek waveform, voice-note style: one loudness per slice of the
// recording. Two choices keep the shape alive where mastered music is
// involved. The slice's loudness is the loudest short window inside it, not
// the slice average — a 30ms mean squares drums into the sustained bed around
// them, while an 8ms window lets a kick or a hit stand out of its slice. And
// the slice value is a mean square rather than a sample maximum, which would
// ride full scale all song long. Slices are keyed by presentation time, so
// seeks and stalls cannot reorder the shape, and playback and an offline
// decode may feed the same collector. Read-out normalises between the 10th and
// 90th percentile of what the recording actually does, then stretches around
// the middle so quiet passages drop away and loud ones stand tall.
class WaveformPeaks {
  std::vector<double> power{};
  std::vector<qint64> counts{};
  double subSum=0;
  qint64 subCount=0;
  qsizetype subBucket=-1;
  qint64 durationMs=0;
  int filledCount=0;
  int rate=0;
  qint64 countedFrames=0;
public:
  static constexpr int resolution=400;
  // Roughly 8ms at 48kHz: short enough to catch a hit, long enough to stay a
  // loudness rather than a sample maximum.
  static constexpr int subWindowFrames=384;
  // How hard the middle of the scale is stretched apart at read-out.
  static constexpr double contrast=1.6;
  void begin(qint64 ms) {
    durationMs=ms;power.assign(resolution,0.0);counts.assign(resolution,0);
    filledCount=0;rate=0;countedFrames=0;subSum=0;subCount=0;subBucket=-1;
  }
  // A duration that arrives late can only be trusted before any audio lands;
  // re-bucketing afterwards would stretch a half-drawn shape.
  void setDuration(qint64 ms) {if(ms>0&&durationMs<=0)begin(ms);}
  qint64 duration() const {return durationMs;}
  double coverage() const {return double(filledCount)/resolution;}
  void process(const QAudioBuffer &buffer) {
    const auto format=buffer.format();
    if(!buffer.isValid()||!format.isValid()||durationMs<=0)return;
    const qint64 startUs=buffer.startTime();
    if(rate!=format.sampleRate()){rate=format.sampleRate();countedFrames=0;subSum=0;subCount=0;subBucket=-1;}
    const auto bytes=buffer.constData<char>();
    const int channels=std::min(8,format.channelCount());
    const int frames=buffer.frameCount(),bytesPerFrame=format.bytesPerFrame(),bytesPerSample=format.bytesPerSample();
    for(qsizetype frame=0;frame<frames;++frame){
      double energy=0;
      for(int channel=0;channel<channels;++channel){
        double x=format.normalizedSampleValue(bytes+frame*bytesPerFrame+channel*bytesPerSample);
        x=std::isfinite(x)?std::clamp(x,-1.0,1.0):0;
        energy+=x*x;
      }
      qint64 ms;
      if(startUs>=0) ms=startUs/1000+frame*1000/format.sampleRate();
      else ms=countedFrames*1000/format.sampleRate();
      const qsizetype bucket=std::clamp<qsizetype>(ms*resolution/durationMs,0,resolution-1);
      if(counts[bucket]==0)++filledCount;
      ++counts[bucket];
      if(bucket!=subBucket){flush();subBucket=bucket;}
      subSum+=energy/channels;++subCount;
      if(subCount>=subWindowFrames)flush();
      ++countedFrames;
    }
    flush();
  }
  QVariantList levels() const {
    std::vector<double> audible;audible.reserve(resolution);
    for(int i=0;i<resolution;++i)
      if(counts[i]>0&&dbOf(i)>-60)audible.push_back(dbOf(i));
    QVariantList result;result.reserve(resolution);
    if(audible.empty()){for(int i=0;i<resolution;++i)result.append(0.0);return result;}
    // The heights span what the recording actually does: the quiet floor is
    // where its softer slices sit and the ceiling where its loud ones sit,
    // ignoring the extremes either side. A near-uniform recording still
    // reads, centred at half height rather than collapsing to zero.
    const auto pick=[&](double fraction){
      const auto index=std::min(audible.size()-1,size_t(fraction*(audible.size()-1)));
      auto sorted=audible;std::nth_element(sorted.begin(),sorted.begin()+index,sorted.end());
      return sorted[index];
    };
    const double low=pick(0.10),high=pick(0.90);
    const double mid=(low+high)/2,span=std::max(high-low,4.0);
    for(int i=0;i<resolution;++i){
      double level=0;
      if(counts[i]>0){
        level=std::clamp((dbOf(i)-(mid-span/2))/span,0.0,1.0);
        level=std::clamp((level-0.5)*contrast+0.5,0.0,1.0);
      }
      result.append(std::round(level*100)/100);
    }
    return result;
  }
private:
  void flush() {
    if(subCount<=0)return;
    const double rms2=subSum/subCount;
    if(subBucket>=0&&rms2>power[subBucket])power[subBucket]=rms2;
    subSum=0;subCount=0;
  }
  double dbOf(int i) const {
    return 10*std::log10(std::max(power[i],1e-12));
  }
};
