#include "backend.h"
#include "localization.h"
#include "desktoptheme.h"
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QColor>
#include <QInputMethodEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QSaveFile>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QtTest>
#include <functional>

namespace {
QQuickItem *visibleItem(QQuickItem *root, const QString &name) {
  if (!root->isVisible()) return nullptr;
  if (root->objectName() == name) return root;
  for (auto child : root->childItems())
    if (auto result = visibleItem(child, name)) return result;
  return nullptr;
}
}

void runLanguageTests(Backend *backend, QQuickWindow *window, Localization *language) {
  QDir().mkpath(qEnvironmentVariable("SUNG_TEST_OUTPUT"));
  int failures = 0;
  auto check = [&](bool ok, const char *message) {
    fprintf(stdout, "%s %s\n", ok ? "PASS" : "FAIL", message);
    fflush(stdout);
    if (!ok) ++failures;
  };
  auto waitFor = [&](const std::function<bool()> &test) {
    QElapsedTimer timer; timer.start();
    while (!test() && timer.elapsed() < 5000) QTest::qWait(25);
    return test();
  };
  auto click = [&](const QString &name) {
    auto item = visibleItem(window->contentItem(), name);
    check(item != nullptr, qPrintable("find " + name));
    if (!item) return;
    const auto point = item->mapToScene(QPointF(item->width()/2, item->height()/2));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
    QTest::qWait(400);
  };
  auto capture = [&](const QString &name) {
    QTest::mouseMove(window, QPoint(2, 2));
    QTest::qWait(500);
    const auto directory = qEnvironmentVariable("SUNG_TEST_OUTPUT");
    QDir().mkpath(directory);
    check(window->grabWindow().save(directory + "/" + name + ".png"), qPrintable("capture " + name));
  };

  language->setLanguage("en");
  backend->setTheme("dark");
  backend->setVolume(0);
  backend->setAutoplay(false);
  window->resize(1180, 800);
  QTest::qWait(700);

  // Decode a real, silent PCM file through the normal player, not a mock.
  QTemporaryDir audio;
  QFile wav(audio.filePath("language-test.wav"));
  check(wav.open(QIODevice::WriteOnly), "create silent playback fixture");
  const int bytes = 44100 * 2 * 90;
  QDataStream stream(&wav); stream.setByteOrder(QDataStream::LittleEndian);
  stream.writeRawData("RIFF", 4); stream << quint32(bytes + 36);
  stream.writeRawData("WAVEfmt ", 8); stream << quint32(16) << quint16(1) << quint16(1)
      << quint32(44100) << quint32(88200) << quint16(2) << quint16(16);
  stream.writeRawData("data", 4); stream << quint32(bytes);
  wav.write(QByteArray(bytes, '\0')); wav.close();
  backend->playItem({{"id","local_language_test"},{"kind","song"},{"title","Тест кириллицы"},
                     {"localPath",wav.fileName()},{"seconds",90}});
  check(waitFor([&] { return backend->playing() && backend->position() > 100; }), "real audio playback started");
  auto player = backend->media();
  const auto source = player->source();
  QSignalSpy states(player, &QMediaPlayer::playbackStateChanged);
  QSignalSpy sources(player, &QMediaPlayer::sourceChanged);
  const auto position = backend->position();

  // Only edit a fixture inside this stage's output, never the real desktop.
  const auto palettePath = qEnvironmentVariable("SUNG_NOCTALIA_COLORS");
  const auto outputPath = qEnvironmentVariable("SUNG_TEST_OUTPUT");
  const bool isolatedPalette = QFileInfo(palettePath).absolutePath() == QFileInfo(outputPath).absoluteFilePath();
  auto desktop = qobject_cast<DesktopTheme *>(QQmlEngine::contextForObject(window)->contextProperty("desktopTheme").value<QObject *>());
  auto writePalette = [&](bool light) {
    if (!isolatedPalette) return false;
    const auto background = light ? QByteArray("245,250,245") : QByteArray("16,20,28");
    const auto accent = light ? QByteArray("20,100,50") : QByteArray("90,170,255");
    const auto text = light ? QByteArray("20,30,20") : QByteArray("230,235,250");
    const auto bytes = "[Colors:View]\nBackgroundNormal=" + background + "\nForegroundNormal=" + text
        + "\nForegroundInactive=" + text + "\nDecorationHover=" + background
        + "\nDecorationFocus=" + text + "\nForegroundLink=" + accent
        + "\n[Colors:Complementary]\nBackgroundNormal=" + background
        + "\n[Colors:Window]\nBackgroundNormal=" + background + "\nBackgroundAlternate=" + background
        + "\n[Colors:Button]\nBackgroundNormal=" + background + "\nForegroundActive=" + accent + "\n";
    QSaveFile file(palettePath);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
  };
  check(desktop && writePalette(false), "write isolated Noctalia dark palette");
  check(waitFor([&] { return desktop && desktop->available() && desktop->colors().value("primary").value<QColor>() == QColor(90,170,255); }),
        "Noctalia file watcher loads palette without restarting");
  backend->setTheme("system");
  check(waitFor([&] { return window->color() == QColor(16,20,28); }),
        "rendered window uses Noctalia dark background");

  click("settingsButton");
  click("languageButton");
  click("languageRussian");
  check(language->language() == "ru", "Russian selected through dropdown clicks");
  check(visibleItem(window->contentItem(), "languageRussian") == nullptr, "language menu closes after retranslation");
  auto settings = visibleItem(window->contentItem(), "settingsCategory_0");
  check(settings && settings->property("text").toString() == "Оформление", "existing settings category retranslated");
  auto navigation = visibleItem(window->contentItem(), "navBarLabel_library");
  check(navigation && navigation->property("text").toString() == "Медиатека", "existing navigation retranslated");
  check(QCoreApplication::translate("Untranslated", "Untranslated message") == "Untranslated message", "English fallback preserved");
  capture("language-ru-wide-dark");
  auto settingsSearch = visibleItem(window->contentItem(), "settingsSearch");
  if (settingsSearch) settingsSearch->forceActiveFocus(Qt::TabFocusReason);
  QInputMethodEvent searchInput;
  searchInput.setCommitString("Выравнивание");
  QGuiApplication::sendEvent(window, &searchInput);
  QTest::qWait(400);
  auto normalization = visibleItem(window->contentItem(), "volumeNormalizationSwitch");
  check(normalization && normalization->property("text").toString() == "Выравнивание громкости", "Russian settings search finds translated playback setting");
  capture("language-ru-settings-search");
  QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
  QTest::keyClick(window, Qt::Key_Backspace);
  QTest::qWait(300);
  check(writePalette(true), "atomically replace isolated Noctalia palette");
  check(waitFor([&] { return desktop && !desktop->dark() && desktop->colors().value("primary").value<QColor>() == QColor(20,100,50); }),
        "Noctalia palette changes live after atomic file replacement");
  check(waitFor([&] { return window->color() == QColor(245,250,245); }),
        "rendered window follows new Noctalia background live");
  capture("language-ru-noctalia-light");

  click("languageButton");
  click("languageEnglish");
  check(language->language() == "en", "English restored through dropdown clicks");
  capture("language-en-wide-dark");

  auto keyboardField = visibleItem(window->contentItem(), "languageButton");
  if (keyboardField) keyboardField->forceActiveFocus(Qt::TabFocusReason);
  QTest::keyClick(window, Qt::Key_Space);
  QTest::qWait(300);
  check(visibleItem(window->contentItem(), "languageRussian") != nullptr, "language menu opens from keyboard");
  QTest::keyClick(window, Qt::Key_Escape);
  QTest::qWait(300);
  check(visibleItem(window->contentItem(), "languageRussian") == nullptr, "Escape closes language menu");

  click("languageButton");
  click("languageSystem");
  check(language->language() == "system", "System language selectable");
  click("languageButton");
  click("languageRussian");
  check(language->language() == "ru", "Russian reselected before narrow layout check");
  backend->setTheme("light");
  backend->setMotion(false);
  window->resize(600, 740);
  QTest::qWait(700);
  capture("language-ru-narrow-light");
  auto field = visibleItem(window->contentItem(), "languageButton");
  check(field && field->width() > 100 && field->mapToScene(QPointF()).x() >= 0,
        "language control remains visible at narrow width with reduced motion");
  check(backend->media() == player && player->source() == source && sources.count() == 0,
        "same player and source retained across language changes");
  check(backend->playing() && states.count() == 0 && backend->position() > position,
        "music kept playing without pause or restart");
  check(backend->current().value("title").toString() == "Тест кириллицы", "track metadata unchanged");
  // Close Settings through the same keyboard path as the user and visit the
  // local library, search and playlists while the audio continues.
  QTest::keyClick(window, Qt::Key_Escape);
  QTest::qWait(400);
  window->resize(1180,800);
  QTest::qWait(400);
  click("navBar_library");
  click("localFilesTab");
  auto filesTab = visibleItem(window->contentItem(), "localFilesTab");
  check(filesTab && filesTab->property("text").toString() == "Локальные файлы" && backend->title() == "Local files",
        "local library label translated without changing stored title");
  capture("language-ru-local-library");
  click("playlistsTab");
  capture("language-ru-playlists");
  click(visibleItem(window->contentItem(), "emptyPlaylistGridAction") ? "emptyPlaylistGridAction" : "emptyPlaylistListAction");
  auto playlistDialog = window->findChild<QObject *>("playlistDialog");
  check(playlistDialog && playlistDialog->property("visible").toBool() && playlistDialog->property("title").toString() == "Новый плейлист", "create playlist dialog opened and translated");
  capture("language-ru-new-playlist");
  QTest::keyClick(window, Qt::Key_Escape);
  QTest::qWait(400);
  click("navBar_search");
  auto search = visibleItem(window->contentItem(), "searchField");
  check(search && search->property("placeholderText").toString() == "Поиск музыки", "music search translated");
  capture("language-ru-search");
  auto enterText = [&](const QString &name, const QString &text) {
    auto item = visibleItem(window->contentItem(), name);
    check(item != nullptr, qPrintable("find input " + name));
    if (!item) return;
    item->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    QInputMethodEvent input; input.setCommitString(text);
    QGuiApplication::sendEvent(window, &input);
    QTest::qWait(300);
  };
  auto openCommand = [&](const QString &query, const QString &id) {
    QTest::keyClick(window, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
    QTest::qWait(400);
    enterText("commandSearch", query);
    click("command_" + id);
  };
  auto dialogTitle = [&](const QString &name, const QString &title) {
    auto dialog = window->findChild<QObject *>(name);
    check(dialog && dialog->property("visible").toBool() && dialog->property("title").toString() == title,
          qPrintable("opened translated dialog " + name));
  };
  openCommand("Статистика", "stats");
  dialogTitle("listeningStatsDialog", "Прослушивания");
  check(visibleItem(window->contentItem(), "calendarTitle") &&
        visibleItem(window->contentItem(), "calendarTitle")->property("text").toString() == "Последний год",
        "listening calendar translated");
  capture("language-ru-statistics");
  window->resize(520, 700); QTest::qWait(400);
  capture("language-ru-statistics-narrow");
  QTest::keyClick(window, Qt::Key_Escape); QTest::qWait(400);
  window->resize(1180, 800); QTest::qWait(400);
  openCommand("сеансы", "sessions");
  dialogTitle("sessionsDialog", "Сохранённые сеансы");
  capture("language-ru-sessions");
  QTest::keyClick(window, Qt::Key_Escape); QTest::qWait(400);
  click("settingsButton");
  enterText("settingsSearch", "Музыкальный сервер");
  click("musicServerButton");
  dialogTitle("serverConnectionDialog", "Музыкальный сервер");
  auto address = visibleItem(window->contentItem(), "serverAddress");
  check(address && address->property("label").toString() == "Адрес сервера", "server fields translated");
  capture("language-ru-server");
  window->resize(520, 700); QTest::qWait(400);
  capture("language-ru-server-narrow");
  auto remember = visibleItem(window->contentItem(), "rememberServer");
  auto fields = window->findChild<QQuickItem *>("connectionFields");
  check(remember && fields && remember->width() <= fields->width(), "translated keyring switch fits narrow server form");
  QTest::keyClick(window, Qt::Key_Escape); QTest::qWait(400);
  window->resize(1180, 800); QTest::qWait(400);
  click("navBar_library"); click("playlistsTab"); click("libraryFab"); click("fabMenuItem_1");
  dialogTitle("smartPlaylistDialog", "Новый умный плейлист");
  capture("language-ru-smart-playlist");
  window->resize(520, 700); QTest::qWait(400);
  capture("language-ru-smart-playlist-narrow");
  QTest::keyClick(window, Qt::Key_Escape); QTest::qWait(400);
  window->resize(1180, 800); QTest::qWait(400);
  click("settingsButton"); enterText("settingsSearch", "Горячие клавиши"); click("shortcutHelpButton");
  dialogTitle("shortcutHelp", "Горячие клавиши");
  capture("language-ru-shortcuts");
  check(states.isEmpty() && sources.isEmpty() && backend->playing(), "extended dialogs do not interrupt playback");
  backend->pause();
  QCoreApplication::exit(failures ? 1 : 0);
}

void runLanguageStartupTest(QQuickWindow *window, Localization *language) {
  const auto label = visibleItem(window->contentItem(), "navBarLabel_library");
  const bool ok = language->language() == "ru" && label && label->property("text").toString() == "Медиатека";
  fprintf(stdout, "%s Russian preference restored in a new application process\n", ok ? "PASS" : "FAIL");
  QCoreApplication::exit(ok ? 0 : 1);
}
