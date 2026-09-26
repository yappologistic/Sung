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
  const QString busy = c.directory + "/busy.mp4", large1080 = c.directory + "/busy-1080.mp4",
                large1920 = c.directory + "/busy-1920.mp4",
                fractal = c.directory + "/fractal.mp4", fractalTall = c.directory + "/fractal-tall.mp4",
                still = c.directory + "/still.png";
  c.check(c.encode({"-f", "lavfi", "-i", "testsrc2=size=720x720:rate=24:duration=3", "-threads", "1",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", busy}), "generate a detailed animated cover");
  // The same picture as Apple's 1080p and 2K covers come: 1080 square in
  // H.264, 1920 square in HEVC. 4K is the 2K path at a larger size.
  c.check(c.encode({"-f", "lavfi", "-i", "testsrc2=size=1080x1080:rate=24:duration=3", "-threads", "1",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", large1080}), "generate a 1080p animated cover");
  c.check(c.encode({"-f", "lavfi", "-i", "testsrc2=size=1920x1920:rate=24:duration=3", "-threads", "1",
                    "-c:v", "libx265", "-preset", "ultrafast", "-tag:v", "hvc1", "-pix_fmt", "yuv420p", large1920}),
          "generate a 2K animated cover");
  c.check(c.encode({"-f", "lavfi", "-i", "mandelbrot=size=720x720:rate=24", "-t", "3", "-threads", "1",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", fractal}), "generate a second animated cover");
  // Apple's tall covers are 3:4.
  c.check(c.encode({"-f", "lavfi", "-i", "mandelbrot=size=540x720:rate=24", "-t", "3", "-threads", "1",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", fractalTall}), "generate a tall animated cover");
  c.check(c.encode({"-f", "lavfi", "-i", "gradients=size=800x800:c0=0x2b4a8b:c1=0xe07a3f:n=2:seed=3",
                    "-frames:v", "1", still}), "generate a still cover");
  qputenv("SUNG_MOTION_FIXTURE", busy.toUtf8());
  qputenv("SUNG_MOTION_FIXTURE_1080", large1080.toUtf8());
  qputenv("SUNG_MOTION_FIXTURE_1920", large1920.toUtf8());
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
  c.check(b->motionArtQuality() == "standard" && motion->frame().width() <= 800 && b->currentMotionArt().endsWith("motionbusy1.mp4"),
          "outside Motion the shared cover is the small one");

  // The Motion video size is chosen in Settings, the way a person does it.
  const auto chooseQuality = [&](const QString &segment, const QString &capture) {
    c.click("settingsButton");
    auto *settings = w->findChild<QObject *>("settingsDialog");
    c.check(settings && c.until([&] { return settings->property("opened").toBool(); }), "Settings opens");
    c.click("settingsSearch");
    QTest::keyClick(w, Qt::Key_A, Qt::ControlModifier);
    for (const QChar letter : QStringLiteral("Motion layout video")) QTest::keyClick(w, letter.toLatin1());
    QTest::qWait(300);
    auto *control = shown(w->contentItem(), "motionQualityControl");
    c.check(control && control->isEnabled() && control->property("accessibleName").toString() == "Motion layout video",
            "Settings offers the Motion layout video size");
    c.click(segment);
    if (!capture.isEmpty()) c.shot(capture);
    QTest::keyClick(w, Qt::Key_Escape);
    c.check(settings && c.until([&] { return !settings->property("visible").toBool(); }), "Settings closes");
  };
  c.check(b->motionQuality() == 0, "Auto is the default");

  // Set directly rather than through F11, which would also take the
  // offscreen screen's fixed square and keep the window from resizing.
  w->setProperty("immersive", true);
  auto *player = c.until([&] { return shown(w->contentItem(), "immersivePlayer") != nullptr; })
      ? shown(w->contentItem(), "immersivePlayer") : nullptr;
  c.check(player, "the immersive view opens");
  if (!player) { fprintf(stdout, "RESULT %d failures\n", c.failures); QCoreApplication::exit(1); return; }
  // A laptop's shape: a square cover filling it loses a third of itself.
  // Past the 600ms the window size is given to settle.
  w->resize(1280, 800);
  QTest::qWait(900);

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
  const auto measure = [&](const char *label) {
    QTest::qWait(1500);
    const double start = cpuSeconds();
    QTest::qWait(5000);
    fprintf(stdout, "MEASURE cpu%% motion %s %.1f\n", label, (cpuSeconds() - start) / 5 * 100);
  };
  // Auto: a 1280px-wide window needs a cover at least 1280 wide, and the
  // smallest square that is is 2K.
  c.check(b->motionArtQuality() == "1920" && motion->maximumSize() == 1920, "Auto asks a 1280x800 window for the 2K cover");
  c.check(c.until([&] { return b->currentMotionArt().endsWith("-1920.mp4") && motion->frame().width() == 1920; }, 12000),
          "the 2K cover arrives and is drawn at its own size");
  measure("Auto 2K");
  // Settings is reached from the library, so step out of the immersive view
  // to change the size; Motion stays the chosen layout.
  const auto reenter = [&](const QString &segment, const QString &capture) {
    w->setProperty("immersive", false);
    QTest::qWait(500);
    chooseQuality(segment, capture);
    w->setProperty("immersive", true);
    c.check(c.until([&] { return (player = shown(w->contentItem(), "immersivePlayer")) != nullptr
                                 && player->property("displayedLayout").toString() == "motion"; }),
            "the immersive view comes back in Motion");
    scene = named(player, "motionScene");
    backdrop = named(player, "motionBackdrop");
    return scene && backdrop;
  };
  if (!reenter("motionQuality1080", "07-motion-quality-setting")) { fprintf(stdout, "RESULT %d failures\n", c.failures); QCoreApplication::exit(1); return; }
  c.check(b->motionQuality() == 1080 && motion->maximumSize() == 1080, "1080p chosen in Settings asks for the 1080p cover");
  c.check(c.until([&] { return b->currentMotionArt().endsWith("-1080.mp4") && motion->frame().width() == 1080
                               && backdrop->property("moving").toBool(); }, 12000),
          "the 1080p cover arrives and is drawn at its own size");
  measure("1080p");
  if (!reenter("motionQualityAuto", "")) { fprintf(stdout, "RESULT %d failures\n", c.failures); QCoreApplication::exit(1); return; }
  c.check(b->motionQuality() == 0, "Auto can be chosen again");
  c.check(c.until([&] { return b->currentMotionArt().endsWith("-1920.mp4") && motion->frame().width() == 1920
                               && backdrop->property("moving").toBool(); }, 12000),
          "and the 2K cover comes back without a new download");
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

  // --- The line being sung, over the picture ---
  // Lyrics arrive the way a person adds them: an .lrc file imported for the
  // song. Each line lasts four seconds; the fifth is long enough to wrap.
  const QStringList sung{"Neon rain on the window", "Every light a little louder", "", "Hold the signal steady",
                         "And if the city keeps on humming through the night we will follow every wire home"};
  QFile lrc(c.directory + "/neon.lrc");
  if (lrc.open(QIODevice::WriteOnly)) {
    for (int i = 0; i < sung.size(); ++i)
      lrc.write(QString("[00:%1.00]%2\n").arg(i * 4 + 2, 2, 10, QChar('0')).arg(sung[i]).toUtf8());
    lrc.close();
  }
  b->importLyrics(QUrl::fromLocalFile(lrc.fileName()), "motionbusy1");
  c.check(c.until([&] { return b->lyricLines().size() == sung.size(); }), "timed lyrics are imported for the song");
  auto *lyricSlot = named(player, "motionLyricSlot");
  auto *lyricLine = named(player, "motionLyric");
  const auto lineAt = [&](int ms, const QString &expected) {
    b->seek(ms);
    return c.until([&] { return lyricLine && lyricLine->property("text").toString() == expected
                                && (expected.isEmpty() || lyricLine->opacity() > 0.99); }, 4000);
  };
  c.check(lyricSlot && lyricSlot->isVisible(), "Motion shows the line being sung, on by default");
  c.check(lineAt(2500, sung[0]), "the first line shows");
  c.check(lineAt(6500, sung[1]), "and gives way to the next when it is sung");
  auto *titleNow = shown(player, "immersiveTitle");
  const qreal titleY = titleNow ? titleNow->mapToScene(QPointF()).y() : -1;
  c.check(lineAt(18500, sung[4]), "a long line shows");
  c.check(lyricLine && lyricLine->property("lineCount").toInt() >= 2 && lyricLine->property("lineCount").toInt() <= 3,
          QString("and wraps to at most three lines (%1)").arg(lyricLine ? lyricLine->property("lineCount").toInt() : 0));
  c.check(titleNow && qAbs(titleNow->mapToScene(QPointF()).y() - titleY) < 0.5, "without moving the title under it");
  auto *bottomVeil = named(player, "motionBottomScrim");
  if (lyricLine && bottomVeil) {
    // The scrim's flat, full-strength part starts a third of the way down it.
    const qreal full = bottomVeil->mapToScene(QPointF(0, bottomVeil->height() / 3)).y();
    const qreal top = lyricLine->mapToScene(QPointF()).y();
    c.check(full <= top + 1, QString("the scrim holds its full strength behind the whole line (%1 above %2)").arg(full).arg(top));
  }
  c.shot("08-motion-lyrics");
  c.check(lineAt(10500, ""), "an instrumental gap shows no line");
  c.check(lyricSlot && lyricSlot->isVisible() && lyricLine && !lyricLine->isVisible(), "and leaves nothing over the picture");
  c.check(lineAt(14500, sung[3]), "the next sung line comes back");
  // Left alone, the controls leave; the line and its scrim stay.
  QTest::mouseMove(w, QPoint(w->width() / 2, w->height() / 3));
  c.check(c.until([&] { return !player->property("controlsShown").toBool(); }, 6000), "the controls hide");
  auto *topVeil = named(player, "motionTopScrim");
  auto *lyricVeil = named(player, "motionLyricScrim");
  c.check(c.until([&] { return bottomVeil && bottomVeil->opacity() < 0.01 && topVeil && topVeil->opacity() < 0.01
                               && lyricVeil && lyricVeil->opacity() > 0.99; }),
          "the controls' scrims leave with them and the line keeps one of its own");
  if (lyricVeil && lyricLine) {
    // Full strength across the middle half of the band, where the line is.
    const qreal fullTop = lyricVeil->mapToScene(QPointF(0, lyricVeil->height() / 4)).y();
    const qreal fullBottom = lyricVeil->mapToScene(QPointF(0, lyricVeil->height() * 3 / 4)).y();
    const QRectF line = lyricLine->mapRectToScene(lyricLine->boundingRect());
    c.check(fullTop <= line.top() + 1 && fullBottom >= line.bottom() - 1,
            QString("the line sits inside its scrim's full strength (%1-%2 within %3-%4)")
                .arg(line.top()).arg(line.bottom()).arg(fullTop).arg(fullBottom));
    c.check(lyricVeil->mapToScene(QPointF(0, lyricVeil->height())).y() < w->height() - 60,
            "and the picture below it is left clear");
  }
  c.check(c.until([&] { return lyricLine && lyricLine->isVisible() && lyricLine->opacity() > 0.99; }, 3000), "and the line stays");
  c.shot("09-motion-lyrics-idle");
  // Turned off in the menu, the way a person turns it off.
  QTest::mouseMove(w, QPoint(w->width() / 2 + 30, w->height() / 3 + 10));
  c.check(c.until([&] { return player->property("controlsShown").toBool(); }), "the pointer brings the controls back");
  c.click("immersiveLayoutButton");
  auto *lyricToggle = shown(w->contentItem(), "immersiveMotionLyrics");
  c.check(lyricToggle && lyricToggle->isEnabled() && lyricToggle->property("checked").toBool()
              && lyricToggle->property("text").toString() == "Lyrics over video",
          "the menu offers Lyrics over video, on");
  c.shot("11-motion-lyrics-menu-on");
  c.click("immersiveMotionLyrics");
  c.check(c.until([&] { return lyricSlot && !lyricSlot->isVisible() && !player->property("motionLyrics").toBool(); }),
          "turning it off takes the line away");
  c.check(c.until([&] { return b->motionArtQuality() == "1920"; }), "and leaves the cover as it was");
  c.shot("10-motion-lyrics-off");
  c.click("immersiveLayoutButton");
  auto *lyricToggleOff = shown(w->contentItem(), "immersiveMotionLyrics");
  c.check(lyricToggleOff && !lyricToggleOff->property("checked").toBool(), "the menu shows it off");
  c.shot("12-motion-lyrics-menu-off");
  c.click("immersiveMotionLyrics");
  c.check(c.until([&] { return lyricSlot && lyricSlot->isVisible() && player->property("motionLyrics").toBool(); }),
          "and on again brings it back");
  // With animation off the next line is simply there.
  b->setMotion(false);
  b->seek(6500);
  QTest::qWait(120);
  c.check(lyricLine && lyricLine->property("text").toString() == sung[1] && lyricLine->opacity() > 0.99,
          "reduced motion swaps the line at once");
  b->setMotion(true);
  c.check(c.until([&] { return backdrop->property("moving").toBool(); }), "the animated cover returns with motion");

  // A different song crossfades to its own cover.
  qputenv("SUNG_MOTION_FIXTURE", fractal.toUtf8());
  qputenv("SUNG_MOTION_FIXTURE_1920", fractal.toUtf8());
  qputenv("SUNG_MOTION_FIXTURE_tall1080", fractalTall.toUtf8());
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
  c.check(c.until([&] { return b->lyricLines().isEmpty(); }) && lyricSlot && !lyricSlot->isVisible(),
          "a song without timed lyrics shows no line");
  // Lyrics for this song too, so the light theme and the narrow window are
  // seen with a line over them.
  QFile deep(c.directory + "/deep.lrc");
  if (deep.open(QIODevice::WriteOnly)) {
    for (int i = 0; i < 30; ++i) deep.write(QString("[00:%1.00]Down through the colours, deeper still\n").arg(i * 2, 2, 10, QChar('0')).toUtf8());
    deep.close();
  }
  b->importLyrics(QUrl::fromLocalFile(deep.fileName()), "motionfrac1");
  c.check(c.until([&] { return lyricLine && lyricLine->isVisible() && lyricLine->opacity() > 0.99
                               && lyricLine->property("text").toString().startsWith("Down through"); }),
          "and one with them shows its line");

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
  // Taller than it is wide by more than sqrt(4/3): the tall cover crops less
  // than the square one, and a 480px window needs only its 1080p.
  c.check(c.until([&] { return b->motionArtQuality() == "tall1080"; }, 3000), "a narrow window asks for the tall cover");
  c.check(c.until([&] { return b->currentMotionArt().endsWith("-tall1080.mp4") && motion->frame().height() > motion->frame().width()
                               && backdrop->property("moving").toBool(); }, 12000),
          "and draws it, taller than wide");
  c.check(motion->maximumSize() == 1438, "decoded up to the tall cover's own height");
  if (title) {
    const QRectF at = title->mapRectToScene(title->boundingRect());
    c.check(at.right() <= 480 && at.left() >= 0, "the title stays inside a narrow window");
  }
  QTest::mouseMove(w, QPoint(200, 300));
  QTest::mouseMove(w, QPoint(210, 310));
  c.check(c.until([&] { return player->property("controlsShown").toBool() && lyricLine && lyricLine->opacity() > 0.99; }),
          "the controls and the line show in a narrow window");
  if (lyricLine) {
    const QRectF line = lyricLine->mapRectToScene(lyricLine->boundingRect());
    auto *topRow = named(player, "immersiveTopControls");
    const qreal topBottom = topRow ? topRow->mapToScene(QPointF(0, topRow->height())).y() : 0;
    c.check(lyricLine->property("typeRole").toString() == "headlineMedium", "a compact window sets the line in headline medium");
    c.check(line.left() >= 0 && line.right() <= 480, "the line stays inside a narrow window");
    c.check(line.top() >= topBottom, QString("and below the top controls (%1 under %2)").arg(line.top()).arg(topBottom));
  }
  c.shot("06-motion-narrow");
  w->resize(1280, 800);
  c.check(c.until([&] { return b->motionArtQuality() == "1920"; }, 3000), "a wide window goes back to the square cover");

  // Leaving Motion gives the backdrop up and brings the cover back.
  c.click("immersiveLayoutButton");
  c.click("immersiveLayout_artwork");
  c.check(c.until([&] { return !scene->isVisible(); }), "leaving Motion fades the backdrop out");
  c.check(c.until([&] { return !backdrop->property("ready").toBool(); }), "and lets its pictures go");
  c.check(cover && c.until([&] { return cover->isVisible(); }), "the cover returns in Artwork");
  c.click("immersiveLayoutButton");
  auto *lyricsElsewhere = shown(w->contentItem(), "immersiveMotionLyrics");
  c.check(lyricsElsewhere && !lyricsElsewhere->isEnabled(), "outside Motion the lyrics choice waits, disabled");
  QTest::keyClick(w, Qt::Key_Escape);
  QTest::qWait(300);
  c.check(c.until([&] { return b->motionArtQuality() == "standard" && motion->maximumSize() == 800 && !b->currentMotionArt().contains("-1920"); }, 12000),
          "and the shared cover goes back to the small one");
  fprintf(stdout, "RESULT %d failures\n", c.failures);
  fflush(stdout);
  QCoreApplication::exit(c.failures ? 1 : 0);
}
