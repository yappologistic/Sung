import QtQuick
import QtQuick.Controls

Flickable {
    id: tabs
    property string currentKey: "favorites"
    // A tab shows as chosen the moment it is chosen, as Compose selects a tab
    // on the click and moves the indicator while the page changes. The page
    // itself arrives a transition later, when currentKey catches up and the
    // choice stops being pending.
    property string pendingKey: ""
    readonly property string shownKey: pendingKey || currentKey
    onCurrentKeyChanged: pendingKey = ""
    function choose(key) {
        // Choosing the tab already current drops any other still pending.
        pendingKey = key !== currentKey ? key : ""
        chosen(key)
    }
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
    onVisibleChanged: if (visible) Qt.callLater(revealSelected); else pendingKey = ""
    // A tab row is defined by a divider along its bottom edge, which is what
    // separates it from the content that scrolls beneath. Compose draws it
    // under both variants (TabRow.kt, PrimaryTabRow and SecondaryTabRow).
    //
    // The divider spans the row, not the tabs: it lives in the scrolled
    // content, so it follows the view across and is as wide as the view,
    // however far the tabs reach.
    Rectangle {
        objectName: "tabDivider"
        x: tabs.contentX; y: tabs.height-height
        width: tabs.width
        height: 1
        color: Theme.outlineVariant
    }
    // The tab that is chosen, which the indicator stands under.
    readonly property Item selectedTab: {
        tabs.shownKey
        for (let i = 0; i < tabItems.count; ++i) {
            const item = tabItems.itemAt(i)
            if (item && item.selected) return item
        }
        return null
    }
    // One indicator for the row, which travels to the chosen tab. TabRow.kt
    // animates its offset and width on DefaultSpatial (TabRow.kt:406 and
    // 1110-1117, tabIndicatorOffset): it is the indicator moving, so it is a
    // spatial spring, and it may pass its mark and settle.
    Rectangle {
        id: indicator
        objectName: "tabIndicator"
        // A primary tab's indicator is as wide as its label and never under
        // 24dp; a secondary tab's spans the whole tab (TabRow.kt,
        // contentWidth and matchContentSize = false).
        readonly property real targetWidth: !tabs.selectedTab ? 0
            : tabs.secondary ? tabs.selectedTab.width : Math.max(24, tabs.selectedTab.labelWidth)
        readonly property real targetX: tabs.selectedTab ? tabs.selectedTab.x+(tabs.selectedTab.width-targetWidth)/2 : 0
        // The first place it is given is where it starts, rather than a
        // journey from the row's start.
        property bool placed: false
        onTargetWidthChanged: if (targetWidth > 0) Qt.callLater(() => indicator.placed = true)
        visible: !!tabs.selectedTab
        x: targetX; width: targetWidth
        y: tabs.height-height
        z: 1
        height: tabs.secondary ? Theme.tabIndicatorSecondary : Theme.tabIndicatorPrimary
        // It sits on the divider, so it rounds at the top and stays square
        // where the two meet.
        topLeftRadius: height; topRightRadius: height
        bottomLeftRadius: 0; bottomRightRadius: 0
        color: Theme.primary
        Behavior on x { enabled: indicator.placed; NumberAnimation { duration: Theme.springSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial } }
        Behavior on width { enabled: indicator.placed; NumberAnimation { duration: Theme.springSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial } }
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
                readonly property real labelWidth: label.implicitWidth
                readonly property bool selected: tabs.shownKey === modelData.key || (!tabs.secondary && modelData.key === "files" && tabs.shownKey.startsWith("local-")) || (modelData.key === "mixes" && tabs.shownKey.startsWith("mix-"))
                objectName: modelData.name
                text: modelData.label
                implicitWidth: label.implicitWidth + 32 + (tabBadge.visible ? tabBadge.width+8 : 0)
                implicitHeight: tabs.implicitHeight
                hoverEnabled: true
                focusPolicy: (tabs.focusIndex < 0 ? selected : tabs.focusIndex === index) ? Qt.StrongFocus : Qt.ClickFocus
                Accessible.role: Accessible.PageTab
                Accessible.name: text
                Accessible.selected: selected
                onClicked: tabs.choose(modelData.key)
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
                    Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
                }
                contentItem: Item {
                    Row {
                        anchors.centerIn: parent; spacing: 8
                        // Tab.kt:102 uses the TitleSmall LabelTextFont for either state;
                        // PrimaryNavigationTabTokens and SecondaryNavigationTabTokens agree.
                        // Tab.kt:270-290 crosses the label to its chosen colour
                        // on DefaultEffects and back on FastEffects.
                        SungText {
                            id: label; anchors.verticalCenter: parent.verticalCenter; text: tab.text; typeRole: "titleSmall"; font.pixelSize: Theme.titleSmall
                            color: !tab.selected ? Theme.muted : tabs.secondary ? Theme.text : Theme.primary
                            Behavior on color { ColorAnimation { duration: tab.selected ? Theme.springEffectsMs : Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: tab.selected ? Theme.springEffects : Theme.springFastEffects } }
                        }
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
                // Tabs meet edge to edge, so the ring is drawn inside the tab.
                MFocusRing {
                    objectName: "tabFocusRing"
                    inward: true; targetRadius: Theme.shapeSmall
                    visible: tab.visualFocus
                }
            }
        }
    }
}
