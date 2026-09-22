import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Material 3 navigation bar.
//
// Material's rules for it: three to five destinations, never fewer; labels are
// always shown; the active destination carries a filled icon inside an active
// indicator; and the items keep fixed positions.
//
// One thing here leaves the specification, and only one. Material sets the bar
// against the window's bottom edge. In Sung that edge is the player, which is
// on screen for the whole session, and Material does not stack a navigation
// bar and another bar on the same edge. The bar therefore sits in the window's
// top bar instead. Everything inside it is the component Material describes.
Rectangle {
    id: bar
    objectName: "navigationBar"

    // Each destination is {key, icon, label}. The list is meant to be a
    // constant: a Repeater rebuilds every delegate when its model changes, so
    // a destination that carried its own pending state would throw away the
    // hover, focus and animation of all three whenever that state moved.
    property var destinations: []
    // The keys currently carrying a dot, kept apart from the list for that
    // reason. Material's small badge says work is pending without counting it.
    property var badgedKeys: []
    // Whether the bar takes only the width its destinations need. It does
    // where there is room to centre that in the top bar; where there is not,
    // it spans the column it sits in, which is the width Material gives a
    // navigation bar in the first place.
    property bool hugsContent: false
    // Whether the bar reaches the window's edges. Material's own placement
    // does, against the bottom, and a container flush with an edge takes no
    // corner there. Where the bar floats inside a margin it keeps its own.
    property bool edgeToEdge: false
    property string current: ""
    signal chosen(string key)

    // Material draws the bar's items two ways: the label under the icon in an
    // 80dp container, or beside it in a 64dp one. Sung takes the second
    // everywhere. The bar shares a row with the back action and the window's
    // actions, all of which are 48dp, and a column of stacked labels above
    // them would set the top bar two thirds taller than the controls in it.
    implicitHeight: 64
    implicitWidth: row.implicitWidth + 16
    // The container is the floating toolbar's, the one Material draws at this
    // size with a full corner: 64dp tall, cornerFull, surfaceContainer, 8dp at
    // either end. It keeps that corner at both widths, because the bar sits
    // inside the column's margin either way, and a square edge between two
    // rounded surfaces would be the odd one out. Against the window's bottom
    // edge it gives that corner up, which is where edgeToEdge applies.
    radius: edgeToEdge ? 0 : Theme.shapeFull(height)
    color: Theme.container
    Accessible.role: Accessible.PageTabList
    Accessible.name: "Navigation"

    RowLayout {
        id: row
        anchors.fill: parent
        // FloatingToolbarTokens keeps this much at each end, and the bar's own
        // ContainerLeadingSpace is the same 8dp.
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        // NavigationBarTokens.ItemBetweenSpace. Destinations sit flush and the
        // indicator's own padding is what separates them.
        spacing: 0
        Repeater {
            model: bar.destinations
            delegate: AbstractButton {
                id: destination
                required property var modelData
                objectName: "navBar_" + modelData.key
                readonly property bool active: bar.current === modelData.key
                // ActiveIndicatorLeadingSpace and TrailingSpace, 16dp each.
                readonly property int pad: 16
                // What the indicator measures when it is fully in. The item
                // asks the layout for this rather than for the indicator's
                // live width, which moves while the indicator arrives.
                readonly property real fullIndicator: content.implicitWidth + pad*2
                // Take only what the indicator needs where the bar hugs its
                // destinations, and share the room out where it spans.
                Layout.fillWidth: !bar.hugsContent
                Layout.preferredWidth: bar.hugsContent ? fullIndicator : -1
                Layout.preferredHeight: bar.implicitHeight
                Layout.alignment: Qt.AlignVCenter
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.PageTab
                Accessible.name: modelData.label
                Accessible.selected: active
                onClicked: bar.chosen(modelData.key)

                // Material draws two boxes here, not one, and keeps them apart
                // on purpose: the indicator animates its width in, and the
                // layer that answers the pointer must not. A hover on a
                // destination you are not on has to light the whole target,
                // and there is no indicator there to light.
                Rectangle {
                    objectName: "navBarStateLayer_" + destination.modelData.key
                    width: destination.fullIndicator; height: 40
                    x: content.x - destination.pad
                    y: content.y + (content.height-height)/2
                    radius: Theme.shapeFull(height)
                    color: destination.down || destination.visualFocus ? Qt.rgba(Theme.secondaryContainerText.r,Theme.secondaryContainerText.g,Theme.secondaryContainerText.b,Theme.pressedOpacity)
                         : destination.hovered ? Qt.rgba(Theme.secondaryContainerText.r,Theme.secondaryContainerText.g,Theme.secondaryContainerText.b,Theme.hoverOpacity)
                         : "transparent"
                    // A state layer is colour, and colour is an effects spring.
                    // Material's navigation item asks for the default one.
                    Behavior on color { ColorAnimation { duration: Theme.springEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springEffects } }
                }
                // The active indicator marks one destination, and only one. It
                // is drawn before the content so the content sits on it.
                //
                // NavigationBarHorizontalItemTokens gives it no fixed width at
                // all, unlike the stacked item's 56x32 box around the icon: it
                // is 40dp tall and wraps both pieces, holding 16dp of leading
                // and trailing space. That wrapping indicator reads as the
                // pill. Material grows it from nothing across its full width
                // and keeps it centred while it arrives, so the destination
                // you have chosen opens out rather than appearing whole.
                Rectangle {
                    id: indicator
                    objectName: "navBarIndicator_" + destination.modelData.key
                    width: destination.active ? destination.fullIndicator : 0
                    height: 40
                    x: content.x - destination.pad + (destination.fullIndicator-width)/2
                    y: content.y + (content.height-height)/2
                    radius: Theme.shapeFull(height)
                    // Material paints navigation in the secondary pair, not
                    // the primary one. Where you are is not an action, and
                    // the accent that offers an action should not be the
                    // accent that reports a location.
                    color: Theme.secondaryContainer
                    // Width is movement, so it takes a spatial spring, and the
                    // navigation item asks for the default one rather than the
                    // fast one an effect would take.
                    Behavior on width { NumberAnimation { duration: Theme.springSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial } }
                }
                GridLayout {
                    id: content
                    anchors.centerIn: parent
                    flow: GridLayout.LeftToRight
                    // NavigationBarTokens.ItemActiveIndicatorIconLabelSpace,
                    // which is the gap inside the indicator.
                    columnSpacing: 4
                    Item {
                        id: iconSlot
                        Layout.alignment: Qt.AlignHCenter
                        implicitWidth: 24; implicitHeight: 24
                        Icon {
                            id: glyph
                            anchors.centerIn: parent
                            name: destination.modelData.icon
                            size: 24
                            // Filled for the active destination, outlined for
                            // the rest, as Material specifies.
                            fill: destination.active ? 1 : 0
                            ink: destination.active ? Theme.secondaryContainerText : Theme.muted
                            // Ink is colour, so it crosses on the effects
                            // spring the navigation item asks for, in step
                            // with the fill axis Icon already animates.
                            Behavior on ink { ColorAnimation { duration: Theme.springEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springEffects } }
                        }
                        MBadge {
                            objectName: "navBarBadge_" + destination.modelData.key
                            present: bar.badgedKeys.indexOf(destination.modelData.key) >= 0
                            count: -1
                            subject: destination.modelData.label
                            x: glyph.x+glyph.width-inset; y: glyph.y-height+lift
                        }
                    }
                    // Labels are always shown, never dropped to save room.
                    SungText {
                        objectName: "navBarLabel_" + destination.modelData.key
                        Layout.alignment: Qt.AlignHCenter
                        text: destination.modelData.label
                        font.pixelSize: Theme.labelMedium
                        emphasized: destination.active; labelRole: true
                        // The label is inside the indicator here, so it takes
                        // the ink of the container it is drawn on rather than
                        // the secondary accent a label below one would take.
                        color: destination.active ? Theme.secondaryContainerText : Theme.muted
                        Behavior on color { ColorAnimation { duration: Theme.springEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springEffects } }
                    }
                }
                // The ring sits around the destination rather than on its
                // indicator, so it marks the thing the keyboard has reached
                // and reads the same as every other focus ring in the app.
                Rectangle {
                    objectName: "navBarFocusRing_" + destination.modelData.key
                    anchors.fill: parent; anchors.margins: 2
                    radius: Theme.shapeFull(height)
                    color: "transparent"; border.width: 2; border.color: Theme.focusRing
                    visible: destination.visualFocus
                }
            }
        }
    }
}
