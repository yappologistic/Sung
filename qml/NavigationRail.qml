import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Material 3 navigation rail.
//
// Sung's own navigation is the bar in the top bar. This is the arrangement it
// replaced, kept behind a setting for anyone who had learned it: the rail down
// the leading edge, which opens from a menu icon into Material's expanded form
// and shows pinned collections as secondary destinations while it is open.
//
// It owns no state of its own. The preference, the destinations it marks and
// the window actions at its foot all come from the window, so the two
// arrangements cannot drift into disagreeing about where you are.
ColumnLayout {
    id: rail
    objectName: "navigationRail"
    Accessible.role: Accessible.Pane
    Accessible.name: "Navigation"

    // Whether the person has asked for the wide form, and whether the window
    // can give it to them. Material places the expanded rail beside body
    // content, so it only opens where there is room for it.
    property bool expandedPreference: false
    property bool roomToExpand: false
    readonly property bool expanded: expandedPreference && roomToExpand
    property string current: ""
    // Secondary destinations, which only the expanded rail has room to show.
    property var pins: []
    readonly property var shownPins: expanded ? pins.slice(0,6) : []
    property bool libraryPending: false
    // The surface's primary action lives in the window; the rail keeps a slot
    // the right size for it at its head.
    property bool fabApplies: false
    property real fabWidth: 0
    property real fabHeight: 0
    readonly property alias fabSlot: railFabSlot
    readonly property alias miniHost: railMiniHost
    readonly property alias settingsHost: railSettingsHost
    signal toggleRequested()
    signal chosen(string key)
    signal pinChosen(var item)

    // Material's expanded rail runs from 220dp to 360dp; it takes more of that
    // range as the window has room to give.
    property real shownWidth: expanded ? Math.max(220, Math.min(360, rail.parent ? rail.parent.width*0.2 : 220)) : 96
    Behavior on shownWidth { NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
    Layout.preferredWidth: shownWidth; Layout.minimumWidth: shownWidth; Layout.maximumWidth: shownWidth
    // Material's collapsed rail is 96dp wide and sets 4dp between its
    // destinations; the header above them keeps 8dp of its own.
    Layout.fillHeight: true; Layout.topMargin: 24; spacing: 4

    MButton {
        objectName: "navigationMenuButton"
        symbol: rail.expanded ? "menu_open" : "menu"
        tip: rail.expanded ? "Collapse navigation" : "Expand navigation"
        enabled: rail.roomToExpand
        Layout.alignment: rail.expanded ? Qt.AlignLeft : Qt.AlignHCenter
        Layout.leftMargin: rail.expanded ? 16 : 0
        onClicked: rail.toggleRequested()
    }
    // Material's rail carries the surface's primary action above its
    // destinations, as a FAB while the rail is collapsed and as an extended
    // FAB once it has opened and there is room for the words.
    Item {
        id: railFabSlot
        objectName: "railFabSlot"
        visible: rail.fabApplies
        Layout.preferredWidth: visible ? rail.fabWidth : 0
        Layout.preferredHeight: visible ? rail.fabHeight : 0
        Layout.alignment: rail.expanded ? Qt.AlignLeft : Qt.AlignHCenter
        Layout.leftMargin: rail.expanded ? 16 : 0
        Layout.topMargin: 4; Layout.bottomMargin: 4
    }
    Repeater {
        // A constant list: a Repeater rebuilds every delegate when its model
        // changes, so pending state is a binding on the delegate rather than a
        // field in here.
        model: [{key:"home",icon:"home",label:"Home"},{key:"search",icon:"search",label:"Search"},{key:"library",icon:"library",label:"Library"}]
        MNavigationItem {
            required property var modelData
            objectName: "nav_"+modelData.key
            expanded: rail.expanded
            Layout.alignment: rail.expanded ? Qt.AlignLeft : Qt.AlignHCenter
            Layout.preferredWidth: rail.expanded ? rail.shownWidth-24 : 80
            Layout.preferredHeight: rail.expanded ? 56 : 64
            symbol: modelData.icon; text: modelData.label; selected: rail.current===modelData.key
            // Importing is pending work inside the library, so the destination
            // says so while it runs.
            badged: modelData.key==="library" && rail.libraryPending
            onClicked: rail.chosen(modelData.key)
        }
    }
    MDivider { objectName: "navigationDivider"; visible: rail.shownPins.length>0; Layout.preferredWidth: rail.shownWidth-32; Layout.alignment: Qt.AlignLeft; Layout.leftMargin: 16; Layout.topMargin: 4 }
    SungText { objectName: "navigationPinnedLabel"; visible: rail.shownPins.length>0; text: "Pinned"; color: Theme.muted; font.pixelSize: Theme.titleSmall; typeRole: "titleSmall"; Layout.leftMargin: 20; Layout.alignment: Qt.AlignLeft }
    Repeater {
        model: rail.shownPins
        MNavigationItem {
            required property var modelData
            required property int index
            objectName: "navPin_"+index
            expanded: true
            Layout.alignment: Qt.AlignLeft
            Layout.preferredWidth: rail.shownWidth-24; Layout.preferredHeight: 48
            artUrl: modelData.art || ""; text: modelData.title || ""
            selected: rail.current==="library" && !!modelData.id && rail.pinSelected(modelData)
            onClicked: rail.pinChosen(modelData)
        }
    }
    // Which pin, if any, is the collection on screen. The window knows; the
    // rail only draws the answer.
    property var pinSelected: function(item) { return false }
    Item { Layout.fillHeight: true }
    // The window's actions sit at the foot of the rail in this arrangement and
    // in the top bar in the other one. They are one button either way, moved
    // between the two slots, so there is only ever one of each in the window.
    Item {
        id: railMiniHost
        Layout.preferredWidth: rail.expanded ? rail.shownWidth-32 : 48
        Layout.preferredHeight: 48
        Layout.alignment: rail.expanded ? Qt.AlignLeft : Qt.AlignHCenter
        Layout.leftMargin: rail.expanded ? 16 : 0
    }
    Item {
        id: railSettingsHost
        Layout.preferredWidth: rail.expanded ? rail.shownWidth-32 : 48
        Layout.preferredHeight: 48
        Layout.alignment: rail.expanded ? Qt.AlignLeft : Qt.AlignHCenter
        Layout.leftMargin: rail.expanded ? 16 : 0
        Layout.bottomMargin: 20
    }
}
