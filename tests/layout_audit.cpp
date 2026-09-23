#include "tour.h"
#include "uitest.h"
#include "backend.h"

#include <QDir>
#include <QAccessible>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QStringList>
#include <QTest>
#include <algorithm>

namespace {
struct Target {
  QQuickItem *item;
  QQuickItem *owner;
  QString type;
};

QString accessibleName(QQuickItem *item) {
  if (!item)
    return {};
  // Qt Quick exposes the attached Accessible.name through QAccessible.
  // QObject::property and QQmlProperty do not read this attached value.
  if (auto interface = QAccessible::queryAccessibleInterface(item)) {
    const auto name = interface->text(QAccessible::Name).trimmed();
    if (!name.isEmpty())
      return name;
    // Qt 6.11 hides the direct name of a password edit. labelledBy exposes
    // the field's label as a Label relation when queried from the field;
    // Labelled is the reverse relation on the label. Read the same direction
    // as an assistive client starting at the control (QAccessibleQuickItem::relations).
    for (const auto &[related, relation] : interface->relations(QAccessible::Label)) {
      if (relation == QAccessible::Label && related)
        return related->text(QAccessible::Name).trimmed();
    }
  }
  return item->property("accessibleName").toString().trimmed();
}

QString label(QQuickItem *item) {
  if (!item)
    return {};
  if (!item->objectName().isEmpty())
    return item->objectName();
  auto name = accessibleName(item);
  if (name.isEmpty())
    name = item->property("text").toString().trimmed();
  return name.isEmpty() ? QStringLiteral("<unnamed>") : name.left(80);
}

QString typeName(QQuickItem *item) {
  return QString::fromLatin1(item->metaObject()->className());
}

QString rectangle(const QRectF &rect) {
  return QString("[%1,%2 %3x%4]")
      .arg(rect.x(), 0, 'f', 1).arg(rect.y(), 0, 'f', 1)
      .arg(rect.width(), 0, 'f', 1).arg(rect.height(), 0, 'f', 1);
}

bool tabEnabled(QQuickItem *item) {
  const int policy = item->property("focusPolicy").toInt();
  return item->activeFocusOnTab() || (policy & Qt::TabFocus);
}

void collect(QQuickItem *item, QList<Target> &targets, QList<QQuickItem *> &texts,
             QList<QQuickItem *> &tabItems, QSet<QQuickItem *> &seen) {
  if (!item->isVisible() || !item->isEnabled())
    return;
  if (item->inherits("QQuickText") && item->property("truncated").isValid())
    texts.append(item);
  if (tabEnabled(item))
    tabItems.append(item);

  // Scroll bars duplicate wheel, key and flick navigation, so their thin tracks need no separate target check.
  const bool scrollBar = item->inherits("QQuickScrollBar");
  // MouseArea is the hit rectangle; its parent supplies the accessible label.
  // A TapHandler has no visual rectangle, so its parent is the hit rectangle.
  if (!scrollBar && item->inherits("QQuickMouseArea")) {
    auto owner = item->parentItem();
    if (owner && !owner->inherits("QQuickControl") && !seen.contains(item)) {
      targets.append({item, owner, typeName(item)});
      seen.insert(item);
    }
  } else if (!scrollBar && (tabEnabled(item) ||
             (item->inherits("QQuickControl") &&
              (item->metaObject()->indexOfSignal("clicked()") >= 0 ||
               item->metaObject()->indexOfSignal("toggled()") >= 0 ||
               item->property("pressed").isValid() ||
               item->property("value").isValid())))) {
    if (!seen.contains(item)) {
      targets.append({item, item, typeName(item)});
      seen.insert(item);
    }
  }
  if (!scrollBar) {
    for (auto child : item->children()) {
      if (!QString::fromLatin1(child->metaObject()->className()).contains("TapHandler"))
        continue;
      if (child->property("enabled").isValid() && !child->property("enabled").toBool())
        continue;
      if (!seen.contains(item)) {
        targets.append({item, item, "TapHandler"});
        seen.insert(item);
      }
    }
  }
  for (auto child : item->childItems())
    collect(child, targets, texts, tabItems, seen);
}

bool insideFlickableContent(QQuickItem *item) {
  for (auto parent = item->parentItem(); parent; parent = parent->parentItem())
    if (parent->inherits("QQuickFlickable"))
      return true;
  return false;
}

QRectF bounds(QQuickItem *item, QQuickWindow *window) {
  QRectF limit(QPointF(0, 0), QSizeF(window->width(), window->height()));
  for (auto parent = item->parentItem(); parent; parent = parent->parentItem()) {
    if (parent->clip())
      limit = limit.intersected(parent->mapRectToScene(parent->boundingRect()));
  }
  return limit;
}

// Which ancestors clip an item, and where the nearest scroller stands, so a
// focus failure says what hides the control rather than only that it is hidden.
QString clipChain(QQuickItem *item) {
  QStringList parts;
  bool scroller = false;
  for (auto parent = item->parentItem(); parent; parent = parent->parentItem()) {
    const auto name = parent->objectName().isEmpty() ? QString(parent->metaObject()->className())
                                                     : parent->objectName();
    if (!scroller && parent->property("contentHeight").isValid() &&
        parent->property("contentY").isValid() && parent->property("flicking").isValid()) {
      scroller = true;
      parts << QString("view %1 y=%2 content=%3 height=%4")
                   .arg(name).arg(parent->property("contentY").toDouble(), 0, 'f', 0)
                   .arg(parent->property("contentHeight").toDouble(), 0, 'f', 0)
                   .arg(parent->height(), 0, 'f', 0);
    }
    if (parent->clip()) {
      const auto r = parent->mapRectToScene(parent->boundingRect());
      parts << QString("clip %1 [%2,%3 %4x%5]").arg(name).arg(r.x(), 0, 'f', 0).arg(r.y(), 0, 'f', 0)
                   .arg(r.width(), 0, 'f', 0).arg(r.height(), 0, 'f', 0);
    }
  }
  return parts.join("; ");
}

bool outside(const QRectF &rect, const QRectF &limit) {
  return rect.left() < limit.left() - 1 || rect.top() < limit.top() - 1 ||
         rect.right() > limit.right() + 1 || rect.bottom() > limit.bottom() + 1;
}

bool focusCovers(QQuickItem *focus, QQuickItem *candidate) {
  for (auto item = focus; item; item = item->parentItem())
    if (item == candidate)
      return true;
  return false;
}

QQuickItem *popupFrame(QObject *popup) {
  auto item = popup->property("contentItem").value<QQuickItem *>();
  // A modal popup blocks the controls behind its scrim. Its PopupItem holds
  // the header, body and footer, so the audit follows that focus scope.
  for (auto parent = item ? item->parentItem() : nullptr; parent; parent = parent->parentItem()) {
    if (typeName(parent).contains("PopupItem"))
      return parent;
  }
  return item;
}

QObject *frontModal(QQuickWindow *window) {
  QObject *front = nullptr;
  QQuickItem *frontFrame = nullptr;
  int frontOrder = -1;
  for (auto popup : window->findChildren<QObject *>()) {
    if (!popup->inherits("QQuickPopup") || !popup->property("visible").toBool() ||
        !popup->property("modal").toBool())
      continue;
    auto frame = popupFrame(popup);
    auto parent = frame ? frame->parentItem() : nullptr;
    const int order = parent ? parent->childItems().indexOf(frame) : -1;
    // Popups opened later sit later among the overlay's children. QObject
    // creation order can instead select a dialog still open behind them.
    if (!front || (frame && frontFrame &&
                   (frame->z() > frontFrame->z() ||
                    (frame->z() == frontFrame->z() && order >= frontOrder)))) {
      front = popup;
      frontFrame = frame;
      frontOrder = order;
    }
  }
  if (front)
    return front;
  // MBottomSheet is an Item, not a Popup. Its scrim blocks the page too.
  auto sheet = window->findChild<QQuickItem *>("panelSheet");
  return sheet && sheet->property("modal").toBool() && sheet->property("open").toBool() &&
                 sheet->isVisible() ? sheet : nullptr;
}

QQuickItem *auditRoot(QQuickWindow *window, QObject *popup) {
  if (!popup)
    return window->contentItem();
  if (popup->objectName() == "panelSheet")
    return qobject_cast<QQuickItem *>(popup);
  auto item = popupFrame(popup);
  return item ? item : window->contentItem();
}

QString dialogBodyFailure(QObject *popup, QQuickItem *popupItem) {
  if (!popup || !popup->inherits("QQuickDialog"))
    return {};
  auto body = popup->property("contentItem").value<QQuickItem *>();
  if (!body || !popupItem)
    return {};
  auto header = popup->property("header").value<QQuickItem *>();
  const auto bodyRect = body->mapRectToScene(body->boundingRect());
  const auto popupRect = popupItem->mapRectToScene(popupItem->boundingRect());
  const bool hasHeader = header && header->isVisible() && header->height() > 0;
  const auto headerRect = hasHeader ? header->mapRectToScene(header->boundingRect()) : QRectF{};
  const auto left = popupRect.left() + popup->property("leftPadding").toReal();
  const auto right = popupRect.right() - popup->property("rightPadding").toReal();
  const auto bottom = popupRect.bottom() - popup->property("bottomPadding").toReal();
  if ((!hasHeader || bodyRect.top() >= headerRect.bottom() - 1) &&
      bodyRect.left() >= left - 1 && bodyRect.right() <= right + 1 &&
      bodyRect.bottom() <= bottom + 1)
    return {};
  return QString("%1 body %2 header %3 popup %4 inset [%5,%6,%7]")
      .arg(popup->objectName(), rectangle(bodyRect),
           hasHeader ? rectangle(headerRect) : QStringLiteral("<none>"),
           rectangle(popupRect))
      .arg(left, 0, 'f', 1).arg(right, 0, 'f', 1).arg(bottom, 0, 'f', 1);
}

struct Audit {
  Backend *backend;
  QString directory;
  QJsonArray entries;
  int overflow = 0;
  int unnamed = 0;
  int unreached = 0;
  int focus = 0;
  int dialogBody = 0;
  int truncated = 0;
  int smallTargets = 0;

