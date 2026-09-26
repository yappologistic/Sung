#include "uitest.h"
#include "backend.h"
#include "motionartwork.h"
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QtTest/QTest>
#include <ctime>
#include <functional>
#include <qpa/qwindowsysteminterface.h>

// The Motion layout, driven the way a person drives it: the layout chosen
// from the immersive view's menu with the pointer, the controls left alone
// until they hide, the pointer moved to bring them back. The pixels are read
// back from the window, so the feather is checked where it is drawn.
namespace {
QQuickItem *named(QQuickItem *root, const QString &name) {
  if (!root) return nullptr;
  if (root->objectName() == name) return root;
  for (auto *child : root->childItems())
    if (auto *found = named(child, name)) return found;
  return nullptr;
}
QQuickItem *shown(QQuickItem *root, const QString &name) {
  if (!root || !root->isVisible()) return nullptr;
  if (root->objectName() == name) return root;
  for (auto *child : root->childItems())
    if (auto *found = shown(child, name)) return found;
  return nullptr;
}
double cpuSeconds() {
  timespec now{};
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now);
  return now.tv_sec + now.tv_nsec / 1e9;
}
// The largest luminance step between horizontal neighbours. A colour
// boundary drawn sharp is one large step; blurred, it spreads into many
// small ones.
int sharpest(const QImage &image, const QRect &area) {
  int most = 0;
  const QRect r = area.intersected(image.rect().adjusted(0, 0, -1, 0));
  for (int y = r.top(); y <= r.bottom(); ++y)
    for (int x = r.left(); x <= r.right(); ++x)
      most = qMax(most, qAbs(qGray(image.pixel(x, y)) - qGray(image.pixel(x + 1, y))));
  return most;
}
// Resident and proportional memory in MiB, from smaps_rollup.
QPair<double, double> memory() {
  QFile file("/proc/self/smaps_rollup");
  double rss = 0, pss = 0;
  if (file.open(QIODevice::ReadOnly))
    for (const auto &line : file.readAll().split('\n')) {
      const auto parts = line.simplified().split(' ');
      if (parts.size() < 2) continue;
      if (parts[0] == "Rss:") rss = parts[1].toDouble() / 1024;
      if (parts[0] == "Pss:") pss = parts[1].toDouble() / 1024;
    }
  return {rss, pss};
}
struct Stage {
  Backend *backend;
  QQuickWindow *window;
  QString directory;
  int failures = 0;
  void check(bool ok, const QString &label) {
    fprintf(stdout, "%s %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
    fflush(stdout);
    if (!ok) ++failures;
  }
  bool until(const std::function<bool()> &predicate, int timeout = 8000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout) QTest::qWait(25);
    return predicate();
  }
  QImage shot(const QString &name) {
    QTest::qWait(250);
    const QImage image = window->grabWindow();
    check(image.save(directory + '/' + name + ".png"), "capture " + name);
    return image;
  }
  QPoint centre(QQuickItem *item) {
    return item->mapToScene(item->boundingRect().center()).toPoint();
  }
  void click(const QString &name) {
    auto *item = shown(window->contentItem(), name);
    check(item, "find " + name);
    if (!item) return;
    QTest::mouseMove(window, centre(item));
    QTest::qWait(60);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centre(item));
    QTest::qWait(300);
  }
  bool encode(const QStringList &arguments) {
    QProcess ffmpeg;
    ffmpeg.start("ffmpeg", QStringList{"-nostdin", "-v", "error", "-y"} + arguments);
    return ffmpeg.waitForFinished(30000) && ffmpeg.exitCode() == 0;
  }
};
} // namespace

