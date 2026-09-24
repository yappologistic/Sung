#include "uitest.h"
#include "backend.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QProcess>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <qpa/qwindowsysteminterface.h>

namespace {
bool until(const std::function<bool()> &condition, int timeout = 8000) {
  QElapsedTimer time;
  time.start();
  while (!condition() && time.elapsed() < timeout)
    QTest::qWait(25);
  return condition();
}

QQuickItem *shown(QQuickItem *root, const QString &name) {
  if (!root || !root->isVisible())
    return nullptr;
  if (root->objectName() == name)
    return root;
  for (auto child : root->childItems())
    if (auto found = shown(child, name))
      return found;
  return nullptr;
}

QString rectText(const QRectF &rect) {
  return QString("[%1,%2 %3x%4]")
      .arg(rect.x(), 0, 'f', 1).arg(rect.y(), 0, 'f', 1)
      .arg(rect.width(), 0, 'f', 1).arg(rect.height(), 0, 'f', 1);
}

QString itemName(QQuickItem *item) {
  const auto text = item->property("text").toString().simplified().left(60);
  QString identity = QString("%1 text='%2' type=%3 scene=%4")
      .arg(item->objectName().isEmpty() ? QStringLiteral("<unnamed>") : item->objectName(),
           text, QString::fromLatin1(item->metaObject()->className()),
           rectText(item->mapRectToScene(item->boundingRect())));
  const auto symbol = item->property("name").toString();
  if (!symbol.isEmpty()) {
    identity += " symbol='" + symbol + "'";
    for (auto owner = item->parentItem(); owner; owner = owner->parentItem()) {
      if (!owner->inherits("QQuickControl")) continue;
      identity += QString(" owner=%1('%2')")
          .arg(owner->objectName().isEmpty() ? QStringLiteral("<unnamed>") : owner->objectName(),
               owner->property("text").toString().simplified().left(60));
      break;
    }
  }
  return identity;
}

QColor themeColor(QQuickWindow *window, const QString &name) {
  QQmlExpression expression(qmlContext(window), window, "Theme." + name);
  return expression.evaluate().value<QColor>();
}

using Roles = QHash<QRgb, QString>;

Roles rolesFor(QQuickWindow *window) {
  Roles roles;
  // A scheme can resolve two roles to the same RGB. Prefer the primary and
  // secondary roles used by ordinary controls before considering tertiary.
  for (const auto &role : {"primary", "muted", "text", "primaryText", "containerText",
                           "secondaryContainerText", "secondary", "secondaryText",
                           "tertiary", "tertiaryText", "tertiaryContainerText", "focusRing"}) {
    const auto color = themeColor(window, QString::fromLatin1(role)).rgb();
    if (!roles.contains(color)) roles.insert(color, QString::fromLatin1(role));
  }
  return roles;
}

QString roleOf(const QColor &ink, const Roles &roles) {
  return roles.value(ink.rgb(), "custom");
}

double opacityOf(QQuickItem *item) {
  double opacity = 1;
  for (auto ancestor = item; ancestor; ancestor = ancestor->parentItem())
    opacity *= ancestor->opacity();
  return opacity;
}

QRectF visibleRect(QQuickItem *item, QQuickWindow *window) {
  QRectF rect = item->mapRectToScene(item->boundingRect());
  rect = rect.intersected(QRectF(0, 0, window->width(), window->height()));
  for (auto ancestor = item->parentItem(); ancestor; ancestor = ancestor->parentItem())
    if (ancestor->clip())
      rect = rect.intersected(ancestor->mapRectToScene(ancestor->boundingRect()));
  return rect;
}

struct Ink {
  QQuickItem *item;
  QColor color;
  QString category;
  double originalOpacity;
  double effectiveOpacity;
  QRectF rect;
  double floor;
};

bool disabledItem(QQuickItem *item) {
  for (auto ancestor = item; ancestor; ancestor = ancestor->parentItem())
    if (!ancestor->isEnabled()) return true;
  return false;
}

struct Boundary {
  QQuickItem *item;
  QColor color;
  QRectF rect;
  double effectiveOpacity;
  double stroke;
  QString rule;
  bool fillEdge;
};

QQuickItem *nearestControl(QQuickItem *item) {
  for (auto owner = item->parentItem(); owner; owner = owner->parentItem())
    if (owner->inherits("QQuickControl")) return owner;
  return nullptr;
}

QQuickItem *controlBackgroundOwner(QQuickItem *item) {
  auto owner = nearestControl(item);
  if (!owner) return nullptr;
  auto background = owner->property("background").value<QQuickItem *>();
  for (auto ancestor = item; ancestor && ancestor != owner; ancestor = ancestor->parentItem())
    if (ancestor == background) return owner;
  return nullptr;
}

QString boundaryRule(QQuickItem *item, QQuickItem *owner) {
  if (disabledItem(item)) return "disabled-control";
  if (item->objectName() == "checkboxBox") return "checkbox-ring";
  if (item->objectName() == "radioRing") return "radio-ring";
  if (item->objectName() == "switchTrack") return "switch-track";
  if (owner && owner->inherits("QQuickTextField") &&
      !owner->property("filled").toBool() && item->objectName() == "fieldContainer")
    return "outlined-text-field";
  if (owner && owner->inherits("QQuickAbstractButton") &&
      owner->property("text").toString().trimmed().isEmpty() &&
      owner->property("outlined").toBool() && controlBackgroundOwner(item))
    return "icon-only-edge";
  if (owner && !owner->property("text").toString().trimmed().isEmpty())
    return "labelled-control";
  return "nonessential-boundary";
}

bool requiredBoundary(const QString &rule) {
  return rule == "checkbox-ring" || rule == "radio-ring" ||
         rule == "switch-track" || rule == "outlined-text-field" ||
         rule == "icon-only-edge";
}

void collectBoundaries(QQuickItem *root, QQuickWindow *window,
                       QList<Boundary> &out) {
  if (!root || !root->isVisible() || opacityOf(root) < 0.03)
    return;
  if (root->inherits("QQuickRectangle")) {
    auto owner = nearestControl(root);
    const QString rule = boundaryRule(root, owner);
    const bool inBackground = controlBackgroundOwner(root);
    if (!inBackground && !requiredBoundary(rule)) {
      for (auto child : root->childItems())
        collectBoundaries(child, window, out);
      return;
    }
    const QQmlProperty borderWidth(root, "border.width", qmlContext(root));
    const QQmlProperty borderColor(root, "border.color", qmlContext(root));
    const double stroke = borderWidth.read().toDouble();
    const QColor borderInk = borderColor.read().value<QColor>();
    const QColor fillInk = root->property("color").value<QColor>();
    const bool fillEdge = stroke <= 0 && requiredBoundary(rule) &&
                          fillInk.isValid() && fillInk.alpha() > 0;
    const QColor ink = fillEdge ? fillInk : borderInk;
    const QRectF rect = visibleRect(root, window);
    if ((stroke > 0 || fillEdge) && ink.isValid() && ink.alpha() > 0 &&
        rect.width() >= 12 && rect.height() >= 12)
      out.append({root, ink, rect, opacityOf(root) * ink.alphaF(), stroke,
                  rule, fillEdge});
  }
  for (auto child : root->childItems())
    collectBoundaries(child, window, out);
}

void collectInk(QQuickItem *root, QQuickWindow *window, QList<Ink> &out,
                int &skippedDisabled) {
  if (!root || !root->isVisible() || opacityOf(root) < 0.03)
    return;
  // LyricsView fades a line that crosses the viewport's edge (edgeOpacity) so
  // no glyph is ever cut in half: that line is leaving view, not being read.
  // The immersive stage's own contrast check takes resting lines only when
  // they are fully opaque, and this audit does the same. Where a line lands
  // in that band depends on the height the controls below leave the list.
  if (root->objectName() == "lyricLine" && root->property("edgeOpacity").toDouble() < 0.99)
    return;
  const bool text = root->inherits("QQuickText");
  const bool icon = QString::fromLatin1(root->metaObject()->className()).contains("Icon_QMLTYPE") ||
                    root->objectName() == "materialIcon" || root->objectName() == "menuItemLeading";
  if ((text && !root->property("text").toString().isEmpty()) ||
      (icon && !root->property("name").toString().isEmpty())) {
    const QColor color = root->property(text ? "color" : "ink").value<QColor>();
    const QRectF rect = visibleRect(root, window);
    if (color.isValid() && rect.width() >= 3 && rect.height() >= 3) {
      // WCAG 1.4.11 excludes inactive components. Material deliberately
      // draws their content at 38%, so do not test that ink against 3:1.
      if (disabledItem(root)) {
        ++skippedDisabled;
      } else {
        const QFont font = root->property("font").value<QFont>();
        // WCAG 1.4.3 uses 4.5:1 for ordinary text and 3:1 for large text.
        // WCAG 1.4.11 uses 3:1 for graphical controls. The 18.66px/14px
        // bold equivalents follow the audit brief's 96dpi conversion.
        const bool large = text && (font.pixelSize() >= 18.66 ||
                                    (font.pixelSize() >= 14 && font.weight() >= QFont::Bold));
        out.append({root, color, text ? "text" : "icon", root->opacity(),
                    opacityOf(root) * color.alphaF(), rect, text && !large ? 4.5 : 3.0});
      }
    }
  }
  for (auto child : root->childItems())
    collectInk(child, window, out, skippedDisabled);
}

double pixelContrast(const QColor &ink, QRgb background, double alpha) {
  static const auto linear = [] {
    std::array<double, 256> table{};
    for (int i = 0; i < 256; ++i) {
      const double v = i / 255.0;
      table[i] = v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    }
    return table;
  }();
  const int r = qBound(0, qRound(ink.red() * alpha + qRed(background) * (1 - alpha)), 255);
  const int g = qBound(0, qRound(ink.green() * alpha + qGreen(background) * (1 - alpha)), 255);
  const int b = qBound(0, qRound(ink.blue() * alpha + qBlue(background) * (1 - alpha)), 255);
  const double foreground = 0.2126 * linear[r] + 0.7152 * linear[g] + 0.0722 * linear[b];
  const double behind = 0.2126 * linear[qRed(background)] +
                        0.7152 * linear[qGreen(background)] +
                        0.0722 * linear[qBlue(background)];
  return (std::max(foreground, behind) + 0.05) / (std::min(foreground, behind) + 0.05);
}

QRgb expectedInkPixel(const QColor &ink, QRgb background, double alpha) {
  return qRgb(qRound(ink.red() * alpha + qRed(background) * (1 - alpha)),
              qRound(ink.green() * alpha + qGreen(background) * (1 - alpha)),
              qRound(ink.blue() * alpha + qBlue(background) * (1 - alpha)));
}

int colorDistance(QRgb a, QRgb b) {
  return std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) +
         std::abs(qBlue(a) - qBlue(b));
}

