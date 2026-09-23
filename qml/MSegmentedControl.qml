import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Material 3 connected button group.
//
// A connected group is a row of buttons that reads as one control. Material
// gives the leading and trailing buttons asymmetric corners, full on the
// outside and small on the inside, so the ends belong to the row rather than to
// themselves. The chosen button rounds fully; pressing one squares its inner
// corners further and, because Material widens whatever is under the finger,
// expands it while its neighbours give up the same width.
Item {
    id: group
    property var options: []
    property var value
    property string accessibleName: ""
    signal chosen(var value)

    // Material expands the pressed button by 15% of its width and takes that
    // back from the buttons beside it.
    readonly property real expandedRatio: 0.15
    readonly property int pressedIndex: {
        for (let i = 0; i < segments.count; ++i) {
            const item = segments.itemAt(i)
            if (item && item.down) return i
        }
        return -1
    }

    readonly property real segmentWidth: Math.max(76, ...options.map(option => metrics.advanceWidth(option.label) + 52))
    implicitWidth: segmentWidth * options.length
    implicitHeight: 48
    Layout.minimumHeight: implicitHeight
    // A segment's label elides when it does not fit, so the control can be
    // squeezed rather than pushing the column it sits in wider than the space
    // that column was given. It will not shrink past a touch target per
    // segment, which is the floor Material puts under one.
    Layout.minimumWidth: Math.min(implicitWidth, Math.max(1, options.length)*Math.max(40, Theme.minimumTarget))
    Accessible.role: Accessible.Grouping
    Accessible.name: accessibleName
    FontMetrics { id: metrics; font.family: Theme.fontFamily; font.pixelSize: Theme.labelLarge; font.weight: Font.Medium }

    // ConnectedButtonGroupSmallTokens.BetweenSpace is 2dp.
    readonly property real gap: 2
    readonly property real evenWidth: (width - gap*Math.max(0, options.length-1))/Math.max(1, options.length)
    // What the pressed button takes, and what each of the others gives back.
    readonly property real expansion: pressedIndex >= 0 ? evenWidth*expandedRatio : 0
    readonly property real donation: options.length > 1 ? expansion/(options.length-1) : 0

    Row {
    anchors.fill: parent
    spacing: group.gap
    Repeater {
        id: segments
        model: group.options
        AbstractButton {
            id: button
            required property var modelData
            required property int index
            readonly property bool selected: group.value === modelData.key
            readonly property bool leading: index === 0
            readonly property bool trailing: index === segments.count-1
            objectName: modelData.name || ""
            text: modelData.label
            width: group.evenWidth + (group.pressedIndex === index ? group.expansion : -group.donation)
            height: 48
            Behavior on width { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
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
                objectName: "segmentBackground"
                y: 4; height: 40
                // Outer corners are full; inner ones are a small step, and a
                // smaller one still while the button is held. Choosing a button
                // rounds it fully, which is what marks it out along the row.
                readonly property real outer: button.selected ? Theme.shapeFull(height)
                                            : button.leading || button.trailing ? Theme.shapeFull(height)
                                            : Theme.shapeSmall
                readonly property real inner: button.selected ? Theme.shapeFull(height)
                                            : button.down ? Theme.shapeExtraSmall : Theme.shapeSmall
                topLeftRadius: button.leading ? outer : inner
                bottomLeftRadius: topLeftRadius
                topRightRadius: button.trailing ? outer : inner
                bottomRightRadius: topRightRadius
                // ToggleButtonDefaults.defaultToggleButtonColors reads
                // FilledButtonTokens: unselected surfaceContainer/onSurfaceVariant,
                // selected primary/onPrimary. The connected sample uses no border.
                color: button.selected ? Theme.primary : Theme.container
                border.width: 0
                Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
                // ToggleButton.kt:173-181 morphs connected shapes on FastSpatial.
                Behavior on topLeftRadius { enabled: app.motion; NumberAnimation { id: segmentShapeSpring; objectName: "segmentShapeSpring"; duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
                Behavior on topRightRadius { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
                Rectangle {
                    anchors.fill: parent
                    topLeftRadius: parent.topLeftRadius; bottomLeftRadius: parent.bottomLeftRadius
                    topRightRadius: parent.topRightRadius; bottomRightRadius: parent.bottomRightRadius
                    color: button.selected ? Theme.primaryText : Theme.muted
                    opacity: button.down || button.visualFocus ? Theme.pressedOpacity : button.hovered ? Theme.hoverOpacity : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
                }
            }
            contentItem: Item {
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    // Material nudges content inside an asymmetric shape; the
                    // ends of a connected group are the asymmetric ones.
                    x: (parent.width-width)/2 + Theme.opticalShift(button.leading && !button.selected ? Theme.shapeFull(40) : Theme.shapeSmall,
                                                                   button.trailing && !button.selected ? Theme.shapeFull(40) : Theme.shapeSmall)
                    spacing: 8
                    // ButtonGroupSamples.kt:155-190 shows choice by filled colour
                    // and shape; its icons only swap variants when an icon exists.
                    SungText { text: button.text; font.pixelSize: Theme.labelLarge; font.weight: Font.Medium; color: button.selected ? Theme.primaryText : Theme.muted; elide: Text.ElideRight; width: Math.min(implicitWidth, button.width-24) }
                }
            }
            Rectangle {
                anchors.fill: parent; anchors.margins: 2; radius: Theme.shapeSmall
                color: "transparent"; border.width: 2; border.color: Theme.focusRing; visible: button.visualFocus
            }
        }
    }
}
}
