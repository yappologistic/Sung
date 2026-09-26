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
    //
    // A menu is as wide as its widest item, held between
    // DropdownMenuItemDefaultMinWidth 112dp and DropdownMenuItemDefaultMaxWidth
    // 280dp (Menu.kt:2386-2389). A caller that sets a width, such as a
    // dropdown matching its field, keeps it.
    //
    // It keeps MenuHorizontalMargin 8dp and MenuVerticalMargin 48dp from the
    // window's edges (Menu.kt:2324, 2371), so a long menu never runs into the
    // edge of the window.
    readonly property real minimumWidth: 112
    readonly property real maximumWidth: 280
    property real widestItem: 0
    // An item builds its label only once it is first shown, so the menu reads
    // the text itself, in the item's own styles, before it opens.
    TextMetrics {
        id: labelMetrics
        font.family: Theme.fontFamily; font.pixelSize: Theme.bodyLarge
        font.weight: Theme.weightFor(false, false, Theme.bodyLarge, "bodyLarge")
        font.letterSpacing: Theme.trackingFor(Theme.bodyLarge, false, "bodyLarge", false)
    }
    TextMetrics {
        id: shortcutMetrics
        font.family: Theme.fontFamily; font.pixelSize: Theme.labelSmall
        font.weight: Theme.weightFor(false, true, Theme.labelSmall, "labelSmall")
        font.letterSpacing: Theme.trackingFor(Theme.labelSmall, true, "labelSmall", false)
    }
    function measure() {
        let widest = 0
        for (let i = 0; i < menu.count; ++i) {
            const item = menu.itemAt(i)
            if (!item || !item.visible || item.leadingSpace === undefined) continue
            labelMetrics.text = item.text
            let w = item.leftPadding + item.leadingSpace + labelMetrics.advanceWidth + item.rightPadding
            if (item.shortcut) {
                shortcutMetrics.text = item.shortcut
                w += item.shortcutGap + shortcutMetrics.advanceWidth
            }
            widest = Math.max(widest, w)
        }
        widestItem = Math.ceil(widest)
    }
    // Opens the menu under its anchor, its end lined up with the anchor's
    // end, as a menu dropped from an overflow button is. The menu is measured
    // and placed as it is about to show, because an item only reports itself
    // visible once its menu is open; the menu's margins then keep it inside
    // the window.
    property bool alignToAnchorEnd: false
    function openUnder(anchor) {
        anchorItem = anchor
        alignToAnchorEnd = true
        open()
    }
    onAboutToShow: {
        measure()
        if (alignToAnchorEnd && anchorItem && parent) {
            const p = anchorItem.mapToItem(parent, anchorItem.width, anchorItem.height)
            x = p.x - width
            y = p.y
        }
    }
    width: Math.max(minimumWidth, Math.min(maximumWidth, widestItem))
    padding: 0; topPadding: 2; bottomPadding: 2
    leftMargin: 8; rightMargin: 8; topMargin: 48; bottomMargin: 48
    height: Math.min(implicitHeight, Math.max(100, (Overlay.overlay ? Overlay.overlay.height : 600)-topMargin-bottomMargin))

    // The item the menu belongs to. Menus opened with popup(item, ...) already
    // have it as their parent; one placed by hand names it here.
    property Item anchorItem: null
    // Menu.kt:2216-2242, calculateTransformOrigin: the menu grows from the
    // side it shares with its anchor. It is left or right when it clears the
    // anchor sideways, and otherwise the middle of the span the two share;
    // the same rule settles top or bottom. Qt takes one of nine points
    // rather than a fraction, so the fraction is read to its nearest third.
    function pivotFor(ax, ay, aw, ah) {
        function third(f) { return f < 1/3 ? 0 : f > 2/3 ? 2 : 1 }
        function axis(start, size, anchorStart, anchorSize) {
            if (start >= anchorStart+anchorSize) return 0
            if (start+size <= anchorStart) return 2
            if (size <= 0) return 0
            return third(((Math.max(anchorStart,start)+Math.min(anchorStart+anchorSize,start+size))/2-start)/size)
        }
        const origins = [[Item.TopLeft, Item.Top, Item.TopRight],
                         [Item.Left, Item.Center, Item.Right],
                         [Item.BottomLeft, Item.Bottom, Item.BottomRight]]
        return origins[axis(y, height, ay, ah)][axis(x, width, ax, aw)]
    }
    transformOrigin: {
        const anchor = anchorItem || parent
        if (!anchor || !parent) return Item.Top
        const r = anchor === parent ? Qt.rect(0, 0, anchor.width, anchor.height)
                                    : anchor.mapToItem(parent, 0, 0, anchor.width, anchor.height)
        return pivotFor(r.x, r.y, r.width, r.height)
    }
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
    // Menu.kt:1829-1851 opens and closes the menu on one transition: its
    // scale between ClosedScaleTarget 0.8 and 1 on FastSpatial, and its alpha
    // between 0 and 1 on FastEffects (Menu.kt:2402-2405). Spatial because the
    // menu grows out of its anchor; effects for the alpha so it cannot pass
    // through a wrong opacity.
    enter: Transition {
        NumberAnimation { property: "scale"; from: 0.8; to: 1; duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial }
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastEffects }
    }
    exit: Transition {
        NumberAnimation { property: "scale"; to: 0.8; duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial }
        NumberAnimation { property: "opacity"; to: 0; duration: Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastEffects }
    }
}