bool strokeDrawn(const Boundary &border, const QImage &image, const QImage &background) {
  const QRectF rect = border.rect;
  const int inset = std::max(3, int(std::ceil(border.stroke)) + 2);
  const int middleX = int(rect.center().x());
  const int middleY = int(rect.center().y());
  const QList<QPair<QPoint, QPoint>> pairs{
      {{int(rect.left() + border.stroke / 2), middleY}, {int(rect.left()) + inset, middleY}},
      {{int(rect.right() - border.stroke / 2), middleY}, {int(rect.right()) - inset, middleY}},
      {{middleX, int(rect.top() + border.stroke / 2)}, {middleX, int(rect.top()) + inset}},
      {{middleX, int(rect.bottom() - border.stroke / 2)}, {middleX, int(rect.bottom()) - inset}}};
  for (const auto &[edge, inside] : pairs) {
    if (!image.rect().contains(edge) || !background.rect().contains(inside)) continue;
    const QRgb visible = image.pixel(edge);
    const QRgb under = background.pixel(inside);
    const int expectedR = qRound(border.color.red() * border.effectiveOpacity + qRed(under) * (1 - border.effectiveOpacity));
    const int expectedG = qRound(border.color.green() * border.effectiveOpacity + qGreen(under) * (1 - border.effectiveOpacity));
    const int expectedB = qRound(border.color.blue() * border.effectiveOpacity + qBlue(under) * (1 - border.effectiveOpacity));
    const QRgb expected = qRgb(expectedR, expectedG, expectedB);
    if (colorDistance(visible, under) >= 24 &&
        colorDistance(visible, expected) < colorDistance(visible, under))
      return true;
  }
  return false;
}

