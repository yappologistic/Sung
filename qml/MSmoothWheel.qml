import QtQuick

// Browser-style scrolling for a Flickable.
//
// Qt scrolls a Flickable by a few lines per notch and jumps there in one frame,
// which reads as both short and stuttery next to a browser. This handler takes
// the wheel itself and gives it the travel and the easing a browser has.
//
// Two devices, two behaviours. A mouse wheel arrives in notches, so each one is
// animated into a glide. A touchpad reports the pixels the fingers moved, so
// those are applied straight to the view and follow the hand exactly, which is
// what every browser does and what makes a touchpad feel attached to the page.
//
// Declare it beside the Flickable, not inside it: a Flickable puts its
// declared children into the scrolling content, and a wheel handler has to
// stay still over the viewport. Setting `flick` reparents it to the viewport.
//
// Usage: `MSmoothWheel { flick: theFlickable }`
Item {
    id: root

    // The Flickable being scrolled.
    property Flickable flick: null
    // Pixels travelled per wheel notch. Desktop music players move a good deal
    // per notch, since their lists are long and read by scanning rather than
    // by line. Three to four rows is about the point where a wheel covers an
    // album in one flick without losing your place.
    property real step: 260
    // How long a notch takes to land. Short enough that the list responds the
    // instant the wheel turns, long enough that the travel is seen and not
    // teleported past.
    property int duration: app !== null && !app.motion ? 0 : 280

    readonly property bool vertical: flick !== null && flick.contentHeight > flick.height
    readonly property bool horizontal: flick !== null && flick.contentWidth > flick.width

    // Where the current glide is headed. Successive notches add to this rather
    // than to the visible position, which is what makes a fast wheel travel
    // farther than a slow one.
    property real targetY: 0
    property real targetX: 0

    parent: flick
    anchors.fill: flick
    // Above the content so the wheel is seen first, but the Item accepts no
    // mouse or touch events itself, so clicks and drags reach the rows below.
    z: 9999

    function clampY(value) {
        return Math.max(flick.originY, Math.min(value, flick.originY + flick.contentHeight - flick.height))
    }
    function clampX(value) {
        return Math.max(flick.originX, Math.min(value, flick.originX + flick.contentWidth - flick.width))
    }

    // Applies a delta to the view immediately, for devices that are already
    // sending a smooth stream of pixels.
    function moveNow(deltaY, deltaX) {
        if (flick === null) return false
        // A finger takes over from any glide still running.
        glideY.stop(); glideX.stop()
        let handled = false
        if (deltaY !== 0 && vertical) { flick.contentY = clampY(flick.contentY - deltaY); handled = true }
        if (deltaX !== 0 && horizontal) { flick.contentX = clampX(flick.contentX - deltaX); handled = true }
        return handled
    }

    function glideBy(deltaY, deltaX) {
        if (flick === null) return false
        let handled = false
        if (deltaY !== 0 && vertical) {
            // A notch that arrives mid-glide adds to where the last one was
            // heading, so turning the wheel steadily builds speed instead of
            // restarting the same short hop over and over. Once a glide has
            // finished, or the view moved on its own, start from the view.
            if (!glideY.running) targetY = flick.contentY
            targetY = clampY(targetY - deltaY)
            if (targetY !== flick.contentY) {
                glideY.to = targetY
                glideY.restart()
                handled = true
            }
        }
        if (deltaX !== 0 && horizontal) {
            if (!glideX.running) targetX = flick.contentX
            targetX = clampX(targetX - deltaX)
            if (targetX !== flick.contentX) {
                glideX.to = targetX
                glideX.restart()
                handled = true
            }
        }
        return handled
    }

    NumberAnimation {
        id: glideY
        target: root.flick
        property: "contentY"
        duration: root.duration
        // Decelerating only: each notch enters at speed and settles, so a run
        // of notches reads as one push rather than a series of starts.
        easing.type: Easing.OutCubic
    }
    NumberAnimation {
        id: glideX
        target: root.flick
        property: "contentX"
        duration: root.duration
        easing.type: Easing.OutCubic
    }

    WheelHandler {
        id: wheel
        // Every device, so a touchpad is handled here too rather than falling
        // back to Qt's line stepping.
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: event => {
            // A touchpad reports pixels it actually moved, and sends them many
            // times a second. Those go straight onto the view: no target, no
            // animation, so the content tracks the fingers one to one and the
            // platform's own momentum comes through unaltered.
            if (event.pixelDelta.x !== 0 || event.pixelDelta.y !== 0) {
                event.accepted = root.moveNow(event.pixelDelta.y, event.pixelDelta.x)
                return
            }
            // A mouse wheel reports notches of 120 units. Those become glides.
            const notchesY = event.angleDelta.y / 120
            const notchesX = event.angleDelta.x / 120
            // Shift turns a vertical wheel into horizontal scrolling, which is
            // the only way to reach a carousel's ends with a plain mouse.
            const shifted = (event.modifiers & Qt.ShiftModifier) && notchesX === 0
            event.accepted = shifted
                ? root.glideBy(0, notchesY * root.step)
                : root.glideBy(notchesY * root.step, notchesX * root.step)
        }
    }

    // A drag beats the wheel: dropping the glide lets the hand take over at
    // once, and the next notch picks up from wherever the drag left the view.
    Connections {
        target: root.flick
        function onDragStarted() { glideY.stop(); glideX.stop() }
    }
}
