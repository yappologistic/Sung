#include "uitest.h"
#include "backend.h"
#include "rowselection.h"
#include <QColor>
#include <QAccessible>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGTextureProvider>
#include <QSettings>
#include <QTest>
#include <QWheelEvent>
#include <qpa/qwindowsysteminterface.h>
#include <cstdlib>
#include <functional>
#include <unistd.h>

namespace {
QQuickItem *itemNamed(QQuickItem *root, const QString &name) {
  if (root->objectName() == name)
    return root;
  for (auto child : root->childItems())
    if (auto found = itemNamed(child, name))
      return found;
  return nullptr;
}
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
struct Harness {
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
  void shot(const QString &name) {
    QTest::qWait(250);
    check(window->grabWindow().save(directory + '/' + name + ".png"), "capture " + name);
  }
  void click(const QString &name) {
    auto item = shownItem(window->contentItem(), name);
    check(item, "find " + name);
    if (!item)
      return;
    const auto point = item->mapToScene(item->boundingRect().center()).toPoint();
    QTest::mouseMove(window, point);
    QTest::qWait(60);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
    QTest::qWait(300);
  }
  QVariant evaluate(const QString &script) {
    QQmlExpression expression(qmlContext(window), window, script);
    return expression.evaluate();
  }
  void finish() {
    fprintf(stdout, "RESULT %d failures\n", failures);
    fflush(stdout);
    QCoreApplication::exit(failures ? 1 : 0);
  }
};

// A silent track whose cover has enough color for a backdrop to be visible.
bool writeFixture(Harness &c, const QString &path, const QStringList &extra) {
  QStringList arguments{"-nostdin", "-v", "error", "-f", "lavfi", "-i",
                        "anullsrc=r=8000:cl=mono", "-t", "120",
                        "-metadata", "artist=Example Artist"};
  arguments += extra;
  arguments << path;
  QProcess encode;
  encode.start("ffmpeg", arguments);
  const bool ok = encode.waitForFinished(15000) && encode.exitCode() == 0;
  c.check(ok, "generate fixture " + QFileInfo(path).fileName());
  return ok;
}
} // namespace

void runAmbientImmersiveTests(Backend *b, QQuickWindow *w) {
  Harness c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->setMinimumSize({1180, 800});
  w->setMaximumSize({1180, 800});
  w->resize(1180, 800);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(true);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);
  b->setLyricsFallback(false);

  QImage cover(640, 640, QImage::Format_RGB32);
  QPainter paint(&cover);
  paint.fillRect(cover.rect(), QColor("#2b4a7a"));
  paint.fillRect(0, 0, 640, 280, QColor("#c8553d"));
  paint.fillRect(120, 180, 400, 300, QColor("#f2b134"));
  paint.end();
  cover.save(c.directory + "/music/cover.png");
  for (const auto &name : {"01", "02", "03", "04"})
    if (!writeFixture(c, c.directory + "/music/" + name + ".flac",
                      {"-metadata", QString("title=Track ") + name, "-metadata", "album=Ambient fixture"}))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }), "import ambient fixture");
  b->library("files");
  b->enqueueItems(b->results()->rows);
  b->playAt(0);
  c.check(c.until([&] { return b->playing(); }), "fixture playback starts");
  c.check(!b->current().value("art").toString().isEmpty(), "fixture track carries a cover");

  w->setProperty("immersive", true);
  QTest::qWait(500);
  auto player = shownItem(w->contentItem(), "immersivePlayer");
  c.check(player, "immersive player loads");
  if (!player)
    return c.finish();

  // --- Ambient backdrop ---
  auto backdrop = itemNamed(player, "ambientBackdrop");
  c.check(backdrop, "immersive player has an ambient backdrop");
  if (!backdrop)
    return c.finish();
  auto art = itemNamed(backdrop, "ambientArt");
  auto scrim = itemNamed(backdrop, "ambientScrim");
  c.check(b->ambientBackdrop(), "ambient backdrop is on by default");
  c.check(backdrop->property("active").toBool() && backdrop->isVisible() && backdrop->opacity() > 0,
          "backdrop is active while a cover is playing");
  c.check(art && art->property("source").toUrl() == QUrl(b->current().value("art").toString()),
          "backdrop draws the current cover");
  c.check(art && art->property("pixels").toInt() <= 256 && art->property("pixels").toInt() * 3 < backdrop->width(),
          "backdrop decodes a small cover rather than a full-size one");
  c.check(art && art->property("blur").toInt() > 0,
          "the backdrop cover is blurred, not merely enlarged");
  c.check(scrim && scrim->isVisible() && scrim->property("color").value<QColor>().alphaF() > 0.6,
          "a scrim covers the backdrop so surface contrast is preserved");
  c.check(art && art->width() >= backdrop->width() && art->height() >= backdrop->height(),
          "backdrop cover fills the player on both axes");
  c.check(backdrop->property("clip").toBool() && backdrop->property("drifts").toBool(),
          "the full-bleed backdrop drifts inside its own bounds");
  c.shot("01-backdrop");

  c.check(backdrop->property("animating").toBool(), "backdrop drifts while motion is enabled");
  b->setMotion(false);
  QTest::qWait(150);
  c.check(!backdrop->property("animating").toBool(), "reduced motion stops the drift");
  c.check(qFuzzyCompare(backdrop->property("drift").toReal(), 1.0), "stopped drift settles at its resting scale");
  b->setMotion(true);
  QTest::qWait(150);
  c.check(backdrop->property("animating").toBool(), "motion restores the drift");

  b->setAmbientBackdrop(false);
  QTest::qWait(700);
  c.check(!backdrop->property("active").toBool() && !backdrop->isVisible(),
          "disabling the setting hides the backdrop");
  c.check(art && art->property("source").toUrl().isEmpty(), "hidden backdrop releases its decoded cover");
  c.shot("02-backdrop-off");
  b->setAmbientBackdrop(true);
  QTest::qWait(700);
  c.check(backdrop->property("active").toBool() && art && !art->property("source").toUrl().isEmpty(),
          "re-enabling restores the backdrop and its cover");

  // --- Up next carousel ---
  c.check(!player->property("coverflow").toBool(), "carousel is off until it is asked for");
  c.check(!shownItem(w->contentItem(), "coverflowView"), "hidden carousel builds no delegates");
  c.click("immersiveLayoutButton");
  c.click("immersiveCoverflowToggle");
  QTest::qWait(500);
  c.check(player->property("coverflow").toBool() && player->property("coverflowVisible").toBool(),
          "layout menu enables the carousel");
  auto covers = shownItem(w->contentItem(), "coverflowView");
  c.check(covers, "carousel view is shown");
  if (!covers)
    return c.finish();
  c.check(covers->property("count").toInt() == b->queue()->count(), "carousel shows the whole queue");
  c.check(c.until([&] { return covers->property("currentIndex").toInt() == b->currentIndex(); }),
          "carousel centers the playing track");
  auto centered = itemNamed(w->contentItem(), "coverflowItem_" + QString::number(b->currentIndex()));
  auto neighbour = itemNamed(w->contentItem(), "coverflowItem_" + QString::number(b->currentIndex() + 1));
  c.check(centered && neighbour, "carousel builds the centered item and its neighbour");
  if (centered && neighbour) {
    const auto large = centered->property("coverSize").toReal();
    const auto small = neighbour->property("coverSize").toReal();
    c.check(large > small, "the centered item is the large carousel item");
    c.check(small >= 40 && small <= 56, "peeking items stay inside the small carousel item range");
    c.check(qAbs(centered->property("emphasis").toReal() - 1.0) < 0.05, "centered item reaches full emphasis");
  }
  c.shot("03-coverflow");
  w->setMinimumSize({0, 0});
  w->setMaximumSize({16777215, 16777215});
  for (const auto &theme : {"dark", "light"}) {
    b->setTheme(theme);
    for (const int width : {480, 600, 840, 1024, 1440, 2560}) {
      w->resize(width, width == 480 ? 620 : 800);
      QTest::qWait(150);
      c.shot(QString("03-coverflow-%1-%2").arg(width).arg(theme));
    }
  }
  b->setTheme("dark");
  w->setMinimumSize({1180, 800});
  w->setMaximumSize({1180, 800});
  w->resize(1180, 800);
  QTest::qWait(200);

  // The coverflow is one keyboard stop. Arrows preview without seeking or
  // starting playback; Return activates the cover the keyboard selected.
  bool reachedCoverflow = false;
  for (int tab = 0; tab < 24 && !reachedCoverflow; ++tab) {
    QTest::keyClick(w, Qt::Key_Tab);
    auto focused = w->activeFocusItem();
    for (auto item = focused; item; item = item->parentItem())
      if (item == covers) { reachedCoverflow = true; break; }
  }
  c.check(reachedCoverflow, "Tab reaches the coverflow as one stop");
  if (reachedCoverflow) {
    const auto position = b->position();
    const auto playingIndex = b->currentIndex();
    QTest::keyClick(w, Qt::Key_Right);
    c.check(covers->property("keyboardIndex").toInt() == playingIndex + 1,
            "Right moves the focused cover");
    c.check(b->currentIndex() == playingIndex && qAbs(b->position() - position) < 1500,
            "coverflow Right neither plays nor seeks");
    QTest::keyClick(w, Qt::Key_End);
    c.check(covers->property("keyboardIndex").toInt() == b->queue()->count() - 1,
            "End focuses the last cover");
    QTest::keyClick(w, Qt::Key_Home);
    c.check(covers->property("keyboardIndex").toInt() == 0, "Home focuses the first cover");
    QTest::keyClick(w, Qt::Key_Right);
    QTest::keyClick(w, Qt::Key_Return);
    c.check(c.until([&] { return b->currentIndex() == 1; }),
            "Return plays the keyboard focused cover");
    auto accessible = QAccessible::queryAccessibleInterface(covers);
    auto focusedCover = QAccessible::queryAccessibleInterface(w->activeFocusItem());
    const auto focusedTitle = b->queue()->get(1).value("title").toString();
    c.check(accessible && accessible->role() == QAccessible::List &&
                accessible->text(QAccessible::Name).contains(focusedTitle) &&
                accessible->text(QAccessible::Name).contains("2 of 4") &&
                focusedCover && focusedCover->role() == QAccessible::ListItem &&
                focusedCover->text(QAccessible::Name).contains(focusedTitle),
            "the focused cover announces title and position in the list");
    c.shot("03-coverflow-keyboard");
  }

  const auto secondId = b->queue()->get(2).value("id");
  auto target = itemNamed(w->contentItem(), "coverflowItem_2");
  c.check(target, "carousel exposes a reachable neighbour");
  if (target) {
    const auto point = target->mapToScene(target->boundingRect().center()).toPoint();
    QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, point);
    c.check(c.until([&] { return b->currentIndex() == 2 && b->playing(); }),
            "clicking a carousel item plays that track");
    c.check(b->current().value("id") == secondId, "carousel plays the track it displayed");
  }
  c.check(c.until([&] { return covers->property("currentIndex").toInt() == 2; }),
          "carousel re-centers after the track changes");

  // Settling on a cover has to play it; reaching it should not need a click.
  const auto dragged = covers->mapToScene(covers->boundingRect().center()).toPoint();
  QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, dragged);
  for (int step = 1; step <= 9; ++step)
    QTest::mouseMove(w, dragged + QPoint(step * 18, 0), 16);
  QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, dragged + QPoint(162, 0));
  c.check(c.until([&] { return covers->property("currentIndex").toInt() != 2; }),
          "dragging the carousel moves it off the playing track");
  const int settled = covers->property("currentIndex").toInt();
  c.check(c.until([&] { return b->currentIndex() == settled; }),
          "the cover the carousel settles on starts playing on its own");
  c.check(c.until([&] { return b->playing(); }), "the scrolled-to track plays rather than only loading");
  c.check(b->current().value("id") == b->queue()->get(settled).value("id"),
          "the track that plays is the one the carousel shows");
  QTest::qWait(500);
  c.check(b->currentIndex() == settled && covers->property("currentIndex").toInt() == settled,
          "re-centering after that change does not start yet another track");

  // Playback started elsewhere re-centers the carousel without looping back.
  b->playAt(0);
  c.check(c.until([&] { return b->currentIndex() == 0; }), "playback moves to the first track");
  c.check(c.until([&] { return covers->property("currentIndex").toInt() == 0; }),
          "the carousel follows playback that started elsewhere");
  QTest::qWait(500);
  c.check(b->currentIndex() == 0, "following playback never plays a different track back");

  c.click("coverflowShowAll");
  auto sheet = w->findChild<QObject *>("immersiveQueueSheet");
  c.check(sheet && sheet->property("visible").toBool(),
          "Show all opens the full queue, the carousel's non-scrolling alternative");
  if (sheet) {
    QQuickItem *heading = nullptr;
    for (auto text : sheet->findChildren<QQuickItem *>())
      if (text->property("text").toString() == "Queue") { heading = text; break; }
    auto accessible = heading ? QAccessible::queryAccessibleInterface(heading) : nullptr;
    c.check(accessible && accessible->role() == QAccessible::Heading,
            "queue title is an accessibility heading");
  }
  QTest::keyClick(w, Qt::Key_Escape);
  c.check(c.until([&] { return sheet && !sheet->property("visible").toBool(); }, 3000) &&
              w->property("immersive").toBool(),
          "closing the queue keeps the immersive player");

  b->enqueueItems(b->results()->rows);
  w->setMinimumSize({0, 0});
  w->setMaximumSize({16777215, 16777215});
  w->resize(480, 620);
  QTest::qWait(250);
  QTest::keyClick(w, Qt::Key_L, Qt::ControlModifier);
  c.check(c.until([&] { return sheet && sheet->property("visible").toBool(); }),
          "Ctrl+L opens the narrow immersive queue");
  auto queueView = shownItem(w->contentItem(), "queueView");
  c.check(queueView && queueView->height() > 0, "narrow drawer has a queue viewport");
  bool reachedClippedAction = false;
  bool actionInView = false;
  for (int tab = 0; queueView && tab < 80 && !reachedClippedAction; ++tab) {
    QTest::keyClick(w, Qt::Key_Tab);
    auto focused = w->activeFocusItem();
    auto name = focused ? QAccessible::queryAccessibleInterface(focused) : nullptr;
    int focusedRow = -1;
    for (auto item = focused; item && item != queueView; item = item->parentItem())
      if (item->property("selectionIndex").isValid()) { focusedRow = item->property("selectionIndex").toInt(); break; }
    if (!name || !name->text(QAccessible::Name).startsWith("Actions for ") || focusedRow != 5)
      continue;
    reachedClippedAction = true;
    const auto action = focused->mapRectToScene(focused->boundingRect());
    const auto viewport = queueView->mapRectToScene(queueView->boundingRect());
    actionInView = action.top() >= viewport.top() - 1 && action.bottom() <= viewport.bottom() + 1;
  }
  c.check(reachedClippedAction, "Tab reaches the last queue row action");
  c.check(actionInView, "Tab scrolls the focused queue action fully into view");
  c.shot("03-queue-focus-480");
  QTest::keyClick(w, Qt::Key_Escape);
  c.until([&] { return sheet && !sheet->property("visible").toBool(); }, 3000);
  w->setMinimumSize({1180, 800});
  w->setMaximumSize({1180, 800});
  w->resize(1180, 800);
  QTest::qWait(250);

  w->setProperty("immersive", false);
  QTest::qWait(300);
  w->setProperty("immersive", true);
  QTest::qWait(500);
  player = shownItem(w->contentItem(), "immersivePlayer");
  c.check(player && player->property("coverflow").toBool(), "carousel preference survives player recreation");
  c.click("immersiveLayoutButton");
  c.click("immersiveCoverflowToggle");
  QTest::qWait(300);
  c.check(player && !player->property("coverflowVisible").toBool(), "the same menu entry turns the carousel off");
  c.check(!shownItem(w->contentItem(), "coverflowView"), "turning the carousel off releases its view");

  b->setTheme("light");
  QTest::qWait(400);
  c.shot("04-backdrop-light");
  b->setTheme("dark");

  // --- Now playing panel ---
  w->setProperty("immersive", false);
  QTest::qWait(300);
  w->setProperty("side", "now");
  QTest::qWait(600);
  auto panelBackdrop = itemNamed(w->contentItem(), "nowBackdrop");
  c.check(panelBackdrop, "Now playing panel has an ambient backdrop");
  if (panelBackdrop) {
    auto panelArt = itemNamed(panelBackdrop, "ambientArt");
    auto panelScrim = itemNamed(panelBackdrop, "ambientScrim");
    c.check(panelBackdrop->property("active").toBool() && panelBackdrop->isVisible(),
            "panel backdrop follows the playing cover");
    c.check(panelArt && panelArt->property("source").toUrl() == QUrl(b->current().value("art").toString()),
            "panel backdrop draws the current cover");
    c.check(panelScrim && panelScrim->property("color").value<QColor>().alphaF() > 0.8,
            "the panel scrim is deeper than the player's because its text is denser");
    auto panel = itemNamed(w->contentItem(), "sidePanel");
    c.check(panel && panelBackdrop->width() <= panel->width(), "the backdrop stays inside the panel");
    c.check(panel && qFuzzyCompare(panelBackdrop->property("corner").toReal(), panel->property("radius").toReal()),
            "the panel backdrop keeps the panel's rounded corners");
    c.check(!panelBackdrop->property("drifts").toBool() && !panelBackdrop->property("animating").toBool(),
            "a rounded backdrop holds still so its corners stay clean");
    c.shot("05-now-backdrop");
    w->setProperty("side", "queue");
    QTest::qWait(700);
    c.check(!panelBackdrop->property("active").toBool(),
            "switching the panel to the queue releases the backdrop");
    c.check(panelArt && panelArt->property("source").toUrl().isEmpty(),
            "the released panel backdrop keeps no cover");
    w->setProperty("side", "");
    QTest::qWait(300);
  }
  b->clearQueue();
  b->stop();
  c.finish();
}