struct Audit {
  Backend *backend;
  QQuickWindow *window;
  QString directory;
  QJsonArray captures;
  int failures = 0;
  int measurements = 0;
  int skipped = 0;
  int skippedDisabledInk = 0;
  int skippedDisabledBoundary = 0;
  int skippedLabelledBoundary = 0;
  int skippedOtherBoundary = 0;
  int skippedNoVisibleEdge = 0;
  qint64 maskedNonInkPixels = 0;
  qint64 maskedAttenuatedPixels = 0;
  QElapsedTimer runtime;

  Audit(Backend *b, QQuickWindow *w, const QString &path)
      : backend(b), window(w), directory(path) {}

  void fail(const QString &kind, const QString &context, const QString &detail) {
    fprintf(stdout, "FAIL %s %s %s\n", qPrintable(kind), qPrintable(context), qPrintable(detail));
    fflush(stdout);
    ++failures;
  }

  bool click(const QString &name, const QString &context) {
    auto item = shown(window->contentItem(), name);
    if (!item) {
      fail("state", context, "missing " + name);
      return false;
    }
    const auto point = item->mapToScene(item->boundingRect().center()).toPoint();
    QWindowSystemInterface::handleFocusWindowChanged(window);
    QTest::mouseMove(window, point);
    QTest::qWait(60);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
    return true;
  }

  bool choose(const QString &layout, const QString &context) {
    if (!click("immersiveLayoutButton", context))
      return false;
    auto menu = window->findChild<QObject *>("immersiveLayoutMenu");
    auto open = [&] { return menu && menu->property("visible").toBool(); };
    // The first pointer press after entering full screen can activate the
    // offscreen window without triggering the button. Repeat that real press
    // only when the menu is still closed.
    if (!until(open, 1200)) {
      QWindowSystemInterface::handleFocusWindowChanged(window);
      click("immersiveLayoutButton", context);
    }
    if (!until(open, 3000)) {
      fail("state", context, "layout menu did not open");
      return false;
    }
    if (!until([&] { auto row = shown(window->contentItem(), "immersiveLayout_" + layout);
                      return row && row->property("built").toBool() &&
                             row->isEnabled() && row->height() > 0; })) {
      fail("state", context, "layout row did not build: " + layout);
      return false;
    }
    if (!click("immersiveLayout_" + layout, context))
      return false;
    auto player = shown(window->contentItem(), "immersivePlayer");
    if (!until([&] { return player && player->property("displayedLayout").toString() == layout &&
                            player->property("preferredLayout").toString() == layout &&
                            !menu->property("visible").toBool(); })) {
      fail("state", context, "layout did not settle: " + layout);
      return false;
    }
    return true;
  }

