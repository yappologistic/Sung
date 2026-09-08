import QtQuick
import QtQuick.Controls
Menu {
    width: 244; padding: 8
    height: Math.min(implicitHeight, Math.max(100, (Overlay.overlay ? Overlay.overlay.height : 600)-24))
    delegate: MMenuItem {}
    background: Rectangle { color: Theme.high; radius: 20; border.color: Theme.outline }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.enterDuration; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.enterCurve } }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.exitDuration; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.exitCurve } }
}
