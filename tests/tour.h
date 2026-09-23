#pragma once

#include <QString>
#include <functional>

class Backend;
class QQuickWindow;

using TourCapture = std::function<bool(QQuickWindow *, const QString &, int)>;
using TourFinish = std::function<int()>;

void runGuidedTour(Backend *, QQuickWindow *, const TourCapture &,
                   const TourFinish &, bool motion);