  void capture(const QString &cover, const QString &theme, const QString &contrast,
               int width, const QString &state) {
    QElapsedTimer frameTime;
    frameTime.start();
    const QString context = QString("%1 %2x%3 %4 %5 %6")
                                .arg(state).arg(width).arg(window->height()).arg(theme, contrast, cover);
    const QString filename = QString("%1-%2-%3-%4-%5.png")
                                 .arg(cover, theme, contrast).arg(width).arg(state);
    QJsonObject frame{{"cover", cover}, {"theme", theme}, {"contrast", contrast},
                      {"width", width}, {"state", state}, {"screenshot", filename}};
    if (state == "pane")
      frame.insert("paneMode", window->property("sheetMode").toBool() ? "bottom-sheet" : "supporting-pane");
    const QImage image = window->grabWindow();
    if (image.isNull() || !image.save(directory + "/" + filename))
      fail("capture", context, filename);

    QList<Ink> items;
    QList<Boundary> borders;
    auto scope = window->contentItem();
    if (state == "pane" && window->property("sheetMode").toBool())
      if (auto sheet = shown(window->contentItem(), "panelSheet")) scope = sheet;
    if (state == "menu") {
      if (auto menu = window->findChild<QObject *>("immersiveLayoutMenu"))
        if (auto content = menu->property("contentItem").value<QQuickItem *>())
          scope = content;
    }
    collectInk(scope, window, items, skippedDisabledInk);
    collectBoundaries(scope, window, borders);
    QJsonArray rows;
    const Roles roles = rolesFor(window);
    for (const auto &item : items) {
      // Render only this item without its ink. Badges, focus rings and other
      // siblings keep painting over it, so unchanged pixels cannot be
      // mistaken for the background under the item's visible glyph.
      QQuickItem *underFill = nullptr;
      for (auto ancestor = item.item->parentItem(); ancestor; ancestor = ancestor->parentItem()) {
        if (ancestor->objectName() != "singAlongFill") continue;
        auto line = ancestor->parentItem();
        if (line)
          for (auto sibling : line->childItems())
            if (sibling->objectName() == "singAlongCurrent") underFill = sibling;
        break;
      }
      // SingAlong paints primary over an identical onSurface body. The body
      // belongs to the same line, so expose the ambient surface beneath both
      // when measuring that primary fill.
      const double underFillOpacity = underFill ? underFill->opacity() : 0;
      item.item->setOpacity(0);
      if (underFill) underFill->setOpacity(0);
      const QImage background = window->grabWindow();
      if (underFill) underFill->setOpacity(underFillOpacity);
      item.item->setOpacity(item.originalOpacity);
      if (background.isNull()) {
        fail("capture", context, "ink-free render failed for " + itemName(item.item));
        continue;
      }
      const auto rect = item.rect;
      const int x0 = std::max(0, int(std::floor(rect.left() * background.width() / window->width())));
      const int x1 = std::min(background.width(), int(std::ceil(rect.right() * background.width() / window->width())));
      const int y0 = std::max(0, int(std::floor(rect.top() * background.height() / window->height())));
      const int y1 = std::min(background.height(), int(std::ceil(rect.bottom() * background.height() / window->height())));
      QList<double> ratios;
      QColor worstBackground;
      double smallest = 100;
      // The normal and ink-free frames differ only where this item actually
      // contributes visible ink. An opaque badge above an icon is identical
      // in both frames, so its red pixels cannot become the icon background.
      // Reject attenuated differences too: a translucent overlay or an
      // antialiased edge must not be treated as a full-strength ink pixel.
      const int step = item.category == "icon" || rect.height() < 28 ? 2 : 4;
      // SingAlong's fill is a sibling drawn over the same glyphs, so the
      // body is only visible ink outside it. Inside, the ink-free frame still
      // holds the fill, and its antialiased edges read as a light background
      // under the body; measure the unsung part alone.
      QRectF coveredByFill;
      if (item.item->objectName() == "singAlongCurrent" && item.item->parentItem())
        for (auto sibling : item.item->parentItem()->childItems())
          if (sibling->objectName() == "singAlongFill" && sibling->isVisible() && sibling->opacity() > 0)
            coveredByFill = sibling->mapRectToScene(sibling->boundingRect());
      for (int y = y0; y < y1; y += step)
        for (int x = x0; x < x1; x += step) {
          if (!coveredByFill.isEmpty() &&
              coveredByFill.contains(QPointF((x + 0.5) * window->width() / background.width(),
                                             (y + 0.5) * window->height() / background.height()))) {
            ++maskedNonInkPixels;
            continue;
          }
          const QRgb bg = background.pixel(x, y);
          const QRgb visible = image.pixel(x, y);
          if (visible == bg) {
            ++maskedNonInkPixels;
            continue;
          }
          const int expectedChange = colorDistance(expectedInkPixel(item.color, bg,
                                                                     item.effectiveOpacity), bg);
          if (expectedChange < 8 || colorDistance(visible, bg) * 100 < expectedChange * 60) {
            ++maskedAttenuatedPixels;
            continue;
          }
          const double ratio = pixelContrast(item.color, bg, item.effectiveOpacity);
          ratios.append(ratio);
          if (ratio < smallest) {
            smallest = ratio;
            worstBackground = QColor::fromRgb(bg);
          }
        }
      if (ratios.isEmpty()) {
        ++skipped;
        continue;
      }
      std::sort(ratios.begin(), ratios.end());
      const double ratio = ratios[std::min<int>(ratios.size() - 1, int(ratios.size() * 0.05))];
      const QString role = roleOf(item.color, roles);
      const QString identity = itemName(item.item);
      QJsonObject row{{"item", identity}, {"kind", item.category},
                      {"ink", item.color.name(QColor::HexArgb)}, {"role", role},
                      {"background", worstBackground.name()}, {"ratio", ratio},
                      {"minimum", item.floor}, {"samples", ratios.size()}};
      const auto name = item.item->objectName();
      QString expectedRole;
      if (name == "lyricLabel") {
        const bool current = item.item->parentItem() &&
                             item.item->parentItem()->property("current").toBool();
        expectedRole = current ? "primary" : "muted";
      } else if (name == "singAlongCurrent") {
        // SingAlong.qml keeps the unsung body on onSurface. Its clipped
        // singAlongFill child paints primary only over the sung fraction.
        expectedRole = "text";
      } else if (name == "singAlongLine") {
        expectedRole = "muted";
      } else {
        for (auto ancestor = item.item->parentItem(); ancestor; ancestor = ancestor->parentItem())
          if (ancestor->objectName() == "singAlongFill") {
            expectedRole = "primary";
            break;
          }
      }
      if (!expectedRole.isEmpty()) row.insert("expectedRole", expectedRole);
      rows.append(row);
      ++measurements;
      if (ratio + 0.01 < item.floor)
        fail("contrast", context, QString("%1 ink=%2 role=%3 bg=%4 ratio=%5 floor=%6")
             .arg(identity, item.color.name(QColor::HexArgb), role, worstBackground.name())
             .arg(ratio, 0, 'f', 2).arg(item.floor, 0, 'f', 1));
      if (role == "tertiary" || role == "tertiaryText" || role == "tertiaryContainerText") {
        for (auto owner = item.item->parentItem(); owner; owner = owner->parentItem()) {
          if (!owner->inherits("QQuickControl")) continue;
          // MenuTokens.ListItemSelected* uses the secondary pair. A tertiary
          // control ink requires an explicit component token and design choice.
          fail("tertiary-role", context, identity + " role=" + role);
          break;
        }
      }
      // Two roles can resolve to one RGB: at high contrast over a dark surface
      // MCU drives onSurface and onSurfaceVariant both to T100, and the lookup
      // names whichever it met first. The ink passes when it is the expected
      // role's colour.
      if (!expectedRole.isEmpty() && role != expectedRole &&
          themeColor(window, expectedRole).rgb() != item.color.rgb())
        fail("lyric-role", context, identity + " role=" + role + " expected=" + expectedRole);
    }
    QJsonArray boundaryChecks;
    for (const auto &border : borders) {
      const auto owner = nearestControl(border.item);
      const QString identity = itemName(border.item) +
                               (owner ? " owner=" + itemName(owner) : QString());
      QJsonObject decision{{"item", identity}, {"boundaryRule", border.rule},
                           {"checked", false}, {"edge", border.fillEdge ? "fill" : "stroke"}};
      if (border.rule == "disabled-control") {
        ++skippedDisabledBoundary;
        boundaryChecks.append(decision);
        continue;
      }
      if (border.rule == "labelled-control") {
        ++skippedLabelledBoundary;
        boundaryChecks.append(decision);
        continue;
      }
      if (!requiredBoundary(border.rule)) {
        ++skippedOtherBoundary;
        boundaryChecks.append(decision);
        continue;
      }
      if (!border.fillEdge && !strokeDrawn(border, image, image)) {
        ++skippedNoVisibleEdge;
        decision.insert("skipReason", "no-rendered-stroke");
        boundaryChecks.append(decision);
        continue;
      }
      QList<double> ratios;
      QColor worstBackground;
      const auto &rect = border.rect;
      const int inset = std::max(3, int(std::ceil(border.stroke)) + 2);
      const int left = std::max(0, int(rect.left()) + inset);
      const int right = std::min(image.width() - 1, int(rect.right()) - inset);
      const int top = std::max(0, int(rect.top()) + inset);
      const int bottom = std::min(image.height() - 1, int(rect.bottom()) - inset);
      for (int step = 1; step <= 19 && left < right && top < bottom; ++step) {
        const int x = left + (right - left) * step / 20;
        const int y = top + (bottom - top) * step / 20;
        const QList<QPair<QPoint, QPoint>> probes = border.fillEdge
            ? QList<QPair<QPoint, QPoint>>{{{x, top}, {x, int(rect.top()) - 2}},
                                           {{x, bottom}, {x, int(rect.bottom()) + 2}},
                                           {{left, y}, {int(rect.left()) - 2, y}},
                                           {{right, y}, {int(rect.right()) + 2, y}}}
            : QList<QPair<QPoint, QPoint>>{{{x, top}, {x, top}},
                                           {{x, bottom}, {x, bottom}},
                                           {{left, y}, {left, y}},
                                           {{right, y}, {right, y}}};
        for (const auto &[inside, outside] : probes) {
          if (!image.rect().contains(outside) || !image.rect().contains(inside)) continue;
          const QRgb bg = image.pixel(outside);
          if (border.fillEdge && colorDistance(image.pixel(inside), bg) < 12) continue;
          const double ratio = pixelContrast(border.color, bg, border.effectiveOpacity);
          ratios.append(ratio);
          if (worstBackground.isValid() == false ||
              ratio < pixelContrast(border.color, worstBackground.rgb(), border.effectiveOpacity))
            worstBackground = QColor::fromRgb(bg);
        }
      }
      if (ratios.isEmpty()) {
        ++skippedNoVisibleEdge;
        decision.insert("skipReason", "no-rendered-edge");
        boundaryChecks.append(decision);
        continue;
      }
      std::sort(ratios.begin(), ratios.end());
      const double ratio = ratios[std::min<int>(ratios.size() - 1, int(ratios.size() * 0.05))];
      decision.insert("checked", true);
      decision.insert("ratio", ratio);
      boundaryChecks.append(decision);
      rows.append(QJsonObject{{"item", identity}, {"kind", "boundary"},
                              {"boundaryRule", border.rule},
                              {"ink", border.color.name(QColor::HexArgb)},
                              {"role", roleOf(border.color, roles)},
                              {"background", worstBackground.name()},
                              {"ratio", ratio}, {"minimum", 3.0},
                              {"samples", ratios.size()}});
      ++measurements;
      // WCAG 1.4.11 sets a 3:1 floor for the visible edge that identifies
      // a control. Sample just inside the stroke so its own pixels are absent.
      if (ratio + 0.01 < 3.0)
        fail("boundary", context, QString("%1 ink=%2 bg=%3 ratio=%4 floor=3.0")
             .arg(identity, border.color.name(QColor::HexArgb), worstBackground.name())
             .arg(ratio, 0, 'f', 2));
    }
    frame.insert("boundaryChecks", boundaryChecks);
    QJsonArray controlRoles;
    const QColor tertiary = themeColor(window, "tertiary");
    const QColor tertiaryContainer = themeColor(window, "tertiaryContainer");
    std::function<void(QQuickItem *)> inspectControls = [&](QQuickItem *item) {
      if (!item || !item->isVisible() || opacityOf(item) < 0.95) return;
      if (item->inherits("QQuickRectangle") && !visibleRect(item, window).isEmpty()) {
        if (auto owner = controlBackgroundOwner(item)) {
          const QColor fill = item->property("color").value<QColor>();
          const QString role = fill.rgb() == tertiary.rgb() ? "tertiary" :
                               fill.rgb() == tertiaryContainer.rgb() ? "tertiaryContainer" : "";
          if (!role.isEmpty()) {
            const QString identity = itemName(owner) + " fill=" + itemName(item);
            controlRoles.append(QJsonObject{{"item", identity}, {"role", role},
                                            {"color", fill.name()}});
            // MenuTokens selects the secondary pair. Controls with a tertiary
            // container need an explicit component token and design decision.
            fail("tertiary-role", context, identity + " role=" + role + " color=" + fill.name());
          }
        }
      }
      for (auto child : item->childItems()) inspectControls(child);
    };
    inspectControls(scope);
    frame.insert("controlRoles", controlRoles);
    frame.insert("measurements", rows);

    if (state == "menu") {
      QJsonArray roleRows;
      for (const auto &layout : {"artwork", "lyrics", "split", "singalong"}) {
        auto control = shown(scope, "immersiveLayout_" + QString::fromLatin1(layout));
        if (!control || !control->property("checked").toBool()) continue;
        auto container = shown(control, "menuItemContainer");
        const QColor actual = container ? container->property("color").value<QColor>() : QColor();
        const QColor expected = themeColor(window, "secondaryContainer");
        const QColor ink = control->property("ink").value<QColor>();
        const QColor expectedInk = themeColor(window, "secondaryContainerText");
        roleRows.append(QJsonObject{{"item", control->objectName()},
                                    {"container", actual.name()},
                                    {"expectedRole", "secondaryContainer"},
                                    {"ink", ink.name()},
                                    {"expectedInkRole", "secondaryContainerText"}});
        // MenuTokens.ListItemSelected* publishes the secondary pair, which
        // MMenuItem uses for a checked row and its ink.
        if (!actual.isValid() || actual.rgb() != expected.rgb() ||
            !ink.isValid() || ink.rgb() != expectedInk.rgb())
          fail("menu-role", context, control->objectName() + " container=" + actual.name() +
               " ink=" + ink.name() + " expected=secondary pair");
      }
      if (roleRows.size() != 1)
        fail("menu-role", context, QString("expected one checked layout row, found %1").arg(roleRows.size()));
      frame.insert("roleChecks", roleRows);
    }
    captures.append(frame);
    fprintf(stdout, "CAPTURE %s %lld ms %lld measurements\n", qPrintable(filename),
            static_cast<long long>(frameTime.elapsed()), static_cast<long long>(rows.size()));
    fflush(stdout);
  }

