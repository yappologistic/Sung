import QtQuick
import QtQuick.Controls
Dialog {
    id: dialog
    property bool acceptEnabled: true
    padding: 24
    background: Rectangle { color: Theme.container; radius: 28 }
    header: Item {
        implicitHeight: 72
        SungText { anchors.fill: parent; anchors.leftMargin: 24; anchors.rightMargin: 24; text: dialog.title; font.pixelSize: 24; font.weight: Font.Medium }
    }
    footer: DialogButtonBox {
        visible: dialog.standardButtons !== Dialog.NoButton
        standardButtons: dialog.standardButtons
        alignment: Qt.AlignRight
        padding: 18; spacing: 8
        background: null
        delegate: MButton { objectName: dialog.objectName + "_button_" + DialogButtonBox.buttonRole; ink: Theme.primary; enabled: DialogButtonBox.buttonRole !== DialogButtonBox.AcceptRole || dialog.acceptEnabled }
    }
    enter: Transition { ParallelAnimation { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: app.motion?350:0; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastSpatialCurve } } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
}
