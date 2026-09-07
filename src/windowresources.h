#pragma once
#include <QObject>
#include <QQuickWindow>
#include <QTimer>

// Keep render resources warm for quick toggles. After a window has been hidden
// for 30 seconds, let Qt release its recreatable scene and graphics resources.
// QML objects, playback, navigation and control state remain alive throughout.
class WindowResources : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  Q_INVOKABLE void manage(QQuickWindow *window) {
    if (!window || window->findChild<QTimer *>("renderResourceIdleTimer", Qt::FindDirectChildrenOnly))
      return;
    auto timer = new QTimer(window);
    timer->setObjectName("renderResourceIdleTimer");
    timer->setSingleShot(true);
    timer->setInterval(30000);
    connect(timer, &QTimer::timeout, window, [window] {
      if (window->isVisible() && window->visibility() != QWindow::Minimized)
        return;
      window->setPersistentSceneGraph(false);
      window->setPersistentGraphics(false);
      window->releaseResources();
    });
    const auto visibilityChanged = [window, timer] {
      if (!window->isVisible() || window->visibility() == QWindow::Minimized) {
        timer->start();
      } else {
        timer->stop();
        window->setPersistentGraphics(true);
        window->setPersistentSceneGraph(true);
      }
    };
    connect(window, &QWindow::visibilityChanged, window, visibilityChanged);
    visibilityChanged();
  }
};