  void finish() {
    QFile report(directory + "/colour-audit.json");
    if (!report.open(QIODevice::WriteOnly))
      fail("report", "colour-audit", report.fileName());
    else
      report.write(QJsonDocument(QJsonObject{{"captures", captures},
                                            {"measurements", measurements},
                                            {"failures", failures},
                                            {"skippedInvisibleInk", skipped},
                                            {"skippedDisabledInk", skippedDisabledInk},
                                            {"skippedDisabledBoundary", skippedDisabledBoundary},
                                            {"skippedLabelledBoundary", skippedLabelledBoundary},
                                            {"skippedOtherBoundary", skippedOtherBoundary},
                                            {"skippedNoVisibleEdge", skippedNoVisibleEdge},
                                            {"maskedNonInkOrCoveredPixels", double(maskedNonInkPixels)},
                                            {"maskedAttenuatedPixels", double(maskedAttenuatedPixels)},
                                            {"runtimeMs", runtime.elapsed()}}).toJson());
    fprintf(stdout, "COLOUR AUDIT %lld captures %d measurements; SKIP disabled-ink=%d disabled-boundary=%d labelled-boundary=%d other-boundary=%d no-visible-edge=%d invisible-ink=%d non-ink-or-covered-pixels=%lld attenuated-pixels=%lld; %d failures %lld ms\n",
            static_cast<long long>(captures.size()), measurements, skippedDisabledInk,
            skippedDisabledBoundary, skippedLabelledBoundary, skippedOtherBoundary,
            skippedNoVisibleEdge, skipped, static_cast<long long>(maskedNonInkPixels),
            static_cast<long long>(maskedAttenuatedPixels), failures,
            static_cast<long long>(runtime.elapsed()));
    fflush(stdout);
    QCoreApplication::exit(failures ? 1 : 0);
  }
};

