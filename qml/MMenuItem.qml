import QtQuick
import QtQuick.Controls
MenuItem {
    id: control
    implicitHeight: 44
    height: visible ? implicitHeight : 0
    leftPadding: 14; rightPadding: 14
    palette.windowText: control.enabled ? Theme.text : Theme.muted
    contentItem: SungText {
        objectName: "menuItemLabel"
        text: control.text; color: control.enabled ? Theme.text : Theme.muted
        opacity: control.enabled ? 1 : 0.5; font.pixelSize: 14
        readonly property real indicatorSpace: control.checkable && control.indicator ? control.indicator.width+12 : 0
        leftPadding: control.mirrored ? 0 : indicatorSpace
        rightPadding: control.mirrored ? indicatorSpace : 0
    }
    background: Rectangle { radius: 12; color: control.highlighted || control.activeFocus ? Theme.container : "transparent"; border.color: control.activeFocus ? Theme.primary : "transparent"; border.width: 1 }
}
