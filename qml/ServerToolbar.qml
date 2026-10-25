import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
    id: toolbar
    readonly property bool dialogOpen: newPlaylist.visible || restoreQueue.visible || options.visible || filters.visible || folders.visible
    readonly property string searchFilter: filter.value
    signal connectRequested()
    spacing: 8
    function filterLabel(key) {
        return key === "songs" ? qsTranslate("Main", "Songs") : key === "albums" ? qsTranslate("Main", "Albums") : qsTranslate("Main", "Artists")
    }
    RowLayout {
        Layout.fillWidth: true
        Flickable {
            id: categories
            Layout.fillWidth: true; Layout.preferredHeight: 44
            clip: true; contentWidth: categoryRow.width; contentHeight: height
            boundsBehavior: Flickable.StopAtBounds; flickableDirection: Flickable.HorizontalFlick
            function reveal(item) {const left=item.x;const right=left+item.width;if(left<contentX)contentX=left;else if(right>contentX+width)contentX=right-width;contentX=Math.max(0,Math.min(contentX,Math.max(0,contentWidth-width)))}
            onWidthChanged: contentX=Math.max(0,Math.min(contentX,Math.max(0,contentWidth-width)))
            Row {
                id: categoryRow; spacing: 6
                Repeater {
                    // Each provider lists only the views it can fill.
                    model: [{key:"albums",label:qsTranslate("Main", "Albums")},{key:"artists",label:qsTranslate("Main", "Artists")},{key:"genres",label:qsTranslate("Main", "Genres")},{key:"playlists",label:qsTranslate("Main", "Playlists")},{key:"favorites",label:qsTranslate("Main", "Favorites")},{key:"random",label:qsTranslate("Main", "Discover")}].filter(mode=>app.server.modes.indexOf(mode.key)>=0)
                    MChip { required property var modelData; text: modelData.label; selected: app.serverRequest.mode===modelData.key; enabled: app.server.connected; onClicked: app.browseServer(modelData.key); onActiveFocusChanged: if(activeFocus)categories.reveal(this) }
                }
            }
            ScrollBar.horizontal: MScrollBar { policy: ScrollBar.AsNeeded }
        }
        MButton { id: filter; property string value: "songs"; visible: app.serverRequest.mode==="search"; text: toolbar.filterLabel(value); symbol: "chevron"; enabled: app.server.connected; onClicked: filters.popup(this,0,height) }
        MButton { id: serverActions; symbol: "more"; tip: qsTranslate("Main", "Server actions"); onClicked: options.popup(this,0,height) }
    }
    MMenu { id: filters; Repeater { model:["songs","albums","artists"]; MMenuItem { required property string modelData; text: toolbar.filterLabel(modelData); checkable: true; checked: filter.value===modelData; onTriggered: {filter.value=modelData;if(app.serverRequest.query)app.browseServer("search",app.serverRequest.query,modelData)} } } }
    MMenu {
        id: options
        MMenuItem { text: qsTranslate("Main", "Connection settings"); onTriggered: toolbar.connectRequested() }
        MMenuItem { text: qsTranslate("Main", "Music library"); visible: !app.server.remotePlayback; enabled: app.server.connected && app.server.folders.length>0; onTriggered: folders.popup(serverActions,serverActions.width-folders.width,serverActions.height+4) }
        MDivider { visible: !app.server.remotePlayback }
        MMenuItem { text: qsTranslate("Main", "New server playlist"); visible: !app.server.remotePlayback; enabled: app.server.connected; onTriggered: {name.clear();newPlaylist.open()} }
        MMenuItem { text: qsTranslate("Main", "Save queue to server"); visible: app.server.supportsQueue; enabled: app.server.connected && app.queue.count>0; onTriggered: app.saveServerQueue() }
        MMenuItem { text: qsTranslate("Main", "Restore server queue"); visible: app.server.supportsQueue; enabled: app.server.connected; onTriggered: restoreQueue.open() }
    }
    MMenu {
        id: folders
        MMenuItem { text: qsTranslate("Main", "All libraries"); checkable:true;checked:!app.server.folder;onTriggered:{app.server.folder="";app.browseServer()} }
        Repeater { model:app.server.folders; MMenuItem { required property var modelData;text:modelData.name;checkable:true;checked:app.server.folder===String(modelData.id);onTriggered:{app.server.folder=String(modelData.id);app.browseServer()} } }
    }
    MDialog {
        id: newPlaylist; objectName: "newServerPlaylistDialog"; initialFocus: name; acceptText: qsTranslate("Main", "Create"); parent: Overlay.overlay; anchors.centerIn: parent; width: 360; modal: true
        title: qsTranslate("Main", "New server playlist"); standardButtons: Dialog.Save | Dialog.Cancel; acceptEnabled: name.text.trim().length>0
        MTextField { id: name; width: parent.width; label: qsTranslate("Main", "Playlist name"); maximumLength:120; onAccepted: if(newPlaylist.acceptEnabled)newPlaylist.accept() }
        onAccepted: app.server.createPlaylist(name.text)
    }
    MDialog {
        id: restoreQueue; objectName: "restoreServerQueueDialog"; acceptText: qsTranslate("Main", "Replace"); parent: Overlay.overlay; anchors.centerIn: parent; width: 360; modal: true
        title: qsTranslate("Main", "Replace the current queue?"); standardButtons: Dialog.Ok | Dialog.Cancel
        SungText { width: parent.width; text: qsTranslate("Main", "Load the saved server queue and stop current playback."); wrapMode: Text.Wrap }
        onAccepted: app.restoreServerQueue()
    }
    Connections { target: app.server; function onChanged(){ if(toolbar.visible && app.server.connected && app.serverRequest.mode==="playlists")app.refresh() } }
}
