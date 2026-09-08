import QtQuick
import QtQuick.Templates as T

T.ScrollBar {
    id: bar
    implicitWidth: orientation === Qt.Vertical ? 8 : 48
    implicitHeight: orientation === Qt.Vertical ? 48 : 8
    padding: 2
    minimumSize: 0.08
    active: true
    policy: T.ScrollBar.AsNeeded
    visible: size < 1
    contentItem: Rectangle {
        implicitWidth: 4; implicitHeight: 4; radius: 2
        color: bar.pressed ? Theme.primary : Theme.muted
        opacity: bar.hovered || bar.pressed ? 1 : 0.6
    }
    background: null
}
