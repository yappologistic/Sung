import QtQuick
import QtQuick.Controls

AbstractButton {
    id: control
    property string symbol: ""
    // Secondary destinations are collections, so they identify themselves by cover.
    property string artUrl: ""
    property bool selected: false
    property bool expanded: false
    // Pending work in this destination. A count draws Material's large badge,
    // `badged` alone draws the dot.
    property bool badged: false
    property int badgeCount: -1
    // Where the glyph leads the label, a rail's item keeps 20dp between its
    // leading edge and the indicator (WNRItemHorizontalPadding); a drawer's
    // item is its indicator and keeps none. NavigationItem.kt makes the
    // whole item selectable and shows the ripple on the indicator alone, so
    // the padding still takes the click but never shows a state.
    property real itemPadding: 0
    implicitWidth: expanded ? 220 : 80
    // Material's rail item container is 64dp tall.
    implicitHeight: expanded ? 56 : 64
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.PageTab
    Accessible.name: text
    Accessible.selected: selected
    background: Item {
        Rectangle {
            id: pill
            objectName: "navigationIndicator"
            // M3 lets the expanded indicator fill its container rather than hug
            // the label; the target area spans the full rail either way.
            x: control.expanded ? control.itemPadding : (parent.width-width)/2
            // Material wraps the glyph in the indicator with (32-24)/2 either
            // side of it, so the two share a centre. Stacked, that puts the
            // indicator 4dp down from the top of the container.
            y: control.expanded ? 0 : 4
            // Material's active indicator is 56 by 32 where the destination is
            // stacked, and fills its container where it is laid out in a row.
            width: control.expanded ? parent.width-control.itemPadding : 56
            height: control.expanded ? parent.height : 32
            // The indicator's shape is full; pressing squares it towards the
            // large step, which is the app's own interaction feel rather than
            // anything Material asks for.
            radius: control.down ? Theme.shapeLarge : Theme.shapeFull(height)
            // Material's navigation colours are the secondary pair. The
            // indicator is the secondary container, its content the ink that
            // belongs on it, and the label the secondary role itself.
            color: control.selected ? Theme.secondaryContainer : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
            Behavior on radius { enabled: app.motion; NumberAnimation { duration: Theme.springEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springEffects } }
            Rectangle {
                anchors.fill: parent; radius: parent.radius
                color: Theme.secondaryContainerText
                opacity: control.down || control.visualFocus ? Theme.pressedOpacity : control.hovered ? Theme.hoverOpacity : 0
                Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
            }
        }
        Rectangle {
            objectName: "navigationFocusRing"
            anchors.fill: parent; anchors.margins: 2; radius: Theme.shapeLarge
            anchors.leftMargin: control.expanded ? control.itemPadding+2 : 2
            color: "transparent"; border.color: Theme.focusRing; border.width: Theme.focusRingWidth
            visible: control.visualFocus
        }
    }
    contentItem: Item {
        Item {
            objectName: "navigationStack"
            anchors.fill: parent
            visible: opacity>0; opacity: control.expanded ? 0 : 1
            Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
            Icon { id: stackGlyph; anchors.horizontalCenter: parent.horizontalCenter; y: 8; name: control.symbol; fill: control.selected ? 1 : 0; ink: control.selected ? Theme.secondaryContainerText : Theme.muted; Accessible.ignored: true }
            MBadge {
                objectName: "navigationBadge"
                present: control.badged || control.badgeCount >= 0
                count: control.badgeCount; subject: control.text
                x: stackGlyph.x+stackGlyph.width-inset; y: stackGlyph.y-height+lift
            }
            SungText {
                objectName: "navigationLabel"
                // The stacked item is 4dp, a 32dp indicator, 4dp, a 20dp label
                // and 4dp again, which is the 64dp container Material gives it.
                y: 40; width: parent.width; height: 20
                text: control.text; horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Theme.labelMedium
                // NavigationRailVerticalItemTokens.LabelTextFont stays LabelMedium
                // for both states; the indicator and ink show selection.
                labelRole: true; typeRole: "labelMedium"
                color: control.selected ? Theme.secondary : Theme.muted
                Accessible.ignored: true
            }
        }
        Row {
            objectName: "navigationRow"
            // Material's horizontal rail item: 16dp inside the indicator either
            // side of the row, and 8dp between the glyph and the words.
            anchors.fill: parent; anchors.leftMargin: control.itemPadding+16; anchors.rightMargin: 16
            spacing: 8
            visible: opacity>0; opacity: control.expanded ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
            Item {
                width: 24; height: 24; anchors.verticalCenter: parent.verticalCenter
                Icon { anchors.centerIn: parent; visible: !control.artUrl; name: control.symbol; fill: control.selected ? 1 : 0; ink: control.selected ? Theme.secondaryContainerText : Theme.muted; Accessible.ignored: true }
                Artwork { anchors.centerIn: parent; visible: !!control.artUrl; width: 24; height: 24; radius: Theme.shapeSmall; pixels: 96; url: control.artUrl }
                MBadge {
                    objectName: "navigationWideBadge"
                    present: control.badged || control.badgeCount >= 0
                    count: control.badgeCount; subject: control.text
                    x: parent.width-inset; y: -height+lift
                }
            }
            SungText {
                objectName: "navigationWideLabel"
                width: parent.width-32; height: parent.height
                verticalAlignment: Text.AlignVCenter
                text: control.text; elide: Text.ElideRight
                font.pixelSize: Theme.labelLarge
                // NavigationRailHorizontalItemTokens.LabelTextFont stays LabelLarge.
                labelRole: true; typeRole: "labelLarge"
                // Where the glyph leads and the label sits inside the
                // indicator, the label is on the container and takes its ink.
                // Stacked, the label is below the indicator and on the surface,
                // so it takes the secondary role instead.
                color: control.selected ? Theme.secondaryContainerText : Theme.muted
                Accessible.ignored: true
            }
        }
    }
}