void runPersonalizationTests(Backend *b, QQuickWindow *w) {
  Harness c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1180, 800);
  QTest::qWait(400);
  b->setTheme("dark");
  b->setMotion(false);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);
  b->setLyricsFallback(false);
  b->setArtworkAccent(false);
  b->setAccentColor("");

  // --- Hand-picked Material source color ---
  const auto defaultPrimary = c.evaluate("Theme.primary").value<QColor>();
  c.check(!c.evaluate("Theme.useSource").toBool(), "no source color is applied by default");
  b->setAccentColor("#386a20");
  QTest::qWait(150);
  c.check(b->accentColor() == "#386a20", "accent color is stored normalized");
  c.check(c.evaluate("Theme.useAccent").toBool() && c.evaluate("Theme.useSource").toBool(),
          "a chosen source color drives the scheme");
  const auto themed = c.evaluate("Theme.primary").value<QColor>();
  c.check(themed != defaultPrimary, "primary role follows the chosen source color");
  for (const auto &surface : {"background", "surface", "container", "high"})
    c.check(c.evaluate(QString("Theme.contrast(Theme.primary,Theme.%1)").arg(surface)).toReal() >= 4.5,
            QString("primary keeps 4.5:1 against %1").arg(surface));
  c.check(c.evaluate("Theme.contrast(Theme.containerText,Theme.primaryContainer)").toReal() >= 4.5,
          "container text keeps 4.5:1 against its container");
  c.check(c.evaluate("Theme.contrast(Theme.primaryText,Theme.primary)").toReal() >= 4.5,
          "text on primary keeps 4.5:1");
  b->setTheme("light");
  QTest::qWait(150);
  for (const auto &surface : {"background", "surface", "container", "high"})
    c.check(c.evaluate(QString("Theme.contrast(Theme.primary,Theme.%1)").arg(surface)).toReal() >= 4.5,
            QString("light theme keeps 4.5:1 against %1").arg(surface));
  b->setTheme("dark");
  QTest::qWait(150);

  b->setArtworkAccent(true);
  c.evaluate("Theme.artworkSeed=Qt.rgba(0.85,0.2,0.3,1)");
  QTest::qWait(150);
  c.check(c.evaluate("Theme.useArtwork").toBool() && !c.evaluate("Theme.useAccent").toBool(),
          "artwork accent takes priority over the chosen color");
  b->setArtworkAccent(false);
  c.evaluate("Theme.artworkSeed=Qt.rgba(0,0,0,0)");
  QTest::qWait(150);
  c.check(c.evaluate("Theme.useAccent").toBool(), "the chosen color returns when artwork accent is off");
  const auto stored = b->accentColor();
  b->setAccentColor("not a color");
  c.check(b->accentColor() == stored, "an unreadable value never replaces the stored color");
  b->setAccentColor("");
  QTest::qWait(150);
  c.check(!c.evaluate("Theme.useSource").toBool() && c.evaluate("Theme.primary").value<QColor>() == defaultPrimary,
          "clearing the color restores the built-in palette");

  // --- Volume normalization ---
  c.check(!b->volumeNormalization(), "levelling is off by default");
  if (!writeFixture(c, c.directory + "/music/quiet.flac",
                    {"-metadata", "title=Quiet track", "-metadata", "album=Levelling fixture",
                     "-metadata", "replaygain_track_gain=-6.00 dB"}))
    return c.finish();
  if (!writeFixture(c, c.directory + "/music/loud.flac",
                    {"-metadata", "title=Loud track", "-metadata", "album=Levelling fixture",
                     "-metadata", "replaygain_track_gain=+18.00 dB"}))
    return c.finish();
  if (!writeFixture(c, c.directory + "/music/plain.flac",
                    {"-metadata", "title=Plain track", "-metadata", "album=Levelling fixture"}))
    return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }), "import levelling fixture");
  b->library("files");
  b->collection()->setSortKey("title");
  QTest::qWait(200);
  QVariantMap quiet, loud, plain;
  for (const auto &row : b->results()->rows) {
    const auto track = row.toMap();
    if (track.value("title") == "Quiet track")
      quiet = track;
    else if (track.value("title") == "Loud track")
      loud = track;
    else if (track.value("title") == "Plain track")
      plain = track;
  }
  c.check(!quiet.isEmpty() && !loud.isEmpty() && !plain.isEmpty(), "levelling fixture imported");
  if (quiet.isEmpty() || loud.isEmpty() || plain.isEmpty())
    return c.finish();
  c.check(quiet.value("replaygainTrackGain").toString().startsWith("-6"),
          "import reads the ReplayGain tag from the file");
  c.check(!plain.contains("replaygainTrackGain"), "an untagged file carries no gain");

  b->setVolume(0.5);
  b->enqueueItems({quiet, loud, plain});
  b->playAt(0);
  c.check(c.until([&] { return b->playing() && b->current().value("title") == "Quiet track"; }),
          "tagged track plays");
  c.check(qAbs(b->effectiveVolume() - 0.5) < 0.001, "levelling changes nothing while it is off");
  b->setVolumeNormalization(true);
  QTest::qWait(150);
  c.check(b->normalizationSource() == "Track tag", "a tagged track is levelled from its tag");
  c.check(qAbs(b->normalizationGainDb() + 6.0) < 0.01, "the tag's gain is applied as written");
  c.check(qAbs(b->effectiveVolume() - 0.5 * std::pow(10.0, -6.0 / 20.0)) < 0.002,
          "output level drops by the tagged amount");
  c.check(qAbs(b->volume() - 0.5) < 0.001, "levelling never rewrites the volume the user chose");

  b->playAt(1);
  c.check(c.until([&] { return b->current().value("title") == "Loud track"; }), "second tagged track plays");
  QTest::qWait(150);
  c.check(qAbs(b->normalizationGainDb() - 6.0) < 0.01, "an extreme tag is clamped to a safe boost");
  c.check(b->effectiveVolume() <= 1.0, "levelling never asks the mixer for more than full scale");
  b->playAt(2);
  c.check(c.until([&] { return b->current().value("title") == "Plain track"; }), "untagged track plays");
  QTest::qWait(150);
  c.check(b->normalizationSource().isEmpty() && qAbs(b->effectiveVolume() - 0.5) < 0.001,
          "an untagged, unmeasured track plays at the chosen volume");
  c.check(qIsNaN(b->measuredLoudness(plain.value("id").toString())),
          "a track this short is never treated as measured");

  // A cached measurement levels sources that carry no tags at all.
  {
    QSettings settings;
    settings.setValue("loudness/" + plain.value("id").toString(), -23.0);
    settings.sync();
  }
  b->playAt(0);
  c.check(c.until([&] { return b->current().value("title") == "Quiet track"; }), "return to the tagged track");
  b->playAt(2);
  c.check(c.until([&] { return b->current().value("title") == "Plain track"; }), "replay the untagged track");
  QTest::qWait(150);
  c.check(b->normalizationSource() == "Measured", "a measured track is levelled from its measurement");
  c.check(qAbs(b->normalizationGainDb() - 5.0) < 0.01, "measurement targets a consistent reference level");

  b->setVolumeNormalization(false);
  QTest::qWait(150);
  c.check(b->normalizationSource().isEmpty() && qAbs(b->effectiveVolume() - 0.5) < 0.001,
          "turning levelling off restores the plain volume immediately");
  b->setVolumeNormalization(true);
  QTest::qWait(150);
  bool listed = false;
  for (const auto &row : b->trackDetails(b->current()))
    if (row.toMap().value("label") == "Volume normalization")
      listed = true;
  c.check(listed, "Track details reports the applied levelling");
  b->setVolumeNormalization(false);
  b->pause();
  b->clearQueue();

  // --- Type-ahead jump ---
  b->library("files");
  b->collection()->setSortKey("title");
  QTest::qWait(300);
  auto tracks = shownItem(w->contentItem(), "tracksView");
  c.check(tracks && tracks->property("count").toInt() >= 3, "local songs are listed for type-ahead");
  if (!tracks)
    return c.finish();
  c.check(b->typeAheadJump(), "type-ahead is on by default");
  tracks->forceActiveFocus();
  tracks->setProperty("currentIndex", -1);
  QTest::keyClick(w, Qt::Key_L);
  QTest::qWait(80);
  c.check(tracks->property("typeAhead") == "l" && tracks->property("currentIndex").toInt() >= 0,
          "a single key jumps to the first matching row");
  // The list shows the sorted collection, so rows are read through that view.
  auto shownTitle = [&] { return b->collection()->get(tracks->property("currentIndex").toInt()).value("title").toString(); };
  c.check(shownTitle().toLower().startsWith("l"), "the jump lands on a matching title");
  QTest::keyClick(w, Qt::Key_O);
  QTest::qWait(80);
  c.check(tracks->property("typeAhead") == "lo", "a second key refines the same search");
  c.check(shownTitle() == "Loud track", "refining reaches the intended row");
  c.shot("05-type-ahead");
  QTest::keyClick(w, Qt::Key_Z);
  QTest::qWait(80);
  c.check(tracks->property("typeAhead") == "lo", "a key with no match leaves the search untouched");
  c.check(shownTitle() == "Loud track", "a key with no match does not move the selection");
  QTest::qWait(1100);
  c.check(tracks->property("typeAhead").toString().isEmpty(), "the search clears after a pause");
  QTest::keyClick(w, Qt::Key_Q);
  QTest::qWait(80);
  c.check(shownTitle() == "Quiet track", "a new search starts from the cleared buffer");

  // Space and modified keys keep belonging to their shortcuts, not to the search.
  QTest::qWait(1100);
  c.check(tracks->property("typeAhead").toString().isEmpty(), "search buffer is idle before the shortcut checks");
  const auto before = tracks->property("currentIndex").toInt();
  QTest::keyClick(w, Qt::Key_Space);
  QTest::qWait(120);
  c.check(tracks->property("typeAhead").toString().isEmpty() && tracks->property("currentIndex").toInt() == before,
          "Space never starts a search");
  QTest::keyClick(w, Qt::Key_L, Qt::AltModifier);
  QTest::qWait(80);
  c.check(tracks->property("typeAhead").toString().isEmpty() && tracks->property("currentIndex").toInt() == before,
          "a modified key is left to its shortcut");

  b->setTypeAheadJump(false);
  tracks->forceActiveFocus();
  QTest::keyClick(w, Qt::Key_L);
  QTest::qWait(80);
  c.check(tracks->property("typeAhead").toString().isEmpty() && tracks->property("currentIndex").toInt() == before,
          "disabling type-ahead stops the jump");
  b->setTypeAheadJump(true);
  QTest::keyClick(w, Qt::Key_L);
  QTest::qWait(80);
  c.check(shownTitle() == "Loud track", "re-enabling restores the jump");

  // --- Settings surface ---
  c.click("settingsButton");
  QTest::qWait(400);
  c.check(shownItem(w->contentItem(), "settingsSearch"), "settings search field is available");
  auto typeText = [&](const QString &text) {
    QQmlExpression apply(qmlContext(w), w, QString("settingsDialog.searchQuery=\"%1\"").arg(text));
    apply.evaluate();
    QTest::qWait(250);
  };
  typeText("accent");
  auto picker = shownItem(w->contentItem(), "accentPicker");
  c.check(picker && picker->isVisible() && picker->width() > 0, "settings search finds the accent picker");
  c.check(picker && picker->height() >= 44, "accent swatches keep a full touch target");
  c.shot("06-settings-accent");
  c.click("accentSeed_00658e");
  c.check(b->accentColor() == "#00658e", "clicking a swatch applies that source color");
  c.check(c.evaluate("Theme.useAccent").toBool(), "the applied swatch themes the app");
  c.shot("07-settings-accent-applied");
  c.click("accentSeed_default");
  c.check(b->accentColor().isEmpty() && !c.evaluate("Theme.useSource").toBool(),
          "the default swatch clears the source color");

  typeText("ambient backdrop");
  auto ambient = shownItem(w->contentItem(), "ambientBackdropSwitch");
  c.check(ambient && ambient->isVisible(), "settings search finds the backdrop switch");
  c.check(ambient && ambient->property("checked").toBool() == b->ambientBackdrop(), "backdrop switch shows its state");
  c.click("ambientBackdropSwitch");
  c.check(!b->ambientBackdrop(), "the backdrop switch turns the backdrop off");
  c.click("ambientBackdropSwitch");
  c.check(b->ambientBackdrop(), "the backdrop switch turns it back on");

  typeText("volume normalization");
  auto levelling = shownItem(w->contentItem(), "volumeNormalizationSwitch");
  c.check(levelling && levelling->isVisible(), "settings search finds the levelling switch");
  c.click("volumeNormalizationSwitch");
  c.check(b->volumeNormalization(), "the levelling switch turns levelling on");
  c.shot("08-settings-playback");
  c.click("volumeNormalizationSwitch");
  c.check(!b->volumeNormalization(), "the levelling switch turns it off again");

  typeText("type to jump");
  auto jump = shownItem(w->contentItem(), "typeAheadSwitch");
  c.check(jump && jump->isVisible(), "settings search finds the type-ahead switch");
  c.click("typeAheadSwitch");
  c.check(!b->typeAheadJump(), "the type-ahead switch turns the jump off");
  c.click("typeAheadSwitch");
  c.check(b->typeAheadJump(), "the type-ahead switch turns it back on");
  c.shot("09-settings-library");

  typeText("");
  QQmlExpression category(qmlContext(w), w, "settingsDialog.category=0");
  category.evaluate();
  QTest::qWait(300);
  auto options = shownItem(w->contentItem(), "settingsOptions");
  c.check(options && options->width() > 0, "appearance settings stay laid out");
  c.shot("10-settings-appearance");
  w->resize(780, 580);
  QTest::qWait(400);
  c.shot("11-settings-narrow");
  auto narrowPicker = shownItem(w->contentItem(), "accentPicker");
  c.check(narrowPicker && narrowPicker->width() <= 780,
          "the accent picker wraps inside a narrow settings dialog");
  QTest::keyClick(w, Qt::Key_Escape);
  QTest::qWait(250);
  w->resize(1180, 800);
  b->stop();
  c.finish();
}

