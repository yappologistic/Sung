import QtQuick
import QtQuick.Controls
// Material 3 expressive menu: one DropdownMenuGroup in a DropdownMenuPopup
// (Menu.kt:176-300). Its items use the group's first, middle and last shapes.
//
// MenuDefaults.groupStandardContainerColor puts the group on
// StandardMenuTokens.ContainerColor (surfaceContainerLow), with the
// MenuTokens.ContainerElevation Level2 shadow.
// It has one colour, and it is not a palette accent: a menu that took the
// tertiary container followed the source hue around the wheel and landed on a
// colour the rest of the window had never heard of, which over a warm cover
// came out green.
//
Menu {
    id: menu

    // The items learn where they sit in the group from the menu, because a
    // MenuItem cannot see its own place in one.
    function restyle() {
        for (let i = 0; i < menu.count; ++i) {
            const item = menu.itemAt(i)
            if (!item || item.firstInRun === undefined) continue
            item.firstInRun = i === 0
            item.lastInRun = i === menu.count-1
        }
    }
    onCountChanged: Qt.callLater(restyle)
    Component.onCompleted: restyle()

    // MenuDefaults.DropdownMenuGroupContentPadding is 0dp horizontally and
    // 2dp vertically (MenuDefaults.kt:886, Menu.kt:2378-2379). Each item
    // supplies its own 4dp horizontal Surface inset (Menu.kt:2038-2046).
    width: 244; padding: 0; topPadding: 2; bottomPadding: 2; margins: 12
    height: Math.min(implicitHeight, Math.max(100, (Overlay.overlay ? Overlay.overlay.height : 600)-24))
    delegate: MMenuItem {}
    contentItem: ListView {
        implicitHeight: contentHeight
        spacing: 0
        model: menu.contentModel; currentIndex: menu.currentIndex
        clip: true; boundsBehavior: Flickable.StopAtBounds
        highlightMoveDuration: 0
        ScrollBar.vertical: MScrollBar { objectName: "menuScrollBar" }
    }
    background: Rectangle {
        // MenuDefaults.groupShape(0, 1) uses SegmentedMenuTokens.ContainerShape
        // CornerLarge (16dp); StandardMenuTokens.ContainerColor is the neutral
        // group surface. MenuTokens.ContainerElevation is Level2.
        color: Theme.surfaceLow
        radius: Theme.shapeLarge
        MElevation { anchors.fill: parent; radius: parent.radius; level: 2 }
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.enterDuration; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.exitDuration; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
}
