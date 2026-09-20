import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Material 3 navigation bar.
//
// The specification splits navigation by window size: a bar along the bottom
// for compact windows, a rail beside the content for medium ones. Sung uses the
// rail at its usual sizes and this bar once the window is narrow enough that a
// rail would be taking room the content needs.
//
// Material's rules for it: three to five destinations, never fewer; labels are
// always shown; the active destination carries a filled icon inside an active
// indicator; and the items keep fixed positions.
Rectangle {
    id: bar
    objectName: "navigationBar"

    // Each destination is {key, icon, label}, and may carry `badged` for a dot
    // or `badge` for a count.
    property var destinations: []
    // Material's short bar sets the label beside the icon in a 64dp container
    // rather than under it, for a window with height to spare but not much.
    property bool shortBar: false
    property string current: ""
    signal chosen(string key)

    implicitHeight: shortBar ? 64 : 80
    color: Theme.container
    Accessible.role: Accessible.PageTabList
    Accessible.name: "Navigation"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 0
        Repeater {
            model: bar.destinations
            delegate: AbstractButton {
                id: destination
                required property var modelData
                objectName: "navBar_" + modelData.key
                readonly property bool active: bar.current === modelData.key
                Layout.fillWidth: true
                Layout.preferredHeight: bar.shortBar ? 56 : 64
                Layout.alignment: Qt.AlignVCenter
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.PageTab
                Accessible.name: modelData.label
                Accessible.selected: active
                onClicked: bar.chosen(modelData.key)

                GridLayout {
                    anchors.centerIn: parent
                    // The short bar lays the same two pieces out side by side.
                    flow: bar.shortBar ? GridLayout.LeftToRight : GridLayout.TopToBottom
                    columnSpacing: 8; rowSpacing: 4
                    // The active indicator marks one destination, and only one.
                    Rectangle {
                        objectName: "navBarIndicator_" + destination.modelData.key
                        Layout.alignment: Qt.AlignHCenter
                        implicitWidth: bar.shortBar ? 56 : 64; implicitHeight: 32
                        radius: Theme.shapeFull(implicitHeight)
                        // Material paints navigation in the secondary pair, not
                        // the primary one. Where you are is not an action, and
                        // the accent that offers an action should not be the
                        // accent that reports a location.
                        color: destination.active ? Theme.secondaryContainer
                             : destination.down || destination.visualFocus ? Qt.rgba(Theme.secondaryContainerText.r,Theme.secondaryContainerText.g,Theme.secondaryContainerText.b,Theme.pressedOpacity)
                             : destination.hovered ? Qt.rgba(Theme.secondaryContainerText.r,Theme.secondaryContainerText.g,Theme.secondaryContainerText.b,Theme.hoverOpacity)
                             : "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastEffects } }
                        Icon {
                            id: glyph
                            anchors.centerIn: parent
                            name: destination.modelData.icon
                            size: 24
                            // Filled for the active destination, outlined for
                            // the rest, as Material specifies.
                            fill: destination.active ? 1 : 0
                            ink: destination.active ? Theme.secondaryContainerText : Theme.muted
                        }
                        MBadge {
                            objectName: "navBarBadge_" + destination.modelData.key
                            present: !!destination.modelData.badged || destination.modelData.badge !== undefined
                            count: destination.modelData.badge === undefined ? -1 : destination.modelData.badge
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
                        color: destination.active ? Theme.secondary : Theme.muted
                    }
                }
                // The ring sits around the destination rather than on its
                // indicator, so it marks the thing the keyboard has reached
                // and reads the same as every other focus ring in the app.
                Rectangle {
                    objectName: "navBarFocusRing_" + destination.modelData.key
                    anchors.fill: parent; anchors.margins: 2; radius: Theme.shapeLarge
                    color: "transparent"; border.width: 2; border.color: Theme.focusRing
                    visible: destination.visualFocus
                }
            }
        }
    }
}