void runHomeRailTests(Backend *b, QQuickWindow *w) {
  Harness c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1280, 860);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(true);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);
  b->setLyricsFallback(false);
  b->home();
  c.check(c.until([&] { return !b->busy(); }), "home shelves load");

  // --- The ambient backdrop ---
  // The wash belongs to the window rather than to one panel of it, so Home is
  // tinted by the same surface that tints everything else.
  auto backdrop = itemNamed(w->contentItem(), "windowBackdrop");
  c.check(backdrop, "the window has an ambient backdrop");
  if (!backdrop)
    return c.finish();
  auto art = itemNamed(backdrop, "ambientArt");
  c.check(art && art->property("blur").toInt() > 0, "the cover is blurred, not enlarged");
  c.check(backdrop->property("drifts").toBool(),
          "a full-bleed wash drifts, having no corners to keep clean");
  c.check(itemNamed(w->contentItem(), "homeBackdrop"),
          "the panel keeps its own backdrop for when the window carries none");
  // Nothing playing and no shelf covers: Home must not invent a backdrop.
  c.check(w->property("homeArtwork").toString().isEmpty(), "Home finds no cover to borrow yet");
  c.check(w->property("windowArtwork").toString().isEmpty(), "so the window has none either");
  c.check(!backdrop->property("active").toBool() && art->property("source").toUrl().isEmpty(),
          "a coverless Home shows no backdrop at all");
  c.check(qAbs(w->property("washAlpha").toReal() - 1) < 0.001,
          "and the surfaces stay opaque while there is nothing to show through them");
  c.shot("01-home-bare");

  // Playing something tints Home from the track.
  QImage cover(480, 480, QImage::Format_RGB32);
  QPainter paint(&cover);
  paint.fillRect(cover.rect(), QColor("#1f6f5c"));
  paint.fillRect(0, 0, 480, 200, QColor("#d98324"));
  paint.end();
  cover.save(c.directory + "/music/cover.png");
  QProcess encode;
  encode.start("ffmpeg", {"-nostdin", "-v", "error", "-f", "lavfi", "-i", "anullsrc=r=8000:cl=mono",
                          "-t", "60", "-metadata", "title=Home fixture", "-metadata", "artist=Example Artist",
                          "-metadata", "album=Home fixture", c.directory + "/music/01.flac"});
  c.check(encode.waitForFinished(15000) && encode.exitCode() == 0, "generate home fixture");
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }), "import home fixture");
  b->library("files");
  b->enqueueItems(b->results()->rows);
  b->playAt(0);
  c.check(c.until([&] { return b->playing(); }), "fixture plays");
  b->home();
  c.check(c.until([&] { return !b->busy(); }), "return to Home");
  QTest::qWait(400);
  c.check(w->property("homeArtwork").toString() == b->current().value("art").toString(),
          "Home prefers the playing cover once there is one");
  c.check(c.until([&] { return art->property("source").toUrl() == QUrl(b->current().value("art").toString()); }),
          "the backdrop follows the playing cover");
  c.check(w->property("washAlpha").toReal() < 1,
          "and the surfaces open up so it reaches the whole window");
  c.shot("02-home-playing");

  b->library("favorites");
  QTest::qWait(400);
  // The wash follows the music rather than the page, so leaving Home keeps it.
  c.check(backdrop->property("active").toBool(),
          "leaving Home keeps the wash, because the music has not stopped");
  c.check(art->property("source").toUrl() == QUrl(b->current().value("art").toString()),
          "still taken from what is playing");
  b->home();
  c.check(c.until([&] { return !b->busy(); }), "back to Home");
  QTest::qWait(300);
  b->setAmbientBackdrop(false);
  QTest::qWait(700);
  c.check(!backdrop->property("active").toBool(), "the backdrop setting governs the wash");
  c.check(qAbs(w->property("washAlpha").toReal() - 1) < 0.001,
          "and turning it off closes the surfaces again");
  b->setAmbientBackdrop(true);
  QTest::qWait(700);
  c.check(backdrop->property("active").toBool(), "re-enabling restores it");

  // --- The navigation capsule in the top bar ---
  auto bar = itemNamed(w->contentItem(), "navigationBar");
  c.check(bar, "the navigation bar exists");
  if (!bar)
    return c.finish();
  c.check(bar->property("hugsContent").toBool(),
          "a window with room for it shows the bar as the capsule");
  // FloatingToolbarTokens and NavigationBarTokens both put this container at
  // 64dp, and the capsule takes a full corner rather than a fixed radius.
  c.check(qAbs(bar->height() - 64) < 1,
          QString("which is 64dp tall (%1)").arg(bar->height(), 0, 'f', 0));
  c.check(qAbs(bar->property("radius").toReal() - bar->height() / 2) < 0.5,
          "and rounded to its own half height");
  // The capsule hugs its destinations rather than spanning the window, so it
  // has to be narrower than the bar it replaced would have been.
  c.check(bar->width() < w->width() / 2,
          QString("it hugs its destinations (%1 across a %2 window)")
              .arg(bar->width(), 0, 'f', 0).arg(w->width()));
  // Centred on the window, not on the gap between the two sides of the bar.
  const double middle = bar->mapToScene(QPointF(bar->width() / 2, 0)).x();
  c.check(qAbs(middle - w->width() / 2.0) < 1.5,
          QString("and centred on the window (%1 against %2)")
              .arg(middle, 0, 'f', 1).arg(w->width() / 2.0, 0, 'f', 1));
  auto home = itemNamed(w->contentItem(), "navBar_home");
  c.check(home, "Home is one of its destinations");
  // The indicator only measures anything on the destination you are on, so
  // the anatomy has to be read off that one.
  QString onKey;
  for (const auto *key : {"home", "search", "library"})
    if (auto d = itemNamed(w->contentItem(), QString("navBar_") + key))
      if (d->property("active").toBool()) onKey = key;
  c.check(!onKey.isEmpty(), "one destination is the one you are on");
  auto homePill = itemNamed(w->contentItem(), "navBarIndicator_" + onKey);
  auto homeLabel = itemNamed(w->contentItem(), "navBarLabel_" + onKey);
  // NavigationBarHorizontalItemTokens gives the horizontal item no indicator
  // width at all: 40dp tall, wrapping both pieces, with 16dp of leading and
  // trailing space. That wrapping indicator is what makes it read as a pill.
  c.check(homePill && qAbs(homePill->height() - 40) < 1,
          QString("the active indicator is 40dp tall (%1)")
              .arg(homePill ? homePill->height() : 0, 0, 'f', 0));
  if (homePill && homeLabel) {
    const double pillLeft = homePill->mapToScene(QPointF(0, 0)).x();
    const double pillRight = pillLeft + homePill->width();
    const double labelRight = homeLabel->mapToScene(QPointF(homeLabel->width(), 0)).x();
    c.check(qAbs(pillRight - labelRight - 16) < 1.5,
            QString("and wraps the label with 16dp after it (%1)")
                .arg(pillRight - labelRight, 0, 'f', 1));
  }
  // The window's own actions moved here with it.
  c.check(itemNamed(w->contentItem(), "miniPlayerButton"), "the mini player is a top bar action");
  c.check(itemNamed(w->contentItem(), "settingsButton"), "and so is settings");
  c.shot("03-navigation-capsule");

  // Secondary destinations appear only once there is room for them.
  const auto playlist = b->createPlaylist("Pinned mix");
  b->addItemsToPlaylist(playlist, b->queue()->rows);
  b->library("playlists");
  QTest::qWait(300);
  b->openPlaylist(playlist);
  QTest::qWait(300);
  b->togglePin(b->collectionItem());
  c.check(c.until([&] { return !b->pins().isEmpty(); }), "a collection is pinned");
  QTest::qWait(400);

  // With nothing playing, Home falls back to the first cover on its shelves.
  b->home();
  c.check(c.until([&] { return !b->busy(); }), "reload Home with a pinned shelf");
  // Pinning says "Pinned to Home", and Home is the one place it lands now that
  // no navigation surface carries a second copy of the same list. The shelf is
  // built for the page you are on, so this can only be asked once Home is it.
  const auto pinnedOnHome = [&] {
    for (const auto &section : b->homeSections())
      for (const auto &entry : section.toMap().value("items").toList())
        if (entry.toMap().value("title").toString() == "Pinned mix")
          return true;
    return false;
  };
  c.check(c.until(pinnedOnHome), "the pinned collection reaches Home's shelves");
  b->clearQueue();
  QTest::qWait(400);
  const auto shelfArt = w->property("homeArtwork").toString();
  c.check(b->current().value("art").toString().isEmpty() && !shelfArt.isEmpty(),
          "Home borrows a shelf cover once nothing is playing");
  c.check(c.until([&] { return art->property("source").toUrl() == QUrl(shelfArt); }),
          "the backdrop follows the borrowed shelf cover");
  c.check(backdrop->property("active").toBool(), "the backdrop returns with it");

  b->home();
  c.check(c.until([&] { return !b->busy(); }), "leave the playlist");
  c.shot("05-home-with-pin");

  // A compact window cannot centre the capsule and still clear the actions
  // either side of it, so the bar gives up the capsule and spans the column,
  // which is the width Material gives a navigation bar in the first place.
  w->resize(520, 800);
  QTest::qWait(600);
  c.check(c.until([&] { return !bar->property("hugsContent").toBool(); }),
          "a compact window stands the capsule down");
  c.check(bar->width() > w->width() * 0.85,
          QString("and spans the column instead (%1 of %2)")
              .arg(bar->width(), 0, 'f', 0).arg(w->width()));
  for (const auto *key : {"home", "search", "library"})
    c.check(itemNamed(w->contentItem(), QString("navBar_") + key),
            QString("%1 survives the narrow window").arg(key));
  c.shot("06-navigation-narrow");
  w->resize(1280, 860);
  QTest::qWait(600);
  c.check(c.until([&] { return bar->property("hugsContent").toBool(); }),
          "widening brings the capsule back");
  b->stop();
  b->clearQueue();
  c.finish();
}

