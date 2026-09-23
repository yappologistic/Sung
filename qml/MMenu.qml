import QtQuick
import QtQuick.Controls
// Material 3 menu.
//
// MenuTokens puts the container on surfaceContainer and lifts it two levels.
// It has one colour, and it is not a palette accent: a menu that took the
// tertiary container followed the source hue around the wheel and landed on a
// colour the rest of the window had never heard of, which over a warm cover
// came out green.
//
// One thing beyond the standard menu. A segmented menu draws its items as one
// run on a group container of their own, which is the shape Material gives a
// menu that is a choice between peers rather than a list of actions.
Menu {
    id: menu

    property bool segmented: false

    // The items learn where they sit in the run from the menu, because a
    // MenuItem cannot see its own place in one.
    function restyle() {
        for (let i = 0; i < menu.count; ++i) {
            const item = menu.itemAt(i)
            if (!item || item.segmented === undefined) continue
            item.segmented = menu.segmented
            item.firstInRun = i === 0
            item.lastInRun = i === menu.count-1
        }
    }
    onCountChanged: Qt.callLater(restyle)
    Component.onCompleted: restyle()

    width: 244; padding: segmented ? 4 : 8; margins: 12
    height: Math.min(implicitHeight, Math.max(100, (Overlay.overlay ? Overlay.overlay.height : 600)-24))
    delegate: MMenuItem {}
    contentItem: ListView {
        implicitHeight: contentHeight
        spacing: menu.segmented ? Theme.listSegmentedGap : 0
        model: menu.contentModel; currentIndex: menu.currentIndex
        clip: true; boundsBehavior: Flickable.StopAtBounds
        highlightMoveDuration: 0
        ScrollBar.vertical: MScrollBar { objectName: "menuScrollBar" }
    }
    background: Rectangle {
        // MenuTokens.ContainerColor for a list; a run sits on the group
        // container, SegmentedMenuTokens.GroupContainerColor. The lift is
        // what sets a menu off from the window: no menu token has an outline.
        color: menu.segmented ? Theme.surfaceLow : Theme.container
        radius: menu.segmented ? Theme.shapeLarge : Theme.shapeLargeIncreased
        MElevation { anchors.fill: parent; radius: parent.radius; level: 2 }
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.enterDuration; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.exitDuration; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
}