bool makeCover(const QString &folder, const QString &kind) {
  QDir().mkpath(folder);
  QImage image(480, 480, QImage::Format_RGB32);
  if (kind == "white") image.fill("#f8f8f3");
  else if (kind == "black") image.fill("#080a12");
  else if (kind == "warm") image.fill("#ec4421");
  else if (kind == "cool") image.fill("#1649d9");
  else if (kind == "grey") image.fill("#808080");
  else if (kind == "split") {
    image.fill("#f6d545");
    QPainter painter(&image);
    painter.fillRect(240, 0, 240, 480, QColor("#244ed1"));
    for (int y = 0; y < 480; y += 48) {
      painter.fillRect(0, y, 240, 16, QColor("#244ed1"));
      painter.fillRect(240, y + 24, 240, 16, QColor("#f6d545"));
    }
  } else {
    image.fill("#090d21");
    QPainter painter(&image);
    painter.setBrush(QColor("#fff6bf"));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPoint(320, 170), 35, 35);
  }
  if (!image.save(folder + "/cover.png"))
    return false;
  QFile lyrics(folder + "/01.lrc");
  if (!lyrics.open(QIODevice::WriteOnly))
    return false;
  lyrics.write("[00:00]The light arrives\n[00:08]Across the still water\n"
               "[00:16]A quiet moment\n[00:24]We move with the tide\n"
               "[00:32]The evening settles\n");
  lyrics.close();
  QProcess encode;
  encode.start("ffmpeg", {"-nostdin", "-v", "error", "-f", "lavfi", "-i",
                          "anullsrc=r=8000:cl=mono", "-t", "90", "-metadata",
                          "title=" + kind, "-metadata", "artist=Audit Artist", "-metadata",
                          "album=" + kind, folder + "/01.flac"});
  return encode.waitForFinished(20000) && encode.exitCode() == 0;
}
} // namespace

