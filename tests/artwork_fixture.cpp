#include <QCoreApplication>
#include <QNetworkDiskCache>
#include <QNetworkCacheMetaData>
#include <QStandardPaths>
#include <QImage>
#include <QBuffer>
#include <QDateTime>

// Prepare synthetic network-cache fixtures in a separate process, so image
// generation and compression allocations do not pollute the app's RAM sample.
int main(int argc,char **argv) {
  QCoreApplication app(argc,argv);app.setApplicationName("sung");app.setOrganizationName("Sung");
  QNetworkDiskCache disk;
  disk.setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/art");
  for(int i=0;i<80;++i) {
    QImage art(544,544,QImage::Format_RGB32);
    for(int y=0;y<544;++y)for(int x=0;x<544;++x)art.setPixel(x,y,qRgb((x+i*19)%256,(y+i*37)%256,(x+y+i*11)%256));
    QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);if(!art.save(&buffer,"PNG"))return 2;
    QNetworkCacheMetaData meta;meta.setUrl(QUrl(QString("https://sung-benchmark.invalid/%1.png").arg(i)));
    meta.setExpirationDate(QDateTime::currentDateTimeUtc().addDays(1));
    meta.setRawHeaders({{"Content-Type","image/png"},{"Cache-Control","max-age=86400"}});
    auto device=disk.prepare(meta);if(!device)return 2;device->write(bytes);disk.insert(device);
  }
}