void runOnboardingTests(Backend *b, QQuickWindow *w) {
  Harness c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1180, 820);
  QTest::qWait(400);
  b->setTheme("dark");
  b->setMotion(false);
  b->setAutoplay(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);

  auto dialog = w->findChild<QObject *>("onboarding");
  c.check(dialog, "the first-run flow exists");
  if (!dialog)
    return c.finish();
  c.check(b->onboarded(), "an isolated session is never asked to set up");
  c.check(!dialog->property("visible").toBool(), "so the flow stays closed");

  b->setOnboarded(false);
  QMetaObject::invokeMethod(dialog, "open");
  c.check(c.until([&] { return dialog->property("visible").toBool(); }), "a first run opens the flow");
  c.check(w->property("modalOpen").toBool(), "the flow blocks the shortcuts behind it");
  c.check(dialog->property("step").toInt() == 0, "it starts at the first step");
  auto progress = itemNamed(w->contentItem(), "onboardingProgress");
  c.check(progress && progress->property("text") == "Step 1 of 3", "the step is stated plainly");
  c.check(shownItem(w->contentItem(), "onboardingAppearance"), "step one is appearance");
  c.check(!shownItem(w->contentItem(), "onboardingMusic"), "later steps are not shown yet");

  // Step one really changes the theme and the source color.
  c.click("onboardTheme_light");
  c.check(b->theme() == "light", "the theme control applies immediately");
  c.click("onboardTheme_dark");
  c.check(b->theme() == "dark", "and can be changed again");
  c.check(b->accentColor().isEmpty(), "no accent is chosen yet");
  c.click("accentSeed_386a20");
  c.check(b->accentColor() == "#386a20", "the accent picker works inside the flow");
  c.check(c.evaluate("Theme.useAccent").toBool(), "the chosen accent themes the app behind the flow");
  c.shot("01-onboarding-appearance");

  c.click("onboardingBack");
  c.check(dialog->property("step").toInt() == 0, "Back on the first step goes nowhere");
  c.click("onboardingNext");
  c.check(dialog->property("step").toInt() == 1, "Next moves on");
  c.check(shownItem(w->contentItem(), "onboardingMusic"), "step two is music");
  c.check(progress && progress->property("text") == "Step 2 of 3", "the step count follows");

  // Step two imports for real, and reports a bad path instead of failing quietly.
  QProcess encode;
  encode.start("ffmpeg", {"-nostdin", "-v", "error", "-f", "lavfi", "-i", "anullsrc=r=8000:cl=mono",
                          "-t", "30", "-metadata", "title=Onboarding fixture", "-metadata", "artist=Example Artist",
                          c.directory + "/music/01.flac"});
  c.check(encode.waitForFinished(15000) && encode.exitCode() == 0, "generate onboarding fixture");
  // Browse is a real action standing beside a more important one, which is the
  // emphasis Material's outlined button carries.
  if (auto browse = shownItem(w->contentItem(), "onboardingBrowse"))
    c.check(browse->property("outlined").toBool(),
            "browsing is offered at the medium emphasis beside Add folder");
  auto path = itemNamed(w->contentItem(), "onboardingFolderPath");
  c.check(path, "the folder field is present");
  if (path)
    path->setProperty("text", c.directory + "/does-not-exist");
  c.click("onboardingAddFolder");
  auto error = itemNamed(w->contentItem(), "onboardingFolderError");
  c.check(error && error->isVisible() && !error->property("text").toString().isEmpty(),
          "an unusable path is reported in place");
  c.check(b->musicFolders().isEmpty(), "and nothing is imported");
  c.shot("02-onboarding-error");
  if (path)
    path->setProperty("text", c.directory + "/music");
  c.check(error && !error->isVisible(), "editing the path clears the error");
  c.click("onboardingAddFolder");
  c.check(c.until([&] { return !b->importingLocal(); }), "the import finishes");
  c.check(b->musicFolders().contains(c.directory + "/music"), "the folder is really added");
  c.check(itemNamed(w->contentItem(), "onboardingFolderDone")->isVisible(), "the flow confirms it");
  c.shot("03-onboarding-music");

  c.click("onboardingNext");
  c.check(dialog->property("step").toInt() == 2, "the last step opens");
  c.check(shownItem(w->contentItem(), "onboardingStart"), "step three is the start page");
  c.check(!shownItem(w->contentItem(), "onboardingSkip"), "the last step has nothing left to skip");
  c.click("onboardStart_files");
  c.check(b->startPage() == "files", "the start page is set from the flow");
  c.shot("04-onboarding-start");
  c.click("onboardingBack");
  c.check(dialog->property("step").toInt() == 1 && b->startPage() == "files",
          "going back keeps what was already chosen");
  c.click("onboardingNext");

  c.click("onboardingNext");
  c.check(c.until([&] { return !dialog->property("visible").toBool(); }), "finishing closes the flow");
  c.check(b->onboarded(), "and records that setup is done");
  c.check(!w->property("modalOpen").toBool(), "the app is usable again");

  // It never returns on its own, and skipping is a complete answer too.
  QMetaObject::invokeMethod(w, "Component.onCompleted");
  QTest::qWait(300);
  c.check(!dialog->property("visible").toBool(), "a completed setup does not come back");
  b->setOnboarded(false);
  QMetaObject::invokeMethod(dialog, "open");
  c.check(c.until([&] { return dialog->property("visible").toBool(); }), "the flow can be reopened");
  c.check(dialog->property("step").toInt() == 0, "reopening starts from the beginning");
  c.click("onboardingSkip");
  c.check(c.until([&] { return !dialog->property("visible").toBool(); }), "Skip closes the flow");
  c.check(b->onboarded(), "skipping still counts as set up");
  c.check(b->musicFolders().contains(c.directory + "/music"), "skipping keeps what was already added");
  b->stop();
  c.finish();
}

