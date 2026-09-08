import QtQuick
import QtQuick.Controls

Flickable {
    id: tabs
    property string currentKey: "favorites"
    property int focusIndex: -1
    signal chosen(string key)
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
        for (let i = 0; i < entries.count; ++i)
            if (entries.itemAt(i) && entries.itemAt(i).activeFocus) return
        focusIndex = -1
    }
    function revealSelected() {
        for (let i = 0; i < entries.count; ++i) {
            const item = entries.itemAt(i)
            if (item && item.activeFocus) { reveal(item); return }
        }
        for (let i = 0; i < entries.count; ++i) {
            const item = entries.itemAt(i)
            if (item && item.selected) { reveal(item); return }
        }
    }
    onWidthChanged: Qt.callLater(revealSelected)
    onVisibleChanged: if (visible) Qt.callLater(revealSelected)
    Row {
        id: row
        Repeater {
            id: entries
            model: [
                {label:"Liked songs", key:"favorites", name:"likedTab"},
                {label:"Playlists", key:"playlists", name:"playlistsTab"},
                {label:"Local files", key:"files", name:"localFilesTab"},
                {label:"Mixes", key:"mixes", name:"mixesTab"},
                {label:"History", key:"history", name:"historyTab"}
            ]
            AbstractButton {
                id: tab
                required property var modelData
                required property int index
                readonly property bool libraryNavigation: true
                readonly property bool selected: tabs.currentKey === modelData.key || (modelData.key === "mixes" && tabs.currentKey.startsWith("mix-"))
                objectName: modelData.name
                text: modelData.label
                implicitWidth: label.implicitWidth + 32; implicitHeight: 48
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
                function moveFocus(offset) { entries.itemAt((index + offset + entries.count) % entries.count).forceActiveFocus(Qt.TabFocusReason) }
                Keys.onLeftPressed: moveFocus(-1)
                Keys.onRightPressed: moveFocus(1)
                Keys.onReturnPressed: clicked()
                Keys.onEnterPressed: clicked()
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                        entries.itemAt(event.key === Qt.Key_Home ? 0 : entries.count-1).forceActiveFocus(Qt.TabFocusReason)
                        event.accepted = true
                    }
                }
                background: Rectangle {
                    color: Theme.primary
                    opacity: tab.down || tab.visualFocus ? Theme.pressedOpacity : tab.hovered ? Theme.hoverOpacity : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.fast } }
                }
                contentItem: SungText { id: label; text: tab.text; horizontalAlignment: Text.AlignHCenter; font.weight: Font.Medium; font.pixelSize: Theme.labelLarge; color: tab.selected ? Theme.primary : Theme.muted }
                Rectangle {
                    objectName: "tabFocusRing"
                    anchors.fill: parent; anchors.margins: 2; anchors.bottomMargin: 6; radius: 8
                    color: "transparent"; border.width: 2; border.color: Theme.primary
                    visible: tab.visualFocus
                }
                Rectangle {
                    anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
                    width: label.implicitWidth; height: 3; radius: 1.5; color: Theme.primary
                    opacity: tab.selected ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
                }
            }
        }
    }
}