  void failure(const QString &kind, const QString &detail) {
    fprintf(stdout, "FAIL %s %s\n", qPrintable(kind), qPrintable(detail));
    fflush(stdout);
  }

  bool capture(QQuickWindow *window, const QString &screen, int stop) {
    const QSize original = window->size();
    const bool mini = window->objectName() == "miniPlayerWindow";
    const QList<QSize> sizes{{1440, 900}, {1024, 768}, {840, 800}, {600, 800}, {480, 620}};
    bool saved = true;
    for (const auto &size : sizes) {
      for (const QString &theme : {QStringLiteral("dark"), QStringLiteral("light")}) {
        backend->setTheme(theme);
        if (!mini)
          window->resize(size);
        QTest::qWait(45);
        const QString file = QString("%1-%2-%3-%4.png")
                                 .arg(size.width()).arg(theme)
                                 .arg(stop, 2, 10, QChar('0')).arg(screen);
        const bool shot = window->grabWindow().save(directory + "/" + file);
        saved &= shot;
        if (!shot)
          failure("capture", file);

        QList<Target> targets;
        QList<QQuickItem *> texts, tabItems;
        QSet<QQuickItem *> seen;
        auto popup = frontModal(window);
        auto scope = auditRoot(window, popup);
        collect(scope, targets, texts, tabItems, seen);
        QJsonArray overflowRows, unnamedRows, unreachedRows, focusRows, truncatedRows, smallRows, dialogRows;
        const QString context = QString("%1 %2x%3 %4")
                                    .arg(screen).arg(size.width()).arg(size.height()).arg(theme);
        const auto bodyFailure = dialogBodyFailure(popup, scope);
        if (!bodyFailure.isEmpty()) {
          dialogRows.append(context + " " + bodyFailure);
          failure("dialog-body", context + " " + bodyFailure);
          ++dialogBody;
        }
        for (const auto &target : targets) {
          const auto rect = target.item->mapRectToScene(target.item->boundingRect());
          const auto detail = QString("%1 %2 %3 %4")
                                  .arg(context, label(target.owner), target.type, rectangle(rect));
          if (!insideFlickableContent(target.item) && outside(rect, bounds(target.item, window))) {
            overflowRows.append(detail);
            failure("overflow", detail);
            ++overflow;
          }
          if (accessibleName(target.owner).isEmpty() &&
              target.owner->property("text").toString().trimmed().isEmpty()) {
            unnamedRows.append(detail);
            failure("unnamed", detail);
            ++unnamed;
          }
          if (target.item->width() < 24 || target.item->height() < 24) {
            smallRows.append(detail);
            fprintf(stdout, "WARN target %s\n", qPrintable(detail));
            ++smallTargets;
          }
        }
        for (auto text : texts) {
          if (!text->property("truncated").toBool())
            continue;
          const auto detail = QString("%1 %2 %3")
                                  .arg(context, label(text), rectangle(text->mapRectToScene(text->boundingRect())));
          truncatedRows.append(detail);
          fprintf(stdout, "WARN truncated %s\n", qPrintable(detail));
          ++truncated;
        }

        // Main.qml's revealFocus scrolls lists as Tab moves, so a delegate
        // seen early in the walk can be destroyed by the time it is compared.
        // Guarded pointers let the audit skip those rather than read freed
        // memory.
        QList<QPointer<QQuickItem>> tabGuards(tabItems.cbegin(), tabItems.cend());
        QList<QPointer<QQuickItem>> visited;
        if (scope == window->contentItem())
          scope->forceActiveFocus(Qt::TabFocusReason);
        const int cap = std::min(600, std::max(60, int(tabItems.size()) * 3));
        for (int step = 0; step < cap; ++step) {
          QTest::keyClick(window, Qt::Key_Tab);
          // Main.qml's revealFocus runs through Qt.callLater once the focus
          // change has settled; let it finish, as the next frame would for a
          // person, before judging what is on screen.
          QCoreApplication::processEvents();
          auto current = window->activeFocusItem();
          if (!current || std::any_of(visited.cbegin(), visited.cend(),
                                      [current](const auto &seen) { return seen == current; }))
            break;
          visited.append(current);
          // A reveal that repositions a ListView can finish on the view's
          // next layout pass. Give a control that starts out hidden up to
          // 200 ms to arrive, and judge and report where it settles.
          QPointer<QQuickItem> settling = current;
          for (int wait = 0; wait < 12 && settling &&
                             outside(settling->mapRectToScene(settling->boundingRect()), bounds(settling, window));
               ++wait)
            QTest::qWait(16);
          if (!settling)
            continue;
          const auto focusRect = current->mapRectToScene(current->boundingRect());
          const auto detail = QString("%1 %2 %3").arg(label(current), typeName(current), rectangle(focusRect));
          focusRows.append(detail);
          const auto shownRect = bounds(current, window);
          if (!current->isVisible() || current->width() <= 0 || current->height() <= 0 ||
              outside(focusRect, shownRect)) {
            failure("focus", context + " " + detail + " shown " + rectangle(shownRect) +
                                 " (" + clipChain(current) + ")");
            ++focus;
          }
        }
        for (const auto &guard : tabGuards) {
          auto item = guard.data();
          if (!item)
            continue;
          bool reached = false;
          for (const auto &focusItem : visited)
            if (focusItem && focusCovers(focusItem, item)) {
              reached = true;
              break;
            }
          if (!reached) {
            const auto detail = QString("%1 %2 %3")
                                    .arg(context, label(item), typeName(item));
            unreachedRows.append(detail);
            failure("unreached", detail);
            ++unreached;
          }
        }
        entries.append(QJsonObject{{"screen", screen},
                                   {"size", QString("%1x%2").arg(size.width()).arg(size.height())},
                                   {"window_size", QString("%1x%2").arg(window->width()).arg(window->height())},
                                   {"theme", theme}, {"screenshot", file},
                                   {"overflow", overflowRows}, {"unnamed", unnamedRows},
                                   {"unreached", unreachedRows}, {"dialog_body", dialogRows},
                                   {"focus_chain", focusRows},
                                   {"truncated", truncatedRows}, {"small_targets", smallRows}});
      }
    }
    backend->setTheme("dark");
    if (!mini)
      window->resize(original);
    QTest::qWait(45);
    return saved;
  }

  int finish() {
    QFile file(directory + "/layout-audit.json");
    if (!file.open(QIODevice::WriteOnly)) {
      failure("report", file.fileName());
      ++focus;
    } else {
      file.write(QJsonDocument(QJsonObject{{"captures", entries}}).toJson());
    }
    fprintf(stdout,
            "LAYOUT AUDIT %lld captures; FAIL overflow=%d unnamed=%d unreached=%d focus=%d dialog-body=%d; WARN truncated=%d small-targets=%d\n",
            static_cast<long long>(entries.size()), overflow, unnamed, unreached, focus, dialogBody,
            truncated, smallTargets);
    fflush(stdout);
    return overflow + unnamed + unreached + focus + dialogBody;
  }
};
} // namespace

void runLayoutAuditTests(Backend *backend, QQuickWindow *window) {
  QAccessible::setActive(true);
  Audit audit{backend, qEnvironmentVariable("SUNG_TEST_OUTPUT"), {}};
  QDir().mkpath(audit.directory);
  runGuidedTour(backend, window,
                [&](QQuickWindow *target, const QString &name, int stop) {
                  return audit.capture(target, name, stop);
                },
                [&] { return audit.finish(); }, false);
}