void runLibraryExchangeTests(Backend *b, QQuickWindow *w) {
  Harness c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1180, 820);
  QTest::qWait(400);
  b->setTheme("dark");
  b->setMotion(false);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);
  b->setLyricsFallback(false);

  struct Fixture { const char *file; const char *title; const char *album; const char *year; int seconds; };
  const QList<Fixture> fixtures{{"01", "Short opener", "Early record", "1998", 90},
                                {"02", "Long closer", "Early record", "1998", 420},
                                {"03", "Later single", "Late record", "2016", 200}};
  for (const auto &fixture : fixtures) {
    QProcess encode;
    encode.start("ffmpeg", {"-nostdin", "-v", "error", "-f", "lavfi", "-i", "anullsrc=r=8000:cl=mono",
                            "-t", QString::number(fixture.seconds), "-metadata", QString("title=") + fixture.title,
                            "-metadata", QString("album=") + fixture.album, "-metadata", QString("date=") + fixture.year,
                            "-metadata", "artist=Example Artist",
                            c.directory + "/music/" + fixture.file + ".flac"});
    c.check(encode.waitForFinished(20000) && encode.exitCode() == 0, QString("generate ") + fixture.title);
  }
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 20000), "import exchange fixture");
  b->library("files");
  QTest::qWait(300);
  c.check(b->results()->count() == 3, "three songs are saved");
  auto trackNamed = [&](const QString &title) {
    for (const auto &row : b->results()->rows)
      if (row.toMap().value("title") == title)
        return row.toMap();
    return QVariantMap();
  };
  const auto opener = trackNamed("Short opener"), closer = trackNamed("Long closer"), single = trackNamed("Later single");
  c.check(!opener.isEmpty() && !closer.isEmpty() && !single.isEmpty(), "fixture songs are identifiable");
  if (opener.isEmpty() || closer.isEmpty() || single.isEmpty())
    return c.finish();
  c.check(opener.value("year") == "1998" && single.value("year") == "2016", "import reads the year tag");
  c.check(opener.value("album") == "Early record", "import reads the album tag");

  // --- Smart playlist rules ---
  const auto byAlbum = b->saveSmartPlaylist({}, "Early only", {{"album", "Early"}});
  c.check(!byAlbum.isEmpty(), "album rule saves");
  b->openPlaylist(byAlbum);
  QTest::qWait(200);
  c.check(b->results()->count() == 2, "an album rule keeps only that album");
  for (const auto &row : b->results()->rows)
    c.check(row.toMap().value("album") == "Early record", "every match is from the named album");

  const auto byYear = b->saveSmartPlaylist({}, "This decade", {{"yearFrom", 2000}});
  b->openPlaylist(byYear);
  QTest::qWait(200);
  c.check(b->results()->count() == 1 && b->results()->get(0).value("title") == "Later single",
          "a year floor keeps only newer songs");
  b->saveSmartPlaylist(byYear, "This decade", {{"yearFrom", 1990}, {"yearTo", 1999}});
  b->openPlaylist(byYear);
  QTest::qWait(200);
  c.check(b->results()->count() == 2, "a year range keeps only songs inside it");
  b->saveSmartPlaylist(byYear, "This decade", {{"yearFrom", 2100}});
  b->openPlaylist(byYear);
  QTest::qWait(200);
  c.check(b->results()->count() == 0, "a year no song reaches matches nothing");
  // A reversed range is a mistake, not a rule that silently excludes everything.
  b->saveSmartPlaylist(byYear, "This decade", {{"yearFrom", 2016}, {"yearTo", 1990}});
  c.check(b->smartPlaylist(byYear).value("rules").toMap().value("yearTo").toInt() == 0,
          "a range that ends before it starts drops its upper bound");
  b->openPlaylist(byYear);
  QTest::qWait(200);
  c.check(b->results()->count() == 1, "and still matches from the lower bound");

  const auto byLength = b->saveSmartPlaylist({}, "Long ones", {{"minSeconds", 300}});
  b->openPlaylist(byLength);
  QTest::qWait(200);
  c.check(b->results()->count() == 1 && b->results()->get(0).value("title") == "Long closer",
          "a length floor keeps only longer songs");
  b->saveSmartPlaylist(byLength, "Long ones", {{"maxSeconds", 120}});
  b->openPlaylist(byLength);
  QTest::qWait(200);
  c.check(b->results()->count() == 1 && b->results()->get(0).value("title") == "Short opener",
          "a length ceiling keeps only shorter songs");
  b->saveSmartPlaylist(byLength, "Long ones", {{"album", "Early"}, {"minSeconds", 300}});
  b->openPlaylist(byLength);
  QTest::qWait(200);
  c.check(b->results()->count() == 1 && b->results()->get(0).value("title") == "Long closer",
          "rules combine rather than replace each other");

  // A smart playlist is only measured when it is opened, so the backend
  // reports its size as -1. The playlist grid printed that as "-1 tracks".
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("playlists")));
  c.check(c.until([&] { return b->libraryId() == "playlists"; }), "the playlists page opens");
  QTest::qWait(400);
  // The cards are the grid layout of this page; the list layout names a
  // smart playlist in its own row already.
  const auto listLayout = b->viewMode();
  b->setViewMode("grid");
  QStringList subtitles;
  const std::function<void(QQuickItem *)> collect = [&](QQuickItem *item) {
    if (!item->isVisible()) return;
    if (item->objectName() == "cardSubtitle") subtitles << item->property("sourceText").toString();
    for (auto child : item->childItems()) collect(child);
  };
  c.check(c.until([&] { subtitles.clear(); collect(w->contentItem()); return subtitles.size() >= 2; }),
          "the playlists show as cards");
  c.check(!subtitles.join("|").contains("-1"),
          QString("no card prints an unknown count (%1)").arg(subtitles.join(" | ")));
  c.check(subtitles.contains("Smart playlist"), "and a smart playlist says what it is instead");
  c.shot("02-playlist-cards");
  b->setViewMode(listLayout);

  // The dialog has to round-trip everything it can save.
  auto dialog = w->findChild<QObject *>("smartPlaylistDialog");
  c.check(dialog, "the smart playlist dialog exists");
  if (dialog) {
    QMetaObject::invokeMethod(dialog, "edit", Q_ARG(QVariant, QVariant(byLength)));
    QTest::qWait(400);
    auto field = [&](const QString &name) { return itemNamed(w->contentItem(), name); };
    c.check(field("smartAlbum") && field("smartAlbum")->property("text") == "Early", "the album rule loads back");
    c.check(field("smartMinutesFrom") && field("smartMinutesFrom")->property("text") == "5",
            "seconds are shown to the reader as minutes");
    c.check(field("smartMinutesTo") && field("smartMinutesTo")->property("text").toString().isEmpty(),
            "an unset bound stays empty rather than showing zero");
    c.shot("01-smart-rules");
    field("smartYearFrom")->setProperty("text", "1990");
    field("smartYearTo")->setProperty("text", "1999");
    QMetaObject::invokeMethod(dialog, "accept");
    QTest::qWait(400);
    const auto saved = b->smartPlaylist(byLength).value("rules").toMap();
    c.check(saved.value("yearFrom").toInt() == 1990 && saved.value("yearTo").toInt() == 1999,
            "the dialog saves what was typed into it");
    c.check(saved.value("minSeconds").toInt() == 300, "and keeps the rules it did not touch");
  }

  // --- M3U export and import ---
  const auto mixed = b->createPlaylist("Travelling mix");
  b->addItemsToPlaylist(mixed, {opener, single, QVariantMap{{"id", "0_M2JX-Olv8"}, {"videoId", "0_M2JX-Olv8"},
                                                            {"kind", "song"}, {"title", "Streamed"}, {"artist", "Nobody"}}});
  QTest::qWait(200);
  const auto target = c.directory + "/travelling.m3u8";
  b->exportPlaylistM3u(mixed, QUrl::fromLocalFile(target));
  QFile written(target);
  c.check(written.exists(), "the playlist file is written");
  c.check(written.open(QIODevice::ReadOnly), "the playlist file can be read back");
  const auto text = QString::fromUtf8(written.readAll());
  written.close();
  c.check(text.startsWith("#EXTM3U"), "it is an extended M3U");
  c.check(text.contains("#PLAYLIST:Travelling mix"), "it carries the playlist name");
  c.check(text.contains(opener.value("localPath").toString()) && text.contains(single.value("localPath").toString()),
          "it lists the local files by absolute path");
  c.check(!text.contains("0_M2JX-Olv8") && !text.contains("Streamed"),
          "streamed songs are left out rather than written as unusable entries");
  c.check(text.contains("#EXTINF:90,Example Artist - Short opener"), "each entry carries its length and name");

  // Importing it back rebuilds the playlist from the files on disk.
  const int before = b->playlists().size();
  b->importPlaylistM3u(QUrl::fromLocalFile(target));
  c.check(c.until([&] { return b->playlists().size() == before + 1; }, 20000), "importing adds one playlist");
  // The import opens what it created, so its contents are read from the view.
  c.check(c.until([&] { return b->libraryId() != mixed && b->results()->count() == 2; }, 8000),
          "the imported playlist opens with its songs");
  c.check(b->title() == "Travelling mix", "the imported playlist takes the name from the file");
  c.check(b->results()->count() == 2, "it holds only the songs the file could name");
  c.check(b->results()->get(0).value("title") == "Short opener" && b->results()->get(1).value("title") == "Later single",
          "and keeps the order the file listed");
  c.shot("02-imported-playlist");

  // Relative paths are the common case for playlists that travel with music.
  QFile relative(c.directory + "/music/relative.m3u");
  c.check(relative.open(QIODevice::WriteOnly), "write a relative playlist");
  relative.write("#EXTM3U\n#PLAYLIST:Relative mix\n03.flac\n01.flac\n");
  relative.close();
  const int beforeRelative = b->playlists().size();
  b->importPlaylistM3u(QUrl::fromLocalFile(c.directory + "/music/relative.m3u"));
  c.check(c.until([&] { return b->playlists().size() == beforeRelative + 1; }, 20000), "the relative playlist imports");
  c.check(c.until([&] { return b->title() == "Relative mix" && b->results()->count() == 2; }, 8000),
          "it is named from its #PLAYLIST line and opens with its songs");
  c.check(b->results()->get(0).value("title") == "Later single" && b->results()->get(1).value("title") == "Short opener",
          "relative entries resolve against the file's own folder, in order");

  // Files that name nothing usable say so instead of leaving an empty playlist.
  QFile remote(c.directory + "/remote.m3u");
  c.check(remote.open(QIODevice::WriteOnly), "write a remote-only playlist");
  remote.write("#EXTM3U\nhttps://example.com/stream.mp3\n/nowhere/missing.flac\n");
  remote.close();
  const int beforeRemote = b->playlists().size();
  b->dismissError();
  b->importPlaylistM3u(QUrl::fromLocalFile(c.directory + "/remote.m3u"));
  QTest::qWait(500);
  c.check(b->playlists().size() == beforeRemote, "a playlist with no local files creates nothing");
  c.check(!b->error().isEmpty(), "and says why");
  b->dismissError();
  b->importPlaylistM3u(QUrl::fromLocalFile(c.directory + "/not-here.m3u8"));
  QTest::qWait(300);
  c.check(!b->error().isEmpty() && b->playlists().size() == beforeRemote, "a missing file is reported too");
  b->dismissError();

  // Exporting a playlist with nothing local is refused rather than written empty.
  const auto streamOnly = b->createPlaylist("Streams only");
  b->addItemsToPlaylist(streamOnly, {QVariantMap{{"id", "0_M2JX-Olv8"}, {"videoId", "0_M2JX-Olv8"},
                                                 {"kind", "song"}, {"title", "Streamed"}}});
  b->exportPlaylistM3u(streamOnly, QUrl::fromLocalFile(c.directory + "/streams.m3u8"));
  QTest::qWait(300);
  c.check(!QFile::exists(c.directory + "/streams.m3u8") && !b->error().isEmpty(),
          "a playlist with no local files is not exported");
  b->dismissError();

  // A smart playlist exports the songs its rules currently match.
  b->exportPlaylistM3u(byAlbum, QUrl::fromLocalFile(c.directory + "/smart.m3u8"));
  QFile smart(c.directory + "/smart.m3u8");
  c.check(smart.exists() && smart.open(QIODevice::ReadOnly), "a smart playlist exports its matches");
  if (smart.isOpen()) {
    const auto smartText = QString::fromUtf8(smart.readAll());
    smart.close();
    c.check(smartText.contains(opener.value("localPath").toString()) && !smartText.contains(single.value("localPath").toString()),
            "and only those matches");
  }
  b->stop();
  b->clearQueue();
  c.finish();
}