void runMotionLayoutTests(Backend *b, QQuickWindow *w) {
  Stage c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory);
  // Two animated covers that look nothing alike, so a change of song is a
  // change anyone could see, and a still for when animation is off.
  // testsrc2 has fine detail across the whole frame, which is what the
  // sharpness readings below need.
  const QString busy = c.directory + "/busy.mp4", large = c.directory + "/busy-large.mp4",
                fractal = c.directory + "/fractal.mp4",
                still = c.directory + "/still.png";
  c.check(c.encode({"-f", "lavfi", "-i", "testsrc2=size=720x720:rate=24:duration=3", "-threads", "1",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", busy}), "generate a detailed animated cover");
  // The same picture as Apple's large covers come: 2160 square, HEVC.
  c.check(c.encode({"-f", "lavfi", "-i", "testsrc2=size=2160x2160:rate=24:duration=3", "-threads", "1",
                    "-c:v", "libx265", "-preset", "ultrafast", "-tag:v", "hvc1", "-pix_fmt", "yuv420p", large}),
          "generate a large animated cover");
  c.check(c.encode({"-f", "lavfi", "-i", "mandelbrot=size=720x720:rate=24", "-t", "3", "-threads", "1",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", fractal}), "generate a second animated cover");
  c.check(c.encode({"-f", "lavfi", "-i", "gradients=size=800x800:c0=0x2b4a8b:c1=0xe07a3f:n=2:seed=3",
                    "-frames:v", "1", still}), "generate a still cover");
  qputenv("SUNG_MOTION_FIXTURE", busy.toUtf8());
  qputenv("SUNG_MOTION_FIXTURE_LARGE", large.toUtf8());
  qputenv("SUNG_BUFFER_FIXTURE", "1");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1280, 800);
  b->setVolume(0); b->setAutoplay(false); b->setPrepareNext(false); b->setTheme("dark");
  b->setMotion(true); b->setAnimatedArtwork(true); b->setOnlineArtwork(true);
  const QString art = QUrl::fromLocalFile(still).toString();
  QVariantMap first{{"id", "motionbusy1"}, {"videoId", "motionbusy1"}, {"title", "Neon Weather"},
                    {"artist", "Fixture Artist"}, {"album", "Signal Garden"}, {"kind", "song"}, {"art", art}};
  b->playItem(first);
  c.check(c.until([&] { return b->playing(); }), "the song starts");
  c.check(c.until([&] { return !b->currentMotionArt().isEmpty(); }, 12000), "its animated cover is found");
  auto *motion = qmlContext(w)->contextProperty("motionArtwork").value<MotionArtwork *>();
  c.check(motion && c.until([&] { return !motion->frame().isNull(); }), "the animated cover decodes");
  c.check(!b->largeMotionArt() && !b->currentMotionArt().contains("-hq") && motion->frame().width() <= 800,
          "outside Motion the shared cover is the small one");

  // Set directly rather than through F11, which would also take the
  // offscreen screen's fixed square and keep the window from resizing.
  w->setProperty("immersive", true);
  auto *player = c.until([&] { return shown(w->contentItem(), "immersivePlayer") != nullptr; })
      ? shown(w->contentItem(), "immersivePlayer") : nullptr;
  c.check(player, "the immersive view opens");
  if (!player) { fprintf(stdout, "RESULT %d failures\n", c.failures); QCoreApplication::exit(1); return; }
  // A laptop's shape: a square cover filling it loses a third of itself.
  w->resize(1280, 800);
  QTest::qWait(600);

  // Chosen from the menu the way a person chooses it.
  c.click("immersiveLayoutButton");
  auto *entry = shown(w->contentItem(), "immersiveLayout_motion");
  c.check(entry && entry->property("text").toString() == "Motion", "the layout menu offers Motion");
  c.click("immersiveLayout_motion");
  c.check(c.until([&] { return player->property("displayedLayout").toString() == "motion"; }),
          "choosing it switches the layout");
  c.check(player->property("preferredLayout").toString() == "motion", "the choice is kept");
  auto *scene = named(player, "motionScene");
  auto *backdrop = named(player, "motionBackdrop");
  c.check(scene && backdrop, "the motion backdrop exists");
  if (!scene || !backdrop) { fprintf(stdout, "RESULT %d failures\n", c.failures); QCoreApplication::exit(1); return; }
  c.check(c.until([&] { return backdrop->property("ready").toBool() && backdrop->property("moving").toBool(); }),
          "the backdrop draws the animated cover");
  c.check(b->largeMotionArt() && motion->maximumSize() == 2160, "Motion asks for the large cover");
  c.check(c.until([&] { return b->currentMotionArt().endsWith("-hq.mp4") && motion->frame().width() == 2160; }, 12000),
          "the large cover arrives and is drawn at its own size, not scaled down");
  QTest::qWait(1500);
  const double start = cpuSeconds();
  QTest::qWait(5000);
  fprintf(stdout, "MEASURE cpu%% motion 2160 %.1f\n", (cpuSeconds() - start) / 5 * 100);
  c.check(c.until([&] { return scene->opacity() == 1; }), "the backdrop fades all the way in");
  auto *ambient = named(player, "ambientBackdrop");
  c.check(ambient && c.until([&] { return !ambient->isVisible(); }), "the ambient wash steps aside");
  auto *cover = named(player, "immersiveArtwork");
  c.check(cover && !cover->isVisible(), "the separate cover is not drawn over its own backdrop");
  auto *lyrics = named(player, "lyricsView");
  c.check(!lyrics || !lyrics->isVisible(), "no lyrics column takes the picture's room");
  auto *title = shown(player, "immersiveTitle");
  c.check(title && title->property("text").toString() == "Neon Weather", "the title shows");
  if (title) {
    const QRectF at = title->mapRectToScene(title->boundingRect());
    c.check(at.left() < w->width() * 0.2 && at.top() > w->height() * 0.5,
            "the details sit low and to the left, out of the picture's middle");
  }
  // The frame keeps moving on screen, not only in the decoder.
  QSignalSpy frames(motion, &MotionArtwork::frameChanged);
  const QImage before = w->grabWindow();
  QTest::qWait(400);
  const QImage after = w->grabWindow();
  c.check(frames.count() >= 4, "frames keep arriving while the song plays");
  c.check(before != after, "the drawn backdrop changes with them");

  // With the controls away, the picture has the whole window. The feather is
  // read from those pixels: sharp in the middle, blurred at the border.
  QTest::mouseMove(w, QPoint(w->width() / 2, w->height() / 2));
  c.check(c.until([&] { return !player->property("controlsShown").toBool(); }, 6000),
          "left alone, Motion hides its controls");
  c.check(c.until([&] { return title && title->opacity() == 0; }), "the details leave with them");
  auto *artist = named(player, "immersiveArtistButton");
  c.check(artist && !artist->isEnabled(), "a hidden link takes no clicks or focus");
  QTest::qWait(500);
  const QImage bare = c.shot("02-motion-bare");
  // testsrc2's colour bars cross every row, so each strip below holds the
  // same boundaries; only their distance from the window's edge differs.
  const int h = bare.height(), wd = bare.width();
  const QRect across(wd / 5, 0, wd * 3 / 5, 0);
  const int middle = sharpest(bare, across.adjusted(0, h / 2 - 5, 0, h / 2 + 5));
  const int top = sharpest(bare, across.adjusted(0, 2, 0, 12));
  const int bottom = sharpest(bare, across.adjusted(0, h - 12, 0, h - 2));
  fprintf(stdout, "MEASURE sharpest step middle %d top %d bottom %d\n", middle, top, bottom);
  c.check(middle > 60, "the middle of the frame is sharp");
  c.check(top < middle / 3, "the frame softens toward the window's top edge");
  c.check(bottom < middle / 3, "and toward its bottom edge");
  auto *theme = qmlEngine(w)->singletonInstance<QObject *>("SungUi", "Theme");
  const QColor surface = theme ? theme->property("background").value<QColor>() : QColor();
  const QColor edgePixel = bare.pixelColor(2, h / 2);
  c.check(edgePixel != surface, "the edge is the picture blurred, not the surface showing through");

  // The pointer brings everything back.
  QTest::mouseMove(w, QPoint(w->width() / 2 + 40, w->height() / 2 + 20));
  c.check(c.until([&] { return player->property("controlsShown").toBool() && title->opacity() == 1; }),
          "moving the pointer brings the controls and details back");
  auto *bottomScrim = named(player, "motionBottomScrim");
  c.check(bottomScrim && bottomScrim->isVisible() && bottomScrim->height() > 100,
          "a scrim lies under the details and the transport");
  const qreal solved = backdrop->property("bottomScrim").toReal();
  fprintf(stdout, "MEASURE scrim top %.3f bottom %.3f\n", backdrop->property("topScrim").toReal(), solved);
  c.check(solved > 0 && solved <= 1, "the bottom scrim is solved from the picture under it");
  c.shot("01-motion");

  c.click("immersiveLayoutButton");
  auto *autoHide = shown(w->contentItem(), "immersiveAutoHide");
  c.check(autoHide && autoHide->property("checked").toBool() && !autoHide->isEnabled(),
          "the auto-hide entry reads on and cannot be turned off in Motion");
  QTest::keyClick(w, Qt::Key_Escape);
  QTest::qWait(400);
  c.check(player->isVisible(), "Escape closes the menu, not the immersive view");

  // A different song crossfades to its own cover.
  qputenv("SUNG_MOTION_FIXTURE", fractal.toUtf8());
  qputenv("SUNG_MOTION_FIXTURE_LARGE", fractal.toUtf8());
  QSignalSpy mixes(backdrop, SIGNAL(mixChanged()));
  QVariantMap second{{"id", "motionfrac1"}, {"videoId", "motionfrac1"}, {"title", "Deep Zoom"},
                     {"artist", "Fixture Artist"}, {"album", "Iterations"}, {"kind", "song"}, {"art", art}};
  b->playItem(second);
  c.check(c.until([&] { return b->currentMotionArt().contains("motionfrac1"); }, 12000),
          "the next song's animated cover is found");
  bool faded = false;
  c.check(c.until([&] {
            faded = faded || backdrop->property("mix").toReal() < 1;
            return faded && backdrop->property("mix").toReal() == 1 && backdrop->property("moving").toBool();
          }, 10000), "the backdrop crossfades to the next cover");
  c.check(mixes.count() > 3, "through intermediate values, not a cut");
  QTest::mouseMove(w, QPoint(w->width() / 2 - 40, w->height() / 2 - 20));
  QTest::qWait(900);
  c.shot("03-motion-second-song");

  // What it costs to draw the video in Motion against drawing it as the
  // cover in the Artwork layout, over the same five seconds of playback.
  const auto sample = [&] {
    const double start = cpuSeconds();
    QTest::qWait(5000);
    return (cpuSeconds() - start) / 5 * 100;
  };
  const double motionCpu = sample();
  const auto motionMemory = memory();
  player->setProperty("preferredLayout", "artwork");
  QTest::qWait(1200);
  const double artworkCpu = sample();
  const auto artworkMemory = memory();
  fprintf(stdout, "MEASURE MiB rss/pss motion %.1f/%.1f artwork %.1f/%.1f\n", motionMemory.first,
          motionMemory.second, artworkMemory.first, artworkMemory.second);
  player->setProperty("preferredLayout", "motion");
  c.check(c.until([&] { return player->property("displayedLayout").toString() == "motion"; }), "back to Motion");
  fprintf(stdout, "MEASURE cpu%% motion %.1f artwork %.1f\n", motionCpu, artworkCpu);

  // Reduced motion keeps the layout and draws the still the same way.
  b->setMotion(false);
  c.check(c.until([&] { return backdrop->property("ready").toBool() && !backdrop->property("moving").toBool(); }),
          "with animation off, the still cover fills the view instead");
  c.shot("04-motion-still");
  b->setMotion(true);

  // Light theme, then a narrow window.
  b->setTheme("light");
  c.check(c.until([&] { return backdrop->property("moving").toBool(); }), "the animated cover returns");
  c.shot("05-motion-light");
  b->setTheme("dark");
  w->resize(480, 620);
  QTest::qWait(600);
  c.check(qAbs(backdrop->width() - 480) < 1 && qAbs(backdrop->height() - 620) < 1, "the backdrop fills a narrow window");
  if (title) {
    const QRectF at = title->mapRectToScene(title->boundingRect());
    c.check(at.right() <= 480 && at.left() >= 0, "the title stays inside a narrow window");
  }
  c.shot("06-motion-narrow");
  w->resize(1280, 800);

  // Leaving Motion gives the backdrop up and brings the cover back.
  c.click("immersiveLayoutButton");
  c.click("immersiveLayout_artwork");
  c.check(c.until([&] { return !scene->isVisible(); }), "leaving Motion fades the backdrop out");
  c.check(c.until([&] { return !backdrop->property("ready").toBool(); }), "and lets its pictures go");
  c.check(cover && c.until([&] { return cover->isVisible(); }), "the cover returns in Artwork");
  c.check(c.until([&] { return !b->largeMotionArt() && motion->maximumSize() == 800 && !b->currentMotionArt().contains("-hq"); }, 12000),
          "and the shared cover goes back to the small one");
  fprintf(stdout, "RESULT %d failures\n", c.failures);
  fflush(stdout);
  QCoreApplication::exit(c.failures ? 1 : 0);
}
