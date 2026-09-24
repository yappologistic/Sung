#pragma once
#include <QAudioBuffer>
#include <QVariantList>
#include <array>
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

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
    const int frameCount=buffer.frameCount(),bytesPerFrame=format.bytesPerFrame(),bytesPerSample=format.bytesPerSample();
    for(qsizetype frame=0;frame<frameCount;++frame){
      for(int channel=0;channel<channels;++channel){
        double x=format.normalizedSampleValue(bytes+frame*bytesPerFrame+channel*bytesPerSample);
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
    samples+=frameCount*channels;
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

// Mean square of decoded PCM for playback levelling. This is a broadband RMS
// estimate, not an EBU R128 meter: it costs one multiply per sample and only
// has to be stable enough to compare one recording against another.
class LoudnessMeter {
  double sum=0;
  qint64 samples=0;
  int rate=0,used=0;
public:
  void reset() {sum=0;samples=0;rate=0;used=0;}
  void process(const QAudioBuffer &buffer) {
    const auto format=buffer.format();
    if(!buffer.isValid()||!format.isValid())return;
    rate=format.sampleRate();
    const int channels=std::min(8,format.channelCount());
    used=channels;
    const auto bytes=buffer.constData<char>();
    const int frames=buffer.frameCount(),bytesPerFrame=format.bytesPerFrame(),bytesPerSample=format.bytesPerSample();
    for(qsizetype frame=0;frame<frames;++frame)
      for(int channel=0;channel<channels;++channel){
        double x=format.normalizedSampleValue(bytes+frame*bytesPerFrame+channel*bytesPerSample);
        if(!std::isfinite(x))continue;
        x=std::clamp(x,-1.0,1.0);sum+=x*x;++samples;
      }
  }
  // Short measurements describe an intro, not a recording. Wait for enough audio.
  double seconds() const {return rate>0&&used>0?double(samples)/(double(rate)*used):0;}
  bool ready() const {return seconds()>=45;}
  double levelDb() const {
    if(samples<=0)return 0;
    return 20*std::log10(std::max(std::sqrt(sum/samples),1e-6));
  }
};

// The shape of the sound, for the immersive visualizer. The five meters above
// are band-pass filters and say how loud each region is; a ring of bars around
// the cover needs many narrow bands, which a filter bank would pay for on
// every sample. This keeps the last 2048 decoded samples of each of up to two
// channels and, only when asked, runs one windowed FFT per channel: at the
// 30 Hz Sung publishes at, that is a few microseconds of work, and none at all
// while the visualizer is not on screen.
//
// Channels are transformed separately and their power averaged, for the same
// reason the meters measure them separately: out-of-phase stereo would cancel
// in a mono mix. The bands are spaced evenly in pitch from 40 Hz to 16 kHz.
// Recorded music loses roughly 3 dB per octave going up, so each band is
// tilted by +3 dB per octave around 1 kHz before it is scaled, or the top of
// the ring would never move.
class AudioSpectrum {
  static constexpr int Size=2048;
  std::array<std::vector<float>,2> ring{std::vector<float>(Size,0.f),std::vector<float>(Size,0.f)};
  int write=0,filled=0,rate=0,channels=0;
  int bands=36;
  static void fft(std::vector<std::complex<float>> &a) {
    const int n=int(a.size());
    for(int i=1,j=0;i<n;++i){
      int bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;
      if(i<j)std::swap(a[i],a[j]);
    }
    for(int len=2;len<=n;len<<=1){
      const float angle=-2*float(3.14159265358979323846)/len;
      const std::complex<float> step(std::cos(angle),std::sin(angle));
      for(int i=0;i<n;i+=len){
        std::complex<float> w(1,0);
        for(int k=0;k<len/2;++k){
          const auto u=a[i+k],v=a[i+k+len/2]*w;
          a[i+k]=u+v;a[i+k+len/2]=u-v;w*=step;
        }
      }
    }
  }
public:
  explicit AudioSpectrum(int bandCount=36):bands(std::max(4,bandCount)){}
  int bandCount() const {return bands;}
  void reset() {for(auto &r:ring)std::fill(r.begin(),r.end(),0.f);write=filled=0;rate=channels=0;}
  void process(const QAudioBuffer &buffer) {
    const auto format=buffer.format();
    if(!buffer.isValid() || !format.isValid())return;
    const int used=std::min(2,format.channelCount());
    if(rate!=format.sampleRate() || channels!=used){reset();rate=format.sampleRate();channels=used;}
    const auto bytes=buffer.constData<char>();
    const int frames=buffer.frameCount(),bytesPerFrame=format.bytesPerFrame(),bytesPerSample=format.bytesPerSample();
    for(qsizetype frame=0;frame<frames;++frame){
      for(int channel=0;channel<channels;++channel){
        double x=format.normalizedSampleValue(bytes+frame*bytesPerFrame+channel*bytesPerSample);
        ring[channel][write]=float(std::isfinite(x)?std::clamp(x,-1.0,1.0):0.0);
      }
      write=(write+1)%Size;filled=std::min(Size,filled+1);
    }
  }
  // One value per band from 0 (silent) to 1 (a full-scale tone), the lowest
  // band first.
  QVariantList take() const {
    QVariantList out;out.reserve(bands);
    if(filled<Size/2 || rate<=0 || channels<=0){for(int b=0;b<bands;++b)out.append(0.0);return out;}
    std::vector<double> power(Size/2,0.0);
    std::vector<std::complex<float>> data(Size);
    for(int channel=0;channel<channels;++channel){
      for(int i=0;i<Size;++i){
        const float hann=0.5f-0.5f*std::cos(2*float(3.14159265358979323846)*i/(Size-1));
        data[i]={ring[channel][(write+i)%Size]*hann,0.f};
      }
      fft(data);
      for(int k=0;k<Size/2;++k)power[k]+=std::norm(data[k])/channels;
    }
    // A full-scale sine through a Hann window peaks at Size/4.
    const double reference=double(Size)*Size/16;
    const double low=40,high=std::min(16000.0,rate*0.45),binWidth=double(rate)/Size;
    for(int b=0;b<bands;++b){
      const double from=low*std::pow(high/low,double(b)/bands),to=low*std::pow(high/low,double(b+1)/bands);
      const int first=std::clamp(int(std::floor(from/binWidth)),1,Size/2-1);
      const int last=std::clamp(int(std::ceil(to/binWidth)),first,Size/2-1);
      double peak=0;for(int k=first;k<=last;++k)peak=std::max(peak,power[k]);
      const double centre=std::sqrt(from*to);
      const double db=10*std::log10(std::max(peak/reference,1e-12))+3*std::log2(centre/1000);
      out.append(std::round(std::clamp((db+60)/54,0.0,1.0)*1000)/1000);
    }
    return out;
  }
};