void runBackdropPulseTests(Backend *b, QQuickWindow *w) {
  Harness c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1180, 820);
  QTest::qWait(400);
  b->setTheme("dark");
  b->setMotion(true);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);
  b->setLyricsFallback(false);

  QImage cover(480, 480, QImage::Format_RGB32);
  QPainter paint(&cover);
  paint.fillRect(cover.rect(), QColor("#31415e"));
  paint.fillRect(0, 0, 480, 190, QColor("#c2553a"));
  paint.end();
  cover.save(c.directory + "/music/cover.png");
  // A tone, not silence: the meters have to see something to react to.
  QProcess encode;
  encode.start("ffmpeg", {"-nostdin", "-v", "error", "-f", "lavfi", "-i", "sine=frequency=110:duration=90",
                          "-metadata", "title=Pulse fixture", "-metadata", "artist=Example Artist",
                          c.directory + "/music/01.flac"});
  c.check(encode.waitForFinished(20000) && encode.exitCode() == 0, "generate a tone fixture");
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }), "import pulse fixture");
  b->library("files");
  b->enqueueItems(b->results()->rows);

  w->setProperty("immersive", true);
  QTest::qWait(500);
  auto player = shownItem(w->contentItem(), "immersivePlayer");
  c.check(player, "immersive player loads");
  if (!player)
    return c.finish();
  auto backdrop = itemNamed(player, "ambientBackdrop");
  auto art = backdrop ? itemNamed(backdrop, "ambientArt") : nullptr;
  c.check(backdrop && art, "the immersive backdrop is present");
  if (!backdrop || !art)
    return c.finish();

  c.check(b->backdropPulse(), "the backdrop follows the music by default");
  c.check(!backdrop->property("reactive").toBool(), "a stopped player gives it nothing to follow");
  const auto resting = art->property("scale").toReal();
  c.check(resting > 1, "the drifting cover rests oversized so it can move");

  b->playAt(0);
  c.check(c.until([&] { return b->playing(); }), "the tone plays");
  c.check(c.until([&] { return backdrop->property("reactive").toBool(); }), "playback makes the backdrop reactive");
  // The meters are real decoded audio, so a tone has to move them off zero.
  c.check(c.until([&] { return backdrop->property("level").toReal() > 0.05; }, 10000),
          "decoded audio raises the level the backdrop reads");
  c.check(c.until([&] { return backdrop->property("pulse").toReal() > 0.05; }, 6000),
          "the pulse follows that level");
  c.check(c.until([&] { return art->property("scale").toReal() > resting; }, 6000),
          "and the cover swells beyond its resting size");
  const auto swollen = art->property("scale").toReal();
  // Subtlety is the point: this is a wash, not a visualiser.
  c.check(swollen < resting * 1.2, "the swell stays gentle rather than pumping");
  c.shot("01-pulse-playing");

  b->pause();
  c.check(c.until([&] { return !backdrop->property("reactive").toBool(); }), "pausing stops the reaction");
  c.check(c.until([&] { return backdrop->property("pulse").toReal() < 0.01; }, 4000), "and the pulse settles back");
  b->play();
  c.check(c.until([&] { return backdrop->property("pulse").toReal() > 0.05; }, 10000), "resuming picks it back up");

  b->setBackdropPulse(false);
  QTest::qWait(400);
  c.check(!backdrop->property("reactive").toBool() && backdrop->property("pulse").toReal() < 0.01,
          "the setting turns the reaction off while playback continues");
  c.check(b->playing(), "and never interferes with playback");
  c.check(backdrop->property("animating").toBool(), "the timed drift carries on without it");
  b->setBackdropPulse(true);
  c.check(c.until([&] { return backdrop->property("pulse").toReal() > 0.05; }, 10000), "turning it back on resumes");

  b->setMotion(false);
  // A3-11: a disabled Behavior must snap its last audio level change in the
  // first event frame. Waiting 400ms alone would also pass the old 220ms fade.
  QCoreApplication::processEvents();
  c.check(backdrop->property("level").toReal() < 0.01 &&
              backdrop->property("pulse").toReal() < 0.01,
          "reduced motion settles the pulse in one frame");
  QTest::qWait(400);
  c.check(!backdrop->property("reactive").toBool() && backdrop->property("pulse").toReal() < 0.01,
          "reduced motion stops the reaction as well as the drift");
  c.check(qFuzzyCompare(art->property("scale").toReal(), 1.08), "and the cover returns to its resting size");
  b->setMotion(true);

  // The rounded backdrops stay out of it, because swelling would square their corners.
  w->setProperty("immersive", false);
  QTest::qWait(300);
  w->setProperty("side", "now");
  QTest::qWait(600);
  auto panel = itemNamed(w->contentItem(), "nowBackdrop");
  c.check(panel, "the Now playing backdrop is present");
  if (panel) {
    c.check(!panel->property("drifts").toBool() && !panel->property("reactive").toBool(),
            "a rounded backdrop never reacts");
    auto panelArt = itemNamed(panel, "ambientArt");
    c.check(panelArt && qFuzzyCompare(panelArt->property("scale").toReal(), 1.0),
            "so its cover stays exactly inside its rounded corners");
    c.shot("02-panel-still");
  }
  w->setProperty("side", "");
  b->stop();
  b->clearQueue();
  c.finish();
}

