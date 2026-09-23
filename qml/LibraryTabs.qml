import QtQuick
import QtQuick.Controls

Flickable {
    id: tabs
    property string currentKey: "favorites"
    property int focusIndex: -1
    signal chosen(string key)
    // Material's two tab variants. Primary tabs sit under the app bar and mark
    // the main destinations; secondary tabs sit inside a content area to divide
    // it further, and carry a simpler indicator.
    property bool secondary: false
    // The tab currently carrying a dot, for work pending inside it. An entry
    // may also carry `badge` for a count.
    property string dotKey: ""
    property var entries: [
        {label:"Liked songs", key:"favorites", name:"likedTab"},
        {label:"Playlists", key:"playlists", name:"playlistsTab"},
        {label:"Local files", key:"files", name:"localFilesTab"},
        {label:"Mixes", key:"mixes", name:"mixesTab"},
        {label:"History", key:"history", name:"historyTab"},
        {label:"Music server", key:"server", name:"serverTab"}
    ]
    // Both variants are 48dp tall (Primary/SecondaryNavigationTabTokens
    // .ContainerHeight); the secondary one differs in its indicator, not its
    // height.
    implicitHeight: 48
    contentWidth: row.width; contentHeight: height
    flickableDirection: Flickable.HorizontalFlick
    boundsBehavior: Flickable.StopAtBounds
    clip: true
    Accessible.role: Accessible.PageTabList
    Accessible.name: "Library"
    function reveal(item) {
        let offset = contentX
        if (item.x < offset) offset = item.x
        else if (item.x + item.width > offset + width) offset = item.x + item.width - width
        contentX = Math.max(0, Math.min(offset, Math.max(0, contentWidth-width)))
    }
    function resetFocusStop() {
        for (let i = 0; i < tabItems.count; ++i)
            if (tabItems.itemAt(i) && tabItems.itemAt(i).activeFocus) return
        focusIndex = -1
    }
    function revealSelected() {
        for (let i = 0; i < tabItems.count; ++i) {
            const item = tabItems.itemAt(i)
            if (item && item.activeFocus) { reveal(item); return }
        }
        for (let i = 0; i < tabItems.count; ++i) {
            const item = tabItems.itemAt(i)
            if (item && item.selected) { reveal(item); return }
        }
    }
    onWidthChanged: Qt.callLater(revealSelected)
    onVisibleChanged: if (visible) Qt.callLater(revealSelected)
    // A tab row is defined by a divider along its bottom edge, which is what
    // separates it from the content that scrolls beneath. Compose draws it
    // under both variants (TabRow.kt, PrimaryTabRow and SecondaryTabRow).
    Rectangle {
        objectName: "tabDivider"
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        width: tabs.width
        height: 1
        color: Theme.outlineVariant
    }
    Row {
        id: row
        Repeater {
            id: tabItems
            model: tabs.entries
            AbstractButton {
                id: tab
                required property var modelData
                required property int index
                readonly property bool libraryNavigation: true
                readonly property bool selected: tabs.currentKey === modelData.key || (!tabs.secondary && modelData.key === "files" && tabs.currentKey.startsWith("local-")) || (modelData.key === "mixes" && tabs.currentKey.startsWith("mix-"))
                objectName: modelData.name
                text: modelData.label
                implicitWidth: label.implicitWidth + 32 + (tabBadge.visible ? tabBadge.width+8 : 0)
                implicitHeight: tabs.implicitHeight
                hoverEnabled: true
                focusPolicy: (tabs.focusIndex < 0 ? selected : tabs.focusIndex === index) ? Qt.StrongFocus : Qt.ClickFocus
                Accessible.role: Accessible.PageTab
                Accessible.name: text
                Accessible.selected: selected
                onClicked: tabs.chosen(modelData.key)
                onActiveFocusChanged: {
                    if (activeFocus) { tabs.focusIndex=index; tabs.reveal(tab) }
                    else Qt.callLater(tabs.resetFocusStop)
                }
                onSelectedChanged: if (selected) { tabs.focusIndex=index; Qt.callLater(() => tabs.reveal(tab)) }
                function moveFocus(offset) { tabItems.itemAt((index + offset + tabItems.count) % tabItems.count).forceActiveFocus(Qt.TabFocusReason) }
                Keys.onLeftPressed: moveFocus(-1)
                Keys.onRightPressed: moveFocus(1)
                Keys.onReturnPressed: clicked()
                Keys.onEnterPressed: clicked()
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                        tabItems.itemAt(event.key === Qt.Key_Home ? 0 : tabItems.count-1).forceActiveFocus(Qt.TabFocusReason)
                        event.accepted = true
                    }
                }
                // Compose's tab ripple is the selected content colour: the
                // accent for a primary tab, onSurface for a secondary one.
                background: Rectangle {
                    color: tabs.secondary ? Theme.text : Theme.primary
                    opacity: tab.down || tab.visualFocus ? Theme.pressedOpacity : tab.hovered ? Theme.hoverOpacity : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.fast } }
                }
                contentItem: Item {
                    Row {
                        anchors.centerIn: parent; spacing: 8
                        SungText { id: label; anchors.verticalCenter: parent.verticalCenter; text: tab.text; emphasized: tab.selected; labelRole: true; font.pixelSize: Theme.labelLarge; color: !tab.selected ? Theme.muted : tabs.secondary ? Theme.text : Theme.primary }
                        MBadge {
                            id: tabBadge
                            objectName: "tabBadge_"+tab.modelData.key
                            anchors.verticalCenter: parent.verticalCenter
                            present: tabs.dotKey === tab.modelData.key || tab.modelData.badge !== undefined
                            count: tab.modelData.badge === undefined ? -1 : tab.modelData.badge
                            subject: tab.text
                        }
                    }
                }
                Rectangle {
                    objectName: "tabFocusRing"
                    anchors.fill: parent; anchors.margins: 2; anchors.bottomMargin: 6; radius: Theme.shapeSmall
                    color: "transparent"; border.width: 2; border.color: Theme.focusRing
                    visible: tab.visualFocus
                }
                Rectangle {
                    objectName: "tabIndicator"
                    anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
                    // A primary tab's indicator is as wide as its label and
                    // never under 24dp; a secondary tab's spans the whole tab
                    // (TabRow.kt, contentWidth and matchContentSize = false).
                    width: tabs.secondary ? tab.width : Math.max(24, label.implicitWidth)
                    height: tabs.secondary ? Theme.tabIndicatorSecondary : Theme.tabIndicatorPrimary
                    // It sits on the divider, so it rounds at the top and stays
                    // square where the two meet.
                    topLeftRadius: height; topRightRadius: height
                    bottomLeftRadius: 0; bottomRightRadius: 0
                    color: Theme.primary
                    opacity: tab.selected ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
                }
            }
        }
    }
}
