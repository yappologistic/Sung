#pragma once
#include <QAudioBuffer>
#include <QVariantList>
#include <array>
#include <algorithm>
#include <cmath>

// Five overlapping band-pass filters measure decoded PCM, before volume control.
// Channels are measured separately so out-of-phase stereo cannot cancel out.
class AudioLevels {
  struct Filter { double b=0,a1=0,a2=0; std::array<double,8> z1{},z2{}; };
  std::array<Filter,5> filters{};
  std::array<double,5> energy{};
  int rate=0,channels=0;
  qint64 samples=0;
public:
  void reset() {filters={};energy={};rate=channels=0;samples=0;}
  void process(const QAudioBuffer &buffer) {
    const auto format=buffer.format();
    if(!buffer.isValid() || !format.isValid())return;
    const int usedChannels=std::min(8,format.channelCount());
    if(rate!=format.sampleRate() || channels!=usedChannels){
      reset();rate=format.sampleRate();channels=usedChannels;
      constexpr std::array<double,5> centers{100,350,1200,3500,10000};
      for(int band=0;band<5;++band){
        if(centers[band]>=rate*0.45)continue;
        const double w=2*3.14159265358979323846*centers[band]/rate;
        const double alpha=std::sin(w)/(2*0.65),denominator=1+alpha;
        filters[band].b=alpha/denominator;
        filters[band].a1=-2*std::cos(w)/denominator;
        filters[band].a2=(1-alpha)/denominator;
      }
    }
    const auto bytes=buffer.constData<char>();
    for(qsizetype frame=0;frame<buffer.frameCount();++frame){
      for(int channel=0;channel<channels;++channel){
        double x=format.normalizedSampleValue(bytes+frame*format.bytesPerFrame()+channel*format.bytesPerSample());
        x=std::isfinite(x)?std::clamp(x,-1.0,1.0):0;
        for(int band=0;band<5;++band){
          auto &f=filters[band];const double y=f.b*x+f.z1[channel];
          f.z1[channel]=-f.a1*y+f.z2[channel];f.z2[channel]=-f.b*x-f.a2*y;
          energy[band]+=y*y;
        }
      }
    }
    // Flush inaudible filter tails before they become costly denormal values.
    for(auto &f:filters)for(int c=0;c<channels;++c){if(std::abs(f.z1[c])<1e-15)f.z1[c]=0;if(std::abs(f.z2[c])<1e-15)f.z2[c]=0;}
    samples+=buffer.frameCount()*channels;
  }
  QVariantList takeLevels() {
    QVariantList result;result.reserve(5);
    for(double sum:energy){
      const double rms=samples>0?std::sqrt(sum/samples):0;
      const double db=20*std::log10(std::max(rms,0.000001));
      result.append(std::round(std::clamp((db+54)/48,0.0,1.0)*100)/100);
    }
    energy={};samples=0;return result;
  }
};