void runPlaybackMemoryTests(Backend *b, QQuickWindow *w) {
  Harness c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1180, 820);
  QTest::qWait(400);
  b->setTheme("dark");
  b->setMotion(false);
  b->setVolume(0.5);
  b->setAutoplay(false);
  b->setShuffle(false);
  b->setRepeat(0);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);
  b->setLyricsFallback(false);
  b->setSleepFade(false);

  // One recording long enough to be worth returning to, one that is not.
  const QList<QPair<QString, int>> fixtures{{"long", 1800}, {"short", 120}, {"tail", 150}};
  for (const auto &fixture : fixtures) {
    QProcess encode;
    encode.start("ffmpeg", {"-nostdin", "-v", "error", "-f", "lavfi", "-i", "anullsrc=r=8000:cl=mono",
                            "-t", QString::number(fixture.second), "-metadata", "title=" + fixture.first,
                            "-metadata", "artist=Example Artist",
                            c.directory + "/music/" + fixture.first + ".flac"});
    c.check(encode.waitForFinished(30000) && encode.exitCode() == 0, "generate " + fixture.first);
  }
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 30000), "import playback fixture");
  b->library("files");
  QTest::qWait(300);
  auto trackNamed = [&](const QString &title) {
    for (const auto &row : b->results()->rows)
      if (row.toMap().value("title") == title)
        return row.toMap();
    return QVariantMap();
  };
  const auto longer = trackNamed("long"), shorter = trackNamed("short"), tail = trackNamed("tail");
  c.check(!longer.isEmpty() && !shorter.isEmpty() && !tail.isEmpty(), "fixtures are identifiable");
  if (longer.isEmpty() || shorter.isEmpty() || tail.isEmpty())
    return c.finish();
  c.check(longer.value("seconds").toInt() >= 1200 && shorter.value("seconds").toInt() < 1200,
          "one fixture is a long recording and one is not");
  const auto longId = longer.value("id").toString(), shortId = shorter.value("id").toString();

  // --- Resuming long recordings ---
  c.check(b->resumeLongTracks(), "resuming long recordings is on by default");
  b->enqueueItems({longer, shorter, tail});
  b->playAt(0);
  c.check(c.until([&] { return b->playing(); }), "the long recording plays");
  c.check(b->resumePosition(longId) == 0, "nothing is remembered before it has been listened to");
  b->seek(600000);
  c.check(c.until([&] { return b->position() >= 595000; }, 8000), "seek ten minutes in");
  b->pause();
  QTest::qWait(200);
  const auto remembered = b->resumePosition(longId);
  c.check(remembered >= 595000 && remembered <= 615000, "pausing remembers where the recording got to");
  b->playAt(1);
  c.check(c.until([&] { return b->current().value("title") == "short"; }), "a different track plays");
  c.check(b->resumePosition(shortId) == 0, "a short track is never remembered");
  b->seek(60000);
  QTest::qWait(200);
  b->pause();
  QTest::qWait(200);
  c.check(b->resumePosition(shortId) == 0, "even after listening to most of it");

  b->playAt(0);
  c.check(c.until([&] { return b->playing() && b->position() >= 595000; }, 20000),
          "returning to the recording resumes where it was left");
  c.shot("01-resumed");

  // The beginning and the very end are not worth returning to.
  b->seek(5000);
  QTest::qWait(200);
  b->pause();
  QTest::qWait(200);
  c.check(b->resumePosition(longId) == 0, "a position near the start forgets the mark instead of saving it");
  b->play();
  c.check(c.until([&] { return b->playing(); }), "playback resumes");
  b->seek(700000);
  c.check(c.until([&] { return b->position() >= 695000; }, 8000), "seek further in again");
  b->pause();
  QTest::qWait(200);
  c.check(b->resumePosition(longId) > 0, "the mark comes back");
  b->setResumeLongTracks(false);
  b->play();
  c.check(c.until([&] { return b->playing(); }), "playback resumes with the setting off");
  b->seek(900000);
  c.check(c.until([&] { return b->position() >= 895000; }, 8000), "seek somewhere new");
  b->pause();
  QTest::qWait(200);
  c.check(b->resumePosition(longId) <= 700001, "the setting stops new marks being written");
  b->setResumeLongTracks(true);

  // --- Number keys seek across the track ---
  b->playAt(0);
  c.check(c.until([&] { return b->playing(); }), "playback for the seek shortcuts");
  // The digit shortcuts have the same reach as Space: the browsing surface,
  // which is what the app focuses after a search or a click on the background.
  c.evaluate("content.forceActiveFocus()");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  QTest::qWait(150);
  b->seek(0);
  c.check(c.until([&] { return b->position() < 2000; }, 5000), "start the shortcuts from the beginning");
  QTest::keyClick(w, Qt::Key_5);
  c.check(c.until([&] { return qAbs(b->position() - b->duration() / 2) < 2000; }, 5000),
          "5 jumps to the middle of the track");
  QTest::keyClick(w, Qt::Key_2);
  c.check(c.until([&] { return qAbs(b->position() - b->duration() / 5) < 2000; }, 5000),
          "2 jumps back to a fifth in");
  QTest::keyClick(w, Qt::Key_9);
  c.check(c.until([&] { return qAbs(b->position() - b->duration() * 9 / 10) < 2000; }, 5000),
          "9 jumps nine tenths in");
  QTest::keyClick(w, Qt::Key_0);
  c.check(c.until([&] { return b->position() < 2000; }, 5000), "0 returns to the start");
  auto hud = itemNamed(w->contentItem(), "playbackHud");
  c.check(hud && hud->isVisible(), "the jump shows the same feedback as the other seek shortcuts");
  c.shot("02-digit-seek");
  // Typing a digit into a field is typing, not seeking.
  const auto before = b->position();
  QMetaObject::invokeMethod(w, "focusSearch");
  QTest::qWait(250);
  QTest::keyClick(w, Qt::Key_3);
  QTest::qWait(250);
  c.check(b->position() >= before - 1000, "a digit typed into search never seeks");
  auto search = itemNamed(w->contentItem(), "searchField");
  c.check(search && search->property("text").toString().contains("3"), "it reaches the field instead");
  if (search)
    search->setProperty("text", "");
  QTest::keyClick(w, Qt::Key_Escape);
  c.evaluate("content.forceActiveFocus()");
  QTest::qWait(200);

  // --- Scroll on the seek bar ---
  auto seek = itemNamed(w->contentItem(), "seekBar");
  c.check(seek, "the seek bar is present");
  if (seek) {
    b->seek(600000);
    c.check(c.until([&] { return b->position() >= 595000; }, 5000), "park the position for the wheel");
    const auto parked = b->position();
    const auto over = seek->mapToScene(seek->boundingRect().center()).toPoint();
    QWheelEvent forward(QPointF(over), w->mapToGlobal(over), {}, QPoint(0, 120), Qt::NoButton,
                        Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(w, &forward);
    c.check(c.until([&] { return b->position() > parked + 3000; }, 5000), "a wheel notch seeks forward");
    const auto advanced = b->position();
    QWheelEvent back(QPointF(over), w->mapToGlobal(over), {}, QPoint(0, -120), Qt::NoButton,
                     Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(w, &back);
    c.check(c.until([&] { return b->position() < advanced - 3000; }, 5000), "and the other way seeks back");
    // The bar still follows playback: the wheel must not replace its binding.
    b->seek(300000);
    c.check(c.until([&] { return qAbs(seek->property("value").toReal() - b->position()) < 2000; }, 5000),
            "the bar keeps following playback after a wheel seek");
  }

  // --- Per-track volume trim ---
  b->setVolumeNormalization(false);
  b->playAt(0);
  c.check(c.until([&] { return b->playing(); }), "playback for the trim checks");
  c.check(qFuzzyIsNull(b->trackTrim(longId)), "a track starts with no trim");
  c.check(qAbs(b->effectiveVolume() - 0.5) < 0.001, "and plays at the chosen volume");
  b->setTrackTrim(longId, -6);
  QTest::qWait(150);
  c.check(qAbs(b->trackTrim(longId) + 6) < 0.01, "the trim is stored");
  c.check(qAbs(b->trackTrimDb() + 6) < 0.01, "and reported for the playing track");
  c.check(qAbs(b->effectiveVolume() - 0.5 * std::pow(10.0, -6.0 / 20.0)) < 0.002,
          "the output level drops by the trim even with levelling off");
  c.check(qAbs(b->volume() - 0.5) < 0.001, "the volume the listener chose is untouched");
  b->playAt(1);
  c.check(c.until([&] { return b->current().value("title") == "short"; }), "another track plays");
  QTest::qWait(150);
  c.check(qFuzzyIsNull(b->trackTrimDb()) && qAbs(b->effectiveVolume() - 0.5) < 0.001,
          "the trim belongs to its own track only");
  b->playAt(0);
  c.check(c.until([&] { return b->current().value("id") == longId; }), "back to the trimmed track");
  QTest::qWait(150);
  c.check(qAbs(b->trackTrimDb() + 6) < 0.01, "the trim is still there");
  b->setTrackTrim(longId, -40);
  c.check(qAbs(b->trackTrim(longId) + 12) < 0.01, "an extreme trim is clamped to a usable range");
  bool listed = false;
  for (const auto &row : b->trackDetails(b->current()))
    if (row.toMap().value("label") == "Volume trim")
      listed = true;
  c.check(listed, "Track details reports the trim");
  b->setTrackTrim(longId, 0);
  c.check(qFuzzyIsNull(b->trackTrim(longId)) && qAbs(b->effectiveVolume() - 0.5) < 0.001,
          "resetting the trim restores the plain volume");

  auto dialog = w->findChild<QObject *>("trimDialog");
  c.check(dialog, "the trim dialog exists");
  if (dialog) {
    QMetaObject::invokeMethod(dialog, "adjust", Q_ARG(QVariant, QVariant(longer)));
    QTest::qWait(400);
    c.check(dialog->property("visible").toBool(), "it opens for the chosen track");
    auto slider = itemNamed(w->contentItem(), "trimSlider");
    c.check(slider && qFuzzyIsNull(slider->property("value").toReal()), "it opens on the stored value");
    if (slider) {
      slider->setProperty("value", 4.5);
      QMetaObject::invokeMethod(slider, "moved");
      QTest::qWait(200);
      c.check(qAbs(b->trackTrim(longId) - 4.5) < 0.01, "dragging the slider applies the trim");
      auto label = itemNamed(w->contentItem(), "trimValue");
      c.check(label && label->property("text") == "+4.5 dB", "and the dialog says so");
    }
    c.shot("03-trim");
    c.click("trimReset");
    c.check(qFuzzyIsNull(b->trackTrim(longId)), "Reset clears it from the dialog");
    QMetaObject::invokeMethod(dialog, "close");
    QTest::qWait(250);
  }

  // --- Sleep timer at the end of the queue ---
  b->clearQueue();
  b->enqueueItems({shorter, tail});
  b->setSleep(0);
  c.check(b->sleepLabel() == "Off", "the sleep timer starts off");
  b->setSleep(-2);
  c.check(b->sleepLabel() == "End of queue", "it can be set to the end of the queue");
  b->playAt(0);
  c.check(c.until([&] { return b->playing(); }), "the queue plays");
  c.check(b->sleepLabel() == "End of queue", "moving through the queue keeps the timer set");
  b->playAt(1);
  c.check(c.until([&] { return b->playing() && b->duration() > 4000; }, 20000),
          "the last track is loaded and playing");
  c.check(b->current().value("title") == "tail", "it is the last track in the queue");
  c.check(b->sleepLabel() == "End of queue", "the timer survives reaching the last track");
  b->seek(b->duration() - 2500);
  c.check(c.until([&] { return !b->playing(); }, 25000), "playback stops when the queue runs out");
  c.check(b->sleepLabel() == "Off", "and the timer clears itself");
  c.check(b->currentIndex() == 1, "it stops rather than wrapping or autoplaying");
  c.shot("04-sleep-stopped");

  // The option only makes sense where a queue can actually end.
  auto menuItem = w->findChild<QObject *>("sleepQueueEnd");
  c.check(menuItem, "the sleep menu offers it");
  if (menuItem) {
    b->setShuffle(false);
    b->setRepeat(0);
    QTest::qWait(100);
    c.check(menuItem->property("enabled").toBool(), "it is offered for a queue that ends");
    b->setShuffle(true);
    QTest::qWait(100);
    c.check(!menuItem->property("enabled").toBool(), "shuffle has no last track, so it is not offered");
    b->setShuffle(false);
    b->setRepeat(1);
    QTest::qWait(100);
    c.check(!menuItem->property("enabled").toBool(), "nor does a repeating queue");
    b->setRepeat(0);
  }
  b->stop();
  b->clearQueue();
  c.finish();
}

namespace {
// The first piece of text a row actually shows, which is the thing a reader
// scans down the list.
QQuickItem *rowLabel(QQuickItem *root) {
  if (!root->isVisible())
    return nullptr;
  if (root->objectName() == "sungText" && !root->property("text").toString().isEmpty())
    return root;
  for (auto child : root->childItems())
    if (auto found = rowLabel(child))
      return found;
  return nullptr;
}
QList<QQuickItem *> visibleRows(QQuickItem *group) {
  QList<QQuickItem *> rows;
  if (!group)
    return rows;
  for (auto child : group->childItems())
    if (child->isVisible() && child->height() > 0)
      rows.append(child);
  return rows;
}
} // namespace

void runInterfaceAuditTests(Backend *b, QQuickWindow *w) {
  Harness c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory);
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1280, 850);
  QTest::qWait(400);
  b->setMotion(false);
  b->setAutoplay(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);

  // A server account that has not been entered is an empty page, not a
  // request failure. Test the dialog with an actual failed local connection.
  w->resize(600, 800);
  b->browseServer();
  c.check(c.until([&] { return !b->busy(); }), "the disconnected server page settles");
  c.check(shownItem(w->contentItem(), "serverEmptyState"),
          "the unconfigured server page explains how to connect");
  c.check(!shownItem(w->contentItem(), "errorBar"),
          "an unconfigured account does not raise an error bar");
  c.shot("00-server-empty");
  c.click("serverEmptyConnect");
  auto connection = w->findChild<QObject *>("serverConnectionDialog");
  c.check(c.until([&] { return connection && connection->property("visible").toBool(); }),
          "the empty state's Connect button opens the connection dialog");
  QQuickItem *address = nullptr, *username = nullptr, *password = nullptr;
  c.check(c.until([&] {
    if (!connection || !connection->property("visible").toBool()) return false;
    address = connection->findChild<QQuickItem *>("serverAddress");
    username = connection->findChild<QQuickItem *>("serverUsername");
    password = connection->findChild<QQuickItem *>("serverPassword");
    return address && username && password && address->isVisible() &&
           username->isVisible() && password->isVisible();
  }), "the open connection dialog offers its fields");
  if (address && username && password) {
    address->setProperty("text", "http://127.0.0.1:1");
    username->setProperty("text", "audit");
    password->setProperty("text", "audit");
    c.click("connectServerButton");
    c.check(c.until([&] { return !b->server()->connecting() && !b->server()->error().isEmpty(); }),
            "a failed connection reports its error");
    c.check(!shownItem(w->contentItem(), "errorBar"),
            "the error bar stays behind the open connection dialog");
    auto errorBar = itemNamed(w->contentItem(), "errorBar");
    c.check(errorBar && errorBar->property("lastAnnouncedError").toString().isEmpty(),
            "a hidden error is not announced");
    c.shot("00-server-error-dialog");
    QTest::keyClick(w, Qt::Key_Escape);
    c.check(c.until([&] { return shownItem(w->contentItem(), "errorBar") != nullptr; }),
            "the failed attempt appears on the page after the dialog closes");
    auto alert = errorBar ? QAccessible::queryAccessibleInterface(errorBar) : nullptr;
    c.check(alert && alert->role() == QAccessible::AlertMessage &&
            alert->text(QAccessible::Name) == b->error(),
            "the error bar exposes its alert role and error text");
    c.check(errorBar && errorBar->property("lastAnnouncedError").toString() == b->error(),
            "the newly visible error is announced once");
    const auto announced = errorBar ? errorBar->property("lastAnnouncedError").toString() : QString();
    c.shot("00-server-error-page");
    c.click("serverEmptyConnect");
    c.check(!shownItem(w->contentItem(), "errorBar"),
            "opening a dialog hides an error that is standing");
    QTest::keyClick(w, Qt::Key_Escape);
    c.check(c.until([&] { return shownItem(w->contentItem(), "errorBar") != nullptr; }) &&
            errorBar && errorBar->property("lastAnnouncedError").toString() == announced,
            "showing the same error again keeps its announcement state");
    b->notifyError("A different connection error");
    c.check(c.until([&] { return errorBar &&
                errorBar->property("lastAnnouncedError").toString() == b->error(); }) &&
            b->error() != announced && alert && alert->text(QAccessible::Name) == b->error(),
            "a different error updates the alert and its announcement");
  }
  b->server()->disconnectServer();
  b->dismissError();
  b->home();

  // ModalBottomSheet.kt keeps keyboard traversal inside the sheet and returns
  // focus to the opener when the sheet is dismissed.
  b->setMotion(true);
  c.click("queueButton");
  auto sheet = itemNamed(w->contentItem(), "panelSheet");
  auto queueButton = itemNamed(w->contentItem(), "queueButton");
  auto scrim = itemNamed(w->contentItem(), "bottomSheetScrim");
  auto accessibleScrim = scrim ? QAccessible::queryAccessibleInterface(scrim) : nullptr;
  c.check(accessibleScrim && accessibleScrim->text(QAccessible::Name) == "Close sheet" &&
          accessibleScrim->actionInterface(),
          "the scrim has a named accessible dismiss action");
  auto fade = scrim ? scrim->findChild<QObject *>("bottomSheetScrimFade") : nullptr;
  c.check(fade && fade->property("duration").toInt() == c.evaluate("Theme.springEffectsMs").toInt(),
          "the modal scrim fades on Material's DefaultEffects spring");
  auto inSheet = [&] {
    for (auto item = w->activeFocusItem(); item; item = item->parentItem())
      if (item == sheet) return true;
    return false;
  };
  c.check(sheet && sheet->property("open").toBool() && w->property("modalOpen").toBool(),
          "the narrow queue opens as a modal sheet");
  auto firstSheetControl = shownItem(w->contentItem(), "revealPlayingButton");
  if (!firstSheetControl || !firstSheetControl->isEnabled())
    firstSheetControl = shownItem(w->contentItem(), "closePanelButton");
  c.check(c.until([&] { return firstSheetControl && w->activeFocusItem() == firstSheetControl; }),
          "opening the queue focuses its first reachable control once");
  if (queueButton && firstSheetControl) {
    queueButton->forceActiveFocus(Qt::OtherFocusReason);
    c.check(!sheet->property("focusInside").toBool(),
            "sheet shortcuts stand down when focus leaves the sheet");
    QTest::keyClick(w, Qt::Key_Escape);
    c.check(sheet->property("open").toBool(),
            "Escape outside the sheet does not dismiss it");
    firstSheetControl->forceActiveFocus(Qt::TabFocusReason);
  }
  for (int i = 0; i < 10; ++i) {
    QTest::keyClick(w, Qt::Key_Tab);
    c.check(inSheet(), QString("Tab %1 stays inside the queue sheet").arg(i+1));
  }
  for (int i = 0; i < 10; ++i) {
    QTest::keyClick(w, Qt::Key_Backtab, Qt::ShiftModifier);
    c.check(inSheet(), QString("Shift+Tab %1 stays inside the queue sheet").arg(i+1));
  }
  c.shot("00-modal-queue");
  QTest::keyClick(w, Qt::Key_Escape);
  c.check(c.until([&] { return sheet && !sheet->property("open").toBool(); }),
          "Escape dismisses the queue sheet");
  c.check(c.until([&] { return w->activeFocusItem() == queueButton; }),
          "focus returns to the queue button");
  c.click("queueButton");
  if (accessibleScrim && accessibleScrim->actionInterface())
    accessibleScrim->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
  c.check(c.until([&] { return sheet && !sheet->property("open").toBool(); }),
          "the scrim's accessibility press dismisses the sheet");
  b->setMotion(false);
  w->resize(1280, 850);
  QTest::qWait(400);

  // The mini window keeps Qt's unbound DragHandler for compositor moving.
  // Open and restore through the same shortcuts a person uses.
  QTest::keyClick(w, Qt::Key_M, Qt::ControlModifier);
  QQuickWindow *mini = nullptr;
  c.check(c.until([&] {
    for (auto top : QGuiApplication::topLevelWindows())
      if (top->objectName() == "miniPlayerWindow")
        mini = qobject_cast<QQuickWindow *>(top);
    return mini && mini->isVisible();
  }), "Ctrl+M opens the mini player");
  auto moveHandler = mini ? mini->findChild<QObject *>("miniMoveHandler") : nullptr;
  c.check(moveHandler && !moveHandler->property("target").value<QObject *>(),
          "the mini window uses an unbound drag handler for system move");
  if (mini) QTest::keyClick(mini, Qt::Key_M, Qt::ControlModifier);
  c.check(c.until([&] { return w->isVisible(); }), "Ctrl+M restores the full player");

  auto settings = w->findChild<QObject *>("settingsDialog");
  c.check(settings, "the settings dialog exists");
  if (!settings)
    return c.finish();
  QMetaObject::invokeMethod(settings, "open");
  QTest::qWait(500);

  // M3 asks that primary text sit in the same position in every list item.
  // A settings group is a list, so one leading edge has to serve all of it.
  const QStringList names{"Appearance", "Playback", "Library", "Keyboard", "Connections", "Privacy & data"};
  const QStringList groupNames{"settingsGroup0", "settingsGroup1", "settingsGroup2",
                               "settingsGroupKeyboard", "settingsGroup3", "settingsGroup4"};
  const QStringList rowNames{"settingsRows0", "settingsRows1", "settingsRows2",
                             "settingsRowsKeyboard", "settingsRows3", "settingsRows4"};
  const QStringList headingNames{"settingsAppearanceHeading", "settingsPlaybackHeading",
                                 "settingsLibraryHeading", "settingsKeyboardHeading",
                                 "settingsConnectionsHeading", "settingsPrivacyHeading"};
  auto auditGroups = [&](const QString &context) {
    for (int category = 0; category < names.size(); ++category) {
      settings->setProperty("category", category);
      QTest::qWait(250);
      auto group = itemNamed(w->contentItem(), groupNames[category]);
      c.check(group && group->isVisible(), context + ": " + names[category] + " is shown");
      if (!group)
        continue;
      // CLAUDE.md removes headings that repeat a selected category. Search
      // results show headings because several categories can appear together.
      auto heading = itemNamed(group, headingNames[category]);
      c.check(heading && !heading->isVisible(),
              context + ": " + names[category] + " does not repeat its selected category");
      auto options = itemNamed(group, rowNames[category]);
      const auto rows = visibleRows(options);
      c.check(rows.size() >= 3, context + ": " + names[category] + " has rows to align");
      const auto edge = group->mapToScene(QPointF(0, 0)).x();
      qreal smallest = 1e9;
      int aligned = 0, measured = 0;
      for (auto row : rows) {
        // Segmented controls and the swatch picker are composite controls, not
        // list rows; their inner text belongs to the control, not the list.
        if (!row->property("accessibleName").toString().isEmpty() || row->objectName() == "accentPicker")
          continue;
        auto label = rowLabel(row);
        if (!label)
          continue;
        ++measured;
        if (qAbs(label->mapToScene(QPointF(0, 0)).x() - edge) < 0.6)
          ++aligned;
        // Only rows that do something have to carry list-sized label text.
        if (row->property("checked").isValid() || row->property("trailingSymbol").isValid())
          smallest = qMin(smallest, label->property("font").value<QFont>().pixelSize() * 1.0);
      }
      c.check(measured >= 3, context + ": " + names[category] + " labels were measured");
      c.check(aligned == measured,
              context + ": every " + names[category] + " label shares one leading edge");
      c.check(smallest >= 16, context + ": " + names[category] + " rows use list-sized label text");
    }
  };
  b->setTheme("dark");
  QTest::qWait(200);
  auditGroups("dark");
  c.shot("01-settings-dark");
  b->setTheme("light");
  QTest::qWait(300);
  auditGroups("light");
  c.shot("02-settings-light");
  b->setTheme("dark");
  b->setCompactDensity(true);
  QTest::qWait(300);
  auditGroups("compact");
  b->setCompactDensity(false);
  QTest::qWait(200);
  w->resize(760, 820);
  QTest::qWait(400);
  auditGroups("narrow");
  c.check(shownItem(w->contentItem(), "settingsCategoryPicker"), "a narrow dialog offers the category picker");
  c.shot("03-settings-narrow");
  w->resize(1280, 850);
  QTest::qWait(400);

  // Trailing controls line up on their own edge, the same way.
  settings->setProperty("category", 1);
  QTest::qWait(300);
  auto playback = itemNamed(w->contentItem(), "settingsRows1");
  qreal trailing = -1;
  int trailingAligned = 0, trailingCount = 0;
  for (auto row : visibleRows(playback)) {
    QQuickItem *control = nullptr;
    if (row->property("checked").isValid())
      control = row->property("indicator").value<QQuickItem *>();
    else
      for (auto child : row->childItems())
        if (child->isVisible() && child->property("tonal").toBool())
          control = child;
    if (!control || !control->isVisible())
      continue;
    ++trailingCount;
    const auto right = control->mapToScene(QPointF(control->width(), 0)).x();
    if (trailing < 0)
      trailing = right;
    if (qAbs(right - trailing) < 1.5)
      ++trailingAligned;
  }
  c.check(trailingCount >= 4, "playback has switches and value controls to align");
  c.check(trailingAligned == trailingCount, "switches and value controls share one trailing edge");

  // A row that opens something is marked; a row that acts immediately is not.
  settings->setProperty("category", 2);
  QTest::qWait(300);
  auto folders = itemNamed(w->contentItem(), "settingsRows2");
  c.check(folders, "library options are shown");
  settings->setProperty("category", 3);
  QTest::qWait(300);
  auto opener = shownItem(w->contentItem(), "shortcutHelpButton");
  c.check(opener && opener->property("trailingSymbol") == "chevron",
          "a row that opens a panel shows a trailing icon");
  settings->setProperty("category", 5);
  QTest::qWait(300);
  auto immediate = shownItem(w->contentItem(), "clearHistoryButton");
  c.check(immediate && immediate->property("trailingSymbol").toString().isEmpty(),
          "a row that acts immediately does not");
  auto opensDialog = shownItem(w->contentItem(), "exportLibraryButton");
  c.check(opensDialog && opensDialog->property("trailingSymbol") == "chevron",
          "and one that opens a picker does");

  // Nothing in settings explains itself twice.
  int paragraphs = 0;
  for (int category = 0; category < names.size(); ++category) {
    settings->setProperty("category", category);
    QTest::qWait(200);
    auto options = itemNamed(w->contentItem(), rowNames[category]);
    for (auto row : visibleRows(options)) {
      auto label = rowLabel(row);
      if (label && label->property("wrapMode").toInt() != 0 &&
          label->property("text").toString().length() > 60)
        ++paragraphs;
    }
  }
  c.check(paragraphs <= 1, "at most one settings row needs a paragraph of its own");

  // A clipped edge has to read as "more below", and stop saying so at the end.
  settings->setProperty("category", 1);
  QTest::qWait(300);
  auto scroll = itemNamed(w->contentItem(), "settingsScroll");
  auto flick = scroll ? scroll->property("contentItem").value<QQuickItem *>() : nullptr;
  c.check(flick, "the settings pane scrolls");
  if (flick) {
    const auto span = flick->property("contentHeight").toReal() - scroll->height();
    c.check(span > 0, "playback has more rows than fit");
    flick->setProperty("contentY", 0);
    QTest::qWait(300);
    auto divider = itemNamed(w->contentItem(), "dialogScrollDivider");
    c.check(divider && divider->isVisible(), "a divider marks content continuing below");
    flick->setProperty("contentY", span);
    QTest::qWait(300);
    c.check(divider && !divider->isVisible(), "and goes away at the end of the list");
    flick->setProperty("contentY", 0);
    QTest::qWait(200);
  }
  // Actions must never sit on top of the content they belong to.
  auto footer = settings->property("footer").value<QQuickItem *>();
  if (footer && scroll)
    c.check(footer->mapToScene(QPointF(0, 0)).y() >= scroll->mapToScene(QPointF(0, scroll->height())).y() - 1,
            "the dialog's actions sit below its content, not over it");

  // Every category can be reached and searched without leaving an empty pane.
  settings->setProperty("searchQuery", "volume");
  QTest::qWait(300);
  c.check(shownItem(w->contentItem(), "volumeStepButton") && shownItem(w->contentItem(), "volumeNormalizationSwitch"),
          "a search reaches settings in more than one group");
  c.check(!shownItem(w->contentItem(), "settingsNoResults"), "and does not claim there are none");
  settings->setProperty("searchQuery", "music");
  QTest::qWait(300);
  c.check(shownItem(w->contentItem(), "settingsAppearanceHeading") &&
              shownItem(w->contentItem(), "settingsLibraryHeading") &&
              shownItem(w->contentItem(), "settingsConnectionsHeading"),
          "search results name each matching Settings category");
  settings->setProperty("searchQuery", "zzzz");
  QTest::qWait(300);
  c.check(shownItem(w->contentItem(), "settingsNoResults"), "a search with no matches says so");
  settings->setProperty("searchQuery", "");
  QTest::qWait(200);
  QMetaObject::invokeMethod(settings, "close");
  QTest::qWait(300);
  b->setTheme("dark");
  c.finish();
}

