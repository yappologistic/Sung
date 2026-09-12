import QtQuick
import QtQuick.Controls
Dialog {
    id: dialog
    property bool acceptEnabled: true
    property string acceptText: ""
    property Item initialFocus: null
    focus: true
    onOpened: if (initialFocus) initialFocus.forceActiveFocus(Qt.TabFocusReason)
    padding: 24
    background: Rectangle { color: Theme.container; radius: 28 }
    header: Item {
        implicitHeight: Math.max(72, titleLabel.implicitHeight + 48)
        SungText { id: titleLabel; objectName: "dialogTitle"; anchors.verticalCenter: parent.verticalCenter; x: 24; width: parent.width-48; text: dialog.title; font.pixelSize: Theme.headlineSmall; font.weight: Font.Normal; wrapMode: Text.Wrap; maximumLineCount: 2 }
    }
    footer: DialogButtonBox {
        visible: dialog.standardButtons !== Dialog.NoButton
        standardButtons: dialog.standardButtons
        alignment: Qt.AlignRight
        buttonLayout: DialogButtonBox.AndroidLayout
        padding: 24; spacing: 8
        background: null
        delegate: MButton {
            objectName: dialog.objectName + "_button_" + DialogButtonBox.buttonRole
            readonly property bool confirming: DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole || DialogButtonBox.buttonRole === DialogButtonBox.YesRole
            ink: Theme.primary; enabled: !confirming || dialog.acceptEnabled
            Component.onCompleted: if (confirming && dialog.acceptText) text = Qt.binding(() => dialog.acceptText)
        }
    }
    enter: Transition { ParallelAnimation { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.enterDuration; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: app.motion?350:0; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastSpatialCurve } } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.exitDuration; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
}