void runColourAuditTests(Backend *backend, QQuickWindow *window) {
  Audit audit{backend, window, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  audit.runtime.start();
  QDir().mkpath(audit.directory + "/music");
  backend->setMotion(false);
  backend->setVolume(0);
  backend->setAutoplay(false);
  backend->setPrepareNext(false);
  backend->setWatchMusicFolders(false);
  backend->setOnlineArtwork(false);
  backend->setLyricsFallback(false);
  backend->setArtworkAccent(true);
  backend->setAmbientBackdrop(true);
  QWindowSystemInterface::handleFocusWindowChanged(window);
  const QStringList covers{"white", "black", "warm", "cool", "grey", "split", "subject"};
  for (const auto &cover : covers)
    if (!makeCover(audit.directory + "/music/" + cover, cover))
      audit.fail("fixture", cover, "cover, lyrics or silent audio could not be made");
  if (audit.failures) return audit.finish();
  backend->importMusicFolder(QUrl::fromLocalFile(audit.directory + "/music"));
  if (!until([&] { return !backend->importingLocal(); }, 30000))
    audit.fail("fixture", "import", "local import did not finish");
  backend->library("files");
  if (!until([&] { return backend->results()->count() == covers.size(); }))
    audit.fail("fixture", "library", "seven real tracks were not indexed");
  if (audit.failures) return audit.finish();
  const QVariantList fixtureRows = backend->results()->rows;
  backend->enqueueItems(fixtureRows);
  backend->home();
  // Enqueueing announces itself in a snackbar. Dismiss the fixture notice
  // with the same swipe exercised by the product's snackbar stage, before
  // it can cover a control being measured.
  if (until([&] { return window->property("toastPending").toBool(); }, 2000)) {
    auto bar = shown(window->contentItem(), "toastBar");
    if (bar && until([&] { return bar->height() > 1; })) {
      const QPoint centre = bar->mapToScene(bar->boundingRect().center()).toPoint();
      const int reach = int(bar->width() / 2) + 30;
      QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, centre);
      for (int step = 1; step <= 6; ++step) {
        QTest::mouseMove(window, centre + QPoint(reach * step / 6, 0), 20);
        QTest::qWait(30);
      }
      QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier,
                          centre + QPoint(reach, 0));
    }
    if (!until([&] { return !window->property("toastPending").toBool(); }, 1500))
      audit.fail("state", "fixture", "queue snackbar did not dismiss");
  }
  auto accent = window->findChild<QQuickItem *>("accentSample");
  for (const auto &cover : covers) {
    int index = -1;
    for (int i = 0; i < fixtureRows.size(); ++i)
      if (fixtureRows[i].toMap().value("title").toString() == cover) index = i;
    if (index < 0) {
      audit.fail("fixture", cover, "track absent from queue");
      continue;
    }
    backend->playAt(index);
    if (!until([&] { return backend->playing() && backend->current().value("title") == cover &&
                            accent && accent->property("ready").toBool() &&
                            accent->property("source").toUrl().toString() ==
                                backend->current().value("art").toString(); })) {
      audit.fail("state", cover, "artwork seed did not settle on the current cover");
      continue;
    }
    // fetchLyrics reads the track's real 01.lrc sidecar. Importing it would
    // raise a five-second snackbar over the controls during measurements.
    backend->fetchLyrics();
    if (!until([&] { return backend->lyricLines().size() == 5; }))
      audit.fail("state", cover, "timed sidecar lyrics did not load");
    backend->seek(17000);
    for (const auto &theme : {QStringLiteral("dark"), QStringLiteral("light")}) {
      backend->setTheme(theme);
      for (const auto &contrast : {QStringLiteral("standard"), QStringLiteral("high")}) {
        backend->setColorContrast(contrast == "high" ? 1.0 : 0.0);
        for (const int width : {1440, 600}) {
          const QString context = QString("%1 %2 %3 %4").arg(cover, theme, contrast).arg(width);
          window->showNormal();
          window->setMinimumSize({0, 0});
          window->setMaximumSize({16777215, 16777215});
          const int height = width == 1440 ? 900 : 800;
          window->resize(width, height);
          if (!until([&] {
                auto navigation = shown(window->contentItem(), "navigationBar");
                return window->width() == width && window->height() == height &&
                       window->contentItem()->width() == width && !backend->busy() &&
                       navigation && navigation->mapRectToScene(navigation->boundingRect()).right() <= width + 1;
              }))
            audit.fail("state", context, "main window did not settle at requested size");
          if (!until([&] {
                auto backdrop = shown(window->contentItem(), "windowBackdrop");
                auto art = shown(backdrop, "ambientArt");
                return backdrop && backdrop->property("active").toBool() &&
                       art && art->property("ready").toBool() &&
                       art->property("source").toUrl().toString() ==
                           backend->current().value("art").toString();
              }))
            audit.fail("state", context, "window artwork backdrop did not become ready");
          audit.capture(cover, theme, contrast, width, "main");
          window->setProperty("side", "now");
          if (!until([&] {
                if (window->property("sheetMode").toBool()) {
                  auto sheet = shown(window->contentItem(), "panelSheet");
                  auto title = shown(sheet, "sidePanelTitle");
                  return sheet && sheet->property("open").toBool() &&
                         title && title->property("text") == "Now playing" &&
                         visibleRect(title, window).width() > 0;
                }
                auto panel = shown(window->contentItem(), "sidePanel");
                auto backdrop = shown(panel, "nowBackdrop");
                auto art = shown(backdrop, "ambientArt");
                return panel && panel->width() > 0 && backdrop &&
                       backdrop->property("active").toBool() && art &&
                       art->property("ready").toBool();
              }))
            audit.fail("state", context, "now-playing supporting pane did not open");
          audit.capture(cover, theme, contrast, width, "pane");
          window->setProperty("side", "");
          if (window->property("sheetMode").toBool() &&
              !until([&] { auto sheet = window->findChild<QQuickItem *>("panelSheet");
                            return sheet && !sheet->property("open").toBool() && !sheet->isVisible(); }))
            audit.fail("state", context, "now-playing sheet did not close");
          QTest::keyClick(window, Qt::Key_F11);
          if (!until([&] { auto player = shown(window->contentItem(), "immersivePlayer");
                            return window->property("immersive").toBool() && player &&
                                   player->opacity() >= 0.99 && player->property("ready").toBool(); })) {
            audit.fail("state", context, "immersive player did not open");
            continue;
          }
          window->showNormal();
          window->resize(width, height);
          QWindowSystemInterface::handleFocusWindowChanged(window);
          if (!until([&] {
                auto body = shown(window->contentItem(), "immersiveBody");
                auto button = shown(window->contentItem(), "immersiveLayoutButton");
                return window->width() == width && window->height() == height &&
                       window->contentItem()->width() == width && body && button &&
                       std::abs(body->width() - (width - 2 * (width < 600 ? 16 : 24))) < 2 &&
                       visibleRect(button, window).width() > 0;
              }))
            audit.fail("state", context, "immersive layout did not settle at requested size");
          auto player = shown(window->contentItem(), "immersivePlayer");
          if (player && player->property("displayedLayout") != "artwork")
            audit.choose("artwork", context);
          if (player && player->property("displayedLayout") == "artwork")
            audit.capture(cover, theme, contrast, width, "artwork");
          else audit.fail("state", context, "artwork layout unavailable for capture");
          for (const auto &layout : {QStringLiteral("lyrics"), QStringLiteral("split"),
                                    QStringLiteral("singalong")})
            if (audit.choose(layout, context))
              audit.capture(cover, theme, contrast, width, layout);
          auto menuOpen = [&] { auto menu = window->findChild<QObject *>("immersiveLayoutMenu");
                              return menu && menu->property("visible").toBool(); };
          if (audit.click("immersiveLayoutButton", context) && !until(menuOpen, 1200))
            audit.click("immersiveLayoutButton", context);
          if (until(menuOpen, 3000))
            audit.capture(cover, theme, contrast, width, "menu");
          else audit.fail("state", context, "layout menu unavailable for capture");
          QTest::keyClick(window, Qt::Key_Escape);
          QTest::keyClick(window, Qt::Key_F11);
          if (!until([&] { return !window->property("immersive").toBool(); }))
            audit.fail("state", context, "immersive player did not close");
        }
      }
    }
  }
  audit.finish();
}