// What the application costs to run rather than what it shows: the memory
// it holds and the work before its first frame. Each check here fails if
// the change it guards is taken out.
void runFootprintTests(Backend *b, QQuickWindow *w) {
  Harness c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory);
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1180, 800);
  QTest::qWait(400);

  // --- Identical symbols share one picture ---
  // Two icons at one size in one ink are the same image. The pixmap cache
  // hands both the one texture, where each used to ask for its own.
  QQmlComponent pair(qmlEngine(w));
  pair.setData("import QtQuick\n"
               "Row { spacing: 8; x: 24; y: 24; z: 1000\n"
               "  Icon { objectName: \"sharedA\"; name: \"queue\"; size: 24 }\n"
               "  Icon { objectName: \"sharedB\"; name: \"queue\"; size: 24 }\n"
               "  Icon { objectName: \"otherInk\"; name: \"queue\"; size: 24; ink: \"#ff0000\" }\n"
               "}\n",
               QUrl("qrc:/qml/MemoryProbe.qml"));
  auto *row = qobject_cast<QQuickItem *>(pair.create(qmlContext(w)));
  c.check(row, "icons can be built beside the interface");
  if (!row) {
    fprintf(stdout, "%s\n", qPrintable(pair.errorString()));
    return c.finish();
  }
  row->setParentItem(w->contentItem());
  QTest::qWait(300);
  const auto texture = [&](const QString &name) -> QSGTexture * {
    QQuickItem *icon = nullptr;
    for (auto *child : row->childItems())
      if (child->objectName() == name) icon = child;
    QQuickItem *fill = nullptr;
    if (icon)
      for (auto *child : icon->childItems())
        if (child->objectName() == "iconFill") fill = child;
    auto *provider = fill ? fill->textureProvider() : nullptr;
    return provider ? provider->texture() : nullptr;
  };
  auto *first = texture("sharedA"), *second = texture("sharedB"), *other = texture("otherInk");
  c.check(first && second && other, "each icon has a texture to draw");
  c.check(first == second, "two identical icons draw from one texture");
  c.check(first != other, "an icon in another ink draws from a texture of its own");
  c.shot("01-shared-symbols");
  delete row;
  QTest::qWait(100);

  // --- The texture atlas starts at the size the symbols need ---
  // Only a GPU renderer builds an atlas, so offscreen this can only see the
  // default being in place before the window exists. A value brought in by
  // the environment still wins over it.
  c.check(qgetenv("QSG_ATLAS_WIDTH") == "1024" && qgetenv("QSG_ATLAS_HEIGHT") == "1024",
          "the texture atlas defaults to 1024 by 1024");

  // --- The interface loads while the backend starts ---
  // Loading the interface's types on the engine's thread overlaps the audio
  // outputs and the library read instead of queueing behind them.
  c.check(qApp->property("interfaceLoadedWithBackend").toBool(),
          "the interface was already loading while the backend was built");

  // --- Memory freed in use goes back once the application is not in use ---
  // A busy session frees memory in pieces between allocations that stay, and
  // glibc cannot give those pages back by itself. This builds that shape on
  // purpose: 64 MiB in blocks too small to be mapped apart, each followed by
  // a small one that is kept. The writes are volatile so the compiler cannot
  // decide the pages were never touched.
  const auto residentMiB = [] {
    QFile statm("/proc/self/statm");
    if (!statm.open(QIODevice::ReadOnly))
      return 0.0;
    return statm.readAll().split(' ').value(1).toDouble() * sysconf(_SC_PAGESIZE) / 1048576.0;
  };
  constexpr int blockBytes = 64 * 1024;
  QList<void *> freed, kept;
  for (int i = 0; i < 1024; ++i) {
    auto *block = static_cast<volatile char *>(malloc(blockBytes));
    for (int offset = 0; offset < blockBytes; offset += 4096)
      block[offset] = 1;
    freed.append(const_cast<char *>(block));
    kept.append(malloc(64));
  }
  for (void *block : freed)
    free(block);
  const double held = residentMiB();
  QWindowSystemInterface::handleFocusWindowChanged<QWindowSystemInterface::SynchronousDelivery>(nullptr);
  QCoreApplication::processEvents();
  const double given = held - residentMiB();
  c.check(QGuiApplication::applicationState() != Qt::ApplicationActive,
          "losing the focus leaves the application inactive");
  c.check(given > 48, QString("memory freed in use goes back once the application is not in use (%1 MiB)")
                          .arg(given, 0, 'f', 1));
  QWindowSystemInterface::handleFocusWindowChanged<QWindowSystemInterface::SynchronousDelivery>(w);
  QCoreApplication::processEvents();
  c.check(QGuiApplication::applicationState() == Qt::ApplicationActive, "focus brings the application back");
  for (void *block : kept)
    free(block);

  c.finish();
}
