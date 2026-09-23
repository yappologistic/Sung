import QtQuick
import QtQuick.Controls

// Material 3 bottom sheet.
//
// Material puts a supporting pane in a bottom sheet once the window is too
// narrow to set one beside the content. The sheet sits on surfaceContainerLow,
// rounds only its top corners at the extra large step, carries a 32 by 4dp drag
// handle in onSurfaceVariant, and rests one level off the page. Dragging the
// handle down past a third of the sheet closes it.
Item {
    id: sheet

    property bool open: false
    property real peek: 0.6
    // A modal sheet takes the screen over: Material scrims what it covers and
    // a press on that scrim puts the sheet away. A standard one leaves the
    // content behind it live.
    property bool modal: false
    default property alias content: body.data
    signal closed()
    property Item returnFocusItem: null
    property bool focusPending: false
    property bool focusInside: false
    // Window.activeFocusItem changes as the keyboard moves. Keep this state
    // explicit so Shortcut.enabled updates when focus enters or leaves us.
    Connections {
        target: sheet.Window.window
        function onActiveFocusItemChanged() { sheet.focusInside = sheet.ownsFocus() }
    }

    function focusableItems(root, result) {
        for (const child of root.children) {
            if (!child.visible || !child.enabled) continue
            if (child.activeFocusOnTab && child.width > 0 && child.height > 0)
                result.push(child)
            focusableItems(child, result)
        }
    }
    function ownsFocus() {
        const window = sheet.Window.window
        for (let item = window ? window.activeFocusItem : null; item; item = item.parent)
            if (item === sheet) return true
        return false
    }
    // A list that walks its rows in its own order (TrackList.qml) takes the
    // step first, and the sheet counts it as one place: its rows sit among
    // their siblings in whatever order the view made them.
    function owningList(item) {
        for (let part = item ? item.parent : null; part && part !== sheet; part = part.parent)
            if (part.stepFrom !== undefined && part.enter !== undefined && part.inUnit(item)) return part
        return null
    }
    function moveFocus(backward) {
        const focus = sheet.Window.window.activeFocusItem
        const list = owningList(focus)
        if (list && list.stepFrom(focus, !backward)) return
        const items = []
        focusableItems(body, items)
        if (!items.length) { sheet.forceActiveFocus(); return }
        const places = []
        for (const item of items) {
            const place = owningList(item) || item
            if (places.indexOf(place) < 0) places.push(place)
        }
        const current = places.indexOf(list || focus)
        const next = places[(current + (backward ? places.length - 1 : 1)) % places.length]
        if (next.stepFrom !== undefined && next.enter(!backward)) return
        next.forceActiveFocus(backward ? Qt.BacktabFocusReason : Qt.TabFocusReason)
    }
    function focusFirst() {
        if (!focusPending || !open || !visible) return
        const items = []
        focusableItems(body, items)
        if (!items.length) { sheet.forceActiveFocus(Qt.PopupFocusReason); focusInside = ownsFocus(); return }
        focusPending = false
        items[0].forceActiveFocus(Qt.PopupFocusReason)
        focusInside = ownsFocus()
    }
    function dismiss() { if (open) { open = false; closed() } }

    readonly property real restingY: sheet.open ? Math.max(0, parent.height-height) : parent.height
    property real drag: 0

    // The scrim covers the window, not the sheet, so it is a sibling.
    Item {
        parent: sheet.parent
        objectName: "bottomSheetScrimHost"
        anchors.fill: parent
        z: sheet.z - 1
        visible: sheet.modal && (sheet.open || scrim.opacity > 0)
        Rectangle {
            id: scrim
            objectName: "bottomSheetScrim"
            anchors.fill: parent
            color: Theme.scrimColor()
            opacity: sheet.open ? 1 : 0
            // ModalBottomSheet.kt:146-157 fades the scrim on DefaultEffects
            // and gives its dismiss action the CloseSheet description.
            Behavior on opacity { NumberAnimation { objectName: "bottomSheetScrimFade"; duration: Theme.springEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springEffects } }
            Accessible.role: Accessible.Button
            Accessible.name: "Close sheet"
            Accessible.onPressAction: sheet.dismiss()
            TapHandler { onTapped: sheet.dismiss() }
        }
    }

    // Above the page it covers, and below the snackbar, which Material keeps
    // in front of a sheet.
    z: 30
    anchors.left: parent ? parent.left : undefined
    anchors.right: parent ? parent.right : undefined
    height: Math.round(parent ? parent.height*peek : 0)
    y: restingY + drag
    visible: y < (parent ? parent.height : 0)
    onVisibleChanged: if (visible && focusPending) Qt.callLater(() => focusFirst())
    Behavior on y { enabled: app.motion && !grab.active; NumberAnimation { duration: Theme.springSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial } }
    onOpenChanged: {
        drag = 0
        if (open && modal) {
            returnFocusItem = sheet.Window.window.activeFocusItem
            focusPending = true
            Qt.callLater(() => focusFirst())
        } else if (!open && returnFocusItem) {
            focusPending = false
            focusInside = false
            const previous = returnFocusItem
            returnFocusItem = null
            Qt.callLater(() => { if (previous.visible && previous.enabled) previous.forceActiveFocus(Qt.PopupFocusReason) })
        } else if (!open) { focusPending = false; focusInside = false }
    }
    // ModalBottomSheet.kt:136-142 makes the sheet a dialog traversal group.
    // Qt Quick's focus chain continues through a FocusScope, so these keys
    // cycle only while a visible modal sheet owns the current focus.
    Shortcut { sequence: "Tab"; enabled: sheet.modal && sheet.open && sheet.visible && sheet.focusInside; onActivated: sheet.moveFocus(false) }
    Shortcut { sequence: "Shift+Tab"; enabled: sheet.modal && sheet.open && sheet.visible && sheet.focusInside; onActivated: sheet.moveFocus(true) }
    Shortcut { sequence: "Escape"; enabled: sheet.modal && sheet.open && sheet.visible && sheet.focusInside; onActivated: sheet.dismiss() }

    Rectangle {
        objectName: "bottomSheetSurface"
        anchors.fill: parent
        color: Theme.surfaceLow
        topLeftRadius: Theme.shapeExtraLarge
        topRightRadius: Theme.shapeExtraLarge
        MElevation { anchors.fill: parent; radius: Theme.shapeExtraLarge; level: 1 }

        // Material's own drag handle for a sheet: wider and flatter than the
        // one that splits panes, and centred on the leading edge. Compose pads
        // the 4dp bar 22dp above and below (SheetDefaults.kt,
        // DragHandleVerticalPadding), so the handle is a 48dp strip.
        Item {
            id: handle
            objectName: "bottomSheetHandle"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            width: 48; height: 48
            Rectangle {
                anchors.centerIn: parent
                width: 32; height: 4
                radius: Theme.shapeFull(height)
                color: Theme.muted
            }
            DragHandler {
                id: grab
                target: null; xAxis.enabled: false
                onActiveTranslationChanged: if (active) sheet.drag = Math.max(0, activeTranslation.y)
                onActiveChanged: if (!active) {
                    if (sheet.drag > sheet.height/3) sheet.dismiss()
                    sheet.drag = 0
                }
            }
        }
        Item {
            id: body
            anchors.fill: parent
            anchors.topMargin: handle.height
        }
    }
}
