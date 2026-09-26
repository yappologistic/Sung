import QtQuick
import QtQuick.Templates as T

T.ScrollBar {
    id: bar
    implicitWidth: orientation === Qt.Vertical ? 8 : 48
    implicitHeight: orientation === Qt.Vertical ? 48 : 8
    // Compose's scroll indicator is a 4dp thumb inset 2dp along its track, in
    // outline at 70%, never shorter than 24dp (Scrollbar.kt,
    // NonInteractiveScrollbarDefaults: Thickness, MainAxisTrackInset,
    // ThumbOpacity, thumbColor, ThumbMinLength). Compose fades it out after
    // 400ms; on a desktop the thumb is also a handle to drag, so here it
    // stays, and answers the pointer at full strength.
    padding: 2
    readonly property real trackLength: orientation === Qt.Vertical ? height : width
    minimumSize: trackLength > 0 ? Math.min(1, 24/trackLength) : 0.08
    active: true
    policy: T.ScrollBar.AsNeeded
    visible: size < 1
    contentItem: Rectangle {
        implicitWidth: 4; implicitHeight: 4; radius: Theme.shapeFull(4)
        color: bar.pressed ? Theme.primary : Theme.outline
        opacity: bar.hovered || bar.pressed ? 1 : 0.7
    }
    background: null
}
