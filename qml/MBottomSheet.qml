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

    function focusableItems(root, result) {
        for (const child of root.children) {
            if (!child.visible || !child.enabled) continue
            if (child.activeFocusOnTab && child.width > 0 && child.height > 0)
                result.push(child)
            focusableItems(child, result)
        }
    }
    function moveFocus(backward) {
        const items = []
        focusableItems(body, items)
        if (!items.length) { sheet.forceActiveFocus(); return }
        const current = items.indexOf(sheet.Window.window.activeFocusItem)
        const next = (current + (backward ? items.length - 1 : 1)) % items.length
        items[next].forceActiveFocus(backward ? Qt.BacktabFocusReason : Qt.TabFocusReason)
    }
    function focusFirst() {
        if (!focusPending || !open || !visible) return
        const items = []
        focusableItems(body, items)
        if (!items.length) { sheet.forceActiveFocus(Qt.PopupFocusReason); return }
        focusPending = false
        items[0].forceActiveFocus(Qt.PopupFocusReason)
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
            const previous = returnFocusItem
            returnFocusItem = null
            Qt.callLater(() => { if (previous.visible && previous.enabled) previous.forceActiveFocus(Qt.PopupFocusReason) })
        } else if (!open) focusPending = false
    }
    // Qt Quick's window focus chain continues through a FocusScope. A modal
    // sheet cycles its reachable controls here so Tab cannot enter the page.
    Shortcut { sequence: "Tab"; enabled: sheet.modal && sheet.open; onActivated: sheet.moveFocus(false) }
    Shortcut { sequence: "Shift+Tab"; enabled: sheet.modal && sheet.open; onActivated: sheet.moveFocus(true) }
    Shortcut { sequence: "Escape"; enabled: sheet.modal && sheet.open; onActivated: sheet.dismiss() }

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
