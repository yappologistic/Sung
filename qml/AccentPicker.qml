import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Material source colors, hand-picked. The scheme itself is still derived: each
// seed is solved against the current surfaces before it becomes the primary role.
Flow {
    id: picker
    objectName: "accentPicker"
    spacing: 12
    readonly property var seeds: ["","#6750a4","#386a20","#00658e","#8f4c38","#7d5260","#6b5f00"]
    readonly property var names: ["Default","Purple","Green","Blue","Terracotta","Mauve","Olive"]
    Repeater {
        model: picker.seeds
        AbstractButton {
            id: swatch
            required property string modelData
            required property int index
            objectName: "accentSeed_"+(modelData ? modelData.slice(1) : "default")
            readonly property bool chosen: app.accentColor===modelData
            width: 44; height: 44
            focusPolicy: Qt.StrongFocus
            hoverEnabled: true
            Accessible.role: Accessible.RadioButton
            Accessible.name: picker.names[index]+" accent"
            Accessible.checkable: true; Accessible.checked: chosen
            onClicked: app.accentColor=modelData
            MTooltip {
                objectName: "accentTip"
                visible: swatch.hovered; delay: 650
                text: picker.names[swatch.index]
            }
            background: Rectangle {
                anchors.centerIn: parent
                width: 40; height: 40; radius: Theme.shapeLargeIncreased
                color: swatch.modelData ? swatch.modelData : Theme.high
                border.width: swatch.modelData ? 0 : 2
                border.color: Theme.outline
                MFocusRing { targetRadius: parent.radius; visible: swatch.visualFocus }
                Rectangle {
                    anchors.fill: parent; radius: Theme.shapeLargeIncreased
                    color: Theme.text
                    opacity: swatch.down ? Theme.pressedOpacity : swatch.hovered ? Theme.hoverOpacity : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
                }
            }
            contentItem: Item {
                Icon {
                    anchors.centerIn: parent
                    name: "check"; size: 20
                    visible: swatch.chosen
                    ink: swatch.modelData ? (Theme.luminance(swatch.modelData)>0.179?"#000000":"#ffffff") : Theme.text
                }
            }
        }
    }
}
