import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: group
    property var options: []
    property var value
    property string accessibleName: ""
    signal chosen(var value)
    readonly property real segmentWidth: Math.max(76, ...options.map(option => metrics.advanceWidth(option.label) + 52))
    implicitWidth: segmentWidth * options.length
    implicitHeight: 48
    Layout.minimumHeight: implicitHeight
    Layout.minimumWidth: implicitWidth
    Accessible.role: Accessible.Grouping
    Accessible.name: accessibleName
    FontMetrics { id: metrics; font.family: Theme.fontFamily; font.pixelSize: Theme.labelLarge; font.weight: Font.Medium }
    Row {
    anchors.fill: parent
    Repeater {
        id: segments
        model: group.options
        AbstractButton {
            id: button
            required property var modelData
            required property int index
            readonly property bool selected: group.value === modelData.key
            objectName: modelData.name || ""
            text: modelData.label
            width: group.width / Math.max(1, group.options.length); height: 48
            hoverEnabled: true
            focusPolicy: selected ? Qt.StrongFocus : Qt.ClickFocus
            Accessible.role: Accessible.RadioButton
            Accessible.name: text
            Accessible.checkable: true; Accessible.checked: selected
            onClicked: group.chosen(modelData.key)
            function move(offset) {
                const next = segments.itemAt((index + offset + segments.count) % segments.count)
                next.forceActiveFocus(Qt.TabFocusReason)
                group.chosen(next.modelData.key)
            }
            Keys.onLeftPressed: move(mirrored ? 1 : -1)
            Keys.onRightPressed: move(mirrored ? -1 : 1)
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
            background: Rectangle {
                y: 4; height: 40
                topLeftRadius: button.index === 0 ? 20 : 0
                bottomLeftRadius: topLeftRadius
                topRightRadius: button.index === segments.count-1 ? 20 : 0
                bottomRightRadius: topRightRadius
                color: button.selected ? Theme.primaryContainer : "transparent"
                border.width: 1; border.color: Theme.controlOutline
                Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
                Rectangle {
                    anchors.fill: parent
                    topLeftRadius: parent.topLeftRadius; bottomLeftRadius: parent.bottomLeftRadius
                    topRightRadius: parent.topRightRadius; bottomRightRadius: parent.bottomRightRadius
                    color: button.selected ? Theme.containerText : Theme.text
                    opacity: button.down || button.visualFocus ? Theme.pressedOpacity : button.hovered ? Theme.hoverOpacity : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
                }
            }
            contentItem: Item {
                Row {
                    anchors.centerIn: parent; spacing: 8
                    Icon { name: "check"; size: 18; visible: button.selected; ink: Theme.containerText; anchors.verticalCenter: parent.verticalCenter }
                    SungText { text: button.text; font.pixelSize: Theme.labelLarge; font.weight: Font.Medium; color: button.selected ? Theme.containerText : Theme.text }
                }
            }
            Rectangle {
                anchors.fill: parent; anchors.margins: 2; radius: 8
                color: "transparent"; border.width: 2; border.color: Theme.primary; visible: button.visualFocus
            }
        }
    }
}
}
