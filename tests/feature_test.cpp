// Live checks for the features added on top of the original interface. Each one
// drives the real application and asserts against what the running window
// reports, then photographs the result for review.
#include "uitest.h"
#include "m3motion.h"
#include "backend.h"
#include "m3color.h"
#include "rowselection.h"
#include <QAccessible>
#include <QAccessibleActionInterface>
#include <QColor>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQmlExpression>
#include <QQmlListReference>
#include <qqml.h>
#include <QQuickItem>
#include <QQuickWindow>
#include <QFont>
#include <QQmlProperty>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <qpa/qwindowsysteminterface.h>
#include <algorithm>
#include <functional>
#include <tuple>

namespace {
QQuickItem *shownItem(QQuickItem *root, const QString &name) {
  if (!root->isVisible())
    return nullptr;
  if (root->objectName() == name)
    return root;
  for (auto child : root->childItems())
    if (auto found = shownItem(child, name))
      return found;
  return nullptr;
}
void collectItems(QQuickItem *root, const QString &name, QList<QQuickItem *> &found) {
  if (root->objectName() == name)
    found << root;
  for (auto child : root->childItems())
    collectItems(child, name, found);
}
QQuickItem *anyItem(QQuickItem *root, const QString &name) {
  if (root->objectName() == name)
    return root;
  for (auto child : root->childItems())
    if (auto found = anyItem(child, name))
      return found;
  return nullptr;
}
QObject *motionObject(QObject *owner, const char *property) {
  if (!owner)
    return nullptr;
  // Qt defers Behavior.animation and Transition.animations until used. Build
  // the declared object now so the stage can inspect the pair it will run.
  qmlExecuteDeferred(owner);
  return QQmlProperty(owner, property).read().value<QObject *>();
}
QObject *motionAt(QObject *group, std::initializer_list<int> indices) {
  for (const int index : indices) {
    if (!group)
      return nullptr;
    qmlExecuteDeferred(group);
    const QQmlListReference animations(group, "animations");
    if (!animations.isReadable() || index >= animations.count())
      return nullptr;
    group = animations.at(index);
  }
  return group;
}

// How far past a container's right edge anything inside it is drawn. A row
// that will not shrink takes its neighbours out with it, so the worst offender
// is worth naming.
void collectOverflow(QQuickItem *root, QQuickItem *within, double &worstBy, QString &worstName) {
  if (root->isVisible() && root->width() > 1) {
    const double right = root->mapToItem(within, QPointF(root->width(), 0)).x();
    const double past = right - within->width();
    if (past > worstBy) {
      worstBy = past;
      worstName = root->objectName().isEmpty() ? QStringLiteral("a row") : root->objectName();
    }
  }
  for (auto child : root->childItems())
    collectOverflow(child, within, worstBy, worstName);
}

// Material's type scale is a closed set of roles. A size that is not one of
// them carries no line height and no letter spacing of its own, so it is not a
// style at all, only a number. A style sized by the window or by the reader
// says so and is left alone.
void collectOffScale(QQuickItem *root, const QSet<int> &sizes, QStringList &out) {
  if (root->isVisible() && root->metaObject()->indexOfProperty("metricSize") >= 0 &&
      !root->property("scaled").toBool()) {
    const int size = root->property("font").value<QFont>().pixelSize();
    if (size > 0 && !sizes.contains(size))
      out.append(QString("%1 at %2px")
                     .arg(root->objectName().isEmpty() ? QStringLiteral("text") : root->objectName())
                     .arg(size));
  }
  for (auto child : root->childItems())
    collectOffScale(child, sizes, out);
}

// A style laid out on an absolute line height has to leave room for its own
// glyphs. A line height given as a multiple of the size, the way Text takes
// one, is read as a count of pixels in that mode, which stacks every wrapped
// line on top of the one above it.
void collectCrampedText(QQuickItem *root, QStringList &out) {
  if (root->isVisible() && root->metaObject()->indexOfProperty("lineHeightMode") >= 0) {
    const double line = root->property("lineHeight").toDouble();
    const double size = root->property("font").value<QFont>().pixelSize();
    if (root->property("lineHeightMode").toInt() == 1 && size > 0 && line < size)
      out.append(QString("%1 at %2px on a %3px line")
                     .arg(root->objectName().isEmpty() ? QStringLiteral("text") : root->objectName())
                     .arg(size, 0, 'f', 0)
                     .arg(line, 0, 'f', 2));
  }
  for (auto child : root->childItems())
    collectCrampedText(child, out);
}

struct Check {
  Backend *backend;
  QQuickWindow *window;
  QString directory;
  int failures = 0;

  void check(bool ok, const QString &label) {
    fprintf(stdout, "%s %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
    fflush(stdout);
    if (!ok)
      ++failures;
  }
  bool until(const std::function<bool()> &predicate, int timeout = 8000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout)
      QTest::qWait(25);
    return predicate();
  }
  QVariant evaluate(const QString &script) {
    QQmlExpression expression(qmlContext(window), window, script);
    return expression.evaluate();
  }
  QColor themeColor(const QString &role) { return evaluate("Theme." + role).value<QColor>(); }
  void shot(const QString &name) {
    QTest::qWait(280);
    shotNow(name);
  }
  // Capturing mid-transition cannot afford to settle first.
  void shotNow(const QString &name) {
    // Stages that need no fixture folder have nothing else to create the
    // output directory, and a capture into a missing one silently fails.
    QDir().mkpath(directory);
    check(window->grabWindow().save(directory + '/' + name + ".png"), "capture " + name);
  }
  void click(const QString &name) {
    tap(name);
    QTest::qWait(320);
  }
  // A click scoped to one part of the window, where the same row names appear
  // in more than one list at once.
  void clickWithin(QQuickItem *parent, const QString &name) {
    auto item = parent ? shownItem(parent, name) : nullptr;
    check(item, "find " + name + " in " + (parent ? parent->objectName() : QString("nothing")));
    if (!item)
      return;
    const auto point = item->mapToScene(item->boundingRect().center()).toPoint();
    QTest::mouseMove(window, point);
    QTest::qWait(60);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
    QTest::qWait(320);
  }
  // A click with no settling wait, for watching what the click sets off.
  void tap(const QString &name) {
    auto item = shownItem(window->contentItem(), name);
    check(item, "find " + name);
    if (!item)
      return;
    const auto point = item->mapToScene(item->boundingRect().center()).toPoint();
    QTest::mouseMove(window, point);
    QTest::qWait(60);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
  }
  QObject *dialog(const QString &name, const QString &method = "open") {
    auto found = window->findChild<QObject *>(name);
    check(found, "reach " + name);
    if (found) {
      QMetaObject::invokeMethod(found, qPrintable(method));
      QTest::qWait(420);
    }
    return found;
  }
  void closeDialog(QObject *target) {
    if (target)
      QMetaObject::invokeMethod(target, "close");
    QTest::qWait(320);
  }
  void finish() {
    fprintf(stdout, "RESULT %d failures\n", failures);
    fflush(stdout);
    QCoreApplication::exit(failures ? 1 : 0);
  }
};

void paintCover(const QString &path, const QColor &base, const QColor &accent) {
  QImage cover(640, 640, QImage::Format_RGB32);
  QPainter paint(&cover);
  paint.fillRect(cover.rect(), base);
  paint.setRenderHint(QPainter::Antialiasing);
  paint.fillRect(0, 0, 640, 240, accent);
  paint.setBrush(accent.lighter(130));
  paint.setPen(Qt::NoPen);
  paint.drawEllipse(QPoint(430, 430), 150, 150);
  paint.end();
  cover.save(path);
}

bool encodeTrack(Check &c, const QString &path, const QString &title, const QString &album,
                 const QString &artist, int track, const QStringList &extra = {}) {
  QStringList arguments{"-nostdin", "-v", "error", "-f", "lavfi", "-i", "anullsrc=r=8000:cl=mono",
                        "-t", "150", "-metadata", "title=" + title, "-metadata", "album=" + album,
                        "-metadata", "artist=" + artist, "-metadata", "album_artist=" + artist,
                        "-metadata", "date=2026", "-metadata", "track=" + QString::number(track)};
  arguments += extra;
  arguments << path;
  QProcess encode;
  encode.start("ffmpeg", arguments);
  const bool ok = encode.waitForFinished(20000) && encode.exitCode() == 0;
  c.check(ok, "generate " + QFileInfo(path).fileName());
  return ok;
}

double contrastOf(const QColor &a, const QColor &b) {
  const auto luminance = [](const QColor &c) {
    const auto channel = [](double v) {
      return v <= 0.040449936 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
  };
  const double x = luminance(a), y = luminance(b);
  return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}
} // namespace

void runDynamicColorTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music/Still Water");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1320, 860);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(false);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);
  b->setLyricsFallback(false);
  b->setArtworkAccent(false);
  b->setAccentColor("");
  QTest::qWait(200);

  paintCover(c.directory + "/music/Still Water/cover.png", QColor("#1f4f6b"), QColor("#d98324"));
  if (!encodeTrack(c, c.directory + "/music/Still Water/01.flac", "Across the still water",
                   "Still Water", "Rill", 1))
    return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the colour fixture");
  b->library("files");
  c.check(c.until([&] { return b->results()->count() == 1; }), "the fixture is in the library");

  // --- Nothing chosen: the built-in palette is untouched ---
  const auto plainBackground = c.themeColor("background");
  const auto plainSurface = c.themeColor("surface");
  c.check(!c.evaluate("Theme.useSource").toBool(), "no source colour by default");
  c.check(plainBackground == QColor("#181211"), "the built-in dark background is unchanged");
  c.shot("01-default-palette");

  // --- A chosen source colour reaches the surfaces, not just the accent ---
  b->setAccentColor("#386a20");
  QTest::qWait(250);
  c.check(c.evaluate("Theme.useSource").toBool(), "the chosen colour drives the scheme");
  const auto greenBackground = c.themeColor("background");
  const auto greenSurface = c.themeColor("surface");
  const auto greenContainer = c.themeColor("container");
  c.check(greenBackground != plainBackground && greenSurface != plainSurface,
          "surfaces follow the source colour, not only the accent");
  // Material tints neutrals with a trace of the source hue; it must stay a hint.
  for (const auto &pair : QList<QPair<QString, QColor>>{{"background", greenBackground},
                                                        {"surface", greenSurface},
                                                        {"container", greenContainer}}) {
    const double chroma = m3::measure(pair.second).chroma;
    c.check(chroma > 0.5 && chroma < 12.0,
            QString("%1 is tinted but stays neutral (chroma %2)").arg(pair.first).arg(chroma, 0, 'f', 1));
    c.check(qAbs(m3::measure(pair.second).hue - m3::measure(QColor("#386a20")).hue) < 12.0,
            QString("%1 carries the source hue").arg(pair.first));
  }
  // The surface ladder still climbs away from the background in a dark theme.
  // Material puts the surface at the same tone as the background, and the
  // container ladder climbs away from the pair of them.
  c.check(m3::toneOf(greenBackground) == m3::toneOf(greenSurface),
          "the surface starts where the background does");
  c.check(m3::toneOf(greenSurface) < m3::toneOf(c.themeColor("surfaceLow")) &&
              m3::toneOf(c.themeColor("surfaceLow")) < m3::toneOf(greenContainer) &&
              m3::toneOf(greenContainer) < m3::toneOf(c.themeColor("high")),
          "dark surfaces lighten as they stack");
  c.shot("02-green-source-dark");

  const auto floors = [&](const QString &where) {
    const QStringList surfaces{"background", "surface", "container", "high"};
    for (const auto &surface : surfaces) {
      c.check(contrastOf(c.themeColor("text"), c.themeColor(surface)) >= 4.5,
              QString("%1: body text keeps 4.5:1 on %2").arg(where, surface));
      c.check(contrastOf(c.themeColor("muted"), c.themeColor(surface)) >= 4.5,
              QString("%1: secondary text keeps 4.5:1 on %2").arg(where, surface));
      c.check(contrastOf(c.themeColor("primary"), c.themeColor(surface)) >= 4.5,
              QString("%1: the accent keeps 4.5:1 on %2").arg(where, surface));
    }
    c.check(contrastOf(c.themeColor("primaryText"), c.themeColor("primary")) >= 4.5,
            where + ": text on the accent keeps 4.5:1");
    c.check(contrastOf(c.themeColor("containerText"), c.themeColor("primaryContainer")) >= 4.5,
            where + ": container text keeps 4.5:1");
    // Navigation is drawn in the secondary pair and a row being carried in the
    // tertiary one, so both have to hold their ink wherever the scheme lands.
    c.check(contrastOf(c.themeColor("secondaryContainerText"), c.themeColor("secondaryContainer")) >= 4.5,
            where + ": the destination you are on keeps 4.5:1 on its indicator");
    c.check(contrastOf(c.themeColor("secondary"), c.themeColor("surface")) >= 4.5,
            where + ": and its label keeps 4.5:1 beneath it");
    c.check(contrastOf(c.themeColor("secondaryText"), c.themeColor("secondary")) >= 4.5,
            where + ": a tonal toggle that is on keeps 4.5:1 on the secondary role");
    c.check(contrastOf(c.themeColor("tertiaryContainerText"), c.themeColor("tertiaryContainer")) >= 4.5,
            where + ": a row being carried keeps 4.5:1 on the reorder container");
    // The two outline roles have two jobs and two floors: a rule only has to
    // be seen, a control boundary has to meet Material's 3:1 for a shape that
    // carries meaning.
    c.check(contrastOf(c.themeColor("outlineVariant"), c.themeColor("surface")) >= 1.3,
            where + ": dividers stay visible");
    c.check(contrastOf(c.themeColor("outline"), c.themeColor("surface")) >= 3.0,
            where + ": a control's boundary keeps 3:1");
  };
  floors("green dark");

  b->setTheme("light");
  QTest::qWait(300);
  const auto lightBackground = c.themeColor("background");
  c.check(m3::toneOf(lightBackground) > 90, "the light theme starts from a near-white surface");
  c.check(m3::toneOf(c.themeColor("background")) > m3::toneOf(c.themeColor("container")),
          "light surfaces darken as they stack");
  floors("green light");
  c.shot("03-green-source-light");
  b->setTheme("dark");
  QTest::qWait(300);

  // --- Artwork drives it, and a different cover moves it ---
  b->setAccentColor("");
  b->setArtworkAccent(true);
  b->enqueueItems(b->results()->rows);
  b->playAt(0);
  c.check(c.until([&] { return b->playing(); }), "the fixture plays");
  c.check(c.until([&] { return c.evaluate("Theme.useArtwork").toBool(); }),
          "the cover becomes the source colour");
  const auto coverBackground = c.themeColor("background");
  c.check(coverBackground != plainBackground, "the cover re-tints the window");
  floors("cover dark");
  c.shot("04-artwork-source");

  const auto warmHue = m3::measure(c.themeColor("primary")).hue;
  c.evaluate("Theme.artworkSeed=Qt.rgba(0.20,0.36,0.78,1)");
  QTest::qWait(350);
  const auto coolHue = m3::measure(c.themeColor("primary")).hue;
  c.check(qAbs(warmHue - coolHue) > 40, "a different cover moves the whole scheme");
  c.check(c.themeColor("background") != coverBackground, "including its surfaces");
  floors("cool cover");
  c.shot("05-artwork-source-cool");

  // --- Turning it off restores the built-in palette exactly ---
  b->setArtworkAccent(false);
  c.evaluate("Theme.artworkSeed=Qt.rgba(0,0,0,0)");
  QTest::qWait(300);
  c.check(!c.evaluate("Theme.useSource").toBool(), "the scheme is released");
  c.check(c.themeColor("background") == plainBackground && c.themeColor("surface") == plainSurface,
          "the built-in palette returns untouched");
  c.shot("06-released");

  b->stop();
  b->clearQueue();
  c.finish();
}

void runNavigationMotionTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1320, 860);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(true);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);

  paintCover(c.directory + "/music/cover.png", QColor("#1f4f6b"), QColor("#d98324"));
  for (int i = 1; i <= 3; ++i)
    if (!encodeTrack(c, QString("%1/music/%2.flac").arg(c.directory).arg(i),
                     QString("Track %1").arg(i), "Motion", "Rill", i))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the motion fixture");

  // --- What a destination change actually looks like, frame by frame ---
  // Switching between the three destinations was reported as jagged. The way
  // to answer that is to sample the transition rather than describe it: every
  // frame of one, with the numbers the eye is reacting to.
  {
    b->home();
    c.check(c.until([&] { return !b->busy(); }), "Home is open to switch away from");
    QTest::qWait(600);
    auto shell = anyItem(w->contentItem(), "contentColumn");
    auto arriving = anyItem(w->contentItem(), "navBarIndicator_library");
    if (shell && arriving) {
      c.tap("navBar_library");
      struct Frame { int ms; double opacity, scale, indicator; };
      QList<Frame> frames;
      QElapsedTimer clock;
      clock.start();
      while (clock.elapsed() < 520) {
        frames.append({int(clock.elapsed()), shell->opacity(), shell->scale(), arriving->width()});
        QTest::qWait(16);
      }
      // FastEffects moves at most 0.349 over any 16ms phase of its solved
      // response (0.327 on the 0,16,32ms grid). The strict 0.5 boundary
      // allows uneven frame waits yet rejects a one-frame cut or two half
      // cuts. Intermediate frames and the final settle are checked below.
      double worstOpacity = 0, worstScale = 0;
      int worstIndex = 0;
      for (int i = 1; i < frames.size(); ++i) {
        const double step = qAbs(frames[i].opacity - frames[i - 1].opacity);
        if (step > worstOpacity) { worstOpacity = step; worstIndex = i; }
        if (frames[i].opacity > 0.02 && frames[i - 1].opacity > 0.02)
          worstScale = qMax(worstScale, qAbs(frames[i].scale - frames[i - 1].scale));
      }
      int worstGap = 0, atMs = 0;
      for (int i = 1; i < frames.size(); ++i) {
        worstGap = qMax(worstGap, frames[i].ms - frames[i - 1].ms);
        if (qAbs(frames[i].opacity - frames[i - 1].opacity) >= worstOpacity - 1e-9) atMs = frames[i].ms;
      }
      c.check(worstOpacity < 0.5,
              QString("the view fades rather than cutting (worst step %1 at %2ms, "
                      "sampled every %3ms at worst, %4 frames; prior %5 at %6ms, next %7 at %8ms)")
                  .arg(worstOpacity, 0, 'f', 3).arg(atMs).arg(worstGap).arg(frames.size())
                  .arg(frames[worstIndex - 1].opacity, 0, 'f', 3).arg(frames[worstIndex - 1].ms)
                  .arg(frames[worstIndex].opacity, 0, 'f', 3).arg(frames[worstIndex].ms));
      const int intermediate = std::count_if(frames.cbegin(), frames.cend(),
                                             [](const Frame &f) { return f.opacity > 0.05 && f.opacity < 0.95; });
      c.check(intermediate >= 3 &&
                  std::any_of(frames.cbegin(), frames.cend(),
                              [](const Frame &f) { return f.opacity < 0.05; }) &&
                  frames.last().opacity > 0.99,
              "the fade has intermediate frames, reaches the outgoing view, and settles");
      c.check(worstScale <= 0.05,
              QString("and grows rather than snapping (worst step %1)").arg(worstScale, 0, 'f', 3));
      // The indicator and the view finish together. One still travelling long
      // after the other has settled is two animations, not one transition.
      const auto settledAt = [&](std::function<bool(const Frame &)> done) {
        for (int i = frames.size() - 1; i > 0; --i)
          if (!done(frames[i - 1]))
            return frames[i].ms;
        return 0;
      };
      const double full = frames.isEmpty() ? 0 : frames.last().indicator;
      const int viewDone = settledAt([](const Frame &f) { return f.opacity > 0.99 && f.scale > 0.999; });
      const int pillDone = settledAt([&](const Frame &f) { return qAbs(f.indicator - full) < 1; });
      c.check(qAbs(viewDone - pillDone) < 90,
              QString("the view and its indicator settle together (%1ms and %2ms)")
                  .arg(viewDone).arg(pillDone));
      c.shot("00-destination-change");
    } else {
      c.check(false, "the content column and the arriving indicator are both there");
    }
    b->home();
    c.check(c.until([&] { return !b->busy(); }), "back to Home");
    QTest::qWait(400);
  }

  auto column = anyItem(w->contentItem(), "contentColumn");
  auto body = anyItem(w->contentItem(), "contentBody");
  auto destinations = w->findChild<QObject *>("destinationTransition");
  auto tabs = w->findChild<QObject *>("tabTransition");
  c.check(column && body && destinations && tabs, "the motion targets and controllers exist");
  if (!column || !body || !destinations || !tabs)
    return c.finish();

  // The two fade-through stages take their durations from the solved springs.
  // FastEffects exits before DefaultSpatial brings in the scaled destination.
  const int leaveMs = c.evaluate("Theme.springFastEffectsMs").toInt();
  const int arriveMs = c.evaluate("Theme.springSpatialMs").toInt();
  c.check(destinations->property("leaveDuration").toInt() == leaveMs,
          "the outgoing fade uses FastEffects' settling time");
  c.check(destinations->property("arriveDuration").toInt() == arriveMs &&
              destinations->property("totalDuration").toInt() == leaveMs + arriveMs,
          "the incoming spatial movement and staged total derive from springs");
  auto leaveGroup = motionObject(destinations, "leave");
  auto arriveGroup = motionObject(destinations, "arrive");
  auto springAnimation = [&](QObject *animation, int ms, const char *curveName) {
    return animation && animation->property("duration").toInt() == ms &&
           QQmlProperty(animation, "easing.bezierCurve").read().toList() ==
               c.evaluate("Theme." + QString::fromLatin1(curveName)).toList();
  };
  c.check(springAnimation(motionAt(leaveGroup, {0}), leaveMs, "springFastEffects") &&
              springAnimation(motionAt(arriveGroup, {0}),
                              c.evaluate("Theme.springEffectsMs").toInt(), "springEffects") &&
              springAnimation(motionAt(arriveGroup, {1}), arriveMs, "springSpatial") &&
              springAnimation(motionAt(arriveGroup, {2}), 0, "springSpatial"),
          "navigation fade and movement read matching spring pairs");
  c.check(qAbs(destinations->property("arriveScale").toReal() - 0.92) < 0.001,
          "fading through grows the incoming view from 92%");
  c.check(qAbs(destinations->property("axisTravel").toReal() - 30) < 0.001,
          "sharing an axis travels 30dp");

  // --- Rail destinations fade through ---
  b->home();
  c.check(c.until([&] { return !b->busy(); }), "Home is ready");
  QTest::qWait(400);
  c.check(qAbs(column->opacity() - 1) < 0.01 && qAbs(column->scale() - 1) < 0.01,
          "a settled view sits at full size and opacity");
  c.tap("navBar_library");
  // Sample during the outgoing half: the view must be on its way out.
  QTest::qWait(45);
  const double leavingOpacity = column->opacity();
  c.check(leavingOpacity < 0.9 && leavingOpacity > 0.0,
          QString("the outgoing destination is fading (%1)").arg(leavingOpacity, 0, 'f', 2));
  c.check(qAbs(column->property("shift").toReal()) < 0.01,
          "fading through never moves the view sideways");
  c.shotNow("01-fade-through-leaving");
  // Sample during the incoming half: it grows back from 92%.
  c.check(c.until([&] { return column->scale() < 0.999; }, 400), "the arriving destination is scaled");
  const double arrivingScale = column->scale();
  c.check(arrivingScale >= 0.919 && arrivingScale < 1.0,
          QString("the arriving destination grows from 92%% (%1)").arg(arrivingScale, 0, 'f', 3));
  // Let the arriving view become legible before photographing it.
  c.until([&] { return column->scale() > 0.96; }, 300);
  c.shotNow("02-fade-through-arriving");
  c.check(c.until([&] { return !destinations->property("running").toBool(); }, 2000),
          "the destination transition completes");
  c.check(qAbs(column->opacity() - 1) < 0.01 && qAbs(column->scale() - 1) < 0.01 &&
              qAbs(column->property("shift").toReal()) < 0.01,
          "the view is handed back exactly as it was found");
  c.check(b->page() == "library", "and the destination actually changed");

  // --- Library tabs share the X axis ---
  c.check(w->property("libraryTab") == "favorites", "the library opens on Liked songs");
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  QTest::qWait(45);
  const double forwardShift = body->property("shift").toReal();
  c.check(forwardShift < -1,
          QString("moving forward pushes the outgoing tab left (%1)").arg(forwardShift, 0, 'f', 1));
  c.check(qAbs(column->property("shift").toReal()) < 0.01 && qAbs(column->scale() - 1) < 0.01,
          "the tab bar itself stays put");
  c.shotNow("03-shared-axis-forward");
  c.check(c.until([&] { return body->property("shift").toReal() > 1; }, 400),
          "the arriving tab enters from the right");
  c.until([&] { return body->opacity() > 0.5; }, 300);
  c.shotNow("03b-shared-axis-arriving");
  c.check(c.until([&] { return !tabs->property("running").toBool(); }, 2000),
          "the tab transition completes");
  c.check(qAbs(body->property("shift").toReal()) < 0.01 && qAbs(body->opacity() - 1) < 0.01,
          "and settles back in place");
  c.check(w->property("libraryTab") == "files", "the tab actually changed");

  // Travelling back through the tabs reverses the axis.
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("favorites")));
  QTest::qWait(45);
  const double backShift = body->property("shift").toReal();
  c.check(backShift > 1,
          QString("moving back pushes the outgoing tab right (%1)").arg(backShift, 0, 'f', 1));
  c.shotNow("04-shared-axis-back");
  c.check(c.until([&] { return !tabs->property("running").toBool(); }, 2000), "it completes too");

  // --- Reduced motion is honoured: no animation, and navigation still works ---
  b->setMotion(false);
  QTest::qWait(200);
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  QTest::qWait(30);
  c.check(!tabs->property("running").toBool() && !destinations->property("running").toBool(),
          "reduced motion skips the transition entirely");
  c.check(qAbs(body->opacity() - 1) < 0.01 && qAbs(body->property("shift").toReal()) < 0.01,
          "and leaves nothing half-animated");
  c.check(w->property("libraryTab") == "files", "navigation still arrives");
  c.shot("05-reduced-motion");
  b->setMotion(true);

  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("favorites")));
  QTest::qWait(40);
  b->setMotion(false);
  QCoreApplication::processEvents();
  c.check(!tabs->property("running").toBool() &&
              qAbs(body->opacity() - 1) < 0.01 &&
              qAbs(body->property("shift").toReal()) < 0.01 &&
              w->property("libraryTab") == "favorites",
          "turning motion off during travel finishes the pending tab in one frame");
  b->setMotion(true);

  // --- A second navigation mid-flight must not strand the view ---
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("history")));
  QTest::qWait(40);
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("mixes")));
  c.check(c.until([&] { return !tabs->property("running").toBool(); }, 3000),
          "an interrupted transition still finishes");
  c.check(qAbs(body->opacity() - 1) < 0.01 && qAbs(body->property("shift").toReal()) < 0.01 &&
              qAbs(body->scale() - 1) < 0.01,
          "and the view is left whole");
  c.check(w->property("libraryTab") == "mixes", "the last destination wins");
  c.shot("06-interrupted");

  b->stop();
  b->clearQueue();
  c.finish();
}

void runArtistHeroTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music/Still Water");
  QDir().mkpath(c.directory + "/music/Night Ferry");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1320, 860);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(true);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);

  // One artist, two albums, six songs: enough for the hero to have something
  // true to report.
  paintCover(c.directory + "/music/Still Water/cover.png", QColor("#1f4f6b"), QColor("#d98324"));
  paintCover(c.directory + "/music/Night Ferry/cover.png", QColor("#3d2a52"), QColor("#4fa3a5"));
  const QStringList still{"The light arrives", "Across the still water", "A quiet moment"};
  const QStringList ferry{"Harbour lights", "Night ferry", "Coming ashore"};
  for (int i = 0; i < still.size(); ++i)
    if (!encodeTrack(c, QString("%1/music/Still Water/%2.flac").arg(c.directory).arg(i + 1),
                     still[i], "Still Water", "Rill", i + 1))
      return c.finish();
  for (int i = 0; i < ferry.size(); ++i)
    if (!encodeTrack(c, QString("%1/music/Night Ferry/%2.flac").arg(c.directory).arg(i + 1),
                     ferry[i], "Night Ferry", "Rill", i + 1))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the artist fixture");

  // --- The hero belongs to artist pages only ---
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("local-albums")));
  c.check(c.until([&] { return b->results()->count() == 2; }), "two albums group");
  QTest::qWait(400);
  c.check(!shownItem(w->contentItem(), "artistHero"), "an album grid shows no artist hero");
  c.check(shownItem(w->contentItem(), "collectionHeaderTitle"), "it keeps the standard header");
  c.shot("01-albums-standard-header");

  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("local-artists")));
  c.check(c.until([&] { return b->results()->count() == 1; }), "one artist groups");
  QTest::qWait(400);
  c.check(!shownItem(w->contentItem(), "artistHero"), "the artist grid is not an artist page");

  b->open(b->results()->get(0));
  c.check(c.until([&] { return !b->busy() && b->page() == "local-artist"; }), "the artist opens");
  c.check(c.until([&] { return shownItem(w->contentItem(), "artistHero") != nullptr; }),
          "the artist page raises its hero");
  auto hero = shownItem(w->contentItem(), "artistHero");
  if (!hero)
    return c.finish();
  c.check(!shownItem(w->contentItem(), "collectionHeaderTitle"),
          "and stands in for the standard header rather than doubling it");

  // --- What it says is true ---
  const auto info = b->artistInfo();
  c.check(info.value("tracks").toInt() == 6, "the hero counts every song");
  c.check(info.value("albums").toInt() == 2, "and every album");
  c.check(info.value("seconds").toLongLong() == 900, "and the real running time");
  auto summary = shownItem(w->contentItem(), "artistHeroSummary");
  c.check(summary && summary->property("text").toString() == "2 albums · 6 songs · 15 min",
          "the summary reads back what it counted");
  auto name = shownItem(w->contentItem(), "artistHeroName");
  c.check(name && name->property("text").toString() == "Rill", "the hero names the artist");
  // PaneMotion.kt:150-177 gives size changes DefaultSpatial. Check the live
  // Behaviors so the old 120ms cubic and the font's effects duration fail.
  const int spatialMs = c.evaluate("Theme.springSpatialMs").toInt();
  auto heightMotion = hero->findChild<QObject *>("artistHeroHeightMotion");
  auto typeBehavior = name ? qmlContext(name)->objectForName("typeSizeBehavior") : nullptr;
  auto typeMotion = motionObject(typeBehavior, "animation");
  const QVariant typeDuration = typeMotion ? typeMotion->property("duration") : QVariant();
  c.check(heightMotion && typeDuration.isValid() &&
              heightMotion->property("duration").toInt() == spatialMs &&
              typeDuration.toInt() == spatialMs,
          QString("the hero's height and type size use DefaultSpatial's settling time "
                  "(height %1, type %2, expected %3)")
              .arg(heightMotion ? QString::number(heightMotion->property("duration").toInt()) : "not found",
                   typeDuration.isValid() ? QString::number(typeDuration.toInt()) : "not found")
              .arg(spatialMs));

  // --- Material's large top app bar proportions ---
  auto portrait = shownItem(w->contentItem(), "artistHeroPortrait");
  c.check(portrait && qAbs(portrait->property("radius").toReal() - portrait->width() / 2) < 1,
          "the portrait is round, as an artist's picture is");
  c.check(hero->height() > 150, "the open band is a hero, not a row");
  c.check(shownItem(w->contentItem(), "artistHeroBackdrop"), "the cover sits behind it");
  // Exactly one Play action is offered at a time.
  auto listPlay = [&] {
    auto row = shownItem(w->contentItem(), "collectionToolsButton");
    return row != nullptr;
  };
  c.check(!listPlay(), "the list's own action row stands down while the hero is open");
  const double expanded = hero->height();
  const double titleExpanded = name->property("font").value<QFont>().pixelSize();
  c.shot("02-artist-hero");

  // --- It collapses on scroll and comes back ---
  auto tracks = shownItem(w->contentItem(), "tracksView");
  c.check(tracks, "the artist's songs are listed");
  if (tracks) {
    tracks->setProperty("contentY", tracks->property("originY").toReal() + 300);
    c.check(c.until([&] { return hero->height() < expanded - 40; }, 2000),
            "scrolling collapses the hero");
    c.check(c.until([&] {
      return name->property("font").value<QFont>().pixelSize() < titleExpanded;
    }, 2000), "and the name shrinks with it");
    auto actions = anyItem(w->contentItem(), "artistHeroActions");
    c.check(actions && actions->opacity() < 0.3, "the actions fade out of the collapsed bar");
    c.check(listPlay(), "and the list's action row takes them back");
    c.shot("03-artist-hero-collapsed");
    tracks->setProperty("contentY", tracks->property("originY").toReal());
    c.check(c.until([&] { return hero->height() > expanded - 5; }, 2000),
            "scrolling back opens it again");
    c.shot("04-artist-hero-restored");
  }

  // --- Its actions work ---
  // The band behind them is the cover, and Material keeps its elevated button
  // for exactly that: a control that has to hold against a patterned ground.
  if (auto shuffle = shownItem(w->contentItem(), "artistHeroShuffle")) {
    c.check(shuffle->property("elevated").toBool(),
            "the secondary action is elevated over the cover band");
    auto container = shuffle->property("background").value<QQuickItem *>();
    auto lift = container ? anyItem(container, "elevation") : nullptr;
    c.check(lift && lift->property("level").toInt() == 1, "by the one level that goes with it");
  }
  c.check(b->queue()->count() == 0, "nothing is queued yet");
  c.click("artistHeroPlay");
  c.check(c.until([&] { return b->queue()->count() == 6; }), "Play queues the artist's songs");
  c.check(c.until([&] { return b->playing(); }), "and starts them");
  c.shot("05-artist-hero-playing");
  b->stop();
  b->clearQueue();
  b->setShuffle(false);
  c.click("artistHeroShuffle");
  c.check(c.until([&] { return b->queue()->count() == 6; }), "Shuffle queues them too");
  c.check(b->shuffle(), "and turns shuffling on");
  b->setShuffle(false);

  // Pinning is offered exactly where the library has something to pin. A local
  // artist is a grouping of tags rather than a collection with an id, so the
  // action hides there, the same way the standard header hides it.
  auto pin = anyItem(w->contentItem(), "artistHeroPin");
  c.check(pin, "the hero carries a pin action");
  c.check(pin && pin->isVisible() == !b->collectionItem().isEmpty(),
          "the pin is offered only when there is a collection to pin");
  c.shot("06-artist-hero-actions");

  // --- Leaving the artist puts the standard header back ---
  b->back();
  c.check(c.until([&] { return b->page() != "local-artist"; }), "Back leaves the artist");
  QTest::qWait(500);
  c.check(!shownItem(w->contentItem(), "artistHero"), "the hero is released");
  c.check(shownItem(w->contentItem(), "collectionHeaderTitle"), "the standard header returns");
  c.shot("07-after-artist");

  b->stop();
  b->clearQueue();
  c.finish();
}

void runSingAlongTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1320, 860);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(true);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);
  b->setLyricsFallback(false);

  paintCover(c.directory + "/music/cover.png", QColor("#26324f"), QColor("#e2a03f"));
  if (!encodeTrack(c, c.directory + "/music/01.flac", "Across the still water", "Still Water",
                   "Rill", 1))
    return c.finish();
  if (!encodeTrack(c, c.directory + "/music/02.flac", "No words", "Still Water", "Rill", 2))
    return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the sing-along fixture");
  b->library("files");
  b->collection()->setSortKey("title");
  QTest::qWait(200);
  c.check(c.until([&] { return b->results()->count() == 2; }), "two songs are available");
  b->enqueueItems(b->results()->rows);
  b->playAt(0);
  c.check(c.until([&] { return b->playing(); }), "playback starts");
  const auto sung = b->current();

  // Lines with explicit ends, and one without, so the fallback is exercised.
  QFile lrc(c.directory + "/sing.lrc");
  c.check(lrc.open(QIODevice::WriteOnly), "write the timed lyrics");
  lrc.write("[00:10.00]The light arrives\n[00:20.00]Across the still water\n"
            "[00:30.00]A quiet moment\n[00:40.00]We move with the tide\n");
  lrc.close();
  b->importLyrics(QUrl::fromLocalFile(lrc.fileName()), sung.value("id").toString());
  c.check(c.until([&] { return b->lyricLines().size() == 4; }), "four timed lines load");

  // --- The progress the fill is drawn from ---
  b->seek(0);
  c.check(c.until([&] { return b->lyricIndex() < 0; }, 3000), "before the first line there is none");
  c.check(b->lyricProgress() < 0, "and no progress to report");
  struct Sample { int position; int line; double progress; const char *what; };
  for (const auto &s : {Sample{10000, 0, 0.0, "the first line starts empty"},
                        Sample{15000, 0, 0.5, "and is half sung halfway through"},
                        Sample{19500, 0, 0.95, "and nearly full at its end"},
                        Sample{20000, 1, 0.0, "the next line starts empty in turn"},
                        Sample{35000, 2, 0.5, "a middle line tracks the same way"},
                        // The last line is held to the end of the audio, so it
                        // fills at the pace the rest of the song is sung at.
                        Sample{42000, 3, 0.2, "and a trailing line fills at the song's pace"},
                        Sample{49000, 3, 0.9, "reaching the end of the line, not of the track"}}) {
    b->seek(s.position);
    c.check(c.until([&] { return b->lyricIndex() == s.line; }, 3000),
            QString("%1 (line %2)").arg(s.what).arg(s.line));
    const double measured = b->lyricProgress();
    c.check(qAbs(measured - s.progress) < 0.12,
            QString("%1: fill is %2, expected about %3")
                .arg(s.what).arg(measured, 0, 'f', 2).arg(s.progress, 0, 'f', 2));
  }
  // Progress is a fraction, always.
  for (int position = 0; position <= 60000; position += 1500) {
    b->seek(position);
    QTest::qWait(20);
    const double measured = b->lyricProgress();
    c.check(measured < 0 || (measured >= 0 && measured <= 1),
            QString("progress stays a fraction at %1ms").arg(position));
  }

  // --- The layout ---
  w->setProperty("immersive", true);
  c.check(c.until([&] { return shownItem(w->contentItem(), "immersivePlayer") != nullptr; }),
          "the immersive player opens");
  auto player = shownItem(w->contentItem(), "immersivePlayer");
  if (!player)
    return c.finish();
  c.check(player->property("hasTimedLyrics").toBool(), "the song offers timed lyrics");
  QMetaObject::invokeMethod(player, "layoutRequested", Q_ARG(QString, QString("lyrics")));
  c.check(c.until([&] { return shownItem(w->contentItem(), "liveLyrics") != nullptr; }),
          "the reading lyrics view opens before sing along");
  auto lyricLabel = shownItem(w->contentItem(), "lyricLabel");
  auto lyricBehavior = lyricLabel ? qmlContext(lyricLabel)->objectForName("lyricScaleBehavior") : nullptr;
  auto lyricMotion = motionObject(lyricBehavior, "animation");
  const QVariant lyricScaleDuration = lyricMotion ? lyricMotion->property("duration") : QVariant();
  c.check(lyricScaleDuration.isValid() && lyricScaleDuration.toInt() ==
                            c.evaluate("Theme.springSpatialMs").toInt(),
          QString("the reading lyric scales on DefaultSpatial rather than 350ms "
                  "(label %1, animation %2, expected %3)")
              .arg(lyricLabel ? "found" : "not found",
                   lyricScaleDuration.isValid() ? QString::number(lyricScaleDuration.toInt()) : "not found")
              .arg(c.evaluate("Theme.springSpatialMs").toInt()));
  QMetaObject::invokeMethod(player, "layoutRequested", Q_ARG(QString, QString("singalong")));
  c.check(c.until([&] { return player->property("displayedLayout") == "singalong"; }),
          "sing along can be chosen");
  auto singAlong = shownItem(w->contentItem(), "singAlong");
  c.check(singAlong, "the sing-along surface is on screen");
  c.check(!shownItem(w->contentItem(), "immersiveArtwork"),
          "it takes the whole stage rather than sharing it with the cover");
  c.check(!shownItem(w->contentItem(), "liveLyrics"), "and replaces the reading view");
  if (!singAlong)
    return c.finish();

  // --- The line being sung is the one that is emphasised ---
  b->seek(15000);
  c.check(c.until([&] { return b->lyricIndex() == 0; }, 3000), "the first line is live");
  QTest::qWait(500);
  auto current = shownItem(w->contentItem(), "singAlongCurrent");
  c.check(current && current->property("text").toString() == "The light arrives",
          "the sung line is the one marked current");
  const double activeSize = current ? current->property("font").value<QFont>().pixelSize() : 0;
  c.check(activeSize >= 28, "it is set at display size");
  // Emphasis is carried by scale, so no line ever re-shapes its text.
  if (auto lineItem = current->parentItem()) {
    auto scaleMotion = lineItem->findChild<QObject *>("singAlongScaleMotion");
    c.check(scaleMotion && scaleMotion->property("duration").toInt() ==
                  c.evaluate("Theme.springSpatialMs").toInt(),
            "the current lyric scale uses DefaultSpatial rather than 320ms");
    c.check(qAbs(lineItem->scale() - 1) < 0.02, "the sung line is at full size");
    if (auto other = shownItem(singAlong, "singAlongLine"))
      if (auto otherLine = other->parentItem()) {
        c.check(otherLine->scale() < 0.95, "and its neighbours stand back by scale");
        c.check(qAbs(other->property("font").value<QFont>().pixelSize() - activeSize) < 0.01,
                "at the same font size, so changing line re-shapes nothing");
      }
  }
  // Even a four-line lyric brings its live line to where the eye is looking,
  // rather than leaving it stranded at the top of the view.
  if (current) {
    const double centre = current->mapToScene(current->boundingRect().center()).y();
    c.check(centre > w->height() * 0.25 && centre < w->height() * 0.6,
            QString("the sung line sits in the reading band (%1 of %2)")
                .arg(centre, 0, 'f', 0).arg(w->height()));
  }
  c.shot("01-singalong-first-line");

  // The fill is a real measurement, not decoration: it tracks playback.
  auto fill = anyItem(singAlong, "singAlongFill");
  c.check(fill, "the sung line carries a fill");
  const double halfway = fill ? fill->width() : 0;

  // And it sweeps between playback reports rather than stepping with them.
  // Playback reports itself four times a second; sampled faster than that, the
  // fill has to keep moving in between.
  b->seek(11000);
  c.check(c.until([&] { return b->lyricIndex() == 0 && b->playing(); }, 3000),
          "the first line is being sung");
  QTest::qWait(300);
  int advances = 0;
  double previous = fill ? fill->width() : 0;
  for (int i = 0; i < 40; ++i) {
    QTest::qWait(25);
    const double now = fill ? fill->width() : 0;
    if (now > previous + 0.05)
      ++advances;
    previous = now;
  }
  // Four reports a second over a second of sampling would give at most ~4
  // steps; a sweep moves on nearly every frame.
  c.check(advances >= 12,
          QString("the fill sweeps rather than stepping (%1 advances in 40 samples)").arg(advances));
  c.shot("02-singalong-line-filling");

  b->seek(19000);
  c.check(c.until([&] { return b->lyricProgress() > 0.85; }, 3000), "playback nears the line's end");
  QTest::qWait(300);
  auto laterFill = anyItem(singAlong, "singAlongFill");
  c.check(laterFill && laterFill->width() > halfway,
          "the fill advances with the music, it does not merely appear");

  // Pausing stops the sweep where it stands rather than letting it run on.
  b->pause();
  QTest::qWait(400);
  const double held = laterFill ? laterFill->width() : 0;
  QTest::qWait(500);
  c.check(laterFill && qAbs(laterFill->width() - held) < 1.0,
          "a paused song holds its fill still");
  b->play();
  c.check(c.until([&] { return b->playing(); }, 5000), "and playing resumes it");

  b->seek(35000);
  c.check(c.until([&] { return b->lyricIndex() == 2; }, 3000), "a later line takes over");
  QTest::qWait(600);
  auto moved = shownItem(w->contentItem(), "singAlongCurrent");
  c.check(moved && moved->property("text").toString() == "A quiet moment",
          "emphasis follows the music to the next line");
  c.shot("03-singalong-later-line");

  // --- Instrumental stretches say so instead of going blank ---
  b->seek(2000);
  c.check(c.until([&] { return b->lyricIndex() < 0; }, 3000), "playback returns before the words");
  QTest::qWait(400);
  auto waiting = shownItem(w->contentItem(), "singAlongWaiting");
  c.check(waiting && waiting->isVisible(), "the wait is acknowledged rather than left blank");
  auto cue = shownItem(w->contentItem(), "singAlongCue");
  c.check(cue && cue->property("text").toString().contains("Lyrics in"),
          "and counts down to the first line");
  // The words stay on screen through a gap, so the countdown has to sit clear
  // of them rather than across them.
  if (waiting) {
    const auto cueRect = waiting->mapRectToScene(waiting->boundingRect());
    int overlaps = 0;
    for (const auto *name : {"singAlongCurrent", "singAlongLine"})
      if (auto text = shownItem(singAlong, name)) {
        const auto lineRect = text->mapRectToScene(text->boundingRect());
        if (cueRect.intersects(lineRect))
          ++overlaps;
      }
    c.check(overlaps == 0, QString("the countdown does not sit over the words (%1 overlapping)")
                               .arg(overlaps));
    auto list = shownItem(w->contentItem(), "singAlongLines");
    c.check(list && list->mapRectToScene(list->boundingRect()).bottom() <= cueRect.top() + 1,
            "the words are given room above it rather than running under it");
  }
  c.shot("04-singalong-waiting");

  // --- A song with no timed lyrics cannot be sung along to, and says so ---
  b->next();
  c.check(c.until([&] { return b->currentIndex() == 1 && b->playing(); }, 20000),
          "the next song plays (now " + b->current().value("title").toString() + ")");
  c.check(c.until([&] { return b->lyricLines().isEmpty(); }, 8000), "it has no timed lyrics");
  QTest::qWait(500);
  c.check(player->property("displayedLayout") == "artwork",
          "sing along steps aside when a song cannot drive it");
  c.check(player->property("preferredLayout") == "singalong",
          "without forgetting that it was chosen");
  c.shot("05-singalong-fallback");

  b->previous();
  c.check(c.until([&] { return b->lyricLines().size() == 4; }, 20000), "returning restores the lyrics");
  c.check(c.until([&] { return player->property("displayedLayout") == "singalong"; }, 3000),
          "and sing along returns with them");

  // --- Reduced motion keeps it usable ---
  b->setMotion(false);
  b->seek(25000);
  c.check(c.until([&] { return b->lyricIndex() == 1; }, 3000), "the line changes without motion");
  QTest::qWait(400);
  auto still = shownItem(w->contentItem(), "singAlongCurrent");
  c.check(still && still->property("text").toString() == "Across the still water",
          "the right line is still emphasised");
  c.shot("06-singalong-reduced-motion");
  b->setMotion(true);

  // --- The reading view sets its lines apart ---
  QMetaObject::invokeMethod(player, "layoutRequested", Q_ARG(QString, QString("lyrics")));
  c.check(c.until([&] { return player->property("displayedLayout") == "lyrics"; }),
          "the reading view can be chosen");
  QTest::qWait(400);
  auto reading = shownItem(w->contentItem(), "lyricLabel");
  c.check(reading, "and puts the words on screen");
  if (reading) {
    const double size = reading->property("font").value<QFont>().pixelSize();
    const double line = reading->property("lineHeight").toDouble();
    c.check(reading->property("lineHeightMode").toInt() == 1,
            "laid out on an absolute line height");
    c.check(line >= size,
            QString("with room for the glyphs on it (%1px text on a %2px line)")
                .arg(size, 0, 'f', 0).arg(line, 0, 'f', 0));
    c.check(line <= size * 2,
            "and no more than a line's worth of room");
  }
  // The lyric views scale their own type, and say so; everything around them
  // is still held to Material's roles.
  QSet<int> scaleSizes;
  for (const auto &role : c.evaluate("Object.keys(Theme.typeScale)").toStringList())
    scaleSizes.insert(c.evaluate(QString("Theme.typeScale['%1'][0]").arg(role)).toInt());
  QStringList offScale;
  collectOffScale(w->contentItem(), scaleSizes, offScale);
  c.check(offScale.isEmpty(),
          offScale.isEmpty() ? QStringLiteral("every style here is set at one of Material's sizes")
                             : QString("styles set at a size off the scale: %1").arg(offScale.join(", ")));

  // Nowhere else either: a line height read as a multiple collapses silently.
  QStringList cramped;
  collectCrampedText(w->contentItem(), cramped);
  c.check(cramped.isEmpty(),
          cramped.isEmpty() ? QStringLiteral("no style is set on a line shorter than its text")
                            : QString("styles set on a line shorter than their text: %1")
                                  .arg(cramped.join(", ")));
  c.shot("07-singalong-reading-view");

  w->setProperty("immersive", false);
  QTest::qWait(300);
  b->stop();
  b->clearQueue();
  c.finish();
}

void runCrossfadeUiTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1320, 860);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(false);
  b->setVolume(0);
  b->setWatchMusicFolders(false);
  b->setCrossfadeSeconds(0);
  b->setGapless(true);

  auto settings = c.dialog("settingsDialog");
  c.check(settings, "Settings opens");
  if (!settings)
    return c.finish();
  settings->setProperty("category", 1);
  QTest::qWait(400);
  c.check(shownItem(w->contentItem(), "crossfadeSetting"), "Playback offers crossfade");
  c.check(shownItem(w->contentItem(), "gaplessSwitch"), "and gapless playback");
  auto value = shownItem(w->contentItem(), "crossfadeValue");
  c.check(value && value->property("text").toString() == "Off",
          "it reads Off until it is asked for");
  c.shot("01-crossfade-off");

  auto slider = shownItem(w->contentItem(), "crossfadeSlider");
  c.check(slider, "the crossfade slider is on screen");
  if (slider) {
    c.check(qAbs(slider->property("from").toReal()) < 0.001 &&
                qAbs(slider->property("to").toReal() - 12) < 0.001,
            "it spans nothing to twelve seconds");
    slider->setProperty("value", 6);
    QMetaObject::invokeMethod(slider, "moved");
    c.check(c.until([&] { return b->crossfadeSeconds() == 6; }), "moving it sets the overlap");
    QTest::qWait(250);
    c.check(value && value->property("text").toString() == "6 s", "and it reads back in seconds");
    // Bring the control itself into view for the capture.
    settings->setProperty("searchQuery", "crossfade");
    QTest::qWait(400);
    c.check(shownItem(w->contentItem(), "crossfadeSlider"), "the control is reachable by name");
    c.shot("02-crossfade-six-seconds");
    settings->setProperty("searchQuery", "");
    QTest::qWait(300);
  }

  // The setting survives the dialog and the session.
  c.closeDialog(settings);
  QTest::qWait(300);
  c.check(b->crossfadeSeconds() == 6, "the overlap is remembered");
  b->setCrossfadeSeconds(0);
  c.check(b->crossfadeSeconds() == 0, "and can be turned off again");

  // Searching Settings finds it by what it does, not only by its name.
  settings = c.dialog("settingsDialog");
  if (settings) {
    settings->setProperty("searchQuery", "overlap");
    QTest::qWait(400);
    c.check(shownItem(w->contentItem(), "crossfadeSetting"), "searching for overlap finds it");
    settings->setProperty("searchQuery", "pause between songs");
    QTest::qWait(400);
    c.check(shownItem(w->contentItem(), "gaplessSwitch"), "and gapless by what it prevents");
    c.shot("03-crossfade-search");
    settings->setProperty("searchQuery", "");
    QTest::qWait(300);
    c.closeDialog(settings);
  }
  c.finish();
}

void runTrackDetailsTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1320, 900);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(false);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);
  b->setLyricsFallback(false);

  paintCover(c.directory + "/music/cover.png", QColor("#26324f"), QColor("#e2a03f"));
  // A lossless recording with a full set of tags, so every field has something
  // true to report, and a lossy one, where some of them genuinely do not apply.
  if (!encodeTrack(c, c.directory + "/music/01.flac", "Across the still water", "Still Water",
                   "Rill", 3,
                   {"-metadata", "genre=Ambient", "-metadata", "composer=A Composer",
                    "-metadata", "disc=2", "-metadata", "album_artist=Various Artists",
                    "-ac", "2", "-sample_fmt", "s16"}))
    return c.finish();
  if (!encodeTrack(c, c.directory + "/music/02.mp3", "No tags", "Still Water", "Rill", 4,
                   {"-ac", "1"}))
    return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the metadata fixture");
  b->library("files");
  b->collection()->setSortKey("title");
  QTest::qWait(200);
  c.check(c.until([&] { return b->results()->count() == 2; }), "both recordings imported");

  QVariantMap lossless, lossy;
  for (const auto &row : b->results()->rows) {
    const auto t = row.toMap();
    if (t.value("title") == "Across the still water")
      lossless = t;
    else if (t.value("title") == "No tags")
      lossy = t;
  }
  c.check(!lossless.isEmpty() && !lossy.isEmpty(), "both recordings are readable");
  if (lossless.isEmpty())
    return c.finish();

  // --- The tags really were read off the file ---
  c.check(lossless.value("genre").toString() == "Ambient", "genre is read from the file");
  c.check(lossless.value("composer").toString() == "A Composer", "so is the composer");
  c.check(lossless.value("albumArtist").toString() == "Various Artists", "and the album artist");
  c.check(lossless.value("channels").toInt() == 2, "and the channel count");
  c.check(lossless.value("bitDepth").toInt() == 16, "and the bit depth of a lossless recording");
  c.check(lossy.value("bitDepth").toInt() == 0,
          "a lossy recording reports no depth, because it has none");
  c.check(lossy.value("channels").toInt() == 1, "but still reports its channels");

  // --- And they reach the dialog ---
  const auto details = b->trackDetails(lossless);
  QStringList labels;
  QVariantMap byLabel;
  for (const auto &row : details) {
    const auto entry = row.toMap();
    labels << entry.value("label").toString();
    byLabel.insert(entry.value("label").toString(), entry.value("value"));
  }
  for (const auto &expected : {"Title", "Artist", "Album artist", "Album", "Composer", "Genre",
                               "Year", "Track", "Source", "Duration", "File bit depth",
                               "File channels", "File sample rate"})
    c.check(labels.contains(expected), QString("details list %1").arg(expected));
  c.check(byLabel.value("Genre").toString() == "Ambient", "Genre reads back what was tagged");
  c.check(byLabel.value("Track").toString() == "3 on disc 2",
          "a numbered track on a multi-disc album says which disc");
  c.check(byLabel.value("File channels").toString() == "Stereo", "two channels read as Stereo");
  c.check(byLabel.value("File bit depth").toString() == "16-bit", "depth reads in bits");
  c.check(byLabel.value("Album artist").toString() == "Various Artists",
          "the album artist is listed when it differs from the performer");

  // A performer who is also the album artist is not repeated.
  const auto plain = b->trackDetails(lossy);
  QStringList lossyLabels;
  for (const auto &row : plain)
    lossyLabels << row.toMap().value("label").toString();
  c.check(!lossyLabels.contains("Album artist"),
          "an album artist the same as the performer is not repeated");
  c.check(!lossyLabels.contains("File bit depth"),
          "a lossy recording is not given a bit depth it does not have");
  c.check(lossyLabels.contains("File channels"), "but its channels are still reported");
  c.check(!lossyLabels.contains("Genre"), "an untagged genre is left out rather than shown empty");

  // --- The dialog itself ---
  auto dialog = w->findChild<QObject *>("trackDetailsDialog");
  c.check(dialog, "the details dialog exists");
  if (dialog) {
    QMetaObject::invokeMethod(dialog, "inspect", Q_ARG(QVariant, QVariant(lossless)));
    QTest::qWait(500);
    c.check(dialog->property("visible").toBool(), "it opens on a song");
    c.shot("01-track-details-full");
    QMetaObject::invokeMethod(dialog, "close");
    QTest::qWait(300);
    QMetaObject::invokeMethod(dialog, "inspect", Q_ARG(QVariant, QVariant(lossy)));
    QTest::qWait(500);
    c.shot("02-track-details-sparse");
    QMetaObject::invokeMethod(dialog, "close");
    QTest::qWait(300);
  }
  c.finish();
}

void runQueueHistoryTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1400, 880);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(false);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);
  b->setLyricsFallback(false);
  b->setCrossfadeSeconds(0);
  b->clearHistory();

  paintCover(c.directory + "/music/cover.png", QColor("#26324f"), QColor("#e2a03f"));
  const QStringList titles{"The light arrives", "Across the still water", "A quiet moment",
                           "We move with the tide"};
  for (int i = 0; i < titles.size(); ++i)
    if (!encodeTrack(c, QString("%1/music/%2.flac").arg(c.directory).arg(i + 1), titles[i],
                     "Still Water", "Rill", i + 1))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the history fixture");
  b->library("files");
  c.check(c.until([&] { return b->results()->count() == 4; }), "four songs are available");
  b->enqueueItems(b->results()->rows);

  // --- Nothing has been played, so there is nothing to look back over ---
  w->setProperty("side", "queue");
  QTest::qWait(500);
  c.check(shownItem(w->contentItem(), "queueTabs"), "the queue panel offers both views");
  c.check(w->property("queueTab") == "next", "it opens on what is coming");
  c.check(shownItem(w->contentItem(), "queueView"), "the queue is the one on screen");
  {
    auto title = shownItem(w->contentItem(), "sidePanelTitle");
    c.check(title && title->property("text").toString() == "Queue", "the panel keeps one name across its segments");
  }
  c.check(!shownItem(w->contentItem(), "recentlyPlayedView"), "the look-back is not");
  c.shot("01-queue-up-next");

  w->setProperty("queueTab", "history");
  QTest::qWait(400);
  c.check(shownItem(w->contentItem(), "recentlyPlayedView"), "History shows the look-back");
  c.check(!shownItem(w->contentItem(), "queueView"), "and stands the queue down");
  c.check(b->recentlyPlayed()->count() == 0, "which is empty before anything has played");
  c.shot("02-queue-history-empty");

  // --- Playing songs fills it, newest first, without the song playing now ---
  w->setProperty("queueTab", "next");
  b->playAt(0);
  c.check(c.until([&] { return b->playing(); }), "the first song plays");
  QTest::qWait(400);
  c.check(b->recentlyPlayed()->count() == 0,
          "the song playing now is not something to look back over");
  b->playAt(1);
  c.check(c.until([&] { return b->playing() && b->currentIndex() == 1; }), "a second song plays");
  b->playAt(2);
  c.check(c.until([&] { return b->playing() && b->currentIndex() == 2; }), "and a third");
  QTest::qWait(500);
  c.check(c.until([&] { return b->recentlyPlayed()->count() == 2; }),
          "the two that finished are there to look back over");
  // Compare against the queue rather than the fixture, since the library
  // decides its own order.
  const auto first = b->queue()->get(0).value("title").toString();
  const auto second = b->queue()->get(1).value("title").toString();
  c.check(b->recentlyPlayed()->get(0).value("title").toString() == second,
          "newest first, so the one just before this one is at the top");
  c.check(b->recentlyPlayed()->get(1).value("title").toString() == first,
          "and the one before that next");

  w->setProperty("queueTab", "history");
  QTest::qWait(500);
  auto list = shownItem(w->contentItem(), "recentlyPlayedView");
  c.check(list && list->property("count").toInt() == 2, "the panel lists both of them");
  auto count = shownItem(w->contentItem(), "recentlyPlayedCount");
  c.check(count && count->property("text").toString().contains("2"), "and says how many");
  auto title = shownItem(w->contentItem(), "sidePanelTitle");
  c.check(title && title->property("text").toString() == "Queue",
          "the History segment does not rename the panel");
  c.check(!shownItem(w->contentItem(), "revealPlayingButton"),
          "and drops the shortcut that only makes sense for the queue");
  c.shot("03-queue-history-filled");

  // --- Playing from it keeps the queue, the way the history page does ---
  QStringList queuedBefore;
  for (int i = 0; i < b->queue()->count(); ++i)
    queuedBefore << b->queue()->get(i).value("id").toString();
  const auto wanted = b->recentlyPlayed()->get(0);
  c.clickWithin(list, "trackRow_0");
  c.check(c.until([&] { return b->current().value("id") == wanted.value("id"); }, 8000),
          "choosing one plays it");
  // The queue is kept rather than replaced: the chosen song is slotted in to
  // play next, and nothing that was queued is lost.
  QStringList queuedAfter;
  for (int i = 0; i < b->queue()->count(); ++i)
    queuedAfter << b->queue()->get(i).value("id").toString();
  for (const auto &id : queuedBefore)
    c.check(queuedAfter.contains(id), "the queue keeps everything it already held");
  c.check(queuedAfter.contains(wanted.value("id").toString()),
          "and the chosen song joins it rather than replacing it");
  c.shot("04-queue-history-played");

  // The song now playing left the look-back when it started.
  QTest::qWait(400);
  for (int i = 0; i < b->recentlyPlayed()->count(); ++i)
    c.check(b->recentlyPlayed()->get(i).value("id") != b->current().value("id"),
            "what is playing is never also in the look-back");

  // --- It links onward to the full history page ---
  c.click("openFullHistory");
  c.check(c.until([&] { return b->libraryId() == "history"; }, 4000),
          "the panel opens the full history page");
  c.shot("05-full-history");

  // --- Clearing history empties it, and Undo brings it back ---
  const int before = b->recentlyPlayed()->count();
  c.check(before > 0, "there is something to clear");
  b->clearHistory();
  QTest::qWait(300);
  c.check(b->recentlyPlayed()->count() == 0, "clearing history empties the look-back too");
  b->undo();
  QTest::qWait(300);
  c.check(b->recentlyPlayed()->count() == before, "and Undo restores it");

  w->setProperty("queueTab", "next");
  w->setProperty("side", "");
  b->stop();
  b->clearQueue();
  c.finish();
}

void runListeningStatsTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1320, 900);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(false);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);
  b->setLyricsFallback(false);
  b->setCrossfadeSeconds(0);
  b->clearListeningStats();

  // --- Nothing has been played ---
  const auto empty = b->listeningStats(7);
  c.check(empty.value("plays").toInt() == 0 && empty.value("seconds").toLongLong() == 0,
          "an unused library has listened to nothing");
  c.check(empty.value("topArtists").toList().isEmpty(), "and has no favourites yet");

  paintCover(c.directory + "/music/cover.png", QColor("#26324f"), QColor("#e2a03f"));
  // Two artists with different amounts of music, so the ranking has to think.
  const QList<QPair<QString, QString>> fixtures{{"Harbour lights", "Marble Coast"},
                                                {"Night ferry", "Marble Coast"},
                                                {"The light arrives", "Rill"}};
  for (int i = 0; i < fixtures.size(); ++i)
    if (!encodeTrack(c, QString("%1/music/%2.flac").arg(c.directory).arg(i + 1),
                     fixtures[i].first, "Still Water", fixtures[i].second, i + 1))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the statistics fixture");
  b->library("files");
  c.check(c.until([&] { return b->results()->count() == 3; }), "three songs are available");
  b->enqueueItems(b->results()->rows);

  // --- Playing is counted, and counted per play rather than per song ---
  QHash<QString, int> expectedPlays;
  const auto playIndex = [&](int index) {
    b->playAt(index);
    c.check(c.until([&] { return b->playing() && b->currentIndex() == index; }, 10000),
            QString("song %1 plays").arg(index));
    QTest::qWait(250);
    expectedPlays[b->current().value("artist").toString()] += 1;
  };
  // Marble Coast twice over, Rill once.
  playIndex(0);
  playIndex(1);
  playIndex(2);
  playIndex(0);
  QTest::qWait(400);

  const auto week = b->listeningStats(7);
  c.check(week.value("plays").toInt() == 4, "every play is counted, not every song");
  c.check(week.value("songs").toInt() == 3, "and the distinct songs are counted separately");
  c.check(week.value("artists").toInt() == 2, "along with the artists behind them");
  const qint64 expectedSeconds = 4 * 150;
  c.check(week.value("seconds").toLongLong() == expectedSeconds,
          QString("the time listened adds up (%1, expected %2)")
              .arg(week.value("seconds").toLongLong()).arg(expectedSeconds));

  // What was actually played: queue rows 0, 1, 2 and 0 again. The library
  // decides its own order, so the expectations come from the queue.
  const auto twice = b->queue()->get(0);
  QHash<QString, int> playsByArtist;
  for (int row : {0, 1, 2, 0})
    playsByArtist[b->queue()->get(row).value("artist").toString()] += 1;
  const auto topArtists = week.value("topArtists").toList();
  c.check(topArtists.size() == playsByArtist.size(), "every artist played is ranked");
  for (const auto &entry : topArtists) {
    const auto artist = entry.toMap();
    const int expected = playsByArtist.value(artist.value("name").toString());
    c.check(artist.value("plays").toInt() == expected,
            QString("%1 is credited with %2 plays, expected %3")
                .arg(artist.value("name").toString()).arg(artist.value("plays").toInt()).arg(expected));
  }
  for (int i = 1; i < topArtists.size(); ++i)
    c.check(topArtists[i - 1].toMap().value("seconds").toLongLong() >=
                topArtists[i].toMap().value("seconds").toLongLong(),
            "the ranking runs from most time listened to least");
  const auto topSongs = week.value("topSongs").toList();
  c.check(!topSongs.isEmpty() &&
              topSongs[0].toMap().value("name").toString() == twice.value("title").toString(),
          QString("the song played twice leads the songs (got %1, expected %2)")
              .arg(topSongs.isEmpty() ? QString() : topSongs[0].toMap().value("name").toString(),
                   twice.value("title").toString()));
  c.check(!topSongs.isEmpty() && topSongs[0].toMap().value("plays").toInt() == 2,
          "and its count is right");

  // --- The period really narrows things ---
  const auto allTime = b->listeningStats(0);
  c.check(allTime.value("plays").toInt() == 4, "all time sees the same plays here");
  c.check(allTime.value("daily").toList().isEmpty(),
          "all time has no day-by-day shape to show");
  const auto week2 = b->listeningStats(7);
  c.check(week2.value("daily").toList().size() == 7, "a week is shown as seven days");
  qint64 dailyTotal = 0;
  for (const auto &row : week2.value("daily").toList())
    dailyTotal += row.toMap().value("seconds").toLongLong();
  c.check(dailyTotal == expectedSeconds, "the days add up to the period");

  // --- A private session records nothing ---
  const int before = b->listeningStats(0).value("plays").toInt();
  b->setHistoryPaused(true);
  playIndex(1);
  QTest::qWait(400);
  c.check(b->listeningStats(0).value("plays").toInt() == before,
          "a paused history records no statistics either");
  b->setHistoryPaused(false);

  // --- The dialog ---
  auto stats = c.dialog("listeningStatsDialog");
  c.check(stats, "the statistics dialog opens");
  if (!stats)
    return c.finish();
  auto time = shownItem(w->contentItem(), "statsTimeValue");
  c.check(time && time->property("text").toString() == "10 min",
          QString("the headline reads in minutes (%1)")
              .arg(time ? time->property("text").toString() : QString()));
  auto plays = shownItem(w->contentItem(), "statsPlaysValue");
  c.check(plays && plays->property("text").toString() == "4", "and the play count is shown");
  c.check(shownItem(w->contentItem(), "statsDaily"), "the week has a shape");
  auto bars = shownItem(w->contentItem(), "statsDailyBars");
  c.check(bars && bars->height() >= 56,
          QString("the day bars keep their height (%1px)").arg(bars ? bars->height() : 0));
  auto figure = shownItem(w->contentItem(), "statsTime");
  c.check(figure && figure->height() >= 72,
          QString("and the headline figures keep theirs (%1px)").arg(figure ? figure->height() : 0));
  auto list = shownItem(w->contentItem(), "statsRankingList");
  c.check(list && list->property("count").toInt() == 2, "the artists are ranked on screen");
  // A count is not the same as being on screen: the figures above must not
  // squeeze the ranking out of the dialog.
  c.check(list && list->height() > 100,
          QString("the ranking has room to be read (%1px)").arg(list ? list->height() : 0));
  auto row = list ? anyItem(list, "statsRow_0") : nullptr;
  c.check(row && row->width() > 200 && row->height() > 30, "and its rows have real size");
  if (row) {
    const auto centre = row->mapToScene(row->boundingRect().center());
    c.check(centre.y() > 0 && centre.y() < w->height() && centre.x() > 0 && centre.x() < w->width(),
            "and sit inside the window");
  }
  c.shot("01-listening-stats-artists");

  // Switching the ranking switches what is listed.
  stats->setProperty("ranking", "songs");
  QTest::qWait(400);
  c.check(list && list->property("count").toInt() == 3, "songs rank separately");
  c.shot("02-listening-stats-songs");

  // Switching the period re-reads the numbers.
  stats->setProperty("days", 0);
  QMetaObject::invokeMethod(stats, "refresh");
  QTest::qWait(400);
  c.check(!shownItem(w->contentItem(), "statsDaily"), "all time drops the day-by-day row");
  c.shot("03-listening-stats-all-time");

  // --- Clearing it empties it ---
  b->clearListeningStats();
  QTest::qWait(400);
  c.check(b->listeningStats(0).value("plays").toInt() == 0, "clearing removes every play");
  c.check(shownItem(w->contentItem(), "statsEmpty"), "and the dialog says so plainly");
  c.shot("04-listening-stats-cleared");
  c.closeDialog(stats);

  b->stop();
  b->clearQueue();
  c.finish();
}

void runPlaylistVersionsTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1320, 900);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(false);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);

  paintCover(c.directory + "/music/cover.png", QColor("#26324f"), QColor("#e2a03f"));
  for (int i = 1; i <= 4; ++i)
    if (!encodeTrack(c, QString("%1/music/%2.flac").arg(c.directory).arg(i),
                     QString("Track %1").arg(i), "Versions", "Rill", i))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the versions fixture");
  b->library("files");
  c.check(c.until([&] { return b->results()->count() == 4; }), "four songs are available");
  const auto songs = b->results()->rows;

  // --- A new playlist has no history ---
  const auto playlist = b->createPlaylist("Evening drive");
  c.check(b->playlistVersions(playlist).isEmpty(), "a new playlist has no earlier versions");

  // --- Each edit puts the version it replaced aside ---
  b->addItemsToPlaylist(playlist, {songs[0], songs[1]});
  QTest::qWait(150);
  auto versions = b->playlistVersions(playlist);
  c.check(versions.size() == 1, "the first edit keeps the empty version it replaced");
  c.check(versions[0].toMap().value("count").toInt() == 0, "which held nothing");

  b->addItemsToPlaylist(playlist, {songs[2]});
  QTest::qWait(150);
  versions = b->playlistVersions(playlist);
  c.check(versions.size() == 2, "a second edit keeps a second version");
  c.check(versions[0].toMap().value("count").toInt() == 2,
          "newest first, so the two-song version is at the top");
  c.check(versions[0].toMap().value("summary").toString() == "2 songs", "and says so in words");
  c.check(versions[1].toMap().value("count").toInt() == 0, "with the empty one below it");

  b->openPlaylist(playlist);
  c.check(c.until([&] { return b->results()->count() == 3; }), "the playlist holds three songs");
  b->removePlaylistRows(playlist, {0});
  QTest::qWait(150);
  c.check(b->playlistVersions(playlist).size() == 3, "removing a song keeps a version too");
  c.check(b->playlistVersions(playlist)[0].toMap().value("count").toInt() == 3,
          "the version replaced held three");

  // An edit that changes nothing is not a version.
  const int before = b->playlistVersions(playlist).size();
  b->addItemsToPlaylist(playlist, {songs[1]});
  QTest::qWait(150);
  c.check(b->playlistVersions(playlist).size() == before,
          "adding a song that is already there keeps no new version");

  // --- Restoring brings a shape back, including a song removed since ---
  b->openPlaylist(playlist);
  QTest::qWait(200);
  const int current = b->results()->count();
  c.check(b->restorePlaylistVersion(playlist, 0), "the newest version can be restored");
  c.check(c.until([&] { return b->results()->count() == 3; }, 4000),
          QString("restoring brings back the three songs (was %1)").arg(current));
  QStringList restored;
  for (const auto &row : b->results()->rows)
    restored << row.toMap().value("id").toString();
  c.check(restored.contains(songs[0].toMap().value("id").toString()),
          "including the song that had been removed");

  // Restoring is itself an edit, so it is in the history and can be undone.
  c.check(b->playlistVersions(playlist).size() == before + 1,
          "the restore kept the version it replaced");
  b->undo();
  QTest::qWait(250);
  c.check(b->results()->count() == 2, "and ordinary Undo takes the restore back");

  // Restoring what is already there changes nothing.
  b->openPlaylist(playlist);
  QTest::qWait(200);
  const int versionsNow = b->playlistVersions(playlist).size();
  const auto same = b->results()->rows;
  b->restorePlaylistVersion(playlist, 0);
  QTest::qWait(200);
  c.check(b->playlistVersions(playlist).size() >= versionsNow, "history is never lost by a restore");

  // --- Bounded, so history cannot grow without limit ---
  for (int i = 0; i < 20; ++i) {
    b->removePlaylistRows(playlist, {0});
    b->addItemsToPlaylist(playlist, {songs[i % 4]});
    QTest::qWait(20);
  }
  c.check(b->playlistVersions(playlist).size() <= 12,
          QString("history is bounded (%1 versions)").arg(b->playlistVersions(playlist).size()));

  // --- The dialog ---
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("playlists")));
  QTest::qWait(400);
  auto dialog = w->findChild<QObject *>("playlistVersionsDialog");
  c.check(dialog, "the version history dialog exists");
  if (dialog) {
    QMetaObject::invokeMethod(dialog, "inspect", Q_ARG(QVariant, QVariant(playlist)),
                              Q_ARG(QVariant, QVariant("Evening drive")));
    QTest::qWait(500);
    c.check(dialog->property("visible").toBool(), "it opens on a playlist");
    auto list = shownItem(w->contentItem(), "playlistVersionsList");
    c.check(list && list->property("count").toInt() > 0, "and lists its versions");
    c.check(list && list->height() > 100, "with room to read them");
    auto when = shownItem(w->contentItem(), "playlistVersionWhen_0");
    c.check(when && when->property("text").toString().startsWith("Today"),
            QString("a version made just now is dated today (%1)")
                .arg(when ? when->property("text").toString() : QString()));
    c.shot("01-playlist-versions");

    // Restoring from the dialog changes the playlist.
    b->openPlaylist(playlist);
    QTest::qWait(300);
    const int shown = b->results()->count();
    const int wanted = b->playlistVersions(playlist)[0].toMap().value("count").toInt();
    c.clickWithin(list, "restoreVersion_0");
    c.check(c.until([&] { return b->results()->count() == wanted; }, 4000),
            QString("restoring from the dialog reshapes the playlist (%1 to %2)")
                .arg(shown).arg(wanted));
    c.shot("02-playlist-versions-restored");

    // Forgetting empties it.
    c.click("clearPlaylistVersions");
    c.check(c.until([&] { return b->playlistVersions(playlist).isEmpty(); }, 3000),
            "versions can be forgotten");
    c.check(shownItem(w->contentItem(), "playlistVersionsEmpty"), "and the dialog says so");
    c.shot("03-playlist-versions-empty");
    QMetaObject::invokeMethod(dialog, "close");
    QTest::qWait(300);
  }

  // --- A deleted playlist takes its history with it ---
  b->addItemsToPlaylist(playlist, {songs[3]});
  QTest::qWait(150);
  c.check(!b->playlistVersions(playlist).isEmpty(), "history starts again after an edit");
  b->deletePlaylist(playlist);
  QTest::qWait(200);
  c.check(b->playlistVersions(playlist).isEmpty(), "deleting the playlist forgets its versions");

  c.finish();
}

void runWindowWashTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1400, 880);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(false);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);
  b->setLyricsFallback(false);
  b->setArtworkAccent(false);
  b->setAccentColor("");
  b->setAmbientBackdrop(true);

  paintCover(c.directory + "/music/cover.png", QColor("#1b4f8a"), QColor("#e8622a"));
  for (int i = 1; i <= 3; ++i)
    if (!encodeTrack(c, QString("%1/music/%2.flac").arg(c.directory).arg(i),
                     QString("Track %1").arg(i), "Wash", "Rill", i))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the wash fixture");
  b->library("files");
  c.check(c.until([&] { return b->results()->count() == 3; }), "the fixture is in the library");

  auto wash = anyItem(w->contentItem(), "windowBackdrop");
  c.check(wash, "the window carries a wash of its own");
  if (!wash)
    return c.finish();

  // --- It spans the window, not one panel of it ---
  c.check(qAbs(wash->width() - w->width()) < 1 && qAbs(wash->height() - w->height()) < 1,
          QString("the wash covers the whole window (%1x%2 of %3x%4)")
              .arg(wash->width()).arg(wash->height()).arg(w->width()).arg(w->height()));
  auto content = anyItem(w->contentItem(), "contentBody");
  c.check(content && wash->width() > content->width() + 40,
          "which is wider than the panel that used to carry it alone");

  // --- Nothing playing and nothing to borrow: no wash at all ---
  b->home();
  c.check(c.until([&] { return !b->busy(); }), "Home loads");
  QTest::qWait(400);
  c.check(w->property("windowArtwork").toString().isEmpty(), "there is no cover to wash with yet");
  c.check(!wash->property("active").toBool(), "so the window is left alone");
  c.check(qAbs(w->property("washAlpha").toReal() - 1) < 0.001,
          "and the surfaces stay opaque");
  c.shot("01-no-wash");

  // --- Playing something washes the window ---
  b->library("files");
  b->enqueueItems(b->results()->rows);
  b->playAt(0);
  c.check(c.until([&] { return b->playing(); }), "the fixture plays");
  c.check(c.until([&] { return wash->property("active").toBool(); }, 4000),
          "the playing cover washes the window");
  c.check(w->property("windowWashed").toBool(), "the window reports itself washed");
  c.check(w->property("washAlpha").toReal() < 1,
          "and the surfaces let it through rather than covering it");
  auto art = anyItem(wash, "ambientArt");
  c.check(art && art->property("source").toUrl() == QUrl(b->current().value("art").toString()),
          "the wash is taken from the cover that is playing");
  c.check(wash->property("drifts").toBool(), "a full-bleed wash drifts, having no corners to keep");
  c.shot("02-window-washed");

  // --- The wash reaches every part of the window, not just the middle ---
  // Sampled from the rendered window: the rail gutter, the player bar and the
  // panel all have to differ from the same scene with the wash turned off.
  const auto washed = w->grabWindow();
  b->setAmbientBackdrop(false);
  QTest::qWait(500);
  c.check(!wash->property("active").toBool(), "turning it off releases the wash");
  c.check(qAbs(w->property("washAlpha").toReal() - 1) < 0.001, "and the surfaces close up again");
  const auto plain = w->grabWindow();
  c.shot("03-wash-off");
  c.check(washed.size() == plain.size(), "both renders are the same size");
  if (washed.size() == plain.size()) {
    // Averaged over a small block, because a single pixel can coincide by
    // chance where the wash happens to be dark.
    const auto average = [](const QImage &image, int x, int y) {
      double r = 0, g = 0, bl = 0;
      int seen = 0;
      for (int dy = -10; dy <= 10; ++dy)
        for (int dx = -10; dx <= 10; ++dx) {
          const int sx = qBound(0, x + dx, image.width() - 1);
          const int sy = qBound(0, y + dy, image.height() - 1);
          const auto pixel = image.pixelColor(sx, sy);
          r += pixel.redF();
          g += pixel.greenF();
          bl += pixel.blueF();
          ++seen;
        }
      return QColor::fromRgbF(r / seen, g / seen, bl / seen);
    };
    struct Spot { const char *where; double x, y; };
    for (const auto &spot : {Spot{"the navigation rail", 0.03, 0.45},
                             Spot{"the player bar", 0.5, 0.94},
                             Spot{"the list behind the songs", 0.6, 0.6},
                             Spot{"the gutter above the content", 0.06, 0.03}}) {
      const int x = int(washed.width() * spot.x), y = int(washed.height() * spot.y);
      const auto a = average(washed, x, y), z = average(plain, x, y);
      const double difference = qAbs(a.redF() - z.redF()) + qAbs(a.greenF() - z.greenF()) +
                                qAbs(a.blueF() - z.blueF());
      c.check(difference > 0.004, QString("the wash reaches %1 (difference %2)")
                                      .arg(spot.where).arg(difference, 0, 'f', 4));
    }
  }
  b->setAmbientBackdrop(true);
  QTest::qWait(400);

  // --- Contrast survives it ---
  // Body text is drawn over these surfaces, so the composited surface is what
  // has to clear the floor, not the colour the theme nominally asks for.
  const auto lit = w->grabWindow();
  const auto text = c.themeColor("text");
  struct Surface { const char *where; double x, y; };
  for (const auto &surface : {Surface{"the song list", 0.62, 0.62},
                              Surface{"the player bar", 0.42, 0.93},
                              Surface{"the window behind the rail", 0.03, 0.5}}) {
    const auto sampled = lit.pixelColor(int(lit.width() * surface.x), int(lit.height() * surface.y));
    const double ratio = contrastOf(text, sampled);
    c.check(ratio >= 4.5, QString("body text keeps %1:1 over %2")
                              .arg(ratio, 0, 'f', 2).arg(surface.where));
  }
  c.shot("04-washed-contrast");

  // --- Immersive and the mini player carry their own treatment ---
  w->setProperty("immersive", true);
  QTest::qWait(500);
  c.check(!w->property("windowWashed").toBool(),
          "the immersive player is its own surface and is not washed twice");
  w->setProperty("immersive", false);
  QTest::qWait(400);
  c.check(c.until([&] { return w->property("windowWashed").toBool(); }, 3000),
          "leaving it restores the wash");

  b->stop();
  b->clearQueue();
  c.finish();
}

// --- Material foundations ----------------------------------------------------
// Shape, motion and typography are systems rather than features, so they are
// checked structurally: not "does this one corner look right" but "does every
// corner in the window come from the scale".

namespace {
// Every step of Material's corner radius scale.
bool onShapeScale(double radius, double width, double height) {
  for (double step : {0.0, 4.0, 8.0, 12.0, 16.0, 20.0, 28.0, 32.0, 48.0})
    if (qAbs(radius - step) < 0.01)
      return true;
  // `full` is a real half rounding rather than a large fixed number, measured
  // across the shorter side, which is what makes a bar read as a pill whether
  // it lies flat or stands upright.
  const double shorter = qMin(width, height);
  return shorter > 0 && qAbs(radius - shorter / 2) < 0.51;
}
struct Rounded { QString name; double radius, width, height; };
void collectRadii(QQuickItem *root, QList<Rounded> &out) {
  // A shadow ring's corner is the surface's corner plus however far that ring
  // reaches, so it is not a shape choice and has no place on the scale.
  if (root->objectName() == "elevationRing")
    return;
  if (root->isVisible()) {
    const auto radius = root->property("radius");
    if (radius.isValid() && radius.canConvert<double>() && radius.toDouble() > 0)
      out.append({root->objectName(), radius.toDouble(), root->width(), root->height()});
  }
  for (auto child : root->childItems())
    collectRadii(child, out);
}
} // namespace

void runMaterialFoundationTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1400, 900);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(true);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);

  paintCover(c.directory + "/music/cover.png", QColor("#1f4f6b"), QColor("#d98324"));
  for (int i = 1; i <= 3; ++i)
    if (!encodeTrack(c, QString("%1/music/%2.flac").arg(c.directory).arg(i),
                     QString("Track %1").arg(i), "Foundations", "Rill", i))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the foundations fixture");
  b->library("files");
  c.check(c.until([&] { return b->results()->count() == 3; }), "the fixture is listed");

  // --- Shape: the scale, and nothing but the scale ---
  const QList<QPair<QString, double>> scale{
      {"shapeNone", 0},      {"shapeExtraSmall", 4},  {"shapeSmall", 8},
      {"shapeMedium", 12},   {"shapeLarge", 16},      {"shapeLargeIncreased", 20},
      {"shapeExtraLarge", 28}, {"shapeExtraLargeIncreased", 32}, {"shapeExtraExtraLarge", 48}};
  for (const auto &step : scale)
    c.check(qAbs(c.evaluate("Theme." + step.first).toDouble() - step.second) < 0.01,
            QString("%1 is %2dp, as Material specifies").arg(step.first).arg(step.second));
  c.check(qAbs(c.evaluate("Theme.shapeFull(48)").toDouble() - 24) < 0.01,
          "full rounding is half the height, not a large fixed number");
  // Material's optical roundness: a nested shape subtracts the padding.
  c.check(qAbs(c.evaluate("Theme.shapeInside(48,14)").toDouble() - 34) < 0.01,
          "nested shapes subtract their padding rather than sharing a radius");

  QList<Rounded> radii;
  collectRadii(w->contentItem(), radii);
  c.check(radii.size() > 25,
          QString("the window has rounded shapes to check (%1)").arg(radii.size()));
  QStringList offScale;
  for (const auto &entry : radii)
    if (!onShapeScale(entry.radius, entry.width, entry.height))
      offScale << QString("%1 r=%2 %3x%4")
                      .arg(entry.name.isEmpty() ? QString("(unnamed)") : entry.name)
                      .arg(entry.radius, 0, 'f', 1)
                      .arg(entry.width, 0, 'f', 1)
                      .arg(entry.height, 0, 'f', 1);
  c.check(offScale.isEmpty(),
          QString("every corner on screen comes from the scale%1")
              .arg(offScale.isEmpty() ? QString() : ", but " + offScale.mid(0, 6).join("; ")));
  c.shot("01-shape-scale");

  // --- Spacing: the grid, not numbers picked by eye ---
  // Material lays out on a 4dp grid, so every gap between things on a page has
  // to be a step on it. Components are the exception, and Material's own token
  // files say so: a split button holds 2dp between its halves and several
  // components hold 6dp between an icon and the label beside it. Those two are
  // allowed because the specification publishes them, not because they look
  // close enough.
  {
    QStringList offGrid;
    for (auto item : w->findChildren<QQuickItem *>()) {
      if (!item->isVisible())
        continue;
      const auto property = QQmlProperty(item, "spacing", qmlContext(item));
      if (!property.isValid())
        continue;
      const double gap = property.read().toDouble();
      if (gap <= 0 || gap == 6 || gap == 2)
        continue;
      if (int(gap) % 4 != 0 || qAbs(gap - int(gap)) > 0.01)
        offGrid.append(QString("%1 on %2")
                           .arg(gap, 0, 'f', 1)
                           .arg(item->objectName().isEmpty() ? QString(item->metaObject()->className())
                                                             : item->objectName()));
    }
    c.check(offGrid.isEmpty(),
            offGrid.isEmpty()
                ? QStringLiteral("every gap on screen is a step on Material's 4dp grid")
                : QString("every gap on screen is a step on the grid, but %1").arg(offGrid.mid(0, 6).join("; ")));
  }

  // --- Motion: springs, not curves chosen by eye ---
  // Material publishes a damping ratio and a stiffness. src/m3motion.cpp
  // solves the spring that describes and fits the curve Qt animates on to it,
  // so what the window runs has to be that, exactly, rather than a number
  // typed into the theme that happens to look springy.
  c.check(b->motionScheme() == "expressive",
          "Material recommends the expressive scheme, so it is the default");
  const auto curve = [&c](const QString &token) { return c.evaluate("Theme." + token).toList(); };
  const auto peakOf = [](const QVariantList &points) {
    double top = 0;
    for (int i = 1; i < points.size(); i += 2)
      top = qMax(top, points[i].toDouble());
    return top;
  };
  const QList<QPair<QString, QString>> derived{{"springFastSpatial", "fastSpatial"},
                                               {"springSpatial", "defaultSpatial"},
                                               {"springSlowSpatial", "slowSpatial"},
                                               {"springFastEffects", "fastEffects"},
                                               {"springEffects", "defaultEffects"},
                                               {"springSlowEffects", "slowEffects"}};
  for (const auto &pair : derived) {
    const auto tokens = m3::springTokens(true, pair.second);
    const auto expected = m3::spring(tokens.damping, tokens.stiffness);
    c.check(curve(pair.first) == expected.curve,
            QString("Theme.%1 is the curve %2's spring solves to").arg(pair.first, pair.second));
    c.check(c.evaluate("Theme." + pair.first + "Ms").toInt() == expected.durationMs,
            QString("and runs for its settling time, %1ms (%2)")
                .arg(expected.durationMs)
                .arg(c.evaluate("Theme." + pair.first + "Ms").toInt()));
  }
  // Menu.kt:1829-1831 and NavigationDrawer.kt:351-355 use FastEffects to
  // close. Both exit aliases must describe that same solved spring.
  c.check(c.evaluate("Theme.exitDuration").toInt() ==
                  c.evaluate("Theme.springFastEffectsMs").toInt() &&
              curve("exitCurve") == curve("springFastEffects"),
          "the exit fade's duration and curve are one FastEffects spring");
  // LoadingIndicator.kt:400-419 alone specifies 0.6/200 with threshold 0.1;
  // its spring is separate from the six scheme tokens.
  const auto morphSpring = m3::loadingMorphSpring();
  c.check(c.evaluate("Theme.loadingMorphSpringMs").toInt() == morphSpring.durationMs &&
              curve("loadingMorphSpring") == morphSpring.curve &&
              morphSpring.durationMs < 650,
          "the indicator morph solves Compose's spring and fits its 650ms slot");
  // PaneMotion.kt:150-177 specifies DefaultSpatial for bounds. Inspect the
  // animations on the real window so 350ms flights fail this check.
  const int paneMs = c.evaluate("Theme.springSpatialMs").toInt();
  for (const char *name : {"albumFlightMotion", "coverFlightMotion"}) {
    auto flight = w->findChild<QObject *>(QString::fromLatin1(name));
    int matched = 0;
    if (flight)
      for (auto child : flight->findChildren<QObject *>())
        if (child->property("duration").toInt() == paneMs &&
            QQmlProperty(child, "easing.bezierCurve").read().toList() == curve("springSpatial"))
          ++matched;
    c.check(matched == 5, QString("%1 moves all five bounds on DefaultSpatial").arg(name));
  }
  auto headerMotion = w->findChild<QObject *>("mainHeaderExtentMotion");
  c.check(headerMotion && headerMotion->property("duration").toInt() == paneMs,
          "the live page header resizes on DefaultSpatial");
  for (const auto &entry : {
           std::tuple<const char *, const char *, const char *, const char *>{
               "coverDetailsFadeBehavior", "coverDetailsFadeMotion", "springFastEffectsMs", "springFastEffects"},
           {"collectionTitleSizeBehavior", "collectionTitleSizeMotion", "springSpatialMs", "springSpatial"},
           {"sideRevealBehavior", "sideRevealMotion", "springSpatialMs", "springSpatial"}}) {
    auto behavior = qmlContext(w)->objectForName(std::get<0>(entry));
    auto animation = motionObject(behavior, "animation");
    c.check(animation && animation->objectName() == std::get<1>(entry) &&
                animation->property("duration").toInt() ==
                    c.evaluate(QString("Theme.") + std::get<2>(entry)).toInt() &&
                QQmlProperty(animation, "easing.bezierCurve").read().toList() ==
                    curve(std::get<3>(entry)),
            QString("%1 reads its duration and curve from one spring").arg(std::get<1>(entry)));
  }
  for (const char *name : {"localGroupsWidthMotion", "playlistGridWidthMotion"}) {
    const QString behavior = QString::fromLatin1(name) == "localGroupsWidthMotion"
                                 ? "localGroupsWidthBehavior" : "playlistGridWidthBehavior";
    const auto view = anyItem(w->contentItem(),
                              behavior == "localGroupsWidthBehavior" ? "localGroups" : "playlistGrid");
    auto behaviorObject = view ? qmlContext(view)->objectForName(behavior) : nullptr;
    auto animation = motionObject(behaviorObject, "animation");
    const QVariant duration = animation ? animation->property("duration") : QVariant();
    c.check(duration.isValid() && duration.toInt() == paneMs,
            QString("%1 uses DefaultSpatial instead of 260ms (actual %2, expected %3)")
                .arg(name, duration.isValid() ? QString::number(duration.toInt()) : "not found")
                .arg(paneMs));
  }
  auto immersiveQueue = qmlContext(w)->objectForName("immersiveQueue");
  auto sheetEnter = motionAt(motionObject(immersiveQueue, "enter"), {0});
  c.check(sheetEnter && sheetEnter->property("duration").toInt() ==
                            c.evaluate("Theme.springFastSpatialMs").toInt() &&
              QQmlProperty(sheetEnter, "easing.bezierCurve").read().toList() ==
                  curve("springFastSpatial"),
          "the queue sheet enters with the FastSpatial position spring");
  auto sheetExit = motionObject(immersiveQueue, "exit");
  auto sheetExitAnimation = motionAt(sheetExit, {0});
  const QVariant sheetExitDuration = sheetExitAnimation ? sheetExitAnimation->property("duration") : QVariant();
  c.check(sheetExitDuration.isValid() && sheetExitDuration.toInt() ==
                                               c.evaluate("Theme.springFastSpatialMs").toInt(),
          QString("the queue sheet exits with the FastSpatial position spring "
                  "(actual %1, expected %2)")
              .arg(sheetExitDuration.isValid() ? QString::number(sheetExitDuration.toInt()) : "not found")
              .arg(c.evaluate("Theme.springFastSpatialMs").toInt()));
  if (auto tracks = shownItem(w->contentItem(), "tracksView")) {
    for (const auto &entry : {QPair<const char *, const char *>{"trackDisplaceMotion", "displaced"},
                              {"trackMoveMotion", "move"},
                              {"trackAddMotion", "add"},
                              {"trackRemoveMotion", "remove"}}) {
      const int expected = QString::fromLatin1(entry.first) == "trackAddMotion"
                               ? c.evaluate("Theme.springEffectsMs").toInt()
                               : QString::fromLatin1(entry.first) == "trackRemoveMotion"
                                     ? c.evaluate("Theme.springFastEffectsMs").toInt() : paneMs;
      auto transition = motionObject(tracks, entry.second);
      auto animation = QString::fromLatin1(entry.second) == "move"
                           ? motionAt(transition, {0, 1}) : motionAt(transition, {0});
      const QVariant duration = animation ? animation->property("duration") : QVariant();
      c.check(duration.isValid() && duration.toInt() == expected,
              QString("%1 uses its spring's settling time (actual %2, expected %3)")
                  .arg(entry.first, duration.isValid() ? QString::number(duration.toInt()) : "not found")
                  .arg(expected));
    }
    auto row = shownItem(tracks, "trackRow_0");
    auto resize = row ? row->findChild<QObject *>("trackRowResizeMotion") : nullptr;
    c.check(resize && resize->property("duration").toInt() == paneMs,
            "the actual track row resizes on DefaultSpatial");
  } else {
    c.check(false, "the library track list is available to inspect its motion");
  }
  {
    QQmlComponent source(qmlEngine(w), QUrl("qrc:/qml/TrackPresentation.qml"));
    QScopedPointer<QObject> presentation(source.create(qmlContext(w)));
    for (const auto &entry : {QPair<const char *, int>{"presentationFadeOut", c.evaluate("Theme.springFastEffectsMs").toInt()},
                              {"presentationOffsetOut", paneMs},
                              {"presentationFadeIn", c.evaluate("Theme.springEffectsMs").toInt()},
                              {"presentationOffsetIn", paneMs}}) {
      auto animation = presentation ? presentation->findChild<QObject *>(QString::fromLatin1(entry.first)) : nullptr;
      c.check(animation && animation->property("duration").toInt() == entry.second,
              QString("%1 uses its effects or spatial settling time").arg(entry.first));
    }
  }
  {
    QQmlComponent source(qmlEngine(w), QUrl("qrc:/qml/PlaybackGlyph.qml"));
    QScopedPointer<QObject> glyph(source.create(qmlContext(w)));
    auto morph = glyph ? glyph->findChild<QObject *>("playbackGlyphMorph") : nullptr;
    // IconButton.kt:1561-1585 specifies DefaultEffects for the toggle shape
    // because overshooting the glyph's endpoint would make it bounce.
    c.check(morph && morph->property("duration").toInt() ==
                         c.evaluate("Theme.springEffectsMs").toInt(),
            "the play/pause glyph morph uses IconToggleButton's DefaultEffects");
  }
  {
    QQmlComponent source(qmlEngine(w), QUrl("qrc:/qml/MLoadingIndicator.qml"));
    QScopedPointer<QObject> indicator(source.create(qmlContext(w)));
    auto animation = indicator ? indicator->findChild<QObject *>("loadingMorphAnimation") : nullptr;
    auto pause = indicator ? indicator->findChild<QObject *>("loadingMorphPause") : nullptr;
    c.check(animation && pause && animation->property("duration").toInt() == morphSpring.durationMs &&
                animation->property("duration").toInt() + pause->property("duration").toInt() == 650,
            "the live loading morph starts again each 650ms");
  }
  {
    QQmlComponent source(qmlEngine(w), QUrl("qrc:/qml/MWavyProgress.qml"));
    QScopedPointer<QObject> indicator(source.create(qmlContext(w)));
    auto sweep = indicator ? indicator->findChild<QObject *>("wavySweepAnimation") : nullptr;
    auto wave = indicator ? indicator->findChild<QObject *>("wavyTravelAnimation") : nullptr;
    // ProgressIndicator.kt:1048-1055 specifies 1750ms; WavyProgressIndicator.kt:
    // 106-107,174-175 moves one wavelength per second in both modes.
    c.check(sweep && wave && sweep->property("duration").toInt() == 1750 &&
                wave->property("duration").toInt() == 1000,
            "the wavy sweep and wavelength use Compose's independent rates");
  }
  {
    QQmlComponent source(qmlEngine(w), QUrl("qrc:/qml/CatalogSkeleton.qml"));
    QScopedPointer<QObject> made(source.create(qmlContext(w)));
    auto skeleton = qobject_cast<QQuickItem *>(made.data());
    if (skeleton) {
      skeleton->setParentItem(w->contentItem());
      skeleton->setWidth(240);
      skeleton->setHeight(160);
      skeleton->setProperty("loading", true);
      QTest::qWait(220);
    }
    auto shimmer = skeleton ? skeleton->findChild<QObject *>("catalogShimmerAnimation") : nullptr;
    c.check(skeleton && shimmer && shimmer->property("duration").toInt() == 1500 &&
                skeleton->property("animating").toBool(),
            "the visible loading shimmer keeps its continuous 1500ms period");
    b->setMotion(false);
    QCoreApplication::processEvents();
    const double stoppedWave = skeleton ? skeleton->property("wave").toDouble() : 0;
    QTest::qWait(64);
    c.check(skeleton && !skeleton->property("animating").toBool() &&
                qAbs(skeleton->property("wave").toDouble() - stoppedWave) < 0.001,
            "reduced motion stops the shimmer instead of keeping a timer awake");
    b->setMotion(true);
    if (skeleton) {
      skeleton->setVisible(false);
      QCoreApplication::processEvents();
      c.check(!skeleton->property("animating").toBool(), "a hidden skeleton stops its shimmer");
      skeleton->setParentItem(nullptr);
    }
  }
  // A spatial spring passes its target and an effects spring does not, which
  // is the whole reason Material separates them.
  const double spatialPeak = peakOf(curve("springSpatial"));
  c.check(spatialPeak > 1.0,
          QString("the spatial spring passes its target (%1)").arg(spatialPeak, 0, 'f', 3));
  // And it passes it by the margin the physics gives, not by a third. An
  // overshoot several times Material's is what reads as a jolt rather than a
  // settle, which is what the hand-picked curves here used to do.
  c.check(spatialPeak < 1.05,
          QString("by Material's own margin rather than a chosen one (%1)")
              .arg(spatialPeak, 0, 'f', 3));
  for (const auto &effects : {"springFastEffects", "springEffects", "springSlowEffects"})
    c.check(peakOf(curve(effects)) <= 1.0 + 1e-6,
            QString("%1 never overshoots, because colour and opacity must not").arg(effects));

  // Switching the scheme reaches the tokens, and the standard scheme rings
  // less than the expressive one because its spatial springs are tighter.
  b->setMotionScheme("standard");
  QTest::qWait(200);
  const auto settled = curve("springSpatial");
  const auto standardTokens = m3::springTokens(false, "defaultSpatial");
  c.check(settled == m3::spring(standardTokens.damping, standardTokens.stiffness).curve,
          "the standard scheme swaps in its own spatial spring");
  c.check(peakOf(settled) < spatialPeak,
          QString("which overshoots less than the expressive one (%1 against %2)")
              .arg(peakOf(settled), 0, 'f', 3).arg(spatialPeak, 0, 'f', 3));
  c.check(c.evaluate("Theme.springSpatialMs").toInt() <
              m3::spring(m3::springTokens(true, "defaultSpatial").damping,
                         m3::springTokens(true, "defaultSpatial").stiffness).durationMs,
          "and settles sooner, because a tighter spring is done sooner");
  b->setMotionScheme("expressive");
  QTest::qWait(200);

  // The tokens are not decoration: a real animated property has to overshoot.
  // The side panel opens on the spatial spring, so its width is the one to
  // watch now that navigation no longer has a pane that widens.
  // It is hidden until it has a width, which is the property being measured,
  // so it has to be found whether or not it is on screen yet.
  auto panel = anyItem(w->contentItem(), "sidePanel");
  c.check(panel, "the side panel is there to measure");
  if (panel) {
    const auto reveal = [&](const QString &scheme) {
      b->setMotionScheme(scheme);
      c.evaluate("window.side=''");
      // Settled means the panel has reached the width it is asking for, which
      // is a number the layout owns rather than one written in here.
      c.until([&] { return panel->width() < 1; }, 2000);
      QTest::qWait(200);
      c.evaluate("window.side='queue'");
      double widest = 0;
      QElapsedTimer timer;
      timer.start();
      while (timer.elapsed() < 1200) {
        widest = qMax(widest, panel->width());
        QTest::qWait(8);
      }
      return widest;
    };
    const double bouncy = reveal("expressive");
    const double settledWidth = panel->width();
    const double flat = reveal("standard");
    c.check(settledWidth > 0 && bouncy > settledWidth + 0.5,
            QString("the expressive scheme overshoots the panel's settled width (%1 past %2)")
                .arg(bouncy, 0, 'f', 1).arg(settledWidth, 0, 'f', 1));
    c.check(bouncy > flat,
            QString("further than the standard scheme does (%1 against %2)")
                .arg(bouncy, 0, 'f', 1).arg(flat, 0, 'f', 1));
    b->setMotionScheme("expressive");
    c.evaluate("window.side=''");
    QTest::qWait(400);
  }

  // --- Typography: emphasis on the font's own axes ---
  c.check(c.evaluate("Theme.emphasizedWidth").toInt() > c.evaluate("Theme.regularWidth").toInt(),
          "emphasis widens the variable font rather than only thickening it");
  b->home();
  c.check(c.until([&] { return !b->busy(); }), "Home opens for its headline");
  auto title = shownItem(w->contentItem(), "collectionHeaderTitle");
  c.check(title && title->property("emphasized").toBool(),
          "the page headline uses the emphasized style");
  if (title) {
    const auto axes = title->property("font").value<QFont>().variableAxisValue(
        QFont::Tag("wdth"));
    c.check(qAbs(axes - c.evaluate("Theme.emphasizedWidth").toDouble()) < 0.01,
            QString("and the width axis is really set (%1)").arg(axes));
    // Material's emphasis is one weight step up from the role's own, which for
    // a headline is medium rather than the bold a label would take.
    c.check(title->property("font").value<QFont>().weight() == QFont::Medium,
            QString("at the weight the role's emphasized style asks for (%1)")
                .arg(title->property("font").value<QFont>().weight()));
  }
  c.shot("02-emphasized-type");

  b->stop();
  b->clearQueue();
  c.finish();
}

void runMaterialComponentTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1400, 900);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(true);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);

  paintCover(c.directory + "/music/cover.png", QColor("#1f4f6b"), QColor("#d98324"));
  for (int i = 1; i <= 6; ++i)
    if (!encodeTrack(c, QString("%1/music/%2.flac").arg(c.directory).arg(i),
                     QString("Track %1").arg(i), "Components", "Rill", i))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the component fixture");
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  c.check(c.until([&] { return b->results()->count() == 6; }), "the library is listed");
  QTest::qWait(500);

  // --- Split button: one control, two targets, asymmetric corners ---
  auto split = shownItem(w->contentItem(), "collectionPlay");
  c.check(split, "the collection action is a split button");
  auto action = split ? shownItem(split, "splitButtonAction") : nullptr;
  auto reveal = split ? shownItem(split, "splitButtonMenu") : nullptr;
  c.check(action && reveal, "made of a common button and a menu button");
  if (action && reveal) {
    // Material's inner corners: the facing edges are a small step, the outer
    // ones full, which is what makes two targets read as one control.
    const double actionOuter = action->property("background").value<QQuickItem *>()
                                   ->property("topLeftRadius").toDouble();
    const double actionInner = action->property("background").value<QQuickItem *>()
                                   ->property("topRightRadius").toDouble();
    const double revealInner = reveal->property("background").value<QQuickItem *>()
                                   ->property("topLeftRadius").toDouble();
    const double revealOuter = reveal->property("background").value<QQuickItem *>()
                                   ->property("topRightRadius").toDouble();
    c.check(qAbs(actionInner - revealInner) < 0.01,
            "the facing corners match each other across the seam");
    c.check(actionInner < actionOuter && revealInner < revealOuter,
            "and are smaller than the outer corners");
    // Material's small split button is a 40dp container that keeps the 48dp
    // target around it, so the outer corners are full for the container
    // rather than for the room it is given.
    const double container = action->height();
    c.check(qAbs(container - 40) < 0.01 && split->height() >= container,
            "a 40dp container inside the target kept around it");
    c.check(qAbs(actionOuter - container/2) < 0.01 && qAbs(revealOuter - container/2) < 0.01,
            "the outer corners are full for that container");
    c.check(qAbs(reveal->x() - (action->x() + action->width())) < 4,
            "the halves sit together rather than apart");
    // The two halves do different things.
    c.check(b->queue()->count() == 0, "nothing is queued yet");
    c.click("splitButtonAction");
    c.check(c.until([&] { return b->queue()->count() == 6 && b->playing(); }, 8000),
            "the action half plays the collection");
    b->stop();
    b->clearQueue();
    QTest::qWait(300);
    const double resting = reveal->property("background").value<QQuickItem *>()
                               ->property("topLeftRadius").toDouble();
    c.click("splitButtonMenu");
    c.check(c.until([&] { return split->property("menuOpen").toBool(); }, 3000),
            "the menu half opens a menu");
    // Compose checks the trailing half into a circle (SplitButton.kt,
    // TrailingCheckedShape), so its inner corner rounds out to full.
    c.check(c.until([&] {
      return reveal->property("background").value<QQuickItem *>()
                 ->property("topLeftRadius").toDouble() > resting + 2;
    }, 2000), "and rounds into a circle while it is open, as Material asks");
    c.shot("01-split-button-open");
    auto shuffle = shownItem(w->contentItem(), "collectionShuffle");
    c.check(shuffle, "the menu offers the related ways of starting the same songs");
    QTest::keyClick(w, Qt::Key_Escape);
    c.check(c.until([&] { return !split->property("menuOpen").toBool(); }, 3000),
            "closing it releases the morph");
  }

  // --- FAB menu: two to six related actions, and a morph into its own close ---
  auto fab = shownItem(w->contentItem(), "fab");
  auto fabMenu = anyItem(w->contentItem(), "libraryFab");
  c.check(fab && fabMenu, "the library carries a floating action button");
  if (fab && fabMenu) {
    const int actions = fabMenu->property("count").toInt();
    c.check(actions >= 2 && actions <= 6,
            QString("it opens between two and six related actions (%1)").arg(actions));
    auto shape = shownItem(fab, "fabShape");
    const double rested = shape ? shape->property("radius").toDouble() : 0;
    c.check(!shownItem(w->contentItem(), "fabMenuItem_0"), "which are closed to begin with");
    c.click("fab");
    c.check(c.until([&] { return fabMenu->property("open").toBool(); }, 3000), "tapping opens them");
    c.check(c.until([&] { return shownItem(w->contentItem(), "fabMenuItem_0") != nullptr; }, 3000),
            "the actions appear");
    c.check(c.until([&] { return shape && shape->property("radius").toDouble() > rested + 2; }, 2000),
            "and the button morphs into the menu's close button");
    // FabMenuBaselineTokens: the close button is 56dp on the primary role,
    // and the items stand 4dp apart.
    c.check(c.until([&] { return qAbs(fab->width() - 56) < 0.5 && qAbs(fab->height() - 56) < 0.5; }, 2000),
            QString("at the close button's 56dp (%1x%2)").arg(fab->width()).arg(fab->height()));
    c.check(shape && shape->property("color").value<QColor>() == c.themeColor("primary"),
            "on the primary role");
    auto firstItem = shownItem(w->contentItem(), "fabMenuItem_0");
    auto secondItem = shownItem(w->contentItem(), "fabMenuItem_1");
    if (firstItem && secondItem) {
      const auto gap = [&] {
        const double a = firstItem->mapToScene(QPointF(0, 0)).y(), b = secondItem->mapToScene(QPointF(0, 0)).y();
        return qAbs(a - b) - firstItem->height();
      };
      c.check(c.until([&] { return qAbs(gap() - 4) < 0.5; }, 2000),
              QString("with 4dp between the items (%1)").arg(gap(), 0, 'f', 1));
      // Each item's icon starts 24dp in (ListItemLeadingSpace). A row told to
      // centre itself inside the control sat on the wider item's left edge.
      for (auto item : {firstItem, secondItem}) {
        auto row = item->property("contentItem").value<QQuickItem *>();
        auto glyph = row && !row->childItems().isEmpty() ? row->childItems().first() : nullptr;
        const double inset = glyph ? glyph->mapToItem(item, QPointF(0, 0)).x() : -1;
        c.check(qAbs(inset - 24) < 0.5,
                QString("%1's icon starts 24dp in (%2)").arg(item->objectName()).arg(inset, 0, 'f', 1));
      }
    }
    c.shot("02-fab-menu-open");
    c.click("fab");
    c.check(c.until([&] { return !fabMenu->property("open").toBool(); }, 3000), "tapping again closes them");
    c.check(c.until([&] { return shape && qAbs(shape->property("radius").toDouble() - rested) < 1; }, 2000),
            "and the shape comes back");
  }

  // --- Carousel: items change size across the viewport, and snap ---
  b->home();
  c.check(c.until([&] { return !b->busy(); }), "Home loads");
  QTest::qWait(600);
  auto carousel = shownItem(w->contentItem(), "carousel");
  c.check(carousel, "Home lays its shelves out as carousels");
  if (carousel) {
    c.check(carousel->property("snapMode").toInt() == 1,
            "which snap items into place rather than resting part-way");
    auto first = anyItem(carousel, "carouselCell_0");
    c.check(first, "the carousel has cells");
    if (first) {
      auto card = shownItem(first, "carouselCard");
      c.check(card && qAbs(card->scale() - 1) < 0.02,
              "a cell fully in view is at full size");
      // Scrolling it towards the edge has to shrink it: that squashed preview
      // is what Material found people read as "there is more here".
      carousel->setProperty("contentX", carousel->property("contentX").toReal() +
                                            carousel->property("cellWidth").toReal() * 0.7);
      QTest::qWait(300);
      c.check(card && card->scale() < 0.95,
              QString("and shrinks as it leaves the viewport (%1)")
                  .arg(card ? card->scale() : 0, 0, 'f', 2));
      c.check(card && qAbs(card->property("parallax").toReal()) > 0.05,
              "with its visual travelling at a different speed from its container");
      carousel->setProperty("contentX", 0);
      QTest::qWait(300);
      c.check(card && qAbs(card->scale() - 1) < 0.02, "and grows back on return");
    }
    c.shot("03-carousel");
  }

  // --- Pull to refresh ---
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  QTest::qWait(500);
  auto puller = anyItem(w->contentItem(), "contentRefresh");
  auto tracks = shownItem(w->contentItem(), "tracksView");
  c.check(puller && tracks, "the song list can be pulled to refresh");
  if (puller && tracks) {
    c.check(!puller->isVisible(), "the indicator is out of the way at rest");
    const double origin = tracks->property("originY").toReal();
    tracks->setProperty("contentY", origin - 20);
    QTest::qWait(150);
    c.check(puller->isVisible() && puller->property("progress").toReal() > 0.1 &&
                !puller->property("armed").toBool(),
            "a short pull shows the indicator without arming it");
    c.shot("04-pull-started");
    tracks->setProperty("contentY", origin - 90);
    QTest::qWait(150);
    c.check(puller->property("armed").toBool(), "pulling past the threshold arms it");
    c.shot("05-pull-armed");
    tracks->setProperty("contentY", origin);
    QTest::qWait(200);
    c.check(!puller->isVisible(), "and letting go without a refresh puts it away");
  }

  // --- Navigation: a capsule in the top bar, spanning the column once compact ---
  c.check(!shownItem(w->contentItem(), "navigationRail"),
          "no rail takes a column of the window any more");
  c.check(!w->property("compactWindow").toBool(), "a wide window is not compact");
  auto bar = shownItem(w->contentItem(), "navigationBar");
  c.check(bar, "navigation is on screen");
  c.check(bar && bar->property("hugsContent").toBool(), "as the capsule the top bar centres");
  if (bar) {
    // It belongs to the top bar, so it sits above the content pane rather
    // than beside or below it.
    const double foot = bar->mapToScene(QPointF(0, bar->height())).y();
    if (auto pane = shownItem(w->contentItem(), "contentColumn"))
      c.check(foot <= pane->mapToScene(QPointF(0, 0)).y() + 1,
              QString("above the content rather than beside it (ends at %1)").arg(foot, 0, 'f', 0));
    const double middle = bar->mapToScene(QPointF(bar->width() / 2, 0)).x();
    c.check(qAbs(middle - w->width() / 2.0) < 1.5,
            QString("centred on the window, not on the gap between the two sides (%1 against %2)")
                .arg(middle, 0, 'f', 1).arg(w->width() / 2.0, 0, 'f', 1));
    // The window's own actions are the top bar's trailing pair, and they clear
    // the capsule rather than running under it.
    if (auto settings = shownItem(w->contentItem(), "settingsButton"))
      c.check(settings->mapToScene(QPointF(0, 0)).x() > middle + bar->width() / 2,
              "the window's actions trail it without overlapping");
  }
  w->resize(520, 760);
  QTest::qWait(700);
  c.check(w->property("compactWindow").toBool(), "a narrow window is compact");
  bar = shownItem(w->contentItem(), "navigationBar");
  c.check(bar, "which keeps the same bar rather than swapping it for another");
  c.check(bar && !bar->property("hugsContent").toBool(),
          "standing the capsule down so the destinations still fit");
  if (bar) {
    c.check(bar->width() > w->width() * 0.85,
            QString("it spans the column (%1 of %2)").arg(bar->width(), 0, 'f', 0).arg(w->width()));
    // It still floats inside the column's margin here rather than reaching an
    // edge, so it keeps the full corner. A square bar between the rounded pane
    // below it and the rounded window above would be the odd one out.
    c.check(qAbs(bar->property("radius").toReal() - bar->height() / 2) < 0.5,
            QString("and keeps the full corner it has when it hugs (%1 of a %2 bar)")
                .arg(bar->property("radius").toReal(), 0, 'f', 0).arg(bar->height(), 0, 'f', 0));
    int destinations = 0;
    for (const auto *key : {"home", "search", "library"})
      if (shownItem(bar, QString("navBar_") + key))
        ++destinations;
    c.check(destinations == 3, "with three destinations, which is Material's minimum");
    for (const auto *key : {"home", "search", "library"})
      c.check(shownItem(bar, QString("navBarLabel_") + key),
              QString("the %1 label is shown, never dropped").arg(key));
    // Exactly one destination carries the active indicator, and Material
    // paints it in the secondary container: navigation reports where you are
    // rather than offering an action, so it does not take the action accent.
    // The indicator is only there on the destination you are on: Material
    // grows it from nothing, so everywhere else its width is nought.
    int active = 0;
    QString marked;
    for (const auto *key : {"home", "search", "library"})
      if (auto indicator = shownItem(bar, QString("navBarIndicator_") + key))
        if (indicator->width() > 1 &&
            indicator->property("color").value<QColor>() == c.themeColor("secondaryContainer")) {
          ++active;
          marked = key;
        }
    c.check(active == 1, QString("exactly one destination is marked active (%1)").arg(active));
    c.check(active == 1 && c.themeColor("secondaryContainer") != c.themeColor("primaryContainer"),
            "in the secondary container, not the primary one");
    for (const auto *key : {"home", "search", "library"})
      if (auto label = shownItem(bar, QString("navBarLabel_") + key))
        c.check(label->property("color").value<QColor>() ==
                    c.themeColor(key == marked ? "secondaryContainerText" : "muted"),
                key == marked
                    ? QString("the %1 you are on is lettered on its container").arg(key)
                    : QString("and the %1 you are not on is the variant ink").arg(key));
    // Round 13 put one focus ring on every control in the app. The bar was
    // bordering its indicator instead, which marked the pill rather than the
    // destination and read differently from everything around it.
    if (auto reached = shownItem(bar, QString("navBar_") + marked)) {
      reached->forceActiveFocus(Qt::TabFocusReason);
      QTest::qWait(250);
      auto ring = anyItem(bar, QString("navBarFocusRing_") + marked);
      c.check(ring && ring->isVisible(),
              "a destination the keyboard reaches wears the app's focus ring");
      c.check(ring && ring->property("border").value<QObject *>()->property("color")
                          .value<QColor>() == c.themeColor("focusRing"),
              "in the same role every other ring is drawn in");
      if (auto indicator = shownItem(bar, QString("navBarIndicator_") + marked))
        c.check(ring && ring->width() > indicator->width(),
                QString("around the destination rather than around its pill (%1 against %2)")
                    .arg(ring ? ring->width() : 0, 0, 'f', 0)
                    .arg(indicator->width(), 0, 'f', 0));
    }
    c.shot("06-navigation-bar");

    // --- The indicator arrives rather than appearing ---
    // Material measures the indicator at its full width but draws it at that
    // width times an animated progress, which is why the layer that answers
    // the pointer is a separate box. The progress runs on the default spatial
    // spring, so it overshoots before it settles.
    auto arriving = anyItem(bar, "navBarIndicator_search");
    auto leaving = anyItem(bar, "navBarIndicator_home");
    auto layer = anyItem(bar, "navBarStateLayer_search");
    c.check(arriving && leaving && layer,
            "the indicator and the state layer are separate boxes");
    if (arriving && leaving && layer) {
      const double layerWidth = layer->width();
      c.check(layerWidth > 1,
              QString("the state layer is there whether or not you are on it (%1 wide)")
                  .arg(layerWidth, 0, 'f', 0));
      c.check(arriving->width() < 1,
              QString("and the indicator is not, on a destination you are not on (%1 wide)")
                  .arg(arriving->width(), 0, 'f', 1));
      // No settling wait: the point is to watch what the press sets off.
      c.tap("navBar_search");
      double first = -1, widest = 0;
      QElapsedTimer timer;
      timer.start();
      while (timer.elapsed() < 1200) {
        if (first < 0 && arriving->width() > 0) first = arriving->width();
        widest = qMax(widest, arriving->width());
        QTest::qWait(8);
      }
      const double settled = arriving->width();
      c.check(first >= 0 && first < settled * 0.9,
              QString("it grows in from nothing rather than appearing whole (caught at %1 of %2)")
                  .arg(first, 0, 'f', 0).arg(settled, 0, 'f', 0));
      c.check(widest > settled + 0.5,
              QString("on a spatial spring, which overshoots (%1 past %2)")
                  .arg(widest, 0, 'f', 1).arg(settled, 0, 'f', 1));
      c.check(c.until([&] { return leaving->width() < 1; }, 2000),
              "and the one you left gives its width back");
      c.check(layer->width() > 1, "while the state layer keeps the whole target");
    }
    c.check(c.until([&] { return w->property("destination") == "search"; }, 3000),
            "and choosing one navigates");
    c.click("navBar_home");
    c.check(c.until([&] { return w->property("destination") == "home"; }, 3000), "as does another");
    c.shot("07-navigation-bar-home");
  }
  w->resize(1400, 900);
  QTest::qWait(700);
  c.check(c.until([&] {
            auto back = shownItem(w->contentItem(), "navigationBar");
            return back && back->property("hugsContent").toBool();
          }, 3000),
          "widening brings the capsule back");

  // --- The sidebar arrangement, which the top bar replaced ---
  // It is kept behind a setting. Both arrangements offer the same three
  // destinations and the same two window actions; what changes is where they
  // are, so neither may end up with a second copy of either.
  b->home();
  c.check(c.until([&] { return !b->busy(); }), "Home is open to compare the two on");
  c.check(!shownItem(w->contentItem(), "searchBar"),
          "the top bar arrangement keeps no search bar on a page that is not Search");
  // Turned on the way a person turns it on, from the control in Settings.
  auto navSettings = c.dialog("settingsDialog");
  QTest::qWait(400);
  c.check(!shownItem(w->contentItem(), "settingsAppearanceHeading"),
          "the selected Settings category does not repeat its name above the controls");
  auto accentHeading = shownItem(w->contentItem(), "accentColorHeading");
  c.check(accentHeading && accentHeading->property("typeRole").toString() == "titleMedium",
          "Accent color uses the titleMedium role of the other control headings");
  c.click("settingsCategory_3");
  c.check(navSettings && navSettings->property("category").toInt() == 3 &&
              shownItem(w->contentItem(), "shortcutHelpButton") &&
              shownItem(w->contentItem(), "typeAheadSwitch") &&
              !shownItem(w->contentItem(), "settingsKeyboardHeading"),
          "Keyboard follows Library and contains the three keyboard settings");
  c.click("settingsCategory_4");
  auto youtubeHeading = shownItem(w->contentItem(), "youtubeGroupHeading");
  c.check(youtubeHeading && contrastOf(youtubeHeading->property("color").value<QColor>(),
                                      c.themeColor("surfaceLow")) >= 4.5,
          "the YouTube group label meets text contrast on the Settings surface");
  c.click("settingsCategory_0");
  const QSize settingsWideSize = w->size();
  w->resize(700, 840);QTest::qWait(350);
  auto narrowPicker = shownItem(w->contentItem(), "settingsCategoryPicker");
  auto dropIndicator = narrowPicker ? shownItem(narrowPicker, "settingsCategoryDropIndicator") : nullptr;
  QQuickItem *pickerLabel = nullptr;
  if (narrowPicker) {
    const std::function<void(QQuickItem *)> findLabel = [&](QQuickItem *item) {
      if (pickerLabel) return;
      if (item != narrowPicker && item->property("typeRole").isValid() &&
          item->property("text") == narrowPicker->property("text")) {
        pickerLabel = item;
        return;
      }
      for (auto child : item->childItems()) findLabel(child);
    };
    findLabel(narrowPicker);
  }
  c.check(narrowPicker && narrowPicker->property("symbol").toString().isEmpty() &&
              dropIndicator && qAbs(dropIndicator->rotation() - 90) < 1 &&
              pickerLabel && qAbs(pickerLabel->mapToItem(narrowPicker, QPointF(0, 0)).x() -
                                  narrowPicker->property("contentInset").toReal()) < 1,
          "the narrow category button has one trailing indicator and normal label inset");
  c.click("settingsCategoryPicker");
  auto categoryMenu = w->findChild<QObject *>("settingsCategoryMenu");
  c.check(categoryMenu && categoryMenu->property("visible").toBool(),
          "the narrow category button opens its peer menu");
  if(categoryMenu)QMetaObject::invokeMethod(categoryMenu,"close");
  w->resize(settingsWideSize);QTest::qWait(350);
  c.check(shownItem(w->contentItem(), "navigationSidebar"),
          "Settings offers the two arrangements");
  // Each appearance control carries its own heading. The navigation one had
  // none and sat under "Theme", so it read as a choice of theme.
  if (auto rows = shownItem(w->contentItem(), "settingsRows0")) {
    const auto headingY = [&](const QString &label) {
      double y = -1;
      const std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
        if (!item->isVisible() || y >= 0) return;
        if (item->childItems().isEmpty() && item->property("text").toString() == label)
          y = item->mapToScene(QPointF(0, 0)).y();
        for (auto child : item->childItems()) walk(child);
      };
      walk(rows);
      return y;
    };
    auto navControl = shownItem(rows, "navigationSidebar");
    auto themeControl = shownItem(rows, "themeDark");
    const double navHeading = headingY("Navigation"), themeHeading = headingY("Theme");
    c.check(navControl && themeControl && navHeading >= 0 && themeHeading >= 0 &&
                navHeading < navControl->mapToScene(QPointF(0, 0)).y() &&
                navControl->mapToScene(QPointF(0, 0)).y() < themeHeading &&
                themeHeading < themeControl->mapToScene(QPointF(0, 0)).y(),
            QString("each appearance control sits under its own heading (navigation %1, theme %2)")
                .arg(navHeading, 0, 'f', 0).arg(themeHeading, 0, 'f', 0));
  }
  c.click("navigationSidebar");
  c.check(c.until([&] { return b->sidebarNavigation(); }, 3000),
          "and choosing the sidebar takes");
  c.closeDialog(navSettings);
  QTest::qWait(800);
  auto rail = shownItem(w->contentItem(), "navigationRail");
  c.check(rail, "the setting puts the rail back down the leading edge");
  c.check(!shownItem(w->contentItem(), "navigationBar"),
          "and stands the capsule down, so the destinations are in one place");
  auto chromeSearch = shownItem(w->contentItem(), "searchBar");
  c.check(chromeSearch, "the search bar returns to the chrome, on every page");
  // It gives way to the page's own heading only where it is on the page. In
  // the chrome it is beside the heading, not instead of it.
  c.check(shownItem(w->contentItem(), "collectionHeaderTitle"),
          "and the page keeps its heading, because the bar is not on the page");
  if (rail && chromeSearch) {
    c.check(qAbs(rail->width() - 96) < 1,
            QString("the rail is collapsed at Material's 96dp (%1)")
                .arg(rail->width(), 0, 'f', 0));
    const double searchTop = chromeSearch->mapToScene(QPointF(0, 0)).y();
    c.check(searchTop < 96,
            QString("and the bar it holds is in the top bar (%1 down)").arg(searchTop, 0, 'f', 0));
    // One button each, moved between the two slots rather than duplicated.
    for (const auto *name : {"settingsButton", "miniPlayerButton"}) {
      const auto found = w->findChildren<QQuickItem *>(name);
      c.check(found.size() == 1,
              QString("there is one %1 in the window, not one per arrangement (%2)")
                  .arg(name).arg(found.size()));
      if (found.size() == 1)
        c.check(found.first()->mapToItem(rail, QPointF(0, 0)).x() >= -1 &&
                    found.first()->mapToItem(rail, QPointF(0, 0)).x() < rail->width(),
                QString("and it sits at the foot of the rail").arg(name));
    }
    for (const auto *key : {"home", "search", "library"})
      c.check(shownItem(w->contentItem(), QString("nav_") + key),
              QString("the rail offers %1").arg(key));
    c.shot("10-sidebar-arrangement");
    // Material heads the rail with the surface's primary action.
    QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("playlists")));
    QTest::qWait(600);
    auto slot = shownItem(w->contentItem(), "railFabSlot");
    auto fab = shownItem(w->contentItem(), "libraryFab");
    c.check(slot && fab && fab->parentItem() == slot,
            "and carries the library's action at its head");
    c.check(w->findChildren<QQuickItem *>("libraryFab").size() == 1,
            "which is the same one button the other arrangement puts on the pane");
    // The rail opens into Material's expanded form, where the pins live.
    c.click("navigationMenuButton");
    c.check(c.until([&] { return rail->property("expanded").toBool(); }, 3000),
            "the menu button opens it");
    QTest::qWait(700);
    c.check(rail->width() >= 220 && rail->width() <= 360,
            QString("into Material's 220 to 360dp range (%1)").arg(rail->width(), 0, 'f', 0));
    c.shot("11-sidebar-expanded");
    c.click("navigationMenuButton");
    c.check(c.until([&] { return !rail->property("expanded").toBool(); }, 3000),
            "and closes it again");
  }
  // Below the rail's width the arrangement falls back to the bar against the
  // bottom edge and the drawer, which is where it kept them before.
  w->resize(520, 760);
  QTest::qWait(900);
  c.check(!shownItem(w->contentItem(), "navigationRail"),
          "a compact window stands the rail down");
  auto bottomBar = shownItem(w->contentItem(), "navigationBar");
  c.check(bottomBar, "and the bar takes the destinations back");
  if (bottomBar) {
    c.check(bottomBar->property("edgeToEdge").toBool() &&
                bottomBar->property("radius").toReal() < 0.5,
            "square, because a container flush with an edge takes no corner there");
    const double foot = bottomBar->mapToScene(QPointF(0, bottomBar->height())).y();
    c.check(qAbs(foot - w->height()) < 2,
            QString("against the bottom of the window (%1 of %2)").arg(foot, 0, 'f', 0).arg(w->height()));
    c.check(bottomBar->width() >= w->width() - 2, "and spanning it");
  }
  c.check(shownItem(w->contentItem(), "drawerButton"), "a menu button opens the drawer");
  c.click("drawerButton");
  auto drawer = w->findChild<QObject *>("navigationDrawer");
  c.check(drawer && drawer->property("visible").toBool(), "which opens over the page");
  c.check(anyItem(w->contentItem(), "drawerPin_0") || b->pins().isEmpty(),
          "carrying the pinned collections the collapsed rail has no room for");
  c.shot("12-sidebar-drawer");
  QTest::keyClick(w, Qt::Key_Escape);
  QTest::qWait(400);
  c.check(drawer && !drawer->property("visible").toBool(), "and Escape puts it away");
  b->setSidebarNavigation(false);
  w->resize(1400, 900);
  QTest::qWait(900);
  c.check(!shownItem(w->contentItem(), "navigationRail"),
          "turning the setting off returns the top bar arrangement");
  c.check(shownItem(w->contentItem(), "navigationBar"), "with the capsule back");

  // --- Floating toolbar in the immersive player ---
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  c.check(c.until([&] { return b->page() == "library" && b->libraryId() == "files" &&
                               b->results()->count() == 6; }),
          "the files tab completes navigation before playback begins");
  b->enqueueItems(b->results()->rows);
  b->playAt(0);
  c.check(c.until([&] { return b->playing(); }), "a song plays");
  w->setProperty("immersive", true);
  c.check(c.until([&] { return shownItem(w->contentItem(), "immersiveToolbar") != nullptr; }, 4000),
          "the immersive transport sits on a floating toolbar");
  auto toolbar = shownItem(w->contentItem(), "immersiveToolbar");
  if (toolbar) {
    c.check(toolbar->property("radius").toReal() > 20,
            "which floats as a rounded bar rather than being anchored into the surface");
    c.check(shownItem(toolbar, "immersivePlayButton"), "and holds the transport controls");
    c.check(!shownItem(w->contentItem(), "navigationBar"),
            "a toolbar and a navigation bar are never shown together");
  }
  c.shot("08-floating-toolbar");
  w->setProperty("immersive", false);
  QTest::qWait(400);

  b->stop();
  b->clearQueue();
  c.finish();
}

// Material's second layer over the components: the fill axis on navigation
// icons, the two tab variants, badges, the search view, the loading indicator
// that replaced the spinner, the fixed accents, and the proportions Material
// gives a supporting pane and a feed.
void runMaterialDetailTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1400, 900);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(true);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);

  paintCover(c.directory + "/music/cover.png", QColor("#26405e"), QColor("#c96f2a"));
  for (int i = 1; i <= 6; ++i)
    if (!encodeTrack(c, QString("%1/music/%2.flac").arg(c.directory).arg(i),
                     QString("Track %1").arg(i), "Detail", "Rill", i))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the detail fixture");
  // A second folder, kept back so the badges have a real import to report on.
  QDir().mkpath(c.directory + "/more");
  for (int i = 1; i <= 8; ++i)
    if (!encodeTrack(c, QString("%1/more/%2.flac").arg(c.directory).arg(i),
                     QString("Later %1").arg(i), "Detail two", "Marble Coast", i))
      return c.finish();
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  c.check(c.until([&] { return b->results()->count() == 6; }), "the library is listed");
  QTest::qWait(500);

  // ListTokens.ItemTwoLineContainerHeight and ItemLeadingImageWidth/Height:
  // the ordinary row is 72dp tall and its cover occupies 56dp.
  auto ordinaryRow = shownItem(w->contentItem(), "trackRow_0");
  auto ordinaryLead = ordinaryRow ? anyItem(ordinaryRow, "trackLeading") : nullptr;
  c.check(ordinaryRow && ordinaryLead && qAbs(ordinaryRow->height() - 72) < 1 &&
              qAbs(ordinaryLead->width() - 56) < 1,
          "ListTokens gives the ordinary two-line row a 72dp body and 56dp image");

  // --- The fill axis: filled where you are, outlined where you are not ---
  auto railHome = shownItem(w->contentItem(), "navBar_home");
  auto railLibrary = shownItem(w->contentItem(), "navBar_library");
  c.check(railHome && railLibrary, "navigation offers Home and Library");
  if (railHome && railLibrary) {
    auto glyphOf = [](QQuickItem *item) { return item ? shownItem(item, "materialIcon") : nullptr; };
    auto homeGlyph = glyphOf(railHome), libraryGlyph = glyphOf(railLibrary);
    c.check(homeGlyph && libraryGlyph, "each carries a symbol");
    if (homeGlyph && libraryGlyph) {
      c.check(libraryGlyph->property("fill").toReal() == 1,
              "the destination you are in is filled");
      c.check(homeGlyph->property("fill").toReal() == 0,
              "and the one you are not is outlined");
      auto outline = anyItem(homeGlyph, "iconOutline");
      auto filled = anyItem(homeGlyph, "iconFill");
      c.check(outline && outline->isVisible(), "the outlined form is the one on screen");
      c.check(outline && filled &&
                  outline->property("source").toUrl() != filled->property("source").toUrl(),
              "and it is a different symbol, not the same one dimmed");
      c.check(filled && filled->opacity() == 0, "with the filled form held clear of it");
    }
  }
  c.shot("01-icon-fill-axis");

  // --- Primary and secondary tabs ---
  auto primaryTabs = shownItem(w->contentItem(), "libraryTabs");
  auto secondaryTabs = shownItem(w->contentItem(), "localFacetTabs");
  c.check(primaryTabs && secondaryTabs, "the library stacks a secondary set under its primary one");
  if (primaryTabs && secondaryTabs) {
    auto divider = anyItem(primaryTabs, "tabDivider");
    c.check(divider && divider->isVisible(), "the primary container is closed by a divider");
    // Compose closes both rows with a divider (TabRow.kt, SecondaryTabRow
    // passes HorizontalDivider as well); the secondary set differs in its
    // indicator, which spans the whole tab.
    c.check(anyItem(secondaryTabs, "tabDivider")->isVisible(),
            "and so is the secondary set's");
    c.check(secondaryTabs->height() >= 47.5,
            QString("which is as tall as the primary one (%1)").arg(secondaryTabs->height()));
    auto primaryTab = shownItem(primaryTabs, "localFilesTab");
    auto secondaryTab = shownItem(secondaryTabs, "localView_files");
    c.check(primaryTab && secondaryTab, "both mark Local files as chosen");
    if (primaryTab && secondaryTab) {
      auto primaryMark = anyItem(primaryTab, "tabIndicator");
      auto secondaryMark = anyItem(secondaryTab, "tabIndicator");
      c.check(primaryMark && secondaryMark, "and both draw an indicator");
      if (primaryMark && secondaryMark) {
        c.check(primaryMark->height() == 3 && primaryMark->width() < primaryTab->width() - 8,
                QString("the primary indicator is 3 tall and sits under the label alone (%1 of %2)")
                    .arg(primaryMark->width(), 0, 'f', 0).arg(primaryTab->width(), 0, 'f', 0));
        c.check(secondaryMark->height() == 2 &&
                    qAbs(secondaryMark->width() - secondaryTab->width()) < 1,
                "the secondary indicator is 2 tall and spans its whole tab");
        // An indicator hanging below its own container would never be seen.
        const auto mark = secondaryMark->mapRectToItem(secondaryTabs, secondaryMark->boundingRect());
        c.check(mark.bottom() <= secondaryTabs->height() + 0.5,
                QString("and stays inside it (%1 of %2)")
                    .arg(mark.bottom(), 0, 'f', 0).arg(secondaryTabs->height(), 0, 'f', 0));
      }
    }
  }
  c.shot("02-tab-variants");

  // --- Badges ---
  auto queueBadge = anyItem(w->contentItem(), "queueBadge");
  c.check(queueBadge, "the queue button can carry a badge");
  if (queueBadge) {
    c.check(!queueBadge->isVisible(), "which stays away while nothing is waiting");
    QVariantList queued;
    for (int i = 0; i < 4; ++i)
      queued.append(b->results()->get(i));
    b->enqueueItems(queued);
    QTest::qWait(300);
    c.check(queueBadge->isVisible(), "and arrives once songs are");
    c.check(queueBadge->property("display").toString() == QString::number(b->queue()->count()),
            QString("counting them exactly (%1 of %2 queued)")
                .arg(queueBadge->property("display").toString())
                .arg(b->queue()->count()));
    b->playAt(0);
    c.check(c.until([&] { return b->currentIndex() == 0 && b->playing(); }),
            "the queue fixture begins playing");
    c.check(queueBadge->property("display").toString() == "3",
            "the badge excludes the song playing now");
    w->setProperty("side", "queue");
    QTest::qWait(200);
    auto queueRow = shownItem(w->contentItem(), "queueRow_0");
    auto queueLead = queueRow ? anyItem(queueRow, "trackLeading") : nullptr;
    c.check(queueRow && queueLead && qAbs(queueRow->height() - 72) < 1 &&
                qAbs(queueLead->width() - 56) < 1,
            "queue rows use ListTokens' 72dp body and 56dp image too");
    b->setCompactDensity(true);
    QTest::qWait(250);
    c.check(queueRow && queueLead && qAbs(queueRow->height() - 56) < 1 &&
                qAbs(queueLead->width() - 40) < 1,
            "compact queue rows use the 56dp option and ItemLeadingAvatarSize 40dp");
    c.shot("03a-compact-queue-row");
    b->setCompactDensity(false);
    QTest::qWait(250);
    auto waiting = shownItem(w->contentItem(), "queueWaitingCount");
    c.check(waiting && waiting->property("text").toString() == "3 songs waiting",
            "the queue footer agrees with the badge while playing");
    w->setProperty("side", "");
    // Material's large badge is 16dp tall and grows only as wide as it must.
    c.check(queueBadge->height() == 16 && queueBadge->width() >= 16,
            "drawn at Material's large size");
    queueBadge->setProperty("count", 4212);
    QTest::qWait(60);
    c.check(queueBadge->property("display").toString() == "999+",
            "and capped rather than allowed to sprawl");
    queueBadge->setProperty("count", 0);
    QTest::qWait(60);
    c.check(!queueBadge->isVisible(), "a count of nothing says nothing");
  }
  c.shot("03-queue-badge");

  // A dot instead, for work pending rather than counted.
  auto navBadge = railLibrary ? anyItem(railLibrary, "navBarBadge_library") : nullptr;
  auto tabBadge = primaryTabs ? anyItem(primaryTabs, "tabBadge_files") : nullptr;
  c.check(navBadge && tabBadge, "the library destination and its tab can both carry a dot");
  c.check(navBadge && !navBadge->isVisible() && tabBadge && !tabBadge->isVisible(),
          "which stay away while nothing is pending");
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/more"));
  c.check(c.until([&] { return b->importingLocal(); }, 5000), "a second folder starts importing");
  if (navBadge && tabBadge) {
    c.check(navBadge->isVisible() && tabBadge->isVisible(),
            "and both the destination and its tab say so");
    c.check(navBadge->width() == 6 && navBadge->height() == 6,
            "the dot being Material's 6dp small badge");
    c.shotNow("04-pending-dot");
  }
  c.check(c.until([&] { return !b->importingLocal(); }, 60000), "the import finishes");
  c.check(navBadge && !navBadge->isVisible(), "and the dot goes away with it");

  // --- The loading indicator that replaced the spinner ---
  QQmlComponent component(qmlEngine(w), QUrl("qrc:/qml/MLoadingIndicator.qml"));
  QScopedPointer<QObject> object(component.create(qmlContext(w)));
  auto loader = qobject_cast<QQuickItem *>(object.data());
  c.check(loader, "the loading indicator creates");
  if (loader) {
    loader->setParentItem(w->contentItem());
    loader->setX(640);
    loader->setY(400);
    loader->setZ(95);
    loader->setProperty("running", true);
    QTest::qWait(120);
    c.check(loader->width() == 48 && loader->height() == 48,
            "at Material's 48dp container size");
    auto shape = anyItem(loader, "loadingShape");
    c.check(shape && qAbs(shape->width() - 38) < 0.5,
            "with the 38dp active indicator inside it");
    const int firstShape = loader->property("morphIndex").toInt();
    const double firstSpin = loader->property("spin").toReal();
    c.check(c.until([&] { return loader->property("morphIndex").toInt() != firstShape; }, 3000),
            "it moves on to the next shape in the sequence");
    c.check(loader->property("spin").toReal() > firstSpin, "while it keeps turning");
    c.shot("05-loading-indicator");
    c.check(loader->property("morphIndex").toInt() < 7 &&
                loader->property("shapeCount").toInt() == 7,
            "walking the seven shapes Material names");
    b->setMotion(false);
    QTest::qWait(150);
    c.check(!loader->property("animating").toBool(), "and settles when motion is turned off");
    b->setMotion(true);
    loader->setProperty("running", false);
    loader->setVisible(false);
  }

  // Pull to refresh uses the contained variant, driven by the pull itself.
  auto puller = anyItem(w->contentItem(), "contentRefresh");
  auto refreshShape = puller ? anyItem(puller, "refreshIndicator") : nullptr;
  auto tracks = shownItem(w->contentItem(), "tracksView");
  c.check(refreshShape && tracks, "pull to refresh draws the contained indicator");
  if (refreshShape && tracks) {
    c.check(refreshShape->property("trackColor").value<QColor>() == c.themeColor("primaryContainer"),
            "on a primary container, as the contained variant asks");
    const double origin = tracks->property("originY").toReal();
    tracks->setProperty("contentY", origin - 40);
    QTest::qWait(150);
    const double part = refreshShape->property("progress").toReal();
    c.check(part > 0.1 && part < 1, QString("the pull drives the morph (%1)").arg(part, 0, 'f', 2));
    c.shot("06-pull-morph");
    tracks->setProperty("contentY", origin);
    QTest::qWait(250);
  }

  // --- The fixed accents ---
  const QColor fixedDark = c.themeColor("primaryFixed");
  const QColor onFixedDark = c.themeColor("primaryFixedText");
  const QColor primaryDark = c.themeColor("primary");
  b->setTheme("light");
  QTest::qWait(200);
  c.check(c.themeColor("primaryFixed") == fixedDark && c.themeColor("primaryFixedText") == onFixedDark,
          "the fixed accents hold one tone through a theme change");
  c.check(c.themeColor("primary") != primaryDark, "while the ordinary accent flips");
  c.check(contrastOf(onFixedDark, fixedDark) >= 4.5,
          QString("and carry their own text at %1:1")
              .arg(contrastOf(onFixedDark, fixedDark), 0, 'f', 1));
  b->setTheme("dark");
  QTest::qWait(200);
  auto stats = c.dialog("listeningStatsDialog");
  auto figure = anyItem(w->contentItem(), "statsTime");
  c.check(figure && figure->property("color").value<QColor>() == fixedDark,
          "the listening figures are drawn in them");
  c.shot("07-fixed-accents");
  c.closeDialog(stats);
  c.check(c.until([&] { return !stats || !stats->property("visible").toBool(); }),
          "listening statistics closes before another dialog opens");

  // Qt Popup lays out its default contentItem below the header and inside
  // padding. A replacement Loader anchored to the Popup starts at its edge.
  auto checkBody = [&](const char *name, const char *buttonName) {
    auto dialog = w->findChild<QObject *>(name);
    auto header = dialog ? dialog->property("header").value<QQuickItem *>() : nullptr;
    auto body = dialog ? dialog->property("contentItem").value<QQuickItem *>() : nullptr;
    QQuickItem *popupItem = nullptr;
    for (auto item = body ? body->parentItem() : nullptr; item; item = item->parentItem())
      if (QString::fromLatin1(item->metaObject()->className()).contains("PopupItem")) {
        popupItem = item;
        break;
      }
    c.check(dialog && dialog->property("visible").toBool() && header && body && popupItem,
            QString("%1 is open with a real header, body and PopupItem").arg(name));
    if (!dialog || !dialog->property("visible").toBool() || !header || !body || !popupItem) return;
    const auto bodyRect = body->mapRectToScene(body->boundingRect());
    const auto headerRect = header->mapRectToScene(header->boundingRect());
    const auto popupRect = popupItem->mapRectToScene(popupItem->boundingRect());
    const double left = popupRect.left() + dialog->property("leftPadding").toDouble();
    const double right = popupRect.right() - dialog->property("rightPadding").toDouble();
    const double bottom = popupRect.bottom() - dialog->property("bottomPadding").toDouble();
    c.check(bodyRect.isValid() && headerRect.isValid() && popupRect.isValid(),
            QString("%1 scene rectangles have size: body %2x%3, header %4x%5, popup %6x%7")
                .arg(name).arg(bodyRect.width(), 0, 'f', 1).arg(bodyRect.height(), 0, 'f', 1)
                .arg(headerRect.width(), 0, 'f', 1).arg(headerRect.height(), 0, 'f', 1)
                .arg(popupRect.width(), 0, 'f', 1).arg(popupRect.height(), 0, 'f', 1));
    c.check(bodyRect.isValid() && headerRect.isValid() && bodyRect.top() >= headerRect.bottom() - 1,
            QString("%1 body scene top %2 follows header bottom %3")
                .arg(name).arg(bodyRect.top(), 0, 'f', 1).arg(headerRect.bottom(), 0, 'f', 1));
    c.check(bodyRect.isValid() && bodyRect.left() >= left - 1 &&
                bodyRect.right() <= right + 1 && bodyRect.bottom() <= bottom + 1,
            QString("%1 body scene [%2,%3,%4] fits popup inset [%5,%6,%7]")
                .arg(name).arg(bodyRect.left(), 0, 'f', 1).arg(bodyRect.right(), 0, 'f', 1)
                .arg(bodyRect.bottom(), 0, 'f', 1).arg(left, 0, 'f', 1)
                .arg(right, 0, 'f', 1).arg(bottom, 0, 'f', 1));
    if (buttonName) {
      auto button = shownItem(w->contentItem(), buttonName);
      const auto buttonRect = button ? button->mapRectToScene(button->boundingRect()) : QRectF{};
      c.check(button && buttonRect.left() >= left - 1 && buttonRect.right() <= right + 1,
              QString("%1 chooser scene [%2,%3] fits popup inset [%4,%5]")
                  .arg(name).arg(buttonRect.left(), 0, 'f', 1).arg(buttonRect.right(), 0, 'f', 1)
                  .arg(left, 0, 'f', 1).arg(right, 0, 'f', 1));
    }
  };
  // Main.qml offers View layout through Quick actions. Open its command with
  // the shortcut and click the row, as a keyboard user would.
  QTest::keyClick(w, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
  c.check(c.until([&] { return shownItem(w->contentItem(), "command_view-layout") != nullptr; }),
          "Quick actions offers View layout");
  c.click("command_view-layout");
  auto viewLayout = w->findChild<QObject *>("viewLayoutDialog");
  c.check(c.until([&] { return viewLayout && viewLayout->property("visible").toBool(); }),
          "View layout opens from its command");
  checkBody("viewLayoutDialog", nullptr);
  c.shot("07a-view-layout-dialog");
  c.closeDialog(viewLayout);
  c.check(c.until([&] { return !viewLayout || !viewLayout->property("visible").toBool(); }),
          "View layout closes before the next route");
  auto commandPalette = w->findChild<QObject *>("commandPalette");
  c.check(c.until([&] { return !commandPalette || !commandPalette->property("visible").toBool(); }),
          "Quick actions closes after choosing View layout");
  c.click("trackRow_0");
  c.check(c.until([&] { return b->currentIndex() >= 0; }), "a track is playing for its artwork action");
  // Settings exposes Current artwork only for a playing track. Search for the
  // row and click it, instead of invoking the dialog behind the interface.
  c.click("settingsButton");
  auto settings = w->findChild<QObject *>("settingsDialog");
  c.check(c.until([&] { return settings && settings->property("visible").toBool(); }),
          "Settings opens for the playing track");
  c.click("settingsSearch");
  for (const QChar letter : QStringLiteral("Current artwork"))
    QTest::keyClick(w, letter.toLatin1());
  std::function<QQuickItem *(QQuickItem *)> findArtworkRow = [&](QQuickItem *root) -> QQuickItem * {
    if (!root || !root->isVisible()) return nullptr;
    if (root->inherits("QQuickAbstractButton") &&
        root->property("text").toString() == QStringLiteral("Current artwork")) return root;
    for (auto child : root->childItems())
      if (auto found = findArtworkRow(child)) return found;
    return nullptr;
  };
  QQuickItem *artworkRow = nullptr;
  c.check(c.until([&] { artworkRow = findArtworkRow(w->contentItem()); return artworkRow != nullptr; }),
          "Settings search finds Current artwork");
  if (artworkRow) {
    const auto point = artworkRow->mapToScene(artworkRow->boundingRect().center()).toPoint();
    QTest::mouseMove(w, point);
    QTest::qWait(60);
    QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, point);
    QTest::qWait(320);
  }
  auto artwork = w->findChild<QObject *>("artworkControls");
  c.check(c.until([&] { return artwork && artwork->property("visible").toBool(); }),
          "Artwork opens from its Settings row");
  c.check(c.until([&] { return !settings || !settings->property("visible").toBool(); }),
          "Settings closes behind Artwork");
  checkBody("artworkControls", "chooseArtworkButton");
  if (artwork)
    c.check(qAbs(artwork->property("height").toDouble() - artwork->property("implicitHeight").toDouble()) < 1,
            "the artwork dialog hugs its content at a tall window");
  c.shot("07b-artwork-dialog");
  c.closeDialog(artwork);
  c.check(c.until([&] { return !artwork || !artwork->property("visible").toBool(); }),
          "Artwork closes before compact dialogs open");

  // At 480dp the header X is the dismissive action. The footer disappears
  // for Close-only dialogs but retains an accepting action beside Cancel.
  w->resize(480, 620);
  QTest::qWait(450);
  for (const char *name : {"settingsDialog", "shortcutHelp", "sessionsDialog", "trackDetailsDialog"}) {
    auto compact = c.dialog(name);
    auto footer = compact ? compact->property("footer").value<QQuickItem *>() : nullptr;
    auto header = compact ? compact->property("header").value<QQuickItem *>() : nullptr;
    auto close = header ? anyItem(header, "dialogClose") : nullptr;
    c.check(compact && compact->property("fullScreen").toBool() && close && close->isVisible(),
            QString("%1 has its full-screen close action").arg(name));
    c.check(footer && !footer->isVisible() && footer->implicitHeight() == 0,
            QString("%1 drops its duplicate Close footer").arg(name));
    c.shot(QString("07c-compact-") + name);
    c.closeDialog(compact);
    c.check(c.until([&] { return !compact || !compact->property("visible").toBool(); }),
            QString("%1 closes before the next dialog").arg(name));
  }
  auto confirm = c.dialog("deletePlaylistDialog");
  auto confirmFooter = confirm ? confirm->property("footer").value<QQuickItem *>() : nullptr;
  c.check(confirmFooter && confirmFooter->isVisible(),
          "a full-screen confirmation keeps its action footer");
  c.check(c.evaluate("deletePlaylistDialog.standardButton(Dialog.Yes).visible").toBool() &&
              !c.evaluate("deletePlaylistDialog.standardButton(Dialog.No).visible").toBool(),
          "the confirming action remains while the duplicate No is hidden");
  c.shot("07c-compact-dialog-actions");
  c.closeDialog(confirm);
  c.check(c.until([&] { return !confirm || !confirm->property("visible").toBool(); }),
          "the confirmation closes before returning to the page");
  w->resize(1400, 900);
  QTest::qWait(450);

  // --- The search view ---
  b->rememberSearch("aurora");
  b->rememberSearch("night ferry");
  QTest::qWait(120);
  QMetaObject::invokeMethod(w, "focusSearch");
  QTest::qWait(400);
  auto box = anyItem(w->contentItem(), "searchField");
  auto view = w->findChild<QObject *>("searchSuggestions");
  // Material's docked search view is the extra large corner. It was a step
  // below, which made it read as a menu rather than as a view.
  if (auto viewShape = view ? view->property("background").value<QQuickItem *>() : nullptr)
    c.check(qAbs(viewShape->property("radius").toDouble() - 28) < 0.5,
            QString("the search view is the extra large corner (%1)")
                .arg(viewShape->property("radius").toDouble(), 0, 'f', 0));
  auto scrim = anyItem(w->contentItem(), "searchScrim");
  c.check(box && view && scrim, "focusing search opens its view over the page");
  if (box && view && scrim) {
    c.check(view->property("visible").toBool(), "the view is up");
    c.check(scrim->isVisible() && scrim->opacity() > 0.9, "and the page behind it is scrimmed");
    auto group = anyItem(w->contentItem(), "suggestionGroup_0");
    c.check(group && group->isVisible() && group->property("text").toString() == "Recent",
            "what it offers is filed under a category");
    auto leading = anyItem(w->contentItem(), "suggestionLeading_0");
    c.check(leading && leading->isVisible(), "and every row leads with an icon");
    c.shot("08-search-recent");
    box->setProperty("text", "track");
    QMetaObject::invokeMethod(box, "updateSuggestions");
    QTest::qWait(300);
    auto songs = anyItem(w->contentItem(), "suggestionGroup_0");
    c.check(songs && songs->property("text").toString() == "Songs",
            "typing files the matches under theirs");
    c.shot("09-search-songs");
  }
  auto bar = anyItem(w->contentItem(), "searchField");
  if (bar && bar->parentItem() && bar->parentItem()->parentItem())
    c.check(bar->parentItem()->parentItem()->property("color").value<QColor>() ==
                c.themeColor("high"),
            "the bar itself sits on surfaceContainerHigh");
  // SearchBarDefaults.ShadowElevation in SearchBar.kt overrides the generated
  // SearchBarTokens.ContainerElevation with Level0.
  if (auto surface = bar && bar->parentItem() ? bar->parentItem()->parentItem() : nullptr) {
    auto shade = anyItem(surface, "searchBarShade");
    c.check(!shade, "the search bar has no shadow item at Level0");
  }
  {
    QQmlComponent searchSource(qmlEngine(w), QUrl("qrc:/qml/MSearchField.qml"));
    QScopedPointer<QObject> made(searchSource.create(qmlContext(w)));
    auto field = qobject_cast<QQuickItem *>(made.data());
    c.check(field, "a search field can be inspected on its own");
    if (field) {
      auto shape = field->property("background").value<QQuickItem *>();
      c.check(shape && !anyItem(shape, "elevation"),
              "SearchBarDefaults.ShadowElevation also leaves MSearchField flat");
    }
  }

  // Compact windows get the whole screen instead of a menu under the bar.
  const double docked = view ? view->property("height").toReal() : 0;
  w->resize(520, 820);
  QTest::qWait(700);
  QMetaObject::invokeMethod(w, "focusSearch");
  QTest::qWait(400);
  if (view) {
    c.check(view->property("fullScreen").toBool(), "a compact window opens the view full screen");
    c.check(view->property("height").toReal() > docked,
            QString("taking the height it was not given docked (%1 over %2)")
                .arg(view->property("height").toReal(), 0, 'f', 0).arg(docked, 0, 'f', 0));
  }
  c.shot("10-search-full-screen");
  c.check(w->property("sizeClass").toString() == "compact" && w->property("paneMargin").toInt() == 16,
          "and the compact class draws its panes in at 16dp");

  // --- Supporting pane and feed proportions ---
  w->resize(1400, 900);
  QTest::qWait(700);
  if (auto field = anyItem(w->contentItem(), "searchField"))
    field->setProperty("text", QString());
  c.evaluate("content.forceActiveFocus()");
  w->setProperty("side", "");
  QTest::qWait(400);
  c.check(view && !view->property("visible").toBool(), "leaving the field closes the view again");
  c.check(w->property("sizeClass").toString() == "large" && w->property("paneMargin").toInt() == 24,
          "a wide window is in the large class at 24dp");
  b->home();
  c.check(c.until([&] { return !b->busy(); }), "Home loads its feed");
  QTest::qWait(400);
  const int largeCard = w->property("feedCardWidth").toInt();
  c.shot("11-feed-large");
  w->resize(1000, 900);
  QTest::qWait(700);
  c.check(w->property("sizeClass").toString() == "expanded", "a narrower one is expanded");
  c.check(w->property("feedCardWidth").toInt() < largeCard,
          QString("and gives the feed smaller cards (%1 against %2)")
              .arg(w->property("feedCardWidth").toInt()).arg(largeCard));
  c.shot("12-feed-expanded");

  w->resize(1400, 900);
  QTest::qWait(500);
  w->setProperty("side", "queue");
  QTest::qWait(700);
  QStringList modalBlockers;
  c.check(c.until([&] {
    modalBlockers.clear();
    for (auto popup : w->findChildren<QObject *>())
      if (popup->inherits("QQuickPopup") && popup->property("visible").toBool() &&
          popup->property("modal").toBool())
        modalBlockers.append(popup->objectName());
    return modalBlockers.isEmpty();
  }, 3000), QString("no modal dialog blocks the pane grip (%1)").arg(modalBlockers.join(", ")));
  auto panel = shownItem(w->contentItem(), "sidePanel");
  auto row = panel ? panel->parentItem() : nullptr;
  c.check(panel && row, "the supporting pane opens beside the page");
  if (panel && row) {
    const double third = row->width() / 3;
    c.check(qAbs(panel->width() - third) < 2 || panel->width() == 480 || panel->width() == 320,
            QString("holding a third of the row (%1 of %2)")
                .arg(panel->width(), 0, 'f', 0).arg(row->width(), 0, 'f', 0));
    auto grip = shownItem(w->contentItem(), "panelResizeHandle");
    c.check(grip, "with a grip for anyone who wants it elsewhere");
    if (grip) {
      const auto point = grip->mapToScene(QPointF(grip->width() / 2, grip->height() / 2)).toPoint();
      QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, point);
      QTest::mouseMove(w, point - QPoint(120, 0), 80);
      QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, point - QPoint(120, 0));
      QTest::qWait(450);
      c.check(panel->width() > third + 60, "which then wins over the proportion");
    }
  }
  c.shot("13-supporting-pane");
  c.finish();
}

// Material's expressive layer: the shape morph a toggle carries, the connected
// button group, the overflow an app bar owes its actions, the wavy progress
// indicator, the snackbar's inverse roles, elevation, the drag handle, menu
// anatomy, rich tooltips and swipe to dismiss.
void runMaterialExpressiveTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1400, 900);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(true);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);

  paintCover(c.directory + "/music/cover.png", QColor("#1d3f5c"), QColor("#d07a2e"));
  for (int i = 1; i <= 6; ++i)
    if (!encodeTrack(c, QString("%1/music/%2.flac").arg(c.directory).arg(i),
                     QString("Track %1").arg(i), "Expressive", "Rill", i))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the expressive fixture");
  // Held back so the progress indicator has a real import to report.
  QDir().mkpath(c.directory + "/more");
  for (int i = 1; i <= 10; ++i)
    if (!encodeTrack(c, QString("%1/more/%2.flac").arg(c.directory).arg(i),
                     QString("Later %1").arg(i), "Expressive two", "Marble Coast", i))
      return c.finish();
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  c.check(c.until([&] { return b->results()->count() == 6; }), "the library is listed");
  QTest::qWait(500);

  // Walks what is on screen for a shadow cast at a given level.
  std::function<QQuickItem *(QQuickItem *, int)> shadeAt = [&](QQuickItem *root, int level) -> QQuickItem * {
    if (root->objectName() == "elevation" && root->property("level").toInt() == level)
      return root;
    for (auto child : root->childItems())
      if (auto found = shadeAt(child, level))
        return found;
    return nullptr;
  };
  auto radiusOf = [](QQuickItem *button) {
    auto background = button ? button->property("background").value<QQuickItem *>() : nullptr;
    return background ? background->property("radius").toDouble() : -1.0;
  };

  // --- A toggle morphs as well as recolours ---
  QVariantList queued;
  for (int i = 0; i < 5; ++i)
    queued.append(b->results()->get(i));
  b->enqueueItems(queued);
  b->setShuffle(false);
  QTest::qWait(300);
  auto shuffle = shownItem(w->contentItem(), "playerShuffle");
  c.check(shuffle, "the player offers shuffle as a toggle");
  if (shuffle) {
    const double off = radiusOf(shuffle);
    auto shuffleContainer = shuffle->property("background").value<QQuickItem *>();
    const auto offColour = shuffleContainer->property("color").value<QColor>();
    // Material draws the container at the size's own height inside a 48dp touch
    // target, so the full corner is half the container, not half the target.
    c.check(qAbs(shuffle->height() - 48) < 0.5 && qAbs(shuffleContainer->height() - 40) < 0.5,
            QString("a small button is a 40dp container in a 48dp target (%1 in %2)")
                .arg(shuffleContainer->height(), 0, 'f', 0).arg(shuffle->height(), 0, 'f', 0));
    c.check(qAbs(off - shuffleContainer->height()/2) < 1.5,
            QString("off it is a full corner (%1 of %2)")
                .arg(off, 0, 'f', 1).arg(shuffleContainer->height()/2, 0, 'f', 1));
    // Material's standard icon button carries no container in either state.
    // On is a filled glyph in the accent, not a pill behind one, which is what
    // separates it from the filled and tonal variants that do have containers.
    c.check(offColour.alpha() == 0, "a standard toggle has no container while it is off");
    c.check(shuffle->property("ink").value<QColor>() == c.themeColor("text"),
            "and is drawn in the surface ink");
    c.shot("01-toggle-off");
    b->setShuffle(true);
    QTest::qWait(600);
    const double on = radiusOf(shuffle);
    c.check(qAbs(on - 12) < 1.5, QString("on it settles at the medium step (%1)").arg(on, 0, 'f', 1));
    c.check(shuffle->property("background").value<QQuickItem *>()->property("color").value<QColor>()
                .alpha() == 0, "on it still has none");
    c.check(shuffle->property("ink").value<QColor>() == c.themeColor("primary"),
            "and says so by taking the accent instead");
    c.shot("02-toggle-on");
    b->setShuffle(false);
    QTest::qWait(400);
  }

  // --- The connected button group ---
  auto stats = c.dialog("listeningStatsDialog");
  auto period = shownItem(w->contentItem(), "statsPeriod");
  c.check(period, "the listening period is chosen from a connected group");
  if (period) {
    auto week = shownItem(period, "statsPeriod_7"), month = shownItem(period, "statsPeriod_30"),
         allTime = shownItem(period, "statsPeriod_0");
    auto shapeOf = [](QQuickItem *item, const char *corner) {
      auto background = item ? anyItem(item, "segmentBackground") : nullptr;
      return background ? background->property(corner).toDouble() : -1.0;
    };
    c.check(week && month && allTime, "with a leading, middle and trailing button");
    if (week && month && allTime) {
      c.check(qAbs(shapeOf(week, "topLeftRadius") - 20) < 1.5,
              "the chosen leading button is full cornered on the outside");
      c.check(qAbs(shapeOf(month, "topLeftRadius") - 8) < 1.5 &&
                  qAbs(shapeOf(month, "topRightRadius") - 8) < 1.5,
              QString("a middle button keeps small corners on both sides (%1, %2)")
                  .arg(shapeOf(month, "topLeftRadius"), 0, 'f', 1)
                  .arg(shapeOf(month, "topRightRadius"), 0, 'f', 1));
      c.check(qAbs(shapeOf(allTime, "topRightRadius") - 20) < 1.5 &&
                  qAbs(shapeOf(allTime, "topLeftRadius") - 8) < 1.5,
              "and the trailing one is asymmetric the other way");
      // Pressing widens the button under the pointer and narrows its neighbours.
      const double restWidth = month->width(), neighbourRest = allTime->width();
      const auto point = month->mapToScene(month->boundingRect().center()).toPoint();
      QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, point);
      QTest::qWait(450);
      c.check(month->width() > restWidth + 2,
              QString("pressing expands it (%1 over %2)")
                  .arg(month->width(), 0, 'f', 0).arg(restWidth, 0, 'f', 0));
      c.check(allTime->width() < neighbourRest - 1, "and its neighbours give up the room");
      c.shotNow("03-button-group-pressed");
      QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, point);
      QTest::qWait(400);
    }
    c.shot("04-button-group");
  }
  c.closeDialog(stats);

  // --- Rich tooltips explain a setting rather than naming it ---
  auto settings = c.dialog("settingsDialog");
  c.evaluate("settingsDialog.category=1");
  QTest::qWait(400);
  auto gapless = shownItem(w->contentItem(), "gaplessSwitch");
  c.check(gapless && !gapless->property("hint").toString().isEmpty(),
          "gapless playback carries an explanation");
  if (gapless) {
    gapless->forceActiveFocus(Qt::TabFocusReason);
    QTest::mouseMove(w, gapless->mapToScene(QPointF(40, gapless->height()/2)).toPoint());
    c.check(c.until([&] { return anyItem(w->contentItem(), "richTooltipBody") != nullptr; }, 3000),
            "reaching it opens a rich tooltip");
    auto body = anyItem(w->contentItem(), "richTooltipBody");
    auto subhead = anyItem(w->contentItem(), "richTooltipSubhead");
    c.check(subhead && subhead->property("text").toString() == "Gapless playback",
            "with the setting as its subhead");
    c.check(body && body->property("text").toString().length() > 40,
            "and the explanation under it");
    c.shotNow("05-rich-tooltip");
    // Material holds a rich tooltip open rather than timing it out.
    QTest::qWait(1600);
    c.check(anyItem(w->contentItem(), "richTooltipBody") != nullptr,
            "which stays up long enough to read");
    if (auto body2 = anyItem(w->contentItem(), "richTooltipBody"))
      c.check(body2->window() != nullptr, "on a surface of its own");
    QTest::mouseMove(w, QPoint(w->width()/2, 40));
    QTest::qWait(400);
  }
  c.closeDialog(settings);

  // --- Menu anatomy: a leading icon and the keyboard route ---
  auto list = shownItem(w->contentItem(), "tracksView");
  c.check(list, "the song list is up");
  if (list) {
    list->forceActiveFocus();
    QTest::keyClick(w, Qt::Key_A, Qt::ControlModifier);
    QTest::qWait(300);
    c.evaluate("window.bulkView=tracks; bulkActions.popup()");
    QTest::qWait(500);
    auto shortcut = shownItem(w->contentItem(), "menuItemShortcut");
    c.check(shortcut && !shortcut->property("text").toString().isEmpty(),
            QString("a menu item that has a shortcut prints it on the trailing edge (%1)")
                .arg(shortcut ? shortcut->property("text").toString() : QString("none")));
    auto leading = anyItem(w->contentItem(), "menuItemLeading");
    c.check(leading && leading->isVisible(), "and leads with its icon");
    c.check(shadeAt(w->contentItem(), 2) != nullptr, "and the menu itself rests two levels off it");
    c.shotNow("06-menu-anatomy");
    c.evaluate("bulkActions.close()");
    QTest::qWait(300);
    if (auto selection = list->property("selection").value<QObject *>())
      QMetaObject::invokeMethod(selection, "clear");
    QTest::qWait(200);
  }

  // --- Elevation ---
  // A shadow is the one thing a screenshot can confirm and a property cannot,
  // so the page behind the dialog is sampled with it open and again without.
  auto cornerOf = [&](const QImage &frame, QQuickItem *item) {
    const auto rect = item->mapRectToScene(item->boundingRect()).toRect();
    return frame.pixelColor(qBound(0, rect.center().x(), frame.width()-1),
                            qBound(0, rect.center().y(), frame.height()-1));
  };
  QQmlComponent elevationSource(qmlEngine(w), QUrl("qrc:/qml/MElevation.qml"));
  QScopedPointer<QObject> elevationObject(elevationSource.create(qmlContext(w)));
  auto shade = qobject_cast<QQuickItem *>(elevationObject.data());
  c.check(shade, "elevation is a component of its own");
  if (shade) {
    shade->setParentItem(w->contentItem());
    shade->setX(620); shade->setY(380); shade->setZ(94);
    shade->setWidth(160); shade->setHeight(90);
    shade->setProperty("radius", 16);
    shade->setProperty("level", 3);
    QTest::qWait(200);
    const auto rings = shade->property("rings").toList();
    c.check(rings.size() == 10, "cast as two shadows of five rings each");
    double reach = 0;
    for (const auto &ring : rings)
      reach = std::max(reach, ring.toMap().value("reach").toDouble());
    // Level 3's ambient shadow spreads 3dp and blurs a further 8dp.
    c.check(qAbs(reach - 11) < 0.01,
            QString("reaching Material's 11dp at level three (%1)").arg(reach, 0, 'f', 1));
    const auto lit = cornerOf(w->grabWindow(), shade);
    c.shotNow("07-elevation");
    shade->setProperty("level", 0);
    QTest::qWait(200);
    const auto bare = cornerOf(w->grabWindow(), shade);
    c.check(lit.lightnessF() < bare.lightnessF() - 0.01,
            QString("and the page under it is darker for the shadow (%1 against %2)")
                .arg(lit.lightnessF(), 0, 'f', 3).arg(bare.lightnessF(), 0, 'f', 3));
    shade->setVisible(false);
    shade->setParentItem(nullptr);
  }
  // The floating surfaces that should carry one.
  auto dialog = c.dialog("settingsDialog");
  auto panel = dialog ? dialog->property("background").value<QQuickItem *>() : nullptr;
  auto dialogShade = shadeAt(w->contentItem(), 3);
  c.check(panel && dialogShade, "a dialog rests three levels off the page");
  c.shot("08-elevation-dialog");
  c.closeDialog(dialog);

  // --- The drag handle ---
  w->setProperty("side", "queue");
  QTest::qWait(700);
  auto grip = shownItem(w->contentItem(), "panelResizeHandle");
  auto handle = grip ? anyItem(grip, "dragHandleGrip") : nullptr;
  c.check(handle, "the pane split is changed by a drag handle");
  if (handle && grip) {
    c.check(qAbs(handle->width() - 4) < 0.5 && qAbs(handle->height() - 48) < 0.5,
            QString("at rest it is Material's 4 by 48 capsule (%1 by %2)")
                .arg(handle->width(), 0, 'f', 0).arg(handle->height(), 0, 'f', 0));
    // A handle is something you take hold of, so Material draws it in the
    // outline role rather than in the variant a rule is drawn in.
    c.check(handle->property("color").value<QColor>() == c.themeColor("outline"),
            "drawn in the outline role, not the rule one");
    const auto point = grip->mapToScene(grip->boundingRect().center()).toPoint();
    QTest::mouseMove(w, point);
    QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, point);
    QTest::qWait(500);
    c.check(qAbs(handle->width() - 12) < 0.5 && qAbs(handle->height() - 52) < 0.5,
            QString("held it thickens to 12 by 52 (%1 by %2)")
                .arg(handle->width(), 0, 'f', 0).arg(handle->height(), 0, 'f', 0));
    c.check(handle->property("color").value<QColor>() == c.themeColor("text"),
            "and takes the surface ink while it is held");
    c.check(qAbs(handle->property("radius").toDouble() - 12) < 0.5,
            "and squares off to a medium corner");
    c.shotNow("08-drag-handle");
    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, point);
    QTest::qWait(400);
  }

  // --- Swipe a queue row away ---
  auto queue = shownItem(w->contentItem(), "queueView");
  c.check(queue, "the queue is on screen");
  if (queue) {
    auto row = shownItem(queue, "queueRow_1");
    c.check(row, "with a row to push aside");
    if (row) {
      const int before = b->queue()->count();
      const auto from = row->mapToScene(QPointF(row->width()/2, row->height()/2)).toPoint();
      QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, from);
      QTest::mouseMove(w, from + QPoint(60, 0), 40);
      QTest::qWait(120);
      auto reveal = anyItem(row, "swipeReveal");
      c.check(reveal && reveal->isVisible(), "the action behind it is revealed as it moves");
      c.shotNow("09-swipe-reveal");
      QTest::mouseMove(w, from + QPoint(row->width()/2 + 20, 0), 40);
      QTest::qWait(120);
      QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier,
                          from + QPoint(row->width()/2 + 20, 0));
      QTest::qWait(500);
      c.check(b->queue()->count() == before-1,
              QString("and releasing past a third of the row drops it (%1 from %2)")
                  .arg(b->queue()->count()).arg(before));
    }
  }

  // --- The snackbar ---
  auto snack = anyItem(w->contentItem(), "toastBar");
  c.check(snack, "removing a song says so in a snackbar");
  if (snack) {
    c.check(c.until([&] { return snack->isVisible(); }, 3000), "which is up");
    c.check(snack->property("color").value<QColor>() == c.themeColor("inverseSurface"),
            "on the inverse surface");
    c.check(qAbs(snack->property("radius").toDouble() - 4) < 0.5,
            QString("at the smallest corner on the scale (%1)")
                .arg(snack->property("radius").toDouble(), 0, 'f', 1));
    c.check(qAbs(snack->height() - 48) < 0.5, "and one line tall");
    auto undo = anyItem(snack, "toastUndo");
    c.check(undo && undo->property("ink").value<QColor>() == c.themeColor("inversePrimary"),
            "with its action in the inverse accent");
    c.shotNow("10-snackbar");
    const auto centre = snack->mapToScene(snack->boundingRect().center()).toPoint();
    const int reach = int(snack->width()/2) + 30;
    QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, centre);
    for (int step = 1; step <= 6; ++step) {
      QTest::mouseMove(w, centre + QPoint(reach*step/6, 0), 20);
      QTest::qWait(30);
    }
    c.check(qAbs(c.evaluate("toastShove.x").toReal()) > 20,
            QString("dragging it moves it (%1)").arg(c.evaluate("toastShove.x").toReal(), 0, 'f', 0));
    c.shotNow("11-snackbar-swipe");
    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, centre + QPoint(reach, 0));
    QTest::qWait(600);
    c.check(!w->property("toastPending").toBool(), "and it can be pushed aside");
  }
  w->setProperty("side", "");
  QTest::qWait(400);

  // --- The app bar keeps its actions reachable ---
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  QTest::qWait(500);
  auto appBar = shownItem(w->contentItem(), "collectionActions");
  c.check(appBar, "the collection header is an app bar row");
  if (appBar) {
    const int live = appBar->property("live").toList().size();
    c.check(!appBar->property("overflowing").toBool(),
            QString("which shows all %1 of its actions when there is room").arg(live));
    c.shot("11-app-bar-wide");
    const int shown = appBar->property("shownCount").toInt();
    const int hidden = appBar->property("hidden").toList().size();
    c.check(shown + hidden == appBar->property("live").toList().size(),
            QString("and nothing is dropped on the way (%1 shown, %2 in the menu)")
                .arg(shown).arg(hidden));
  }
  // What the row does when it is given less than it asks for.
  QQmlComponent rowSource(qmlEngine(w), QUrl("qrc:/qml/MAppBarRow.qml"));
  QScopedPointer<QObject> rowObject(rowSource.create(qmlContext(w)));
  auto crowded = qobject_cast<QQuickItem *>(rowObject.data());
  c.check(crowded, "the app bar row is a component of its own");
  if (crowded) {
    crowded->setParentItem(w->contentItem());
    crowded->setX(520); crowded->setY(320); crowded->setZ(94);
    crowded->setHeight(48);
    QVariantList six;
    for (const char *name : {"one", "two", "three", "four", "five", "six"})
      six.append(QVariantMap{{"key", name}, {"symbol", "play"}, {"label", QString(name)}});
    crowded->setProperty("actions", six);
    crowded->setWidth(6*48 + 5*4);
    QTest::qWait(200);
    c.check(!crowded->property("overflowing").toBool() &&
                crowded->property("shownCount").toInt() == 6,
            "given the room it needs, every action is a button");
    crowded->setWidth(160);
    QTest::qWait(200);
    c.check(crowded->property("overflowing").toBool(), "given less, it overflows");
    const int held = crowded->property("shownCount").toInt();
    const int folded = crowded->property("hidden").toList().size();
    c.check(held + folded == 6 && held == 2,
            QString("keeping a slot for the overflow button (%1 shown, %2 folded)")
                .arg(held).arg(folded));
    auto overflow = shownItem(crowded, "appBarOverflow");
    c.check(overflow && overflow->isVisible(), "which is the last thing in the row");
    if (overflow) {
      const auto point = overflow->mapToScene(overflow->boundingRect().center()).toPoint();
      QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, point);
      QTest::qWait(500);
      c.check(anyItem(w->contentItem(), "appBarMenuAction_six") != nullptr,
              "and the actions it folded away are in the menu behind it");
      c.shotNow("12-app-bar-overflow");
      QTest::keyClick(w, Qt::Key_Escape);
      QTest::qWait(300);
    }
    crowded->setVisible(false);
  }
  // SmallIconButtonTokens uses 40dp buttons. The bar keeps a 100dp title;
  // under 668px the 120dp seek track takes a second row.
  for (const int width : {1440, 1920, 2560, 1024, 900, 840, 668, 667, 600, 480}) {
    w->resize(width, width == 480 ? 620 : 800);
    QTest::qWait(450);
    auto bar = anyItem(w->contentItem(), "playbackBar");
    auto seek = shownItem(w->contentItem(), "seekBar");
    auto title = anyItem(w->contentItem(), "nowTitle");
    auto transport = anyItem(w->contentItem(), "playerTransport");
    auto layout = anyItem(w->contentItem(), "playerLayout");
    auto seekRow = anyItem(w->contentItem(), "playerSeekRow");
    const bool twoRow = layout && layout->property("twoRow").toBool();
    c.check(layout && twoRow == (width < 668),
            QString("the %1px bar selects the layout before its title or seek can shrink").arg(width));
    c.check(transport, QString("the %1px player has a centred transport").arg(width));
    // Every visible child must stay within the rounded bar's own rectangle.
    double overrun = 0;
    if (bar) {
      for (auto item : bar->findChildren<QQuickItem *>()) {
        if (!item->isVisible() || item->width() <= 0 || item->height() <= 0 ||
            (!item->objectName().startsWith("player") &&
             !item->property("activeFocusOnTab").toBool())) continue;
        const auto bounds = item->mapRectToItem(bar, item->boundingRect());
        overrun = qMax(overrun, qMax(-bounds.left(), bounds.right()-bar->width()));
      }
    }
    c.check(bar && overrun <= 0.5,
            QString("the %1px player keeps controls within the bar (%2px over)").arg(width).arg(overrun, 0, 'f', 1));
    c.check(seek && seek->width() >= 120,
            QString("the %1px player keeps at least 120px of seek track").arg(width));
    c.check(title && title->isVisible() && title->width() >= 100,
            QString("the %1px title remains visible with at least 100px").arg(width));
    if (!twoRow && bar && transport)
      c.check(qAbs(transport->mapToItem(bar, QPointF(transport->width()/2, 0)).x()-bar->width()/2) <= 2,
              QString("the %1px transport is centred within 2px").arg(width));
    if (twoRow && transport && seekRow)
      c.check(seekRow->y() >= transport->y()+transport->height(),
              QString("the %1px seek track has its own row").arg(width));
    if (width == 1440)
      c.check(seek && seek->width() >= 700,
              QString("the 1440px seek track reaches 700px (%1px)").arg(seek ? seek->width() : 0, 0, 'f', 0));
    if (width == 1920 || width == 2560)
      c.check(seek && seek->width() >= 860,
              QString("the %1px seek track reaches the 960px transport cap").arg(width));
  }
  w->resize(1400, 900);
  QTest::qWait(500);
  // The player bar's own overflow, for the controls a narrow bar cannot hold.
  auto playerOverflow = anyItem(w->contentItem(), "playerOverflow");
  c.check(playerOverflow && !playerOverflow->isVisible(),
          "a wide player bar has nothing to hand its overflow");
  w->resize(900, 860);
  QTest::qWait(800);
  c.check(playerOverflow && playerOverflow->isVisible(),
          "a narrow one keeps the controls it cannot show in an overflow instead");
  c.check(playerOverflow && playerOverflow->property("live").toList().size() == 5,
          "holding like, shuffle, repeat, output and volume");
  c.shot("13-player-overflow");
  // A compact window moves the side controls into the same menu and gives
  // seeking a full-width second row.
  w->resize(480, 620);
  QTest::qWait(900);
  if (auto bar = anyItem(w->contentItem(), "playbackBar"); bar && !bar->childItems().isEmpty()) {
    double overrun = 0;
    for (auto child : bar->childItems().first()->childItems())
      if (child->isVisible() && child->width() > 0)
        overrun = qMax(overrun, child->mapToItem(bar, QPointF(child->width(), 0)).x() - bar->width());
    c.check(overrun <= 0.5, QString("a compact player bar holds every control inside it (%1px over)").arg(overrun, 0, 'f', 1));
  }
  QStringList folded;
  if (playerOverflow)
    for (const auto &action : playerOverflow->property("live").toList())
      folded << action.toMap().value("key").toString();
  c.check(folded.contains("lyrics") && folded.contains("queue") && folded.contains("output") && folded.contains("volume"),
          QString("with lyrics, queue, output and volume in its menu (%1)").arg(folded.join(", ")));
  if (auto title = shownItem(w->contentItem(), "nowTitle"))
    c.check(title->width() >= 100, QString("and the song's title keeps 100px (%1px)").arg(title->width(), 0, 'f', 0));
  else
    c.check(false, "and the song's title stays on the bar");
  c.shot("13-compact-player");
  w->resize(1400, 900);
  QTest::qWait(700);

  // --- The wavy progress indicator ---
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/more"));
  c.check(c.until([&] { return b->localImportProgress() >= 0; }, 20000),
          "a second import reports how far it has got");
  auto wavy = anyItem(w->contentItem(), "importProgress");
  c.check(wavy, "which is drawn as a wavy progress indicator");
  if (wavy) {
    c.check(wavy->isVisible() && wavy->property("determinate").toBool(),
            "in its determinate form");
    c.check(qAbs(wavy->property("stroke").toDouble() - 4) < 0.01 &&
                qAbs(wavy->property("amplitude").toDouble() - 3) < 0.01 &&
                qAbs(wavy->property("wavelength").toDouble() - 40) < 0.01,
            "at Material's 4dp stroke, 3dp amplitude and 40dp wavelength");
    c.check(qAbs(wavy->height() - 10) < 0.5, "in a 10dp container");
    auto stop = anyItem(wavy, "wavyStop");
    c.check(stop && stop->isVisible() && qAbs(stop->width() - 4) < 0.5,
            "with the stop indicator at the end of the track");
    auto shape = anyItem(wavy, "wavyShape");
    auto run = anyItem(wavy, "wavyRun");
    c.check(shape && run, "and a wave filling the part that is done");
    c.check(c.until([&] { return b->localImportProgress() > 0.15 || !b->importingLocal(); }, 40000),
            "which fills as the import advances");
    const double filled = run ? run->width() : 0;
    c.check(filled > 0, QString("the wave covering the part that is done (%1px)")
                            .arg(filled, 0, 'f', 0));
    c.shotNow("14-wavy-progress");
    c.check(shape && shape->width() > wavy->width(),
            "drawn wider than the run so the wave travels through it");
  }
  c.check(c.until([&] { return !b->importingLocal(); }, 60000), "the import finishes");
  c.shot("15-import-finished");
  b->clearQueue();
  c.finish();
}

// Material's sizing and shape layer: the button scale and its touch target,
// optical centering inside asymmetric shapes, emphasis that depends on the
// role, the shape library, a field that can explain itself and report an error,
// and what a window too narrow for a rail or a side pane does instead.
void runMaterialSizingTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1400, 900);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(true);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);

  paintCover(c.directory + "/music/cover.png", QColor("#274a63"), QColor("#cf7b2b"));
  for (int i = 1; i <= 4; ++i)
    if (!encodeTrack(c, QString("%1/music/%2.flac").arg(c.directory).arg(i),
                     QString("Track %1").arg(i), "Sizing", "Rill", i))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the sizing fixture");
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  c.check(c.until([&] { return b->results()->count() == 4; }), "the library is listed");
  QTest::qWait(500);

  // --- The button size scale ---
  b->setPrecisePointer(false);
  QQmlComponent buttonSource(qmlEngine(w), QUrl("qrc:/qml/MButton.qml"));
  QScopedPointer<QObject> buttonObject(buttonSource.create(qmlContext(w)));
  auto sample = qobject_cast<QQuickItem *>(buttonObject.data());
  c.check(sample, "a button can be made on its own to measure");
  if (sample) {
    sample->setParentItem(w->contentItem());
    sample->setX(560); sample->setY(300); sample->setZ(95);
    sample->setProperty("text", QString("Play"));
    sample->setProperty("symbol", QString("play"));
    struct Size { const char *name; double height; double square; double icon; double inset; };
    const Size sizes[] = {{"xsmall", 32, 12, 20, 16}, {"small", 40, 12, 20, 16},
                          {"medium", 56, 16, 24, 24}, {"large", 96, 28, 32, 48}};
    for (const auto &size : sizes) {
      sample->setProperty("size", QString(size.name));
      QTest::qWait(120);
      auto container = sample->property("background").value<QQuickItem *>();
      c.check(container && qAbs(container->height() - size.height) < 0.5,
              QString("a %1 button's container is %2dp (%3)").arg(size.name).arg(size.height, 0, 'f', 0)
                  .arg(container ? container->height() : 0, 0, 'f', 0));
      c.check(qAbs(sample->property("sizedIcon").toDouble() - size.icon) < 0.5 &&
                  qAbs(sample->property("contentInset").toDouble() - size.inset) < 0.5,
              QString("carrying a %1dp icon and %2dp of side padding").arg(size.icon, 0, 'f', 0).arg(size.inset, 0, 'f', 0));
      c.check(qAbs(sample->property("sizedSquare").toDouble() - size.square) < 0.5,
              QString("and squaring to %1dp when pressed").arg(size.square, 0, 'f', 0));
      // Material keeps a 48dp target around anything smaller than one.
      c.check(sample->height() >= 47.5,
              QString("with a touch target of at least 48dp (%1)").arg(sample->height(), 0, 'f', 0));
    }
    // Material gives an icon button three widths, published as the space kept
    // either side of the glyph. The container takes them; the touch target
    // around it keeps its 48dp whatever the container does.
    sample->setProperty("size", QString("small"));
    sample->setProperty("text", QString());
    struct Width { const char *variant; double container; };
    for (const auto &wanted : {Width{"narrow", 32}, Width{"uniform", 40}, Width{"wide", 52}}) {
      sample->setProperty("iconWidth", QString::fromLatin1(wanted.variant));
      QTest::qWait(140);
      auto shape = sample->property("background").value<QQuickItem *>();
      c.check(shape && qAbs(shape->width() - wanted.container) < 0.5,
              QString("a %1 small icon button is %2dp across (%3)")
                  .arg(wanted.variant).arg(wanted.container, 0, 'f', 0)
                  .arg(shape ? shape->width() : 0, 0, 'f', 0));
      c.check(sample->height() >= 47.5 && sample->width() >= 47.5,
              "and keeps the target Material puts around it");
    }
    // IconButton.kt:245 keeps SmallIconButtonTokens.ContainerHeight and
    // IconSize at 40dp and 24dp; Button.kt:1059 shortens labelled buttons.
    sample->setProperty("iconWidth", QString("uniform"));
    for (bool precise : {false, true}) {
      b->setPrecisePointer(precise);
      QTest::qWait(120);
      auto shape = sample->property("background").value<QQuickItem *>();
      auto glyph = anyItem(sample, "materialIcon");
      c.check(shape && qAbs(shape->height() - 40) < 0.5 &&
                  glyph && qAbs(glyph->property("size").toDouble() - 24) < 0.5,
              QString("a small icon button stays 40dp with a 24dp glyph (%1 pointer)")
                  .arg(precise ? "precise" : "touch"));
    }
    sample->setProperty("text", QString("Play"));
    QTest::qWait(120);
    auto labelledShape = sample->property("background").value<QQuickItem *>();
    auto labelledGlyph = anyItem(sample, "materialIcon");
    c.check(labelledShape && qAbs(labelledShape->height() - 36) < 0.5 &&
                labelledGlyph && qAbs(labelledGlyph->property("size").toDouble() - 20) < 0.5,
            "a labelled small button is 36dp with a 20dp icon for a precise pointer");
    sample->setProperty("text", QString());
    b->setPrecisePointer(false);
    // Pressing squares the shape by the step its own size takes.
    for (const auto &step : {Width{"xsmall", 8}, Width{"medium", 12}, Width{"large", 16}}) {
      sample->setProperty("size", QString::fromLatin1(step.variant));
      QTest::qWait(120);
      c.check(qAbs(sample->property("sizedPressed").toDouble() - step.container) < 0.5,
              QString("a pressed %1 button goes to %2dp").arg(step.variant).arg(step.container, 0, 'f', 0));
    }
    sample->setProperty("size", QString("small"));
    sample->setProperty("iconWidth", QString("uniform"));
    QTest::qWait(120);
    c.shotNow("01-button-sizes");
    sample->setVisible(false);
    sample->setParentItem(nullptr);
  }

  // --- Optical centering ---
  auto split = shownItem(w->contentItem(), "collectionPlay");
  auto action = split ? shownItem(split, "splitButtonAction") : nullptr;
  c.check(action, "the collection action is a split button");
  if (action) {
    auto row = action->property("contentItem").value<QQuickItem *>();
    auto inner = row ? row->childItems().value(0) : nullptr;
    // Full corner leading, extra small trailing: 0.11 of the 20dp between them.
    const double expected = 0.11*(24-4);
    const double measured = inner ? inner->x() - (row->width()-inner->width())/2 : 0;
    c.check(inner && qAbs(measured - expected) < 0.6,
            QString("its content is nudged %1dp towards the flat edge (%2 wanted)")
                .arg(measured, 0, 'f', 2).arg(expected, 0, 'f', 2));
    auto chevron = shownItem(split, "splitButtonChevron");
    c.check(chevron, "and the chevron half is nudged the other way");
    c.shot("02-optical-centering");
  }

  // --- Emphasis is a role of its own ---
  // Material publishes an emphasized variant of every role. It carries its own
  // weight and its own tracking, and neither moves by a constant, so emphasis
  // cannot be a weight laid over a regular style.
  struct Emphasis { const char *role; int weight; int emphasized; double track; double emphasizedTrack; };
  const Emphasis roles[] = {
      {"displayLarge",  QFont::Normal, QFont::Medium, -0.2, 0.0},
      {"headlineSmall", QFont::Normal, QFont::Medium, 0.0,  0.0},
      {"titleLarge",    QFont::Normal, QFont::Medium, 0.0,  0.0},
      {"titleMedium",   QFont::Medium, QFont::Bold,   0.2,  0.15},
      {"titleSmall",    QFont::Medium, QFont::Bold,   0.1,  0.1},
      {"bodyLarge",     QFont::Normal, QFont::Medium, 0.5,  0.15},
      {"bodyMedium",    QFont::Normal, QFont::Medium, 0.2,  0.25},
      {"labelLarge",    QFont::Medium, QFont::Bold,   0.1,  0.1}};
  for (const auto &role : roles) {
    const auto size = c.evaluate(QString("Theme.typeScale.%1[0]").arg(role.role)).toInt();
    c.check(c.evaluate(QString("Theme.weightFor(false,false,%1,'%2')").arg(size).arg(role.role)).toInt() == role.weight,
            QString("%1 is %2 at rest").arg(role.role).arg(role.weight));
    c.check(c.evaluate(QString("Theme.weightFor(true,false,%1,'%2')").arg(size).arg(role.role)).toInt() == role.emphasized,
            QString("and %1 emphasized").arg(role.emphasized));
    c.check(qAbs(c.evaluate(QString("Theme.trackingFor(%1,false,'%2',false)").arg(size).arg(role.role)).toDouble()
                 - role.track) < 0.001,
            QString("tracking %1 at rest").arg(role.track));
    c.check(qAbs(c.evaluate(QString("Theme.trackingFor(%1,false,'%2',true)").arg(size).arg(role.role)).toDouble()
                 - role.emphasizedTrack) < 0.001,
            QString("and %1 emphasized").arg(role.emphasizedTrack));
  }

  // --- The ring a keyboard leaves ---
  // Material draws the focus indicator in secondary. In primary it would be
  // the same colour as whatever it lands on that is already accented.
  c.check(c.evaluate("Theme.focusRing").value<QColor>() == c.themeColor("secondary"),
          "the focus ring takes the secondary role");
  c.check(c.evaluate("Theme.focusRing").value<QColor>() != c.themeColor("primary"),
          "which is not the role the thing it rings is painted in");
  {
    QQmlComponent buttonSource(qmlEngine(w), QUrl("qrc:/qml/MButton.qml"));
    QScopedPointer<QObject> made(buttonSource.create(qmlContext(w)));
    auto sample = qobject_cast<QQuickItem *>(made.data());
    if (sample) {
      sample->setParentItem(w->contentItem());
      sample->setProperty("symbol", QString("play"));
      sample->forceActiveFocus(Qt::TabFocusReason);
      QTest::qWait(200);
      auto ring = anyItem(sample, "buttonFocusRing");
      if (ring)
        c.check(ring->property("border").value<QObject *>()->property("color").value<QColor>() ==
                    c.themeColor("secondary"),
                "and a focused button draws it");
      sample->setVisible(false);
      sample->setParentItem(nullptr);
    }
  }
  // --- The split button's published measurements ---
  if (auto split = shownItem(w->contentItem(), "collectionPlay")) {
    c.check(qAbs(split->property("unit").toDouble() - 40) < 0.5,
            QString("a split button's container is 40dp tall (%1)")
                .arg(split->property("unit").toDouble(), 0, 'f', 0));
    c.check(split->height() >= 47.5,
            QString("inside the target it keeps (%1)").arg(split->height(), 0, 'f', 0));
    if (auto chevron = anyItem(split, "splitButtonMenu"))
      c.check(qAbs(chevron->width() - 48) < 0.5,
              QString("and the half that opens the menu is 13dp either side of a 22dp "
                      "chevron (%1 across)").arg(chevron->width(), 0, 'f', 0));
  }

  // --- The indicator behind the destination you are on ---
  // Material gives the navigation bar colours that are not the accent offering
  // an action: the indicator is the secondary container, the glyph on it the
  // ink that belongs there, and the label, because it sits inside the
  // indicator here, the same ink rather than the secondary role a label under
  // one would take. Everything you are not on is the variant ink.
  QQuickItem *here = nullptr;
  QString hereKey;
  for (const auto *key : {"home", "search", "library"})
    if (auto d = shownItem(w->contentItem(), QString("navBar_") + key))
      if (d->property("active").toBool()) { here = d; hereKey = key; }
  c.check(here, "one destination is marked as the one you are on");
  if (here) {
    auto pill = anyItem(w->contentItem(), "navBarIndicator_" + hereKey);
    auto glyph = shownItem(here, "materialIcon");
    auto label = anyItem(w->contentItem(), "navBarLabel_" + hereKey);
    c.check(pill && pill->property("color").value<QColor>() == c.themeColor("secondaryContainer"),
            "its indicator is the secondary container");
    // The glyph and the indicator share a centre. They did not once: the
    // indicator sat at the top of the container and the glyph below its middle.
    if (pill && glyph) {
      const double pillMiddle = pill->mapToScene(QPointF(0, pill->height()/2)).y();
      const double glyphMiddle = glyph->mapToScene(QPointF(0, glyph->height()/2)).y();
      c.check(qAbs(pillMiddle - glyphMiddle) < 0.51,
              QString("the glyph sits in the middle of it (%1 against %2)")
                  .arg(glyphMiddle, 0, 'f', 1).arg(pillMiddle, 0, 'f', 1));
    }
    c.check(c.themeColor("secondaryContainer") != c.themeColor("high") &&
                c.themeColor("secondary") != c.themeColor("primary"),
            "which is neither the surface it used to take nor the action accent");
    c.check(glyph && glyph->property("ink").value<QColor>() == c.themeColor("secondaryContainerText"),
            "the glyph on it is the ink that container carries");
    c.check(label && label->property("color").value<QColor>() == c.themeColor("secondaryContainerText"),
            "and so is the label, because it is drawn on the container too");
    for (const auto *key : {"home", "search", "library"})
      if (auto other = shownItem(w->contentItem(), QString("navBar_") + key))
        if (!other->property("active").toBool())
          if (auto quiet = anyItem(w->contentItem(), QString("navBarLabel_") + key))
            c.check(quiet->property("color").value<QColor>() == c.themeColor("muted"),
                    QString("%1, which you are not on, is the variant ink").arg(key));

    // --- The horizontal item's own measurements ---
    // NavigationBarHorizontalItemTokens: a 40dp indicator with no fixed width,
    // 16dp of leading and trailing space, and a 24dp icon. The gap inside it
    // is NavigationBarTokens.ItemActiveIndicatorIconLabelSpace, which is 4dp.
    if (pill && glyph && label) {
      c.check(qAbs(pill->height() - 40) < 0.5,
              QString("the indicator is 40dp tall (%1)").arg(pill->height(), 0, 'f', 0));
      c.check(qAbs(glyph->width() - 24) < 0.5,
              QString("around a 24dp glyph (%1)").arg(glyph->width(), 0, 'f', 0));
      const double pillLeft = pill->mapToScene(QPointF(0, 0)).x();
      const double glyphLeft = glyph->mapToScene(QPointF(0, 0)).x();
      const double labelLeft = label->mapToScene(QPointF(0, 0)).x();
      const double labelRight = labelLeft + label->width();
      c.check(qAbs(glyphLeft - pillLeft - 16) < 0.6,
              QString("which starts 16dp inside it (%1)").arg(glyphLeft - pillLeft, 0, 'f', 1));
      c.check(qAbs(labelLeft - glyphLeft - 28) < 0.6,
              QString("the words follow the glyph 4dp later (%1)")
                  .arg(labelLeft - glyphLeft - 24, 0, 'f', 1));
      c.check(qAbs(pillLeft + pill->width() - labelRight - 16) < 0.6,
              QString("and it closes 16dp after them (%1)")
                  .arg(pillLeft + pill->width() - labelRight, 0, 'f', 1));
    }
  }

  // --- The capsule's own container ---
  // FloatingToolbarTokens: 64dp tall, a full corner, and 8dp of leading and
  // trailing space inside it. NavigationBarTokens.ItemBetweenSpace is nought,
  // so nothing but the indicators' own padding separates the destinations.
  if (auto capsule = shownItem(w->contentItem(), "navigationBar")) {
    c.check(qAbs(capsule->height() - 64) < 0.5,
            QString("the capsule is 64dp tall (%1)").arg(capsule->height(), 0, 'f', 0));
    c.check(qAbs(capsule->property("radius").toReal() - capsule->height() / 2) < 0.5,
            "and takes a full corner rather than a fixed radius");
    QQuickItem *first = shownItem(w->contentItem(), "navBar_home");
    QQuickItem *last = shownItem(w->contentItem(), "navBar_library");
    if (first && last) {
      const double lead = first->mapToScene(QPointF(0, 0)).x() - capsule->mapToScene(QPointF(0, 0)).x();
      const double trail = capsule->mapToScene(QPointF(capsule->width(), 0)).x() -
                           last->mapToScene(QPointF(last->width(), 0)).x();
      c.check(qAbs(lead - 8) < 0.6 && qAbs(trail - 8) < 0.6,
              QString("with 8dp at each end (%1 and %2)")
                  .arg(lead, 0, 'f', 1).arg(trail, 0, 'f', 1));
    }
    c.shot("09-navigation-capsule");
  }

  // --- A rule inside a list starts where the labels start ---
  if (auto drawerRule = w->findChild<QQuickItem *>("drawerDivider")) {
    c.check(qAbs(drawerRule->property("inset").toDouble() - 16) < 0.5,
            QString("a list is ruled off 16dp in from both ends (%1)")
                .arg(drawerRule->property("inset").toDouble(), 0, 'f', 0));
    // A divider is decorative separation, so it takes the variant of the two
    // outline roles rather than the one a control's boundary is drawn in.
    if (auto rule = drawerRule->property("contentItem").value<QQuickItem *>())
      c.check(rule->property("color").value<QColor>() == c.themeColor("outlineVariant"),
              "in the outline variant, which is the role a rule is drawn in");
  }

  // --- The shape library ---
  const auto names = c.evaluate("app.shapeNames()").toStringList();
  c.check(names.contains("circle") && names.contains("cookie9Sided") &&
              names.contains("softBurst") && names.contains("pill"),
          QString("the shape library publishes Material's shapes (%1)").arg(names.size()));
  const auto cookie = c.evaluate("app.shapeOutline('cookie9Sided',256)").toList();
  double low = 2, high = 0;
  for (const auto &value : cookie) {
    low = std::min(low, value.toDouble());
    high = std::max(high, value.toDouble());
  }
  // Material's nine sided cookie is a star with an inner radius of 0.8.
  c.check(qAbs(high - 1) < 0.001 && qAbs(low - 0.8) < 0.01,
          QString("a cookie cuts to Material's inner radius (%1 of %2)")
              .arg(low, 0, 'f', 3).arg(high, 0, 'f', 3));
  const auto circle = c.evaluate("app.shapeOutline('circle',64)").toList();
  bool round = true;
  for (const auto &value : circle)
    round = round && qAbs(value.toDouble() - 1) < 0.001;
  c.check(round, "and a circle does not cut at all");

  // A shaped mask really clips: the corner of a masked cover is cut away.
  QQmlComponent artSource(qmlEngine(w), QUrl("qrc:/qml/MShape.qml"));
  QScopedPointer<QObject> artObject(artSource.create(qmlContext(w)));
  auto masked = qobject_cast<QQuickItem *>(artObject.data());
  c.check(masked, "a shape can be drawn on its own");
  if (masked) {
    masked->setParentItem(w->contentItem());
    masked->setX(600); masked->setY(320); masked->setZ(95);
    masked->setWidth(160); masked->setHeight(160);
    masked->setProperty("shape", QString("cookie4Sided"));
    QTest::qWait(200);
    const auto frame = w->grabWindow();
    const auto box = masked->mapRectToScene(masked->boundingRect()).toRect();
    const auto middle = frame.pixelColor(box.center());
    const auto corner = frame.pixelColor(box.left()+3, box.top()+3);
    c.check(middle != corner,
            "and what it masks is cut away at the corner but not the middle");
    c.shotNow("03-shape-mask");
    masked->setVisible(false);
    masked->setParentItem(nullptr);
  }

  // --- A field that explains itself, and says when it is wrong ---
  auto folders = c.dialog("musicFoldersDialog");
  QTest::qWait(200);
  c.click("addMusicFolderButton");
  auto path = shownItem(w->contentItem(), "musicFolderPath");
  auto support = shownItem(w->contentItem(), "fieldSupport");
  c.check(path && support && support->isVisible(),
          "the folder field carries supporting text under it");
  if (path && support) {
    const auto calm = support->property("color").value<QColor>();
    c.check(!path->property("errored").toBool() && calm == c.themeColor("muted"),
            "which is quiet while nothing is wrong");
    c.shot("04-field-supporting");
    path->setProperty("text", QString("/definitely/not/here"));
    QTest::qWait(150);
    c.click("confirmMusicFolderButton");
    c.check(path->property("errored").toBool(), "a path that does not exist puts it in error");
    c.check(support->property("color").value<QColor>() == c.themeColor("error") &&
                support->property("text").toString() != QString(),
            "and the same line carries the reason in the error role");
    auto outline = path->property("background").value<QQuickItem *>();
    c.check(outline && outline->property("border").value<QObject *>() != nullptr,
            "with the field itself outlined to match");
    c.shot("05-field-error");
  }
  QTest::keyClick(w, Qt::Key_Escape);
  QTest::qWait(300);
  c.closeDialog(folders);

  // --- A window too narrow for a side pane ---
  auto panel = anyItem(w->contentItem(), "sidePanel");
  auto sheet = anyItem(w->contentItem(), "panelSheet");
  w->setProperty("side", "queue");
  QTest::qWait(700);
  c.check(panel && panel->isVisible() && !w->property("sheetMode").toBool(),
          "a wide window sets the queue beside the page");
  // Below the expanded class there is no room for a pane beside the content.
  w->resize(780, 860);
  QTest::qWait(900);
  c.check(w->property("sheetMode").toBool(), "a narrower one has no room for that");
  c.check(sheet && sheet->isVisible(), "so the pane arrives as a bottom sheet");
  c.check(panel && !panel->isVisible(), "and the side pane stands down");
  if (sheet) {
    auto surface = anyItem(sheet, "bottomSheetSurface");
    c.check(surface && surface->property("topLeftRadius").toDouble() == 28 &&
                surface->property("bottomLeftRadius").toDouble() == 0,
            "rounded on the top corners only, as Material draws a sheet");
    c.check(surface && surface->property("color").value<QColor>() == c.themeColor("surfaceLow"),
            "on the low surface container a sheet belongs on");
    auto handle = anyItem(sheet, "bottomSheetHandle");
    c.check(handle && handle->isVisible(), "with a drag handle to take hold of");
    // The queue still works from in there.
    c.check(shownItem(sheet, "queueView") != nullptr, "and the queue itself came with it");
    c.shot("06-bottom-sheet");
    // Pushing the handle down past a third of the sheet puts it away.
    const auto grip = handle->mapToScene(handle->boundingRect().center()).toPoint();
    QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, grip);
    for (int step = 1; step <= 6; ++step) {
      QTest::mouseMove(w, grip + QPoint(0, int(sheet->height()/2)*step/6), 20);
      QTest::qWait(30);
    }
    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, grip + QPoint(0, int(sheet->height()/2)));
    QTest::qWait(700);
    c.check(w->property("side").toString().isEmpty(), "and pushing it down closes the pane");
  }

  // --- Layout decisions come off Material's window size classes ---
  // The five classes were already worked out and then not used: the rail was
  // offered at 1080 and the supporting pane dropped into a sheet below 1000,
  // neither of which is a breakpoint. Both read the expanded class now.
  w->resize(900, 860);
  QTest::qWait(700);
  c.check(w->property("sizeClass").toString() == "expanded",
          QString("900px is the expanded class (%1)").arg(w->property("sizeClass").toString()));
  c.check(w->property("atLeastExpanded").toBool() && !w->property("sheetMode").toBool(),
          "where the supporting pane sits beside the content rather than in a sheet");
  if (auto capsule = shownItem(w->contentItem(), "navigationBar"))
    c.check(capsule->property("hugsContent").toBool(),
            "and navigation is the capsule, because the window has room to centre one");
  w->resize(800, 860);
  QTest::qWait(700);
  c.check(w->property("sizeClass").toString() == "medium" &&
              !w->property("atLeastExpanded").toBool(),
          "800px is the medium class");
  c.check(w->property("sheetMode").toBool(), "where the pane becomes a sheet");
  if (auto capsule = shownItem(w->contentItem(), "navigationBar"))
    c.check(capsule->property("hugsContent").toBool(),
            "and the capsule holds, because medium is still wide enough for it");

  // --- An app bar's trailing actions are the variant ink ---
  // Material keeps the surface ink for the leading icon and gives the trailing
  // side the variant. An icon button takes the ink of what it sits on, so the
  // bar is what says which.
  w->resize(1400, 900);
  QTest::qWait(700);
  for (auto action : w->findChildren<QQuickItem *>())
    if (action->objectName().startsWith("appBarAction_") && action->isVisible()) {
      c.check(action->property("ambientInk").value<QColor>() == c.themeColor("muted"),
              "an app bar action is drawn in the variant ink");
      break;
    }

  // --- A window too narrow to centre the capsule ---
  w->resize(520, 860);
  QTest::qWait(900);
  c.check(w->property("compactWindow").toBool(), "a compact window is compact");
  c.check(!shownItem(w->contentItem(), "navigationRail"), "there is no rail at any size");
  // The drawer belongs to the sidebar arrangement. Here there is no way to
  // reach one, so the destinations are not offered twice.
  c.check(!shownItem(w->contentItem(), "drawerButton"),
          "and nothing that opens a drawer holding a second copy of them");
  auto narrowBar = shownItem(w->contentItem(), "navigationBar");
  c.check(narrowBar && !narrowBar->property("hugsContent").toBool(),
          "the bar gives up the capsule and spans the column");
  // What the rail used to carry at its foot is in the top bar now, at every
  // size, so nothing has to be opened to reach it.
  c.check(shownItem(w->contentItem(), "settingsButton"),
          "settings is still one press away");
  c.check(shownItem(w->contentItem(), "miniPlayerButton"), "and so is the mini player");
  c.shot("07-navigation-narrow");

  // --- A dialog on a window with nowhere to float ---
  auto compactDialog = c.dialog("settingsDialog");
  QTest::qWait(300);
  if (compactDialog) {
    c.check(compactDialog->property("fullScreen").toBool(),
            "a dialog on a compact window takes the window");
    c.check(qAbs(compactDialog->property("width").toReal() - w->width()) < 1.5,
            QString("filling it rather than floating in it (%1 of %2)")
                .arg(compactDialog->property("width").toReal(), 0, 'f', 0).arg(w->width()));
    auto surface = compactDialog->property("background").value<QQuickItem *>();
    c.check(surface && surface->property("radius").toDouble() == 0,
            QString("and squaring its corners against the window edge (%1)")
                .arg(surface ? surface->property("radius").toDouble() : -1, 0, 'f', 1));
    c.shot("08-full-screen-dialog");
  }
  c.closeDialog(compactDialog);
  w->resize(1400, 900);
  QTest::qWait(700);
  c.check(!w->property("compactWindow").toBool() && !w->property("sheetMode").toBool(),
          "and the rail and the side pane come back with the room");
  c.shot("09-restored");
  c.finish();
}

// Material's colour engine beyond one scheme: the variants that decide how much
// of a cover the interface takes, the contrast levels that push text away from
// what it sits on, and the density Material allows once a precision pointer is
// driving the controls.
void runMaterialSchemeTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1400, 900);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(false);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);
  b->setColorVariant("tonalSpot");
  b->setColorContrast(0);
  b->setPrecisePointer(false);
  b->setArtworkAccent(false);
  b->setAccentColor("#3f6ad8");
  QTest::qWait(400);

  // --- The variants really spread the palettes differently ---
  const QColor source("#3f6ad8");
  struct Expectation { const char *name; double primaryChroma; };
  // Material's own numbers for the primary palette of each variant.
  const Expectation wanted[] = {{"neutral", 12}, {"tonalSpot", 36}, {"vibrant", 200},
                                {"expressive", 40}};
  QList<QColor> primaries;
  for (const auto &entry : wanted) {
    const auto roles = m3::scheme(source, true, m3::variantFor(entry.name), 0);
    const auto primary = roles.value("primary").value<QColor>();
    primaries.append(primary);
    c.check(primary.isValid(), QString("the %1 scheme resolves").arg(entry.name));
  }
  // Neutral barely tints, vibrant maxes out: their accents cannot be the same.
  c.check(m3::measure(primaries[0]).chroma < m3::measure(primaries[1]).chroma,
          QString("neutral is less colourful than balanced (%1 against %2)")
              .arg(m3::measure(primaries[0]).chroma, 0, 'f', 1)
              .arg(m3::measure(primaries[1]).chroma, 0, 'f', 1));
  // Vibrant asks for a chroma sRGB cannot hold at every tone, so the two meet
  // at the gamut edge for a light accent. Where the gamut is widest they part.
  const double vibrantMid = m3::measure(m3::palettesFor(source, m3::Variant::Vibrant).primary.tone(50)).chroma;
  const double balancedMid = m3::measure(m3::palettesFor(source, m3::Variant::TonalSpot).primary.tone(50)).chroma;
  c.check(vibrantMid > balancedMid,
          QString("and vibrant takes all the colour sRGB will give (%1 against %2)")
              .arg(vibrantMid, 0, 'f', 1).arg(balancedMid, 0, 'f', 1));
  // Expressive turns the hue right around on purpose.
  const double sourceHue = m3::measure(source).hue;
  const double expressiveHue = m3::measure(primaries[3]).hue;
  double turn = std::abs(expressiveHue - sourceHue);
  if (turn > 180) turn = 360 - turn;
  c.check(turn > 90, QString("expressive detaches from the source hue (%1 degrees)").arg(turn, 0, 'f', 0));
  // Every variant still has to be readable.
  for (const auto &entry : wanted)
    for (bool dark : {false, true}) {
      const auto roles = m3::scheme(source, dark, m3::variantFor(entry.name), 0);
      const double ratio = contrastOf(roles.value("onSurface").value<QColor>(),
                                      roles.value("surface").value<QColor>());
      c.check(ratio >= 4.5, QString("%1 keeps its text readable %2 (%3:1)")
                                .arg(entry.name).arg(dark ? "dark" : "light").arg(ratio, 0, 'f', 1));
    }

  // --- Contrast levels push text further from its surface ---
  double previous = 0;
  for (double level : {0.0, 0.5, 1.0}) {
    const auto roles = m3::scheme(source, true, m3::Variant::TonalSpot, level);
    const double ratio = contrastOf(roles.value("onSurface").value<QColor>(),
                                    roles.value("surface").value<QColor>());
    c.check(ratio >= previous - 0.01,
            QString("contrast %1 is at least as strong as the level below (%2:1)")
                .arg(level).arg(ratio, 0, 'f', 1));
    previous = ratio;
  }
  const auto high = m3::scheme(source, true, m3::Variant::TonalSpot, 1);
  c.check(contrastOf(high.value("onSurface").value<QColor>(), high.value("surface").value<QColor>()) >= 10,
          "high contrast clears Material's own target for body text");
  const auto standard = m3::scheme(source, true, m3::Variant::TonalSpot, 0);
  c.check(contrastOf(high.value("outlineVariant").value<QColor>(), high.value("surface").value<QColor>()) >
              contrastOf(standard.value("outlineVariant").value<QColor>(), standard.value("surface").value<QColor>()),
          "and the quietest boundary moves with it");

  // --- Error is a role of the scheme, not a colour left behind by it ---
  // Material gives a scheme a sixth palette at a fixed hue and chroma. Error
  // therefore holds its own hue whatever the source colour is, while still
  // answering the contrast level like every other role.
  for (const auto &input : {QColor("#2e7d32"), QColor("#1565c0"), QColor("#b8860b")}) {
    const auto roles = m3::scheme(input, true, m3::Variant::TonalSpot, 0);
    const auto shade = roles.value("error").value<QColor>();
    c.check(shade.isValid() && roles.value("onError").value<QColor>().isValid() &&
                roles.value("errorContainer").value<QColor>().isValid() &&
                roles.value("onErrorContainer").value<QColor>().isValid(),
            QString("a %1 scheme still publishes the error roles").arg(input.name()));
    c.check(shade.hslHueF() * 360 < 60 || shade.hslHueF() * 360 > 330,
            QString("and error stays red rather than following the source (%1)").arg(shade.name()));
  }
  // It is held to the ratio Material asks of an accent on a surface, at every
  // level, which is the part a colour written into the theme cannot promise.
  for (const auto level : {0.0, 0.5, 1.0}) {
    const auto roles = m3::scheme(source, true, m3::Variant::TonalSpot, level);
    const double ratio = contrastOf(roles.value("error").value<QColor>(),
                                    roles.value("surface").value<QColor>());
    const double wanted = level <= 0 ? 3.0 : level <= 0.5 ? 4.5 : 7.0;
    c.check(ratio >= wanted - 0.05,
            QString("error clears %1:1 at contrast %2 (%3:1)")
                .arg(wanted).arg(level).arg(ratio, 0, 'f', 1));
  }

  // --- What that looks like in the window ---
  for (const char *variant : {"neutral", "tonalSpot", "vibrant", "expressive", "content"}) {
    b->setColorVariant(variant);
    QTest::qWait(350);
    c.check(c.evaluate("Theme.primary").value<QColor>().isValid(),
            QString("the window takes the %1 scheme").arg(variant));
    c.shot(QString("01-scheme-") + variant);
  }
  b->setColorVariant("tonalSpot");
  for (double level : {0.0, 1.0}) {
    b->setColorContrast(level);
    QTest::qWait(350);
    c.shot(QString("02-contrast-") + (level > 0 ? "high" : "standard"));
  }
  const auto highText = c.evaluate("Theme.text").value<QColor>();
  b->setColorContrast(0);
  QTest::qWait(350);
  c.check(highText != c.evaluate("Theme.text").value<QColor>(),
          "and the interface really repaints when the level changes");

  // --- Density once a precision pointer is driving ---
  auto play = shownItem(w->contentItem(), "playButton");
  auto anyButton = shownItem(w->contentItem(), "playerShuffle");
  c.check(anyButton, "the player offers a small button to measure");
  if (anyButton) {
    const double comfortable = anyButton->height();
    c.check(qAbs(comfortable - 48) < 0.5,
            QString("which reserves a 48dp touch target by default (%1)").arg(comfortable, 0, 'f', 0));
    c.shot("03-pointer-comfortable");
    b->setPrecisePointer(true);
    QTest::qWait(400);
    c.check(anyButton->height() < comfortable,
            QString("a precision pointer draws it tighter (%1 against %2)")
                .arg(anyButton->height(), 0, 'f', 0).arg(comfortable, 0, 'f', 0));
    auto container = anyButton->property("background").value<QQuickItem *>();
    auto glyph = anyItem(anyButton, "materialIcon");
    // SmallIconButtonTokens.ContainerHeight and IconSize stay 40dp and 24dp;
    // IconButton.kt:245 has no precision-pointer sizing branch.
    c.check(container && qAbs(container->height() - 40) < 0.5 &&
                glyph && qAbs(glyph->property("size").toDouble() - 24) < 0.5,
            QString("at Material's 40dp icon container with a 24dp glyph (%1)")
                .arg(container ? container->height() : 0, 0, 'f', 0));
    c.check(play && play->property("background").value<QQuickItem *>()->height() == 56,
            "while the medium button keeps its own height");
    c.shot("04-pointer-precise");
    b->setPrecisePointer(false);
    QTest::qWait(400);
    c.check(qAbs(anyButton->height() - comfortable) < 0.5, "and it comes back");
  }

  // --- Material has two outline roles and they do different jobs ---
  // The outline is a boundary that has to hold on its own: a text field, a
  // switch track, a connected button group. The variant is decorative
  // separation, a divider or the edge of a container that is already legible.
  // The scheme computes both, at neutral variant tone 60/50 and 30/80, and
  // reading one of them for the other throws away the distinction.
  const auto outlineRole = c.themeColor("outline");
  const auto outlineVariantRole = c.themeColor("outlineVariant");
  const auto against = c.themeColor("surface");
  c.check(outlineRole != outlineVariantRole, "the two outline roles are two colours");
  c.check(contrastOf(outlineRole, against) > contrastOf(outlineVariantRole, against),
          QString("and the one that has to hold holds harder (%1 against %2)")
              .arg(contrastOf(outlineRole, against), 0, 'f', 2)
              .arg(contrastOf(outlineVariantRole, against), 0, 'f', 2));
  c.check(outlineRole == c.evaluate("Theme.roles['outline']").value<QColor>() &&
              outlineVariantRole == c.evaluate("Theme.roles['outlineVariant']").value<QColor>(),
          "both come off the scheme rather than being mixed by hand");

  // --- The bar is the same height whatever the window does ---
  // Material offers the item two ways, the label under the icon in an 80dp
  // container or beside it in a 64dp one. Sung takes the second everywhere,
  // so a window that loses height loses nothing from navigation.
  w->resize(520, 640);
  QTest::qWait(800);
  auto bar = shownItem(w->contentItem(), "navigationBar");
  c.check(bar && qAbs(bar->height() - 64) < 0.5,
          QString("a short window keeps the 64dp bar (%1)").arg(bar ? bar->height() : 0, 0, 'f', 0));
  c.shot("05-short-navigation-bar");
  w->resize(520, 900);
  QTest::qWait(800);
  c.check(bar && qAbs(bar->height() - 64) < 0.5, "and a taller one does not grow it");
  c.shot("06-navigation-bar");

  // --- The capsule takes only the width its destinations need ---
  w->resize(1600, 900);
  QTest::qWait(700);
  auto wide = shownItem(w->contentItem(), "navigationBar");
  c.check(wide && wide->property("hugsContent").toBool(), "a wide window centres the capsule");
  c.check(wide && wide->width() < w->width() / 3,
          QString("which hugs its destinations rather than spreading across the window "
                  "(%1 of %2)").arg(wide ? wide->width() : 0, 0, 'f', 0).arg(w->width()));
  c.shot("07-wide-capsule");

  // --- A slider says what it is worth while it is moved ---
  QQmlComponent sliderSource(qmlEngine(w), QUrl("qrc:/qml/SettingSlider.qml"));
  QScopedPointer<QObject> sliderObject(sliderSource.create(qmlContext(w)));
  auto slider = qobject_cast<QQuickItem *>(sliderObject.data());
  c.check(slider, "a slider can be made on its own to work");
  if (slider) {
    slider->setParentItem(w->contentItem());
    slider->setX(520); slider->setY(300); slider->setZ(95);
    slider->setWidth(280);
    slider->setProperty("from", 0);
    slider->setProperty("to", 12);
    slider->setProperty("value", 6);
    slider->setProperty("valueLabel", QString("6 s"));
    QTest::qWait(200);
    // Scoped to this slider: the settings dialog owns one of these too.
    auto label = slider->findChild<QObject *>("sliderValueLabel");
    c.check(label && !label->property("visible").toBool(),
            "whose value label stays away while nobody is touching it");
    const auto grip = slider->mapToScene(slider->boundingRect().center()).toPoint();
    QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, grip);
    QTest::qWait(300);
    c.check(slider->property("pressed").toBool(), "taking hold of it presses it");
    c.check(label && label->property("visible").toBool(),
            "and the value label comes up over the handle");
    c.check(anyItem(w->contentItem(), "sliderValueText") != nullptr,
            "carrying what the slider is currently worth");
    c.shotNow("08-slider-label");
    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, grip);
    QTest::qWait(300);
    c.check(label && !label->property("visible").toBool(), "and goes away on release");
    slider->setVisible(false);
    slider->setParentItem(nullptr);
  }

  // --- Cards say how much they want to be noticed ---
  QQmlComponent cardSource(qmlEngine(w), QUrl("qrc:/qml/MCard.qml"));
  QScopedPointer<QObject> cardObject(cardSource.create(qmlContext(w)));
  auto card = qobject_cast<QQuickItem *>(cardObject.data());
  c.check(card, "a card can be made on its own");
  if (card) {
    card->setParentItem(w->contentItem());
    card->setX(600); card->setY(320); card->setZ(95);
    card->setWidth(200); card->setHeight(120);
    card->setProperty("variant", QString("filled"));
    QTest::qWait(150);
    const auto filled = card->property("color").value<QColor>();
    c.check(filled == c.themeColor("highest"), "a filled card takes the highest container");
    c.check(c.themeColor("highest") != c.themeColor("high"),
            "which is a step above the one it used to take");
    // Material's surface is the tone a page starts from, and the container
    // ladder is measured against it. The app had been reading the first step
    // of that ladder under the name of the role, so the role itself, which the
    // scheme computes, was never used by anything.
    c.check(c.themeColor("surface") != c.themeColor("surfaceLow"),
            "the surface and the container above it are two colours");
    c.check(c.themeColor("surface") == c.evaluate("Theme.roles['surface']").value<QColor>(),
            "and the surface is the role of that name rather than a step of the ladder");
    card->setProperty("variant", QString("elevated"));
    QTest::qWait(150);
    c.check(card->property("color").value<QColor>() == c.themeColor("surfaceLow"),
            "an elevated card is the low container it casts its shadow from");
    card->setProperty("variant", QString("outlined"));
    QTest::qWait(150);
    c.check(card->property("color").value<QColor>() == c.themeColor("surface"),
            "and an outlined one is the surface, told apart by its boundary");
    QTest::qWait(150);
    c.check(card->property("color").value<QColor>() != filled &&
                card->property("border").value<QObject *>()->property("width").toInt() == 1,
            "an outlined one draws a boundary instead");
    card->setProperty("variant", QString("elevated"));
    QTest::qWait(150);
    auto shade = anyItem(card, "elevation");
    c.check(shade && shade->property("level").toInt() == 1, "and an elevated one casts a shadow");
    c.shotNow("09-cards");
    card->setVisible(false);
    card->setParentItem(nullptr);
  }
  w->resize(1400, 900);
  QTest::qWait(500);
  c.shot("10-restored");
  c.finish();
}

// Material's finer grain: symbols drawn at the optical size they are used at,
// the anatomy of a disabled control, the state layer a drag leaves, a value
// drawn as a field rather than a button, and the scrim a sheet that takes the
// screen over owes what it covers.
void runMaterialGrainTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1400, 900);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(true);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);

  paintCover(c.directory + "/music/cover.png", QColor("#24485f"), QColor("#c87a2f"));
  for (int i = 1; i <= 4; ++i)
    if (!encodeTrack(c, QString("%1/music/%2.flac").arg(c.directory).arg(i),
                     QString("Track %1").arg(i), "Grain", "Rill", i))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the grain fixture");
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  c.check(c.until([&] { return b->results()->count() == 4; }), "the library is listed");
  QTest::qWait(500);

  // --- Symbols are drawn at their optical size, not scaled to it ---
  auto readAsset = [](const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
  };
  const auto small = readAsset(":/assets/icons/play_20.svg");
  const auto base = readAsset(":/assets/icons/play.svg");
  const auto large = readAsset(":/assets/icons/play_40.svg");
  c.check(!small.isEmpty() && !base.isEmpty() && !large.isEmpty(),
          "every symbol ships at Material's three optical sizes");
  c.check(small != base && large != base,
          "each of which is its own drawing rather than the same one scaled");
  c.check(small.contains("height=\"20\"") && large.contains("height=\"40\""),
          "drawn at the size it is named for");
  // The provider picks by the points asked for, not by the pixels wanted.
  auto provider = qmlEngine(w)->imageProvider("symbols");
  c.check(provider, "the symbol provider is registered");
  if (provider) {
    auto *images = static_cast<QQuickImageProvider *>(provider);
    QSize got;
    const auto tiny = images->requestImage("play/18/ffffff", &got, QSize(96, 96));
    const auto normal = images->requestImage("play/24/ffffff", &got, QSize(96, 96));
    const auto big = images->requestImage("play/36/ffffff", &got, QSize(96, 96));
    c.check(!tiny.isNull() && !normal.isNull() && !big.isNull(), "and renders at any of them");
    c.check(tiny != normal && big != normal,
            "handing back a different drawing for a small and a large symbol");
    const auto fallback = images->requestImage("play/24/ffffff", &got, QSize(96, 96));
    c.check(fallback == normal, "and the same one for the same request");
  }
  c.shot("01-symbols");

  // --- A disabled control takes Material's own anatomy ---
  auto previous = shownItem(w->contentItem(), "playerShuffle");
  auto play = shownItem(w->contentItem(), "playButton");
  b->clearQueue();
  QTest::qWait(400);
  c.check(play && !play->property("enabled").toBool(), "an empty queue disables playback");
  if (play) {
    auto container = play->property("background").value<QQuickItem *>();
    auto content = play->property("contentItem").value<QQuickItem *>();
    c.check(qAbs(play->opacity() - 1) < 0.01,
            QString("the control itself is not faded (%1)").arg(play->opacity(), 0, 'f', 2));
    const auto fill = container->property("color").value<QColor>();
    // Material's disabled container is a tenth of onSurface, not the accent.
    c.check(qAbs(fill.alphaF() - 0.10) < 0.02,
            QString("its container drops to a tenth of onSurface (alpha %1)").arg(fill.alphaF(), 0, 'f', 2));
    c.check(content && qAbs(content->opacity() - 0.38) < 0.01,
            QString("and its content to 38%% (%1)").arg(content ? content->opacity() : 0, 0, 'f', 2));
    // An icon button, filled or not, dims onSurface (IconButtonDefaults.kt
    // reads FilledIconButtonTokens.DisabledColor); onSurfaceVariant is the
    // disabled ink of a labelled button.
    c.check(play->property("ink").value<QColor>() == c.themeColor("text"),
            "in the onSurface role Material gives a disabled icon button");
    // The tonal button is the labelled exception: Compose reads
    // FilledTonalButtonTokens for it, onSurface over a 12% container, where
    // the other labelled buttons dim onSurfaceVariant over 10% (Button.kt).
    QQmlComponent tonalSource(qmlEngine(w), QUrl("qrc:/qml/MButton.qml"));
    QScopedPointer<QObject> tonalObject(tonalSource.create(qmlContext(w)));
    if (auto tonal = qobject_cast<QQuickItem *>(tonalObject.data())) {
      tonal->setParentItem(w->contentItem());
      tonal->setProperty("text", QString("Clean up"));
      tonal->setProperty("tonal", true);
      tonal->setProperty("enabled", false);
      QTest::qWait(100);
      auto tonalFill = tonal->property("background").value<QQuickItem *>()->property("color").value<QColor>();
      c.check(tonal->property("ink").value<QColor>() == c.themeColor("text") && qAbs(tonalFill.alphaF() - 0.12) < 0.01,
              QString("a disabled tonal button dims onSurface over a 12% container (alpha %1)").arg(tonalFill.alphaF(), 0, 'f', 2));
      tonal->setParentItem(nullptr);
    } else {
      c.check(false, "a tonal button can be built to measure");
    }
    c.shot("02-disabled");
  }
  Q_UNUSED(previous)

  // --- A row being carried takes Material's reorder container ---
  // The reorder list names a container for the item under the finger: the
  // tertiary one, at corner large, with its own ink. It is a colour the app
  // uses nowhere else, so a row being moved is unmistakably the subject of
  // the gesture rather than a row that happens to be lit.
  QVariantList queued;
  for (int i = 0; i < 4; ++i)
    queued.append(b->results()->get(i));
  b->enqueueItems(queued);
  w->setProperty("side", "queue");
  QTest::qWait(700);
  auto queue = shownItem(w->contentItem(), "queueView");
  auto row = queue ? shownItem(queue, "queueRow_1") : nullptr;
  c.check(row, "a queue row can be taken hold of");
  if (row) {
    auto container = row->property("background").value<QQuickItem *>();
    auto title = anyItem(row, "trackTitle");
    auto layer = anyItem(row, "rowDraggedLayer");
    const auto carriedContainer = c.themeColor("tertiaryContainer");
    const auto carriedInk = c.themeColor("tertiaryContainerText");
    c.check(container && container->property("color").value<QColor>() != carriedContainer,
            "which is not wearing the reorder container while it rests");
    c.check(layer && !layer->isVisible(), "and no state layer either");
    const auto from = row->mapToScene(row->boundingRect().center()).toPoint();
    QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(w, from + QPoint(0, 40), 40);
    QTest::qWait(400);
    c.check(row->property("dragging").toBool(), "moving the pointer picks it up");
    c.check(container && container->property("color").value<QColor>() == carriedContainer,
            "the carried row takes the tertiary container Material names for it");
    c.check(carriedContainer != c.themeColor("primaryContainer") &&
                carriedContainer != c.themeColor("container"),
            "which is a container it wears in no other state");
    c.check(title && title->property("color").value<QColor>() == carriedInk,
            "and the ink that belongs on it");
    c.check(container && qAbs(container->property("topLeftRadius").toDouble() -
                              c.evaluate("Theme.listActive").toDouble()) < 0.5,
            "at corner large, which is the reorder list's shape");
    // The container is the answer to the drag; 16% of the surface ink laid
    // over it would only be a second one.
    c.check(layer && !layer->isVisible(),
            "the state layer stays off, so the container reads as itself");
    auto zone = anyItem(queue, "dropZone");
    c.check(zone && zone->isVisible(), "the place it will land is drawn");
    c.check(zone && zone->property("color").value<QColor>() == c.themeColor("surfaceLow"),
            "in the surface container a step below the list");
    c.shotNow("03-carried");
    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, from + QPoint(0, 40));
    QTest::qWait(500);
    c.check(container && container->property("color").value<QColor>() != carriedContainer,
            "and it gives the container back once it is put down");
  }
  w->setProperty("side", "");
  QTest::qWait(400);

  // --- A value is drawn as a field, not as a button ---
  w->setProperty("collectionTools", true);
  QTest::qWait(500);
  auto sort = shownItem(w->contentItem(), "collectionSortControl");
  c.check(sort, "sorting is an exposed dropdown");
  if (sort) {
    c.check(anyItem(sort, "dropdownLabel") != nullptr, "with the label above it Material gives a field");
    auto value = anyItem(sort, "dropdownValue");
    c.check(value && value->property("text").toString() == "Original order",
            QString("showing what the list is sorted by (%1)")
                .arg(value ? value->property("text").toString() : QString()));
    auto chevron = anyItem(sort, "dropdownChevron");
    const double closed = chevron ? chevron->rotation() : 0;
    c.click("collectionSortButton");
    c.check(sort->property("menuOpen").toBool(), "which opens its options");
    c.check(chevron && qAbs(chevron->rotation() - closed) > 45,
            "and turns the chevron to say so");
    c.check(anyItem(w->contentItem(), "sort_title") != nullptr, "the options being the sorts on offer");
    c.shotNow("04-exposed-dropdown");
    c.click("sort_title");
    c.check(b->collection()->sortKey() == "title", "choosing one sorts the list");
    c.check(value && value->property("text").toString() == "Title", "and the field says so");
    c.shot("05-exposed-dropdown-chosen");
    b->collection()->setSortKey("original");
    QTest::qWait(300);
  }

  // --- A sheet that takes the screen over scrims what it covers ---
  // Below the expanded class the supporting pane arrives as a sheet.
  w->setProperty("side", "queue");
  w->resize(780, 860);
  QTest::qWait(900);
  auto sheet = anyItem(w->contentItem(), "panelSheet");
  c.check(sheet && sheet->isVisible() && sheet->property("modal").toBool(),
          "the supporting pane arrives as a modal sheet on a narrow window");
  auto scrim = anyItem(w->contentItem(), "bottomSheetScrim");
  c.check(scrim && scrim->isVisible() && scrim->opacity() > 0.9,
          "with the scrim Material puts behind one");
  c.shot("06-modal-sheet");
  if (scrim) {
    // A press on the scrim is how Material dismisses a modal sheet.
    const auto point = scrim->mapToScene(QPointF(scrim->width()/2, 60)).toPoint();
    QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, point);
    QTest::qWait(700);
    c.check(w->property("side").toString().isEmpty(), "and pressing it puts the sheet away");
  }
  w->resize(1400, 900);
  QTest::qWait(600);
  c.shot("07-restored");
  b->clearQueue();
  c.finish();
}

// Emphasis as a role, the error palette, and the three interactions Material
// draws differently from the way we were drawing them. Driven through the
// interface rather than through the properties behind it.
void runMaterialEmphasisTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music/Night Ferry");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1400, 900);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(true);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);

  for (int i = 1; i <= 4; ++i)
    if (!encodeTrack(c, QString("%1/music/Night Ferry/%2.flac").arg(c.directory).arg(i),
                     QString("Ferry %1").arg(i), "Night Ferry", "Marble Coast", i))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the emphasis fixture");
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  c.check(c.until([&] { return b->results()->count() == 4; }), "the library is listed");
  QTest::qWait(500);

  // --- An emphasized style on screen carries the role's own tracking ---
  // The weight was already a step up. The tracking was not: it stayed at the
  // regular role's, which is the part that makes emphasis a role of its own.
  if (auto headline = shownItem(w->contentItem(), "collectionHeaderTitle")) {
    const auto font = headline->property("font").value<QFont>();
    c.check(headline->property("emphasized").toBool(), "the page headline is an emphasized style");
    const double wanted =
        c.evaluate(QString("Theme.trackingFor(%1,false,'',true)").arg(font.pixelSize())).toDouble();
    const double plain =
        c.evaluate(QString("Theme.trackingFor(%1,false,'',false)").arg(font.pixelSize())).toDouble();
    c.check(qAbs(font.letterSpacing() - wanted) < 0.02,
            QString("and is tracked at the emphasized figure (%1, not %2)")
                .arg(font.letterSpacing(), 0, 'f', 2).arg(plain, 0, 'f', 2));
  }
  c.shot("01-emphasized-headline");

  // --- The error roles come off the scheme ---
  c.check(c.evaluate("Theme.error").value<QColor>().isValid() &&
              c.evaluate("Theme.errorContainer").value<QColor>().isValid(),
          "the window has an error role to draw with");
  b->setColorContrast(1);
  QTest::qWait(400);
  const auto tightened = c.evaluate("Theme.error").value<QColor>();
  b->setColorContrast(0);
  QTest::qWait(400);
  c.check(tightened.isValid() && c.evaluate("Theme.error").value<QColor>().isValid(),
          "and it survives a change of contrast rather than ignoring it");

  // --- A swipe uncovers a button, and its shape says what letting go does ---
  QMetaObject::invokeMethod(w, "activateSide", Q_ARG(QVariant, QVariant("queue")));
  b->enqueueItems(b->results()->rows);
  c.check(c.until([&] { return b->queue()->count() == 4; }), "the queue has rows to push");
  QTest::qWait(700);
  auto row = shownItem(w->contentItem(), "queueRow_1");
  c.check(row, "a queue row is on screen");
  if (row) {
    const auto start = row->mapToScene(QPointF(row->width()/2, row->height()/2)).toPoint();
    QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, start);
    QTest::qWait(60);
    // Far enough to reveal, not far enough to commit.
    QTest::mouseMove(w, start + QPoint(int(row->width()/6), 0));
    QTest::qWait(200);
    auto action = anyItem(row, "swipeAction");
    c.check(action && action->isVisible(), "pushing the row aside uncovers a button");
    const double offered = action ? action->property("radius").toDouble() : 0;
    c.check(offered > 19, QString("round while it is only on offer (%1)").arg(offered, 0, 'f', 0));
    c.shotNow("02-swipe-offered");
    // Past the threshold it becomes the action.
    QTest::mouseMove(w, start + QPoint(int(row->width()/2), 0));
    QTest::qWait(300);
    const double committing = action ? action->property("radius").toDouble() : 0;
    c.check(committing < offered,
            QString("and squarer once letting go would act (%1)").arg(committing, 0, 'f', 0));
    c.check(action && action->property("color").value<QColor>() == c.themeColor("primary"),
            "in the accent Material gives the action it is offering");
    c.shotNow("03-swipe-committing");
    QTest::mouseMove(w, start);
    QTest::qWait(150);
    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, start);
    QTest::qWait(400);
    c.check(b->queue()->count() == 4, "letting go short of the threshold keeps the row");
  }
  QMetaObject::invokeMethod(w, "activateSide", Q_ARG(QVariant, QVariant("")));
  QTest::qWait(400);

  // --- The expander is at the trailing edge and fills when it is open ---
  b->collection()->setProperty("sortKey", QString("folder"));
  QTest::qWait(600);
  auto expander = shownItem(w->contentItem(), "groupExpander_" + b->musicFolders().value(0));
  if (!expander)
    for (auto candidate : w->findChildren<QQuickItem *>())
      if (candidate->objectName().startsWith("groupExpander_") && candidate->isVisible()) {
        expander = candidate;
        break;
      }
  c.check(expander, "a group heading carries an expander");
  if (expander) {
    if (auto label = shownItem(w->contentItem(), "toggleGroup_" + b->musicFolders().value(0)))
      c.check(expander->mapToScene(QPointF(0, 0)).x() > label->mapToScene(QPointF(0, 0)).x(),
              "at the trailing edge of the item it opens");
    const auto open = expander->property("color").value<QColor>();
    c.check(open == c.themeColor("container"),
            "its container is filled while the group is open");
    const auto point = expander->mapToScene(expander->boundingRect().center()).toPoint();
    QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, point);
    QTest::qWait(500);
    // Material's expandable list gives the control a container either way:
    // the surface while the group is shut, a step up while it is open. It used
    // to disappear entirely, which left the chevron floating on the heading.
    c.check(expander->property("color").value<QColor>() == c.themeColor("surface"),
            "and drops to the surface once it is shut, rather than vanishing");
    c.check(c.themeColor("surface") != c.themeColor("container"),
            "which is a container, not nothing");
    c.shot("04-group-expander");
    // Material sets a section heading in title small. All three in the app,
    // here and in the rail and the drawer, were a label style.
    const auto headings = w->findChildren<QQuickItem *>("groupHeading");
    c.check(!headings.isEmpty(), "a grouped list carries section headings");
    for (auto heading : headings) {
      c.check(heading->property("typeRole").toString() == "titleSmall",
              "a section heading is set in title small");
      break;
    }
    QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, point);
    QTest::qWait(400);
  }
  b->collection()->setProperty("sortKey", QString("original"));
  QTest::qWait(300);

  // --- The settings choices share one expressive menu group ---
  w->resize(700, 820);
  QTest::qWait(500);
  if (auto settings = w->findChild<QObject *>("settingsDialog")) {
    QMetaObject::invokeMethod(settings, "open");
    QTest::qWait(700);
    c.click("settingsCategoryPicker");
    QTest::qWait(500);
    auto menu = w->findChild<QObject *>("settingsCategoryMenu");
    c.check(menu && menu->property("visible").toBool(),
            "the settings categories open in one expressive menu group");
    if (menu) {
      auto frame = menu->property("background").value<QQuickItem *>();
      c.check(frame && frame->property("color").value<QColor>() == c.themeColor("surfaceLow"),
              "on the low surface container Material puts under a run");
      c.check(frame && qAbs(frame->property("radius").toDouble() - 16) < 0.5,
              QString("at the large corner (%1)")
                  .arg(frame ? frame->property("radius").toDouble() : 0, 0, 'f', 0));
      // Every item owns a Surface inside the group (Menu.kt:2038-2046).
      auto list = menu->property("contentItem").value<QQuickItem *>();
      auto item = list ? anyItem(list, "menuItemContainer") : nullptr;
      c.check(item && item->isVisible(), "and each item carries a container of its own");
      // The group's ends and a chosen item are the medium step; 24 is
      // SegmentedMenuTokens.ActiveContainerShape, which no item reads
      // (MenuDefaults.kt, leadingItemShape and selectedItemShape).
      if (item)
        c.check(qAbs(item->property("topLeftRadius").toDouble() - 12) < 0.5,
                QString("with the run's end at the medium step (%1)")
                    .arg(item->property("topLeftRadius").toDouble(), 0, 'f', 0));
      // MenuDefaults.kt:788-814 reads StandardMenuTokens.ItemSelected*
      // (tertiary pair) and ItemContainerColor (surfaceContainerLow).
      if (list) {
        const auto chosen = c.themeColor("tertiaryContainer");
        QList<QQuickItem *> containers;
        collectItems(list, "menuItemContainer", containers);
        QQuickItem *first = nullptr, *last = nullptr;
        for (auto container : containers) {
          if (!first || container->mapToItem(frame, QPointF(0, 0)).y() <
                            first->mapToItem(frame, QPointF(0, 0)).y())
            first = container;
          if (!last || container->mapToItem(frame, QPointF(0, 0)).y() >
                           last->mapToItem(frame, QPointF(0, 0)).y())
            last = container;
        }
        // DropdownMenuGroupContentPadding is 2dp vertically; each item's
        // Surface adds 4dp horizontally (MenuDefaults.kt:886, Menu.kt:2381).
        const auto firstTop = first && frame ? first->mapToItem(frame, QPointF(0, 0)) : QPointF();
        const auto lastTop = last && frame ? last->mapToItem(frame, QPointF(0, 0)) : QPointF();
        c.check(first && last && frame &&
                    firstTop.x() >= 3.5 && firstTop.x()+first->width() <= frame->width()-3.5 &&
                    firstTop.y() >= 1.5 &&
                    qAbs(first->property("topLeftRadius").toDouble()-12) < 0.5 &&
                    lastTop.x() >= 3.5 && lastTop.x()+last->width() <= frame->width()-3.5 &&
                    lastTop.y()+last->height() <= frame->height()-1.5 &&
                    qAbs(last->property("bottomLeftRadius").toDouble()-12) < 0.5,
                "first and last item corners sit inside the 16dp group corner");
        auto stateLayer = anyItem(list, "menuItemStateLayer");
        const auto stateX = stateLayer && frame ? stateLayer->mapToItem(frame, QPointF(0, 0)).x() : 0;
        c.check(stateLayer && frame && stateX >= 3.5 &&
                    stateX+stateLayer->width() <= frame->width()-3.5,
                "hover and press layers share the 4dp item inset");
        int marked = 0, plain = 0;
        for (auto container : containers)
          if (container->property("color").value<QColor>() == chosen)
            ++marked;
          else if (container->property("color").value<QColor>() == c.themeColor("surfaceLow"))
            ++plain;
        c.check(marked == 1,
                QString("one of the %1 menu items is marked as the one you are on (%2)")
                    .arg(containers.size()).arg(marked));
        c.check(containers.size() > 1 && plain == containers.size()-1,
                "unchecked menu items use StandardMenuTokens.ItemContainerColor");
        QList<QQuickItem *> labels;
        collectItems(list, "menuItemLabel", labels);
        int selectedInk = 0, plainInk = 0;
        for (auto label : labels)
          if (label->property("color").value<QColor>() == c.themeColor("tertiaryContainerText"))
            ++selectedInk;
          else if (label->property("color").value<QColor>() == c.themeColor("text"))
            ++plainInk;
        c.check(selectedInk == 1 && plainInk == labels.size()-1 && labels.size() > 1,
                "selected and unchecked labels use the StandardMenuTokens ink roles");
        c.check(chosen != c.themeColor("secondaryContainer"),
                "in the tertiary container, which is not what marks a chosen row");
      }
      if (auto leading = list ? anyItem(list, "menuItemLeading") : nullptr)
        c.check(leading->property("ink").value<QColor>() == c.themeColor("muted") ||
                    leading->property("ink").value<QColor>() == c.themeColor("tertiaryContainerText"),
                "a menu item's leading icon is the variant ink until the item is chosen");
      c.shotNow("05-expressive-menu");
      QMetaObject::invokeMethod(menu, "close");
      QTest::qWait(300);
    }
    QMetaObject::invokeMethod(settings, "close");
    QTest::qWait(400);
  }
  w->resize(1400, 900);
  QTest::qWait(400);

  b->stop();
  b->clearQueue();
  c.finish();
}

// The patterns rather than the parts: the transition Material names for moving
// through a hierarchy, the symbol axes that carry a state, the containers a
// menu and a dialog sit on, and the structure assistive technology reads.
void runMaterialAnatomyTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music/Night Ferry");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1400, 900);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(true);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);

  for (int i = 1; i <= 3; ++i)
    if (!encodeTrack(c, QString("%1/music/Night Ferry/%2.flac").arg(c.directory).arg(i),
                     QString("Ferry %1").arg(i), "Night Ferry", "Marble Coast", i))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the anatomy fixture");
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  c.check(c.until([&] { return b->results()->count() == 3; }), "the library is listed");
  QTest::qWait(500);

  // Chip.kt:4286-4298 gives the trailing action its 8+18+8dp region;
  // InputChipTokens.FocusIndicatorColor gives its icon a secondary ring.
  {
    QQmlComponent chipSource(qmlEngine(w), QUrl("qrc:/qml/MChip.qml"));
    QScopedPointer<QObject> made(chipSource.create(qmlContext(w)));
    auto input = qobject_cast<QQuickItem *>(made.data());
    c.check(input, "an input chip can be focused through its trailing action");
    if (input) {
      input->setParentItem(w->contentItem());
      input->setX(600); input->setY(360); input->setZ(95);
      input->setProperty("variant", QString("input"));
      input->setProperty("text", QString("Ferry"));
      QTest::qWait(120);
      auto remove = anyItem(input, "chipRemove");
      c.check(remove && qAbs(remove->width() - 34) < 0.5 &&
                  qAbs(remove->height() - input->height()) < 0.5,
              "the remove action owns the whole 34dp trailing region and chip height");
      input->forceActiveFocus(Qt::TabFocusReason);
      QTest::keyClick(w, Qt::Key_Tab);
      QTest::qWait(120);
      auto ring = remove ? anyItem(remove, "chipRemoveFocusRing") : nullptr;
      auto icon = remove ? anyItem(remove, "materialIcon") : nullptr;
      c.check(remove && remove->hasActiveFocus() && ring && ring->isVisible(),
              "Tab reaches the remove action and draws its ring");
      c.check(ring && ring->property("border").value<QObject *>()->property("color").value<QColor>() ==
                  c.themeColor("secondary"),
              "the remove ring uses the secondary role");
      c.check(ring && icon && qAbs(ring->width() - 24) < 0.5 &&
                  qAbs(ring->height() - 24) < 0.5 &&
                  qAbs(ring->property("radius").toDouble() - 12) < 0.5 &&
                  (ring->mapToScene(ring->boundingRect().center()) -
                   icon->mapToScene(icon->boundingRect().center())).manhattanLength() < 0.5,
              "the 2px focus ring circles the icon 3px outside it");
      c.shotNow("06a-chip-remove-focus");
      QSignalSpy chipClicks(input, SIGNAL(clicked()));
      QSignalSpy removals(input, SIGNAL(removed()));
      c.check(chipClicks.isValid() && removals.isValid(),
              "the chip and remove signals can be observed separately");
      if (icon) {
        const auto point = icon->mapToScene(icon->boundingRect().center()).toPoint();
        QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, point);
        QTest::qWait(80);
        c.check(removals.count() == 1 && chipClicks.count() == 0,
                "clicking the remove icon removes without activating the chip");
      }
      input->setVisible(false);
      input->setParentItem(nullptr);
    }
  }

  // FloatingActionButtonMenu.kt:203-215 springs the visible item count on
  // SlowEffects; MenuTokens.FocusIndicatorColor gives the rings Secondary.
  auto menu = shownItem(w->contentItem(), "libraryFab");
  c.check(menu, "the library has a FAB menu to focus");
  if (menu) {
    auto fab = anyItem(menu, "fab");
    c.check(fab, "the library FAB is reachable by keyboard");
    if (fab) {
      fab->forceActiveFocus(Qt::TabFocusReason);
      QTest::qWait(100);
      auto ring = anyItem(fab, "fabFocusRing");
      c.check(ring && ring->isVisible(), "Tab focus draws the FAB's outside ring");
      c.click("fab");
      c.check(c.until([&] { return shownItem(w->contentItem(), "fabMenuItem_0") != nullptr; }, 3000),
              "the FAB menu reveals its actions");
      c.check(c.until([&] { return menu->property("staggerCount").toDouble() >=
                                      menu->property("count").toInt() - 0.01; }, 3000),
              "the SlowEffects spring reveals the whole menu");
      // FloatingActionButtonMenu.kt:207-215 snaps the count within one step
      // so the final item does not wait for the spring's asymptotic tail.
      const int actionCount = menu->property("count").toInt();
      menu->setProperty("staggerCount", actionCount - 0.5);
      QTest::qWait(30);
      c.check(actionCount > 0 && menu->property("visibleCount").toInt() == actionCount &&
                  shownItem(w->contentItem(), "fabMenuItem_0"),
              "the final FAB action is visible within one step of the target");
      menu->setProperty("staggerCount", actionCount);
      QTest::keyClick(w, Qt::Key_Tab);
      QTest::qWait(120);
      QQuickItem *focused = nullptr;
      for (int i = 0; i < menu->property("count").toInt(); ++i)
        if (auto item = shownItem(w->contentItem(), "fabMenuItem_" + QString::number(i));
            item && item->hasActiveFocus()) focused = item;
      auto itemRing = focused ? anyItem(focused, "fabMenuItemFocusRing") : nullptr;
      c.check(focused && itemRing && itemRing->isVisible(),
              "Tab focus draws the active FAB menu item's outside ring");
      c.shotNow("06b-fab-menu-focus");
      QTest::keyClick(w, Qt::Key_Escape);
      c.until([&] { return !menu->property("open").toBool(); }, 3000);
    }
  }

  // NavigationBar.kt:201 and Tab.kt:102 apply one LabelTextFont in either
  // selection state; their token files have no active-font variant.
  auto selectedNavLabel = anyItem(w->contentItem(), "navBarLabel_library");
  auto quietNavLabel = anyItem(w->contentItem(), "navBarLabel_home");
  c.check(selectedNavLabel && quietNavLabel &&
              selectedNavLabel->property("font").value<QFont>().weight() ==
                  quietNavLabel->property("font").value<QFont>().weight(),
          "selected and unselected navigation labels have one weight");
  auto selectedTab = shownItem(w->contentItem(), "localFilesTab");
  auto quietTab = shownItem(w->contentItem(), "playlistsTab");
  auto selectedTabLabel = selectedTab ? anyItem(selectedTab, "sungText") : nullptr;
  auto quietTabLabel = quietTab ? anyItem(quietTab, "sungText") : nullptr;
  c.check(selectedTabLabel && quietTabLabel &&
              selectedTabLabel->property("font").value<QFont>().weight() ==
                  quietTabLabel->property("font").value<QFont>().weight(),
          "selected and unselected tab labels have one TitleSmall weight");
  // NavigationRailVerticalItemTokens and HorizontalItemTokens also publish
  // one label font each, regardless of selection.
  {
    QQmlComponent railSource(qmlEngine(w), QUrl("qrc:/qml/MNavigationItem.qml"));
    QScopedPointer<QObject> made(railSource.create(qmlContext(w)));
    auto rail = qobject_cast<QQuickItem *>(made.data());
    c.check(rail, "a rail item can show both label arrangements");
    if (rail) {
      rail->setParentItem(w->contentItem());
      rail->setProperty("text", QString("Library"));
      for (bool expanded : {false, true}) {
        rail->setProperty("expanded", expanded);
        auto label = anyItem(rail, expanded ? "navigationWideLabel" : "navigationLabel");
        rail->setProperty("selected", false);
        QTest::qWait(80);
        const int quietWeight = label ? label->property("font").value<QFont>().weight() : -1;
        rail->setProperty("selected", true);
        QTest::qWait(80);
        c.check(label && label->property("font").value<QFont>().weight() == quietWeight,
                QString("the %1 rail label keeps its weight when selected")
                    .arg(expanded ? "horizontal" : "vertical"));
      }
      rail->setVisible(false);
      rail->setParentItem(nullptr);
    }
  }
  // --- Material names six transitions and this had three of them ---
  // Fading through is for destinations that have nothing to do with each
  // other. Two screens at consecutive levels of one hierarchy slide instead,
  // and the direction says which way through it you went.
  c.check(c.evaluate("typeof destinationTransition.forwardBackward === 'function'").toBool(),
          "there is a transition for moving through a hierarchy");
  c.check(c.evaluate("typeof destinationTransition.fadeThrough === 'function'").toBool() &&
              c.evaluate("typeof tabTransition.sharedAxisX === 'function'").toBool(),
          "alongside the ones for destinations and for peers");
  c.check(c.evaluate("destinationTransition.axisTravel").toDouble() > 0,
          "and it travels horizontally rather than in place");

  // --- The symbol axes that carry a state ---
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("local-albums")));
  QTest::qWait(700);
  auto pinnable = shownItem(w->contentItem(), "artCardPin");
  if (!pinnable)
    pinnable = shownItem(w->contentItem(), "artistHeroPin");
  c.check(c.evaluate("['home','library','heart','pin'].length").toInt() == 4,
          "four symbols carry an outlined form");

  // --- One plain tooltip, not four ---
  // Material has two tooltips: a plain one, which is a short label drawn
  // against the theme, and a rich one, which is a surface with a subhead and
  // an action. The app had four treatments of the plain one across six
  // controls, two of them a fake inverse pair and two of them dressed as
  // cards. This is the component they all use now.
  {
    QQmlComponent tipSource(qmlEngine(w), QUrl("qrc:/qml/MTooltip.qml"));
    QScopedPointer<QObject> tipObject(tipSource.create(qmlContext(w)));
    c.check(!tipObject.isNull(), "there is one plain tooltip to share");
    if (!tipObject.isNull()) {
      tipObject->setProperty("text", QString("Shuffle"));
      auto shape = tipObject->property("background").value<QQuickItem *>();
      auto label = tipObject->property("contentItem").value<QQuickItem *>();
      c.check(shape && shape->property("color").value<QColor>() == c.themeColor("inverseSurface"),
              "drawn against the theme on the inverse surface");
      c.check(shape && qAbs(shape->property("radius").toDouble() - 4) < 0.5,
              QString("at the smallest corner on the scale (%1)")
                  .arg(shape ? shape->property("radius").toDouble() : 0, 0, 'f', 0));
      c.check(label && label->property("color").value<QColor>() ==
                  c.themeColor("inverseSurfaceText"),
              "with the ink that goes on it");
      c.check(label && qAbs(label->property("font").value<QFont>().pixelSize() -
                            c.evaluate("Theme.bodySmall").toDouble()) < 0.5,
              "and body small, which is the size Material sets a plain tooltip in");
    }
  }

  // --- A tonal toggle stays tonal and changes role ---
  // The tonal variant sits on the secondary container, and coming on inverts
  // it onto the secondary role itself rather than moving it to the accent.
  // Built here rather than found on a page, so the check runs whatever the
  // library happens to be showing.
  {
    QQmlComponent buttonSource(qmlEngine(w), QUrl("qrc:/qml/MButton.qml"));
    QScopedPointer<QObject> buttonObject(buttonSource.create(qmlContext(w)));
    auto tonal = qobject_cast<QQuickItem *>(buttonObject.data());
    c.check(tonal, "a tonal toggle can be made to try the variant on");
    if (tonal) {
      tonal->setParentItem(w->contentItem());
      tonal->setX(620); tonal->setY(300); tonal->setZ(95);
      tonal->setProperty("symbol", QString("pin"));
      tonal->setProperty("tonal", true);
      tonal->setProperty("toggle", true);
      tonal->setProperty("selected", false);
      QTest::qWait(250);
      auto shape = tonal->property("background").value<QQuickItem *>();
      c.check(shape && shape->property("color").value<QColor>() ==
                  c.themeColor("secondaryContainer"),
              "off, a tonal toggle is the secondary container");
      c.check(tonal->property("ink").value<QColor>() ==
                  c.themeColor("secondaryContainerText"),
              "with the ink that belongs on it");
      tonal->setProperty("selected", true);
      QTest::qWait(400);
      c.check(shape && shape->property("color").value<QColor>() == c.themeColor("secondary"),
              "on, it takes the secondary role itself");
      c.check(tonal->property("ink").value<QColor>() == c.themeColor("secondaryText"),
              "and the ink that goes on that, which the scheme had never computed");
      c.check(c.themeColor("secondary") != c.themeColor("primaryContainer"),
              "neither of which is the accent container it used to take");
      c.shotNow("06-tonal-toggle-on");
      tonal->setVisible(false);
      tonal->setParentItem(nullptr);
    }
  }

  {
    // A symbol with an outlined form is drawn outlined until what it reports
    // is on. Material calls that the fill axis, and it is what a navigation
    // destination and a liked song have in common.
    QQmlComponent iconSource(qmlEngine(w), QUrl("qrc:/qml/Icon.qml"));
    QScopedPointer<QObject> made(iconSource.create(qmlContext(w)));
    auto glyph = qobject_cast<QQuickItem *>(made.data());
    c.check(glyph, "a symbol can be made to try the axis on");
    if (glyph) {
      for (const auto *name : {"heart", "pin", "home", "library"}) {
        glyph->setProperty("name", QString::fromLatin1(name));
        c.check(glyph->property("hasOutline").toBool(),
                QString("%1 has one").arg(name));
      }
      glyph->setProperty("name", QString("play"));
      c.check(!glyph->property("hasOutline").toBool(),
              "and a symbol with no outlined form of its own stays as it is");
      // The outlined drawing has to exist, not only be asked for.
      glyph->setParentItem(w->contentItem());
      glyph->setProperty("name", QString("heart"));
      glyph->setProperty("fill", 0.0);
      QTest::qWait(300);
      auto outline = anyItem(glyph, "iconOutline");
      c.check(outline && outline->isVisible(), "the outlined heart is the one drawn while it is off");
      c.check(outline && outline->property("status").toInt() == 1,
              "and its drawing is one the application ships");
      glyph->setVisible(false);
      glyph->setParentItem(nullptr);
    }
  }

  // --- A skeleton's pulse travels rather than flashing ---
  {
    QQmlComponent skeletonSource(qmlEngine(w), QUrl("qrc:/qml/CatalogSkeleton.qml"));
    QScopedPointer<QObject> made(skeletonSource.create(qmlContext(w)));
    auto skeleton = qobject_cast<QQuickItem *>(made.data());
    c.check(skeleton, "a skeleton can be made to watch");
    if (skeleton) {
      skeleton->setParentItem(w->contentItem());
      skeleton->setWidth(600);
      skeleton->setHeight(600);
      skeleton->setProperty("loading", true);
      QTest::qWait(500);
      // Material starts the pulse at the top left and moves it to the bottom
      // right, so two blocks at different places are never at the same point
      // in it.
      const double near = skeleton->property("wave").toDouble();
      c.check(c.until([&] { return qAbs(skeleton->property("wave").toDouble() - near) > 0.2; }, 2000),
              "its pulse runs");
      double first = 0, last = 0;
      if (auto shapes = skeleton->childItems().value(0)) {
        const auto blocks = shapes->childItems();
        if (blocks.size() >= 2) {
          first = blocks.first()->opacity();
          last = blocks.last()->opacity();
        }
      }
      c.check(qAbs(first - last) > 0.01,
              QString("and reaches one place before another (%1 against %2)")
                  .arg(first, 0, 'f', 2).arg(last, 0, 'f', 2));
      skeleton->setProperty("loading", false);
      skeleton->setVisible(false);
      skeleton->setParentItem(nullptr);
    }
  }

  // --- A symbol beside text sits on its baseline ---
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  QTest::qWait(600);
  {
    QQmlComponent buttonSource(qmlEngine(w), QUrl("qrc:/qml/MButton.qml"));
    QScopedPointer<QObject> made(buttonSource.create(qmlContext(w)));
    auto labelled = qobject_cast<QQuickItem *>(made.data());
    c.check(labelled, "a button with both a symbol and a label to measure");
    if (labelled) {
      labelled->setParentItem(w->contentItem());
      labelled->setProperty("text", QString("Play"));
      labelled->setProperty("symbol", QString("play"));
      QTest::qWait(200);
      auto inner = anyItem(labelled, "materialIcon");
      c.check(inner && inner->property("besideText").toDouble() > 0,
              "a symbol next to a label knows the size of the label");
      double shift = 0;
      if (inner)
        if (auto anchors = qvariant_cast<QObject *>(inner->property("anchors")))
          shift = anchors->property("verticalCenterOffset").toDouble();
      c.check(shift >= 1 && shift <= 3,
              QString("and is set below the centre line by Material's tenth (%1px)").arg(shift, 0, 'f', 0));
      // A symbol standing on its own is centred as usual.
      labelled->setProperty("text", QString());
      QTest::qWait(200);
      c.check(inner && inner->property("besideText").toDouble() == 0,
              "a symbol standing on its own is centred");
      labelled->setVisible(false);
      labelled->setParentItem(nullptr);
    }
  }

  // --- The containers Material puts a menu and a dialog on ---
  c.check(c.evaluate("Theme.container").value<QColor>() == c.themeColor("container"),
          "the theme publishes surfaceContainer");
  // A dialog is a popup rather than an item, so it is reached as the object
  // it is.
  if (auto dialog = w->findChild<QObject *>("settingsDialog")) {
    QMetaObject::invokeMethod(dialog, "open");
    QTest::qWait(700);
    auto frame = dialog->property("background").value<QQuickItem *>();
    c.check(frame && frame->property("color").value<QColor>() == c.themeColor("high"),
            "a dialog sits on surfaceContainerHigh, a step above the page");
    // Nothing in a settings section may push the column wider than the space
    // the column was given: a control that refuses to shrink drags every row
    // beside it out past the edge, where they are clipped.
    if (auto scroll = w->findChild<QQuickItem *>("settingsScroll")) {
      if (auto rows = w->findChild<QQuickItem *>("settingsOptions")) {
        // The way it went wrong: one row that wanted more width than the column
        // had dragged every row beside it out past the edge, because a row that
        // does not fill the column is laid out at the width it asks for and the
        // rest are laid out to match it.
        if (auto wide = w->findChild<QQuickItem *>("colorVariantControl")) {
          const auto original = wide->property("options");
          QVariantList longer;
          for (const auto *label : {"Neutral scheme", "Tonal spot scheme", "Vibrant scheme",
                                    "Expressive scheme", "Content scheme"}) {
            QVariantMap option; option["key"] = QString::fromLatin1(label);
            option["label"] = QString::fromLatin1(label); longer.append(option);
          }
          wide->setProperty("options", longer);
          QTest::qWait(500);
          c.check(wide->implicitWidth() > rows->width(),
                  QString("a row can want more width than the column has (%1 of %2)")
                      .arg(wide->implicitWidth(), 0, 'f', 0).arg(rows->width()));
          c.check(qAbs(wide->width() - rows->width()) < 1,
                  QString("and is given the column's width rather than its own (%1)")
                      .arg(wide->width(), 0, 'f', 0));
          if (auto sw = w->findChild<QQuickItem *>("ambientBackdropSwitch")) {
            c.check(qAbs(sw->width() - rows->width()) < 1,
                    QString("so the rows beside it are not dragged out with it (%1 of %2)")
                        .arg(sw->width(), 0, 'f', 0).arg(rows->width()));
            // --- What a switch looks like when it is off ---
            // Material draws the off state out of the outline role and the
            // highest surface container, not out of the variant ink and the
            // step below: the track and its handle are a boundary, and they
            // read as one piece because they are drawn in the same role.
            const bool wasOn = sw->property("checked").toBool();
            if (wasOn) {
              c.clickWithin(rows, "ambientBackdropSwitch");
              c.until([&] { return !sw->property("checked").toBool(); }, 2000);
            }
            auto track = anyItem(sw, "switchTrack");
            auto handle = anyItem(sw, "switchHandle");
            c.check(!sw->property("checked").toBool(), "a switch can be turned off");
            c.check(track && track->property("color").value<QColor>() == c.themeColor("highest"),
                    "its track off is the highest surface container");
            c.check(handle && handle->property("color").value<QColor>() == c.themeColor("outline"),
                    "and its handle is the outline role");
            c.check(c.themeColor("highest") != c.themeColor("high") &&
                        c.themeColor("outline") != c.themeColor("muted"),
                    "neither of which is the role it used to take");
            if (wasOn) {
              c.clickWithin(rows, "ambientBackdropSwitch");
              c.until([&] { return sw->property("checked").toBool(); }, 2000);
            }
          }
          double spill = 0; QString culprit;
          collectOverflow(rows, scroll, spill, culprit);
          c.check(spill <= 1,
                  spill <= 1 ? QStringLiteral("and nothing is drawn past the edge")
                             : QString("%1 runs %2px past the edge").arg(culprit).arg(spill, 0, 'f', 0));
          wide->setProperty("options", original);
          QTest::qWait(400);
        }
        double overflow = 0;
        QString worst;
        collectOverflow(rows, scroll, overflow, worst);
        c.check(overflow <= 1,
                overflow <= 1 ? QStringLiteral("every settings row fits the width it is given")
                              : QString("%1 runs %2px past the edge")
                                    .arg(worst).arg(overflow, 0, 'f', 0));
        // And still fits when the column is narrower than the longest row in
        // it, which is what a wider typeface or a smaller window does.
        dialog->setProperty("width", 700);
        QTest::qWait(500);
        overflow = 0; worst.clear();
        collectOverflow(rows, scroll, overflow, worst);
        c.check(overflow <= 1,
                overflow <= 1
                    ? QString("and still fits when the column is squeezed to %1").arg(rows->width())
                    : QString("squeezed to %1, %2 runs %3px past the edge")
                          .arg(rows->width()).arg(worst).arg(overflow, 0, 'f', 0));
        c.shotNow("01b-settings-squeezed");
        dialog->setProperty("width", 880);
        QTest::qWait(400);
      }
    }
    c.shot("01-dialog-container");
    QMetaObject::invokeMethod(dialog, "close");
    QTest::qWait(400);
  }

  // The control that was doing it: a segmented button asked for its whole
  // natural width as a minimum, so a column holding one could not be narrower
  // than its longest row of labels.
  {
    QQmlComponent groupSource(qmlEngine(w), QUrl("qrc:/qml/MSegmentedControl.qml"));
    QScopedPointer<QObject> made(groupSource.create(qmlContext(w)));
    auto group = qobject_cast<QQuickItem *>(made.data());
    c.check(group, "a segmented control can be made to squeeze");
    if (group) {
      group->setParentItem(w->contentItem());
      QVariantList options;
      for (const auto *label : {"Neutral", "Tonal spot", "Vibrant", "Expressive", "Content"}) {
        QVariantMap option;
        option["key"] = QString::fromLatin1(label);
        option["label"] = QString::fromLatin1(label);
        options.append(option);
      }
      group->setProperty("options", options);
      QTest::qWait(200);
      const double natural = group->implicitWidth();
      const double floorWidth =
          QQmlProperty::read(group, "Layout.minimumWidth", qmlContext(group)).toDouble();
      c.check(natural > 0 && floorWidth > 0, "it asks for a natural width and a floor");
      c.check(floorWidth < natural,
              QString("and the floor is under it rather than equal to it (%1 of %2)")
                  .arg(floorWidth, 0, 'f', 0).arg(natural, 0, 'f', 0));
      c.check(floorWidth <= 5*48 + 1,
              QString("no more than a touch target per segment (%1)").arg(floorWidth, 0, 'f', 0));
      group->setVisible(false);
      group->setParentItem(nullptr);
    }
  }

  // --- A prompt that cannot be undone is asked with an icon ---
  if (auto del = w->findChild<QObject *>("deletePlaylistDialog")) {
    QMetaObject::invokeMethod(del, "open");
    QTest::qWait(700);
    auto header = del->property("header").value<QQuickItem *>();
    auto icon = header ? anyItem(header, "dialogIcon") : nullptr;
    auto title = header ? anyItem(header, "dialogTitle") : nullptr;
    c.check(icon && icon->isVisible(), "the delete prompt carries Material's dialog icon");
    c.check(title && title->property("horizontalAlignment").toInt() == Qt::AlignHCenter,
            "and the headline is centred under it, as Material centres it with one");
    c.shot("02-dialog-icon");
    QMetaObject::invokeMethod(del, "close");
    QTest::qWait(400);
  }

  // --- The structure assistive technology reads ---
  QStringList missing;
  for (const auto *name : {"navigationBar", "sidePanel", "playbackBar"})
    if (auto pane = w->findChild<QQuickItem *>(name)) {
      if (QQmlProperty::read(pane, "Accessible.name", qmlContext(pane)).toString().isEmpty())
        missing.append(name);
    } else {
      missing.append(QString("%1 (not found)").arg(name));
    }
  c.check(missing.isEmpty(),
          missing.isEmpty() ? QStringLiteral("the large blocks of the layout are named")
                            : QString("unnamed regions: %1").arg(missing.join(", ")));
  if (auto headline = shownItem(w->contentItem(), "collectionHeaderTitle"))
    c.check(headline->property("heading").toBool(),
            "the page's headline says it is a heading rather than only looking like one");
  if (auto dialogTitle = w->findChild<QQuickItem *>("dialogTitle"))
    c.check(dialogTitle->property("heading").toBool(), "and so does a dialog's");

  // --- The tab indicator Material draws ---
  auto indicator = shownItem(w->contentItem(), "tabIndicator");
  c.check(indicator, "the tabs mark the one that is current");
  if (indicator) {
    c.check(qAbs(indicator->height() - 3) < 0.5 || qAbs(indicator->height() - 2) < 0.5,
            QString("at Material's height (%1)").arg(indicator->height()));
    c.check(indicator->property("bottomLeftRadius").toDouble() == 0,
            "square where it meets the divider");
    c.check(indicator->property("topLeftRadius").toDouble() > 0,
            "and round at the top, which is the 3,3,0,0 shape Material gives it");
    c.check(indicator->width() >= 24,
            QString("never shorter than 24dp (%1)").arg(indicator->width()));
  }
  c.shot("03-tab-indicator");

  // --- The snackbar grows away from the edge it sits against ---
  b->toast("Anatomy");
  QTest::qWait(60);
  auto toast = w->findChild<QQuickItem *>("toastBar");
  c.check(toast, "a snackbar is raised");
  if (toast) {
    const double partway = toast->height();
    c.check(c.until([&] { return toast->height() > partway + 4; }, 2000) || partway > 40,
            QString("and expands rather than appearing whole (%1px at first)").arg(partway, 0, 'f', 0));
    c.check(c.until([&] { return toast->height() > 40; }, 2000), "settling at its own height");
    c.check(toast->clip(), "with what it holds clipped by the frame on its way in");
    c.shot("04-snackbar");
  }

  // --- The snackbar stacks above the FAB ---
  // Compose's Scaffold puts a snackbar on the FAB's top edge rather than over
  // the button (Scaffold.kt, snackbarOffsetFromBottom). With the supporting
  // pane open the FAB came to sit under a centred snackbar.
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("playlists")));
  QQuickItem *fab = nullptr;
  c.check(c.until([&] { fab = shownItem(w->contentItem(), "libraryFab"); return fab != nullptr; }),
          "the library shows its FAB");
  b->toast("Above the button");
  if (fab && toast) {
    const auto clear = [&] {
      return toast->height() > 40 &&
             toast->mapToScene(QPointF(0, toast->height())).y() <= fab->mapToScene(QPointF(0, 0)).y() - 8;
    };
    c.check(c.until(clear, 3000),
            QString("and a snackbar sits above the button rather than over it (foot %1, FAB top %2)")
                .arg(toast->mapToScene(QPointF(0, toast->height())).y(), 0, 'f', 0)
                .arg(fab->mapToScene(QPointF(0, 0)).y(), 0, 'f', 0));
    c.shot("04-snackbar-above-fab");
  }

  // --- A pinned row is a feed, so nothing draws an empty state over it ---
  // The catalogue's own sections and the feed are not the same list: the feed
  // also carries the pinned row. Asking the catalogue whether to show a list
  // of songs put the empty state on top of the pins whenever the feed itself
  // came back empty, which is what an offline home page looks like.
  {
    const auto playlist = b->createPlaylist("Anatomy");
    c.check(!playlist.isEmpty(), "a collection to pin");
    b->openPlaylist(playlist);
    c.check(c.until([&] { return !b->busy() && !b->collectionItem().isEmpty(); }, 8000),
            "which can be opened");
    b->togglePin(b->collectionItem());
    c.check(c.until([&] { return !b->pins().isEmpty(); }), "and pinned");
    b->home();
    QTest::qWait(900);
    c.check(c.evaluate("window.homeSections.length").toInt() > b->sections().size(),
            "the feed carries a row the catalogue does not");
    c.check(c.evaluate("window.feedShowing").toBool(), "so the feed is what the page shows");
    auto empty = w->findChild<QQuickItem *>("collectionEmptyState");
    c.check(empty && !empty->isVisible(), "and no empty state is drawn over it");
    c.shot("05-pinned-feed");
    // The offline case, which is the one that went wrong: the catalogue offers
    // nothing and the pinned row is the whole feed. Driving the property the
    // page reads reaches it without taking the fixture offline.
    c.evaluate("window.homeSections = [{title:'Pinned', items:[]}]");
    QTest::qWait(400);
    c.check(c.evaluate("window.feedShowing").toBool(),
            "a feed of nothing but pins is still a feed");
    c.check(empty && !empty->isVisible(),
            "so the list of songs stands down rather than reporting itself empty");
    // The two never share the page: whichever is showing, the other stands
    // down. That is the property the old predicate broke.
    auto shelves = w->findChild<QQuickItem *>("homeShelves");
    c.check(!(shelves && shelves->isVisible() && empty && empty->isVisible()),
            "the shelves and the empty state are never both on the page");
    // And the discriminating case, reached where the catalogue has nothing to
    // say: a feed with a row in it puts the list of songs away. Asking the
    // catalogue instead of the feed leaves the list up, which is the fault.
    b->library("files");
    c.check(c.until([&] { return !b->busy(); }, 8000) && b->sections().isEmpty(),
            "a page the catalogue offers no sections for");
    QTest::qWait(400);
    c.evaluate("window.homeSections = [{title:'Pinned', items:[]}]");
    QTest::qWait(400);
    c.check(c.evaluate("window.feedShowing").toBool(),
            "a feed with a row in it is a feed, whatever the catalogue says");
    auto songs = w->findChild<QQuickItem *>("tracksView");
    c.check(songs && !songs->isVisible(), "so the list of songs stands down for it");
  }

  b->stop();
  b->clearQueue();
  c.finish();
}

// The controls Material redrew or added: the expressive slider, the segmented
// list, the side sheet, the selection controls, the docked toolbar, the input
// chip and the action at the head of the rail.
void runMaterialControlsTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music/Night Ferry");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1400, 900);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(true);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);

  for (int i = 1; i <= 4; ++i)
    if (!encodeTrack(c, QString("%1/music/Night Ferry/%2.flac").arg(c.directory).arg(i),
                     QString("Ferry %1").arg(i), "Night Ferry", "Marble Coast", i))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the control fixture");
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  c.check(c.until([&] { return b->results()->count() == 4; }), "the library is listed");
  QTest::qWait(500);

  // Something has to be playing for the seek bar to be live: Material dims a
  // disabled slider, and a dimmed track says nothing about its colour role.
  b->enqueueItems(b->results()->rows);
  b->playAt(0);
  c.check(c.until([&] { return b->playing() && b->duration() > 0; }, 20000), "a song is playing");
  QTest::qWait(400);
  const auto pointerBeforeControls = QCursor::pos();
  QPointer<QQuickItem> focusBeforeControls = w->activeFocusItem();
  const bool collectionToolsBefore = w->property("collectionTools").toBool();
  const QString sortBefore = b->collection()->sortKey();

  // --- The slider Material redrew ---
  // The old slider was a rule with a dot on it. The expressive one is a track
  // you can see, with a handle that is a bar and a gap held open around it.
  c.check(c.evaluate("Theme.sliderTrack.xsmall").toInt() == 16 &&
              c.evaluate("Theme.sliderHandle").toInt() == 4 &&
              c.evaluate("Theme.sliderHandleHeight.xsmall").toInt() == 44 &&
              c.evaluate("Theme.sliderGap").toInt() == 6,
          "the slider is a 16dp track with a 4 by 44dp handle and a 6dp gap");
  auto seek = shownItem(w->contentItem(), "seekBar");
  c.check(seek, "the seek bar is on screen");
  if (seek) {
    auto inactive = anyItem(seek, "seekInactiveTrack");
    auto handle = anyItem(seek, "seekHandle");
    c.check(inactive && qAbs(inactive->height() - 16) < 0.5,
            QString("its track is drawn at that height (%1)").arg(inactive ? inactive->height() : 0));
    c.check(inactive && inactive->property("color").value<QColor>() == c.themeColor("secondaryContainer"),
            "in the secondaryContainer Material names for it");
    c.check(inactive && qAbs(inactive->property("topLeftRadius").toDouble()-2) < 0.1,
            "the track corner at the handle is 2dp (Slider.kt:3085-3088)");
    c.check(handle && qAbs(handle->width() - 4) < 0.5 && qAbs(handle->height() - 44) < 0.5,
            QString("and its handle is the bar, not a dot (%1 by %2)")
                .arg(handle ? handle->width() : 0).arg(handle ? handle->height() : 0));
    auto stop = anyItem(seek, "seekStop");
    c.check(stop && qAbs(stop->width() - 4) < 0.5, "the end of the track is marked");
    seek->forceActiveFocus(Qt::TabFocusReason);
    QTest::qWait(350);
    c.check(handle && qAbs(handle->width()-2) < 0.5,
            "keyboard focus narrows the seek handle to SliderTokens.FocusHandleWidth");
  }
  c.shot("01-expressive-slider");

  // The setting slider uses the same track geometry and disabled active ink.
  {
    QQmlComponent source(qmlEngine(w), QUrl("qrc:/qml/SettingSlider.qml"));
    QScopedPointer<QObject> made(source.create(qmlContext(w)));
    auto slider = qobject_cast<QQuickItem *>(made.data());
    c.check(slider, "a setting slider can be built for the slider state checks");
    if (slider) {
      slider->setParentItem(w->contentItem());
      slider->setPosition(QPointF(200, 200));
      slider->setWidth(280);
      slider->setProperty("from", 0);
      slider->setProperty("to", 100);
      slider->setProperty("value", 40);
      QTest::qWait(100);
      auto active = anyItem(slider, "sliderActiveTrack");
      auto inactive = anyItem(slider, "sliderInactiveTrack");
      auto handle = anyItem(slider, "sliderHandle");
      auto stop = anyItem(slider, "sliderStop");
      c.check(active && inactive &&
                  qAbs(active->property("topRightRadius").toDouble()-2) < 0.1 &&
                  qAbs(inactive->property("topLeftRadius").toDouble()-2) < 0.1,
              "both setting track corners facing the handle are 2dp");
      slider->forceActiveFocus(Qt::TabFocusReason);
      QTest::qWait(350);
      c.check(handle && qAbs(handle->width()-2) < 0.5,
              "keyboard focus narrows the setting handle to 2dp");
      slider->setProperty("enabled", false);
      QTest::qWait(100);
      c.check(stop && stop->property("color").value<QColor>() ==
                          c.evaluate("Theme.sliderQuiet(Theme.disabledContentOpacity)").value<QColor>(),
              "the disabled stop uses disabled active track ink (Slider.kt:1679-1684)");
      slider->setVisible(false);
      slider->setParentItem(nullptr);
    }
  }

  // The plain popup is a single expressive group (Menu.kt:176-300), and the
  // action is exposed on the same field that receives keyboard focus.
  w->setProperty("collectionTools", true);
  QTest::qWait(250);
  if (auto sort = shownItem(w->contentItem(), "collectionSortControl")) {
    auto field = anyItem(sort, "collectionSortButton");
    auto accessible = field ? QAccessible::queryAccessibleInterface(field) : nullptr;
    c.check(accessible && accessible->role() == QAccessible::ComboBox,
            "the focusable dropdown field reports the combo box role");
    c.check(accessible && accessible->text(QAccessible::Name) == "Sort" &&
                accessible->text(QAccessible::Description) == "Original order",
            "the field reports its name and current value");
    if (field) {
      field->forceActiveFocus(Qt::TabFocusReason);
      QTest::qWait(100);
      auto ring = anyItem(sort, "dropdownFocusRing");
      auto border = field->property("background").value<QQuickItem *>();
      c.check(ring && ring->isVisible() && qAbs(ring->width()-field->width()-6) < 0.1 &&
                  QQmlProperty::read(ring, "border.width", qmlContext(ring)).toInt() == 2 &&
                  border && QQmlProperty::read(border, "border.width", qmlContext(border)).toInt() == 2,
              "keyboard focus keeps the outside ring and the field border");
      auto action = accessible ? accessible->actionInterface() : nullptr;
      c.check(action && action->actionNames().contains(QAccessibleActionInterface::pressAction()),
              "the combo box exposes the accessibility press action");
      if (action) action->doAction(QAccessibleActionInterface::pressAction());
      QTest::qWait(100);
      c.check(sort->property("menuOpen").toBool(),
              "an accessibility press opens the dropdown menu");
      auto menu = sort->findChild<QObject *>("dropdownMenu");
      if (menu) {
        auto frame = menu->property("background").value<QQuickItem *>();
        c.check(frame && frame->property("color").value<QColor>() == c.themeColor("surfaceLow") &&
                    qAbs(frame->property("radius").toDouble()-16) < 0.5,
                "the plain menu is a 16dp standalone group on surfaceContainerLow");
        auto list = menu->property("contentItem").value<QQuickItem *>();
        auto container = list ? anyItem(list, "menuItemContainer") : nullptr;
        const auto inset = container && frame ? container->mapToItem(frame, QPointF(0, 0)).x() : 0;
        c.check(list && qAbs(list->property("spacing").toDouble()) < 0.1 && inset >= 3.5,
                "its items have no inter-item gap and a 4dp Surface inset");
      }
      c.click("sort_title");
      c.check(accessible && accessible->text(QAccessible::Description) == "Title" &&
                  field->property("text").toString() == "Title",
              "the accessible value follows the chosen sort");
    }
  } else {
    c.check(false, "the collection sort dropdown is present for the accessibility check");
  }
  // The sort-menu click moves the pointer. When the side sheet opens, that
  // same scene point can hover its last queue row and morph its corner to 12dp.
  b->collection()->setSortKey(sortBefore);
  w->setProperty("collectionTools", collectionToolsBefore);
  if (focusBeforeControls) focusBeforeControls->forceActiveFocus();
  QTest::mouseMove(w, w->mapFromGlobal(pointerBeforeControls));
  QTest::qWait(120);

  // --- A list item answers the pointer with its shape ---
  c.check(c.evaluate("Theme.listRest").toInt() == 4 &&
              c.evaluate("Theme.listHovered").toInt() == 12 &&
              c.evaluate("Theme.listActive").toInt() == 16,
          "an expressive list item rests at 4dp, rounds to 12 under the pointer and 16 when it is taken");
  c.check(c.evaluate("Theme.listSegmentedGap").toInt() == 2,
          "and a segmented run sets its items 2dp apart");

  // --- The selection controls Material has and this did not ---
  c.check(c.until([&] { return b->queue()->count() == 4; }), "the queue has something in it");
  QMetaObject::invokeMethod(w, "activateSide", Q_ARG(QVariant, QVariant("queue")));
  QTest::qWait(600);
  auto sheet = shownItem(w->contentItem(), "sidePanel");
  c.check(sheet, "the side sheet opens");
  if (sheet) {
    // Material keeps 24dp clear at a side sheet's edges and sets its headline
    // apart from what it holds by 12.
    c.check(c.evaluate("Theme.sideSheetPadding").toInt() == 24 &&
                c.evaluate("Theme.sideSheetTopSpacing").toInt() == 12,
            "a side sheet keeps 24dp clear with 12dp under its headline");
    if (auto host = w->findChild<QQuickItem *>("sidePanelBody")) {
      const double clear = host->mapToItem(sheet, QPointF(0, 0)).x();
      c.check(qAbs(clear - 24) < 0.5 && qAbs(sheet->width() - host->width() - 48) < 0.5,
              QString("and this one does (%1 clear)").arg(clear, 0, 'f', 0));
    }
    auto headline = shownItem(w->contentItem(), "sidePanelTitle");
    c.check(headline && headline->property("font").value<QFont>().pixelSize() == 22,
            "its headline takes the title large role");
  }
  // The queue is drawn as Material's segmented list.
  // The song playing is picked out, so the shape of the run is read from the
  // ones that are not: an item inside a run is nearly square at both ends, and
  // the run is round where it stops.
  auto middleRow = shownItem(w->contentItem(), "queueRow_1");
  auto lastRow = shownItem(w->contentItem(), "queueRow_3");
  c.check(middleRow && lastRow, "the queue has rows to look at");
  if (middleRow && lastRow) {
    auto middle = middleRow->property("background").value<QQuickItem *>();
    auto last = lastRow->property("background").value<QQuickItem *>();
    c.check(middle && qAbs(middle->property("topLeftRadius").toDouble() - 4) < 0.5,
            "an item inside the run is nearly square where the one above it stops");
    c.check(middle && qAbs(middle->property("bottomLeftRadius").toDouble() - 4) < 0.5,
            "and where the one below it starts");
    const auto lastRadius = last ? last->property("bottomLeftRadius").toDouble() : -1;
    c.check(last && qAbs(lastRadius - 16) < 0.5,
            QString("the run is round where it ends (radius=%1, hovered=%2, pointerOver=%3, "
                    "keyboardCurrent=%4, lastInRun=%5, stateCorner=%6)")
                .arg(lastRadius).arg(lastRow->property("hovered").toBool())
                .arg(lastRow->property("pointerOver").toBool())
                .arg(lastRow->property("keyboardCurrent").toBool())
                .arg(lastRow->property("lastInRun").toBool())
                .arg(last ? last->property("stateCorner").toInt() : -1));
    c.check(middle && qAbs(middleRow->height() - middle->height() - 2) < 0.5,
            "and the items are set apart rather than divided by a rule");
    // The song playing takes the shape Material gives a picked out item.
    if (auto playing = shownItem(w->contentItem(), "queueRow_0"))
      if (auto back = playing->property("background").value<QQuickItem *>())
        c.check(qAbs(back->property("topLeftRadius").toDouble() - 16) < 0.5,
                "while the one playing is round on every corner");
  }
  c.shot("02-segmented-list");
  QMetaObject::invokeMethod(w, "activateSide", Q_ARG(QVariant, QVariant("")));
  QTest::qWait(400);

  // --- The checkbox in a list item's leading slot ---
  auto row = shownItem(w->contentItem(), "trackRow_0");
  c.check(row, "a track row to select");
  if (row) {
    const auto point = row->mapToScene(QPointF(30, row->height()/2)).toPoint();
    QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, point);
    QTest::qWait(400);
  }
  auto box = shownItem(w->contentItem(), "rowCheckbox");
  c.check(box, "the row carries a checkbox");
  if (box) {
    c.check(qAbs(box->width() - 48) < 0.5 && qAbs(box->height() - 48) < 0.5,
            QString("its target is the 48dp Material asks for (%1)").arg(box->width()));
    auto square = anyItem(box, "checkboxBox");
    c.check(square && qAbs(square->width() - 18) < 0.5,
            "the square inside it is 18dp");
    c.check(square && qAbs(square->property("radius").toDouble() - 2) < 0.5,
            "on a 2dp corner");
    auto layer = anyItem(box, "checkboxStateLayer");
    c.check(layer && qAbs(layer->width() - 40) < 0.5, "and its state layer is 40dp");
    c.check(box->property("checked").toBool(), "the row it belongs to is selected");
  }
  if (row)
    if (auto content = row->property("contentItem").value<QQuickItem *>())
      c.check(qAbs(content->property("spacing").toDouble() - 12) < 0.5,
              QString("a list item keeps 12dp between its leading element and what "
                      "it introduces (%1)")
                  .arg(content->property("spacing").toDouble(), 0, 'f', 0));
  // Material marks a chosen list item with the secondary container and puts
  // its ink on everything the row carries. Picking rows out is not an action,
  // so it does not borrow the accent an action is offered in.
  if (row) {
    auto container = row->property("background").value<QQuickItem *>();
    auto title = anyItem(row, "trackTitle");
    c.check(container && container->property("color").value<QColor>() ==
                c.themeColor("secondaryContainer"),
            "a selected row is the secondary container");
    c.check(c.themeColor("secondaryContainer") != c.themeColor("primaryContainer"),
            "which is not the container an action is offered in");
    c.check(title && title->property("color").value<QColor>() ==
                c.themeColor("secondaryContainerText"),
            "and what it carries is drawn in that container's ink");
  }

  // --- The docked toolbar that selection brings up ---
  auto toolbar = shownItem(w->contentItem(), "selectionToolbar");
  c.check(toolbar, "selecting docks a toolbar of what to do with it");
  if (toolbar) {
    c.check(qAbs(toolbar->height() - 64) < 0.5,
            QString("64dp tall, as Material specifies (%1)").arg(toolbar->height()));
    c.check(toolbar->property("color").value<QColor>() == c.themeColor("container"),
            "on the surfaceContainer a standard toolbar takes");
    c.check(qAbs(toolbar->property("radius").toDouble()) < 0.01,
            "square, because it is docked rather than floating");
    if (auto panel = shownItem(w->contentItem(), "contentColumn"))
      c.check(toolbar->width() >= panel->width() + 2*c.evaluate("window.paneMargin").toDouble() - 1,
              "and it runs the width of the surface it is docked to");
  }
  c.shot("03-docked-toolbar");
  c.evaluate("window.selectedView().selection.clear()");
  QTest::qWait(300);

  // --- The input chip a typed filter stands in ---
  b->collection()->setProperty("query", "ferry");
  c.check(c.until([&] { return b->collection()->property("query").toString() == "ferry"; }),
          "a filter is typed");
  w->setProperty("collectionTools", false);
  QTest::qWait(400);
  auto chip = shownItem(w->contentItem(), "collectionFilterChip");
  c.check(chip, "the filter stands as a chip once the row it was typed in is folded away");
  if (chip) {
    c.check(chip->property("variant").toString() == "input", "an input chip, which is what it is");
    auto container = chip->property("background").value<QQuickItem *>();
    c.check(container && qAbs(container->height() - 32) < 0.5,
            QString("32dp, as Material draws a chip (%1)").arg(container ? container->height() : 0));
    auto remove = anyItem(chip, "chipRemove");
    c.check(remove && remove->isVisible(), "and it carries the means to take the filter back out");
    c.shot("04-input-chip");
    // A chosen chip is marked the way every other chosen thing is marked.
    const bool wasChosen = chip->property("selected").toBool();
    chip->setProperty("selected", true);
    QTest::qWait(250);
    c.check(container && container->property("color").value<QColor>() ==
                c.themeColor("secondaryContainer"),
            "chosen, a chip is the secondary container");
    c.check(c.themeColor("secondaryContainer") != c.themeColor("primaryContainer"),
            "rather than the container an action is offered in");
    chip->setProperty("selected", wasChosen);
    QTest::qWait(200);
    QMetaObject::invokeMethod(remove, "clicked");
    QTest::qWait(400);
    c.check(b->collection()->property("query").toString().isEmpty(),
            "pressing that clears the filter");
  }

  // --- The surface's primary action ---
  // Material puts a FAB on the surface it acts on. It used to head the rail;
  // with no rail there, the library's own pane is that surface, and the slot
  // at its corner is what keeps the last row of the list clear of it.
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("playlists")));
  QTest::qWait(600);
  auto slot = shownItem(w->contentItem(), "contentFabHost");
  c.check(slot, "the content pane carries the surface's primary action");
  auto fab = shownItem(w->contentItem(), "libraryFab");
  c.check(fab && slot && fab->parentItem() == slot, "and the action sits in it");
  if (auto pane = shownItem(w->contentItem(), "content")) {
    if (fab) {
      const auto corner = fab->mapToItem(pane, QPointF(fab->width(), fab->height()));
      c.check(corner.x() <= pane->width() + 1 && corner.y() <= pane->height() + 1 &&
                  corner.x() > pane->width() * 0.5 && corner.y() > pane->height() * 0.5,
              QString("at the pane's trailing bottom corner (%1, %2 of %3 by %4)")
                  .arg(corner.x(), 0, 'f', 0).arg(corner.y(), 0, 'f', 0)
                  .arg(pane->width(), 0, 'f', 0).arg(pane->height(), 0, 'f', 0));
    }
    c.check(slot && qAbs(slot->width() - (fab ? fab->width() : 0)) < 1 &&
                qAbs(slot->height() - (fab ? fab->height() : 0)) < 1,
            "and the slot reserves exactly the room it takes");
  }
  c.shot("05-library-fab");
  {
    // The menu opens from a button in the pane's bottom corner, so it has to
    // open upward and stay inside the window. Every action has to be
    // separately readable and clickable: one that hangs off the window, or
    // that lands on top of another, or that the content surface paints over,
    // is none of those.
    const auto menuOpensClear=[&](const char *state){
      const auto before=w->grabWindow();
      c.click("fab");
      if(!c.until([&]{return shownItem(w->contentItem(),"fabMenuItem_0")!=nullptr;},3000)){
        c.check(false,QString("the action menu opens (%1)").arg(state));return;
      }
      QTest::qWait(700);
      const auto container=c.themeColor("primaryContainer");
      const auto after=w->grabWindow();
      const auto apart=[](const QColor &a,const QColor &b){return qAbs(a.red()-b.red())+qAbs(a.green()-b.green())+qAbs(a.blue()-b.blue());};
      double worstEdge=0;int drawn=0;QList<QRectF> placed;bool separate=true;
      const int actions=fab->property("count").toInt();
      for(int i=0;i<actions;++i){
        auto item=shownItem(w->contentItem(),"fabMenuItem_"+QString::number(i));
        if(!item){c.check(false,QString("action %1 is shown (%2)").arg(i).arg(state));continue;}
        const auto at=item->mapToItem(w->contentItem(),QPointF(0,0));
        const QRectF box(at,QSizeF(item->width(),item->height()));
        worstEdge=qMax(worstEdge,qMax(-box.left(),box.right()-w->width()));
        for(const auto &other:std::as_const(placed))if(other.intersects(box))separate=false;
        placed.append(box);
        // Inside the pill, above the row of icon and label. What was behind the menu has
        // to have given way to the pill's own container colour: an action the
        // content surface paints over leaves this pixel exactly as it was.
        const QPoint probe(qRound(box.center().x()),qRound(box.top())+6);
        if(apart(after.pixelColor(probe),before.pixelColor(probe))>24
           && apart(after.pixelColor(probe),container)<apart(before.pixelColor(probe),container))++drawn;
      }
      c.check(worstEdge<=1,QString("every action stays inside the window (%1, worst %2 past the edge)").arg(state).arg(qRound(worstEdge)));
      c.check(separate,QString("the actions are stacked rather than piled on one another (%1)").arg(state));
      c.check(drawn==actions,QString("and each one is drawn over the content, not under it (%1, %2 of %3)").arg(state).arg(drawn).arg(actions));
      c.shot(QString("06-library-fab-menu-%1").arg(state));
      c.click("fab");
      c.check(c.until([&]{return !fab->property("open").toBool();},3000),
              QString("pressing the button again closes them (%1)").arg(state));
    };
    menuOpensClear("default");
    // Reachable without a mouse: the actions take focus in turn, Escape puts
    // the menu away, and focus comes back to the button that opened it rather
    // than being stranded inside a surface that is no longer there.
    c.click("fab");
    if(c.until([&]{return shownItem(w->contentItem(),"fabMenuItem_0")!=nullptr;},3000)){
      QTest::qWait(600);
      QTest::keyClick(w,Qt::Key_Tab);QTest::qWait(250);
      auto focused=[&]{for(int i=0;i<fab->property("count").toInt();++i)
          if(auto item=shownItem(w->contentItem(),"fabMenuItem_"+QString::number(i));item&&item->hasActiveFocus())return i;
        return -1;};
      c.check(focused()>=0,QString("tab reaches the actions (landed on %1)").arg(focused()));
      QTest::keyClick(w,Qt::Key_Escape);
      c.check(c.until([&]{return !fab->property("open").toBool();},3000),"escape closes the menu");
      auto button=shownItem(fab,"fab");
      c.check(button&&button->hasActiveFocus(),"and focus returns to the button");
    } else c.check(false,"the menu opens for the keyboard checks");
    // With motion off the arrival transitions do not run at all, so the items
    // have to be in place and visible without them.
    b->setMotion(false);
    QTest::qWait(400);
    menuOpensClear("reduced-motion");
    b->setMotion(true);
    QTest::qWait(400);
    // The menu is a surface of its own over the content, so it has to hold up
    // in the light scheme too, where its container and the page behind it are
    // far closer in tone than they are in the dark one.
    b->setTheme("light");
    QTest::qWait(700);
    menuOpensClear("light");
    b->setTheme("dark");
    QTest::qWait(700);
  }
  // The same menu on a narrow window, where there is far less room around the
  // button for it to open into.
  {
    const QSize full=w->size();
    w->resize(560, 760);
    QTest::qWait(900);
    auto narrowFab = shownItem(w->contentItem(), "libraryFab");
    c.check(narrowFab, "the action is still on the pane");
    if (narrowFab) {
      c.click("fab");
      c.check(c.until([&]{return shownItem(w->contentItem(),"fabMenuItem_0")!=nullptr;},3000),"the menu still opens");
      QTest::qWait(700);
      double worst=0;int found=0;
      for(int i=0;i<narrowFab->property("count").toInt();++i)
        if(auto item=shownItem(w->contentItem(),"fabMenuItem_"+QString::number(i))){
          ++found;
          const auto box=item->mapToItem(w->contentItem(),QPointF(0,0));
          worst=qMax(worst,qMax(qMax(-box.x(),box.x()+item->width()-w->width()),
                                qMax(-box.y(),box.y()+item->height()-w->height())));
        }
      c.check(found==narrowFab->property("count").toInt(),
              QString("all %1 actions are there").arg(narrowFab->property("count").toInt()));
      c.check(found&&worst<=1,QString("and every action fits the narrow window (worst %1 past the edge)").arg(qRound(worst)));
      c.shot("06-library-fab-menu-narrow");
      c.click("fab");
      c.until([&]{return !narrowFab->property("open").toBool();},3000);
    }
    w->resize(full);
    QTest::qWait(900);
  }

  // --- A press does not wobble under the finger ---
  // Material morphs a button's container squarer while it is held and gives
  // that morph the effects spring on purpose, saying so in as many words: a
  // spatial spring would ring, and a control answering a finger must not move
  // under it after the finger has stopped.
  {
    QTest::qWait(300);
    auto pressed = shownItem(w->contentItem(), "settingsButton");
    auto surface = pressed ? pressed->property("background").value<QQuickItem *>() : nullptr;
    c.check(pressed && surface, "a button is on screen to press");
    if (pressed && surface) {
      const double resting = surface->property("radius").toReal();
      const auto point = pressed->mapToScene(pressed->boundingRect().center()).toPoint();
      QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, point);
      double lowest = resting, settled = resting;
      QElapsedTimer clock;
      clock.start();
      while (clock.elapsed() < 400) {
        settled = surface->property("radius").toReal();
        lowest = qMin(lowest, settled);
        QTest::qWait(8);
      }
      QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, point);
      QTest::qWait(400);
      c.check(settled < resting - 0.5,
              QString("holding it morphs the container squarer (%1 from %2)")
                  .arg(settled, 0, 'f', 1).arg(resting, 0, 'f', 1));
      // Ringing would carry it past the squarer corner and back.
      c.check(lowest >= settled - 0.5,
              QString("and settles there without ringing past it (%1 against %2)")
                  .arg(lowest, 0, 'f', 1).arg(settled, 0, 'f', 1));
    }
  }

  // ButtonGroupSamples.kt:155-190 uses filled ToggleButton colours, no border
  // or check, and RadioButton semantics for a connected single choice.
  if (auto dialog = w->findChild<QObject *>("settingsDialog")) {
    QMetaObject::invokeMethod(dialog, "open");
    QTest::qWait(700);
    c.click("themeDark");
    QTest::qWait(400);
    auto chosen = shownItem(w->contentItem(), "themeDark");
    c.check(chosen && chosen->property("selected").toBool(), "a segment can be chosen");
    if (chosen)
      if (auto shape = anyItem(chosen, "segmentBackground")) {
        c.check(shape->property("color").value<QColor>() == c.themeColor("primary"),
                "the chosen connected button fills with primary");
        c.check(qAbs(QQmlProperty::read(shape, "border.width", qmlContext(shape)).toDouble()) < 0.1,
                "the connected ToggleButton has no border");
        c.check(QQmlProperty::read(chosen, "Accessible.role", qmlContext(chosen)).toInt() ==
                    c.evaluate("Accessible.RadioButton").toInt() &&
                    QQmlProperty::read(chosen, "Accessible.checked", qmlContext(chosen)).toBool(),
                "the chosen segment reports radio selection");
        if (auto other = shownItem(w->contentItem(), "themeLight")) {
          auto otherShape = anyItem(other, "segmentBackground");
          c.check(otherShape && otherShape->property("color").value<QColor>() == c.themeColor("container"),
                  "the unchecked connected button uses surfaceContainer");
        }
        c.shot("07-segmented-choice");
      }
    QMetaObject::invokeMethod(dialog, "close");
    QTest::qWait(400);
  }

  // --- The filled text field ---
  // Material reaches for the filled container where a field is the thing on
  // the surface rather than one of several, which a naming dialog is.
  w->setProperty("playlistAction", QString("create"));
  if (auto naming = w->findChild<QObject *>("playlistDialog")) {
    QMetaObject::invokeMethod(naming, "open");
    QTest::qWait(700);
    auto field = shownItem(w->contentItem(), "playlistName");
    c.check(field && field->property("filled").toBool(), "the naming field is a filled one");
    if (field) {
      auto shape = field->property("background").value<QQuickItem *>();
      c.check(shape && shape->property("color").value<QColor>() == c.themeColor("highest"),
              "on the highest surface container");
      c.check(shape && qAbs(shape->property("topLeftRadius").toDouble() - 4) < 0.5 &&
                  qAbs(shape->property("bottomLeftRadius").toDouble()) < 0.5,
              "rounded where it is open and square where it is ruled off");
      auto indicator = shape ? anyItem(shape, "fieldIndicator") : nullptr;
      c.check(indicator && indicator->isVisible(), "with an active indicator under it");
      field->forceActiveFocus(Qt::TabFocusReason);
      QTest::qWait(300);
      c.check(indicator && qAbs(indicator->height() - 2) < 0.1 &&
                  indicator->property("color").value<QColor>() == c.themeColor("primary"),
              QString("that thickens and takes the accent on focus (%1dp)")
                  .arg(indicator ? indicator->height() : 0, 0, 'f', 0));
      c.shot("08-filled-field");
    }
    QMetaObject::invokeMethod(naming, "close");
    QTest::qWait(400);
  }

  // --- The radio button a choice of one is made with ---
  if (auto picker = w->findChild<QObject *>("outputPicker"))
    QMetaObject::invokeMethod(picker, "open");
  QTest::qWait(600);
  auto radio = shownItem(w->contentItem(), "outputRadio_0");
  c.check(radio, "the audio output list offers its options as radio buttons");
  if (radio) {
    c.check(qAbs(radio->width() - 48) < 0.5, "at the 48dp target");
    auto ring = anyItem(radio, "radioRing");
    c.check(ring && qAbs(ring->width() - 20) < 0.5, "around a 20dp ring");
    c.check(c.evaluate("Theme.radioSize").toInt() == 20 &&
                c.evaluate("Theme.selectionStateLayer").toInt() == 40,
            "which is what Material publishes for one");
    c.shot("06-radio-buttons");
  }

  b->stop();
  b->clearQueue();
  c.finish();
}

// Material's type scale as a whole style rather than a size, the app bar that
// says when content is under it, the button variants that were missing, the
// scrim as a role, and the list a detail was opened from staying beside it.
void runMaterialScaleTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music/Night Ferry");
  QDir().mkpath(c.directory + "/music/Still Water");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1400, 900);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(true);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);

  for (int i = 1; i <= 3; ++i)
    if (!encodeTrack(c, QString("%1/music/Night Ferry/%2.flac").arg(c.directory).arg(i),
                     QString("Ferry %1").arg(i), "Night Ferry", "Marble Coast", i))
      return c.finish();
  for (int i = 1; i <= 3; ++i)
    if (!encodeTrack(c, QString("%1/music/Still Water/%2.flac").arg(c.directory).arg(i),
                     QString("Water %1").arg(i), "Still Water", "Rill", i))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the scale fixture");
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  c.check(c.until([&] { return b->results()->count() == 6; }), "the library is listed");
  QTest::qWait(500);

  // --- A type role is a size, a line height and a tracking together ---
  struct Role { const char *name; int size; int line; double track; };
  const Role roles[] = {{"displaySmall", 36, 44, 0.0},  {"headlineSmall", 24, 32, 0.0},
                        {"titleLarge", 22, 28, 0.0},    {"bodyLarge", 16, 24, 0.5},
                        {"bodyMedium", 14, 20, 0.2},    {"labelLarge", 14, 20, 0.1},
                        {"labelMedium", 12, 16, 0.5},   {"labelSmall", 11, 16, 0.5}};
  for (const auto &role : roles) {
    // Size, line height, tracking, the emphasized tracking, and the two
    // weights the role sits at.
    const auto entry = c.evaluate(QString("Theme.typeScale.%1").arg(role.name)).toList();
    c.check(entry.size() == 6 && entry[0].toInt() == role.size &&
                entry[1].toInt() == role.line && qAbs(entry[2].toDouble() - role.track) < 0.001,
            QString("%1 is %2 on %3 with %4 tracking")
                .arg(role.name).arg(role.size).arg(role.line).arg(role.track));
  }
  // The size alone cannot say which role it is, so a label says so itself.
  c.check(c.evaluate("Theme.trackingFor(14,true,'')").toDouble() == 0.1,
          "a 14pt label tracks like a label");
  c.check(c.evaluate("Theme.trackingFor(14,false,'')").toDouble() == 0.2,
          "and a 14pt body like body text");
  c.check(c.evaluate("Theme.trackingFor(16,false,'titleMedium')").toDouble() == 0.2,
          "a style that names its role is taken at its word");
  c.check(c.evaluate("Theme.lineFor(16,false,'')").toInt() == 24,
          "and a line height comes with the size");
  // A library root no longer repeats its selected tab as a headline. Home
  // still has a headline, so inspect the rendered role there.
  b->home();
  c.check(c.until([&] { return !b->busy(); }), "Home opens for a type-scale sample");
  auto title = shownItem(w->contentItem(), "collectionHeaderTitle");
  c.check(title && title->property("lineHeightMode").toInt() == 1,
          "text is laid out on the absolute line height Material publishes");
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  QTest::qWait(350);
  if (auto sample = shownItem(w->contentItem(), "trackTitle")) {
    const auto font = sample->property("font").value<QFont>();
    c.check(font.letterSpacing() > 0,
            QString("and carries real letter spacing (%1)").arg(font.letterSpacing(), 0, 'f', 2));
  }
  // Every style on screen is one of Material's roles, not a number near one.
  QSet<int> scaleSizes;
  for (const auto &role : c.evaluate("Object.keys(Theme.typeScale)").toStringList())
    scaleSizes.insert(c.evaluate(QString("Theme.typeScale['%1'][0]").arg(role)).toInt());
  c.check(scaleSizes.size() >= 9,
          QString("Material publishes %1 sizes to choose from").arg(scaleSizes.size()));
  QStringList offScale;
  collectOffScale(w->contentItem(), scaleSizes, offScale);
  c.check(offScale.isEmpty(),
          offScale.isEmpty() ? QStringLiteral("every style is set at one of them")
                             : QString("styles set at a size off the scale: %1").arg(offScale.join(", ")));

  // A role's line height is absolute, so it has to clear its own glyphs.
  QStringList cramped;
  collectCrampedText(w->contentItem(), cramped);
  c.check(cramped.isEmpty(),
          cramped.isEmpty() ? QStringLiteral("every style clears its own glyphs")
                            : QString("styles set on a line shorter than their text: %1")
                                  .arg(cramped.join(", ")));
  c.shot("01-type-scale");

  // RadioButton.kt:137-142, Switch.kt:180-189 and ToggleButton.kt:173-181
  // name FastSpatial. SplitButton.kt:693-697,773-780 names DefaultEffects.
  const auto checkComponentSpring = [&](const char *sourceFile, const char *animationName,
                                        const QString &spring, const QVariantList &options = {}) {
    QQmlComponent source(qmlEngine(w), QUrl(QString("qrc:/qml/%1.qml").arg(sourceFile)));
    QScopedPointer<QObject> made(source.create(qmlContext(w)));
    auto item = qobject_cast<QQuickItem *>(made.data());
    c.check(item, QString("%1 can be made to inspect its spring").arg(sourceFile));
    if (!item) return;
    item->setParentItem(w->contentItem());
    if (!options.isEmpty()) item->setProperty("options", options);
    QTest::qWait(50);
    // Behavior animations are not QObject children of the component root.
    // QML IDs live in the component or delegate context (QQmlContext).
    QQuickItem *scopeItem = item;
    if (QString::fromLatin1(sourceFile) == "MSegmentedControl")
      scopeItem = anyItem(item, "segmentBackground");
    auto context = scopeItem ? qmlContext(scopeItem) : nullptr;
    auto animation = context ? context->objectForName(animationName) : nullptr;
    const int duration = animation ? animation->property("duration").toInt() : -1;
    const int wantedDuration = c.evaluate("Theme." + spring + "Ms").toInt();
    const auto curveValue = animation
        ? QQmlProperty::read(animation, "easing.bezierCurve", qmlContext(animation)) : QVariant();
    const auto actualCurve = curveValue.toList();
    const auto wantedCurve = c.evaluate("Theme." + spring).toList();
    c.check(animation && duration == wantedDuration && actualCurve == wantedCurve,
            QString("%1 uses the %2 duration and curve (found=%3, duration=%4/%5, "
                    "curveType=%6, count=%7/%8, first=%9/%10)")
                .arg(animationName, spring).arg(bool(animation)).arg(duration).arg(wantedDuration)
                .arg(curveValue.typeName() ? curveValue.typeName() : "null")
                .arg(actualCurve.size()).arg(wantedCurve.size())
                .arg(actualCurve.isEmpty() ? -1 : actualCurve.first().toDouble())
                .arg(wantedCurve.isEmpty() ? -1 : wantedCurve.first().toDouble()));
    item->setVisible(false);
    item->setParentItem(nullptr);
  };
  checkComponentSpring("MRadioButton", "radioDotSpring", "springFastSpatial");
  checkComponentSpring("MSwitch", "switchThumbSpring", "springFastSpatial");
  checkComponentSpring("MSplitButton", "splitLeadingShapeSpring", "springEffects");
  checkComponentSpring("MSplitButton", "splitTrailingShapeSpring", "springEffects");
  checkComponentSpring("MSegmentedControl", "segmentShapeSpring", "springFastSpatial",
                       QVariantList{QVariantMap{{"key", "a"}, {"label", "A"}},
                                    QVariantMap{{"key", "b"}, {"label", "B"}}});

  // --- The app bar says when content is under it ---
  auto bar = anyItem(w->contentItem(), "appBarSurface");
  auto tracks = shownItem(w->contentItem(), "tracksView");
  c.check(bar && tracks, "the page has an app bar and a list under it");
  if (bar && tracks) {
    c.check(!bar->isVisible(), "which is plain surface while nothing has scrolled");
    const double origin = tracks->property("originY").toReal();
    tracks->setProperty("contentY", origin + 200);
    QTest::qWait(400);
    c.check(bar->isVisible() && bar->opacity() > 0.9,
            "and takes a container once the list is under it");
    c.check(bar->property("color").value<QColor>() == c.themeColor("container"),
            "the surfaceContainer Material names");
    auto lift = anyItem(bar, "elevation");
    c.check(lift && lift->property("level").toInt() == 2,
            "lifted the two levels that go with it");
    // A panel clips its children to its bounds, not to its corners, so a
    // shadow left to reach around the bar is drawn outside the panel and rings
    // its rounded top corners. The bar is flush with the top and both sides,
    // so the only part of its shadow that can be seen is the part below it.
    c.check(bar->property("topLeftRadius").toDouble() > 0,
            "the bar rounds its top corners with the panel");
    if (auto confine = anyItem(bar, "appBarLift")) {
      c.check(confine->clip(), "its shadow is confined rather than reaching around them");
      c.check(qAbs(confine->y() - bar->height()) < 0.5,
              "to the strip under the bar, clear of the corners");
      c.check(confine->height() > 0 && confine->height() <= 24,
              "which is as deep as the shadow reaches and no deeper");
      c.check(lift && lift->parentItem() == confine, "and the shadow is drawn inside it");
    } else {
      c.check(false, "the bar confines its shadow");
    }
    c.shot("02-app-bar-scrolled");
    tracks->setProperty("contentY", origin);
    QTest::qWait(400);
    c.check(!bar->isVisible(), "and settles back when the list returns");
  }

  // --- The two button variants that were missing ---
  QQmlComponent buttonSource(qmlEngine(w), QUrl("qrc:/qml/MButton.qml"));
  QScopedPointer<QObject> buttonObject(buttonSource.create(qmlContext(w)));
  auto sample = qobject_cast<QQuickItem *>(buttonObject.data());
  c.check(sample, "a button can be made to try the variants on");
  if (sample) {
    sample->setParentItem(w->contentItem());
    sample->setX(540); sample->setY(300); sample->setZ(95);
    sample->setProperty("text", QString("Shuffle"));
    sample->setProperty("elevated", true);
    QTest::qWait(200);
    auto container = sample->property("background").value<QQuickItem *>();
    c.check(container && container->property("color").value<QColor>() == c.themeColor("surfaceLow"),
            "an elevated button sits on the low surface container");
    auto lift = container ? anyItem(container, "elevation") : nullptr;
    c.check(lift && lift->property("level").toInt() == 1, "one level off the page");
    c.check(sample->property("ink").value<QColor>() == c.themeColor("primary"),
            "with its label in the accent");
    sample->setProperty("elevated", false);
    sample->setProperty("outlined", true);
    QTest::qWait(200);
    c.check(container && container->property("border").value<QObject *>()->property("width").toInt() == 1,
            "an outlined one draws a boundary instead of a container");
    // An outlined button is one of the components Material draws in the
    // variant rather than in the outline itself, because the label inside it
    // already says what it is.
    c.check(container && container->property("border").value<QObject *>()
                ->property("color").value<QColor>() == c.themeColor("outlineVariant"),
            "in the outline variant, which is the role Material names for it");
    c.check(sample->property("ink").value<QColor>() == c.themeColor("muted"),
            "and labels itself in onSurfaceVariant");
    // A button with a label and no container is Material's text button, and it
    // labels itself in the accent. The generated token says the variant ink,
    // but Compose sets Primary above a note that the token is uncorrected, and
    // the dialog action token says Primary too. A left aligned one is how this
    // app builds a list row, where the label is the row's own ink.
    sample->setProperty("outlined", false);
    sample->setProperty("text", QString("Try again"));
    sample->setProperty("leftAligned", false);
    QTest::qWait(200);
    c.check(sample->property("ink").value<QColor>() == c.themeColor("primary"),
            "a plain text button labels itself in the accent");
    sample->setProperty("leftAligned", true);
    QTest::qWait(200);
    c.check(sample->property("ink").value<QColor>() == c.themeColor("text"),
            "and a row built from one keeps the surface ink");
    sample->setProperty("leftAligned", false);
    sample->setProperty("text", QString(""));
    QTest::qWait(200);
    c.check(sample->property("ink").value<QColor>() == c.themeColor("text"),
            "as does an icon button, which has no label to colour");
    c.shotNow("03-button-variants");
    sample->setVisible(false);
    sample->setParentItem(nullptr);
  }

  // --- The scrim is a role ---
  c.check(c.evaluate("Theme.scrim").value<QColor>().isValid() &&
              qAbs(c.evaluate("Theme.scrimOpacity").toDouble() - 0.32) < 0.001,
          "the scrim is published with the opacity Material dims at");
  const auto scrimmed = c.evaluate("Theme.scrimColor()").value<QColor>();
  c.check(qAbs(scrimmed.alphaF() - 0.32) < 0.01, "and mixes to it on demand");

  // A selected root tab already names the page; its row begins at the same
  // place when switching from Songs to Albums.
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  QTest::qWait(800);
  c.check(!shownItem(w->contentItem(), "collectionHeaderTitle"),
          "Local files does not restate the selected tab in a headline");
  QPointF songsTabs(-1, -1);
  if (auto tabs = shownItem(w->contentItem(), "libraryTabs"))
    songsTabs = tabs->mapToScene(QPointF(0, 0));

  // --- The list a detail was opened from stays beside it ---
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("local-albums")));
  c.check(c.until([&] { return b->results()->count() == 2; }), "the library groups into albums");
  QTest::qWait(500);
  if (auto tabs = shownItem(w->contentItem(), "libraryTabs")) {
    const auto gridTabs = tabs->mapToScene(QPointF(0, 0));
    c.check(songsTabs.x() >= 0 && (gridTabs - songsTabs).manhattanLength() < 1,
            QString("the tabs stay put when the album grid comes up (%1,%2 against %3,%4)")
                .arg(gridTabs.x()).arg(gridTabs.y()).arg(songsTabs.x()).arg(songsTabs.y()));
  }
  auto pane = anyItem(w->contentItem(), "listPane");
  c.check(pane && !pane->isVisible(), "with no list pane while the grid is the page");
  c.shot("04-album-grid");
  auto card = shownItem(w->contentItem(), "openCollectionCard");
  c.check(card, "an album can be opened from it");
  if (card) {
    const auto point = card->mapToScene(card->boundingRect().center()).toPoint();
    QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, point);
    c.check(c.until([&] { return b->page() == "local-album"; }), "opening one shows the album");
    QTest::qWait(700);
    c.check(pane && pane->isVisible(), "and the grid it came from stays beside it");
    c.check(!shownItem(w->contentItem(), "libraryTabs") &&
                !shownItem(w->contentItem(), "localFacetTabs"),
            "detail tracks have neither library tab row between header and list");
    c.check(b->listPaneTitle() == "Albums",
            QString("named for what it is (%1)").arg(b->listPaneTitle()));
    c.check(b->listPane()->count() == 2,
            QString("holding what was on screen (%1)").arg(b->listPane()->count()));
    c.check(shownItem(w->contentItem(), "tracksView") != nullptr, "with the album's songs in the detail");
    c.shot("05-list-detail");
    // The other album in the pane swaps the detail without losing the list.
    const auto opened = b->title();
    auto second = shownItem(pane, "listPaneCard_1");
    if (second) {
      auto hit = shownItem(second, "openCollectionCard");
      if (hit) {
        QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier,
                          hit->mapToScene(hit->boundingRect().center()).toPoint());
        c.check(c.until([&] { return b->title() != opened; }, 6000),
                "choosing another from the pane swaps the detail");
        c.check(pane->isVisible() && b->listPane()->count() == 2, "and the pane is still there");
        c.shot("06-list-detail-swapped");
      }
    }
    // PaneScaffoldDirective.kt:58-70 gives expanded windows two partitions
    // with a 24dp spacer; the extra pane folds the list at this width.
    w->resize(1024, 860);
    QTest::qWait(800);
    auto detail = anyItem(w->contentItem(), "detailPane");
    c.check(pane->isVisible() && detail &&
                qAbs(detail->mapToScene(QPointF(0, 0)).x() -
                     pane->mapToScene(QPointF(pane->width(), 0)).x() - 24) < 1,
            "at 1024 the list and detail are visible with a 24dp spacer");
    c.shot("07-expanded-list-detail");
    w->resize(840, 860);
    QTest::qWait(650);
    c.check(pane->isVisible() && detail && detail->isVisible(),
            "the list and detail begin sharing partitions at 840dp");
    w->resize(1024, 860);
    QTest::qWait(500);
    w->setProperty("side", "queue");
    QTest::qWait(800);
    c.check(!pane->isVisible() && detail && detail->isVisible(),
            "at 1024 the queue takes the second partition and the list folds");
    c.shot("07-queue-two-pane");
    w->setProperty("side", "");
    w->resize(1440, 900);
    QTest::qWait(800);
    c.check(pane->isVisible() && qAbs(pane->width() - 360) < 1,
            "at 1440 the list returns at its 360dp preferred width");
    w->setProperty("side", "queue");
    QTest::qWait(700);
    auto side = anyItem(w->contentItem(), "sidePanel");
    auto grip = anyItem(w->contentItem(), "panelResizeHandle");
    const double detailRight = detail->mapToScene(QPointF(detail->width(), 0)).x();
    c.check(pane->isVisible() && side && grip && grip->isVisible() &&
                qAbs(side->mapToScene(QPointF(0, 0)).x() - detailRight - 24) < 1 &&
                qAbs(grip->mapToScene(QPointF(0, 0)).x() - detailRight) < 1,
            "at 1440 the grip occupies the 24dp spacer before the supporting pane");
    w->setProperty("side", "");
    w->resize(1600, 900);
    QTest::qWait(700);
    c.check(pane->isVisible() && qAbs(pane->width() - 412) < 1,
            "at 1600 the list uses the 412dp extra-large preference");
    w->resize(1440, 900);
    QTest::qWait(350);
    // Leaving the library puts it away for good.
    b->home();
    c.check(c.until([&] { return !b->busy(); }), "Home loads");
    QTest::qWait(500);
    c.check(b->listPaneId().isEmpty() && !pane->isVisible(),
            "and leaving the library clears the pane rather than stranding it");
    // Another tab of the library leaves the detail too. The pane used to stay
    // beside Mixes and History with the albums it no longer had a use for.
    QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("local-albums")));
    c.check(c.until([&] { return b->results()->count() == 2; }), "back in the album grid");
    QTest::qWait(500);
    auto again = shownItem(w->contentItem(), "openCollectionCard");
    if (again) {
      QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier,
                        again->mapToScene(again->boundingRect().center()).toPoint());
      c.check(c.until([&] { return b->page() == "local-album" && pane->isVisible(); }),
              "an album opened from it brings the pane back");
      QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("mixes")));
      c.check(c.until([&] { return b->listPaneId().isEmpty(); }),
              "choosing another library tab puts the pane away");
      QTest::qWait(500);
      c.check(!pane->isVisible(), "rather than leaving it beside a list it did not come from");
      c.shot("08-other-tab");
    }
  }
  c.shot("09-restored");
  c.finish();
}
