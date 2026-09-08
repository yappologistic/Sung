import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
    id: toolbar
    readonly property bool dialogOpen: newPlaylist.visible || restoreQueue.visible || options.visible || filters.visible || folders.visible
    readonly property string searchFilter: filter.value
    signal connectRequested()
    spacing: 8
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
                    model: [{key:"albums",label:"Albums"},{key:"artists",label:"Artists"},{key:"genres",label:"Genres"},{key:"playlists",label:"Playlists"},{key:"favorites",label:"Favorites"},{key:"random",label:"Discover"}]
                    MChip { required property var modelData; text: modelData.label; selected: app.serverRequest.mode===modelData.key; enabled: app.server.connected; onClicked: app.browseServer(modelData.key); onActiveFocusChanged: if(activeFocus)categories.reveal(this) }
                }
            }
            ScrollBar.horizontal: MScrollBar { policy: ScrollBar.AsNeeded }
        }
        MButton { id: filter; property string value: "songs"; visible: app.serverRequest.mode==="search"; text: value.charAt(0).toUpperCase()+value.slice(1); symbol: "chevron"; enabled: app.server.connected; onClicked: filters.popup(this,0,height) }
        MButton { id: serverActions; symbol: "more"; tip: "Server actions"; onClicked: options.popup(this,0,height) }
    }
    MMenu { id: filters; Repeater { model:["songs","albums","artists"]; MMenuItem { required property string modelData; text: modelData.charAt(0).toUpperCase()+modelData.slice(1); checkable: true; checked: filter.value===modelData; onTriggered: {filter.value=modelData;if(app.serverRequest.query)app.browseServer("search",app.serverRequest.query,modelData)} } } }
    MMenu {
        id: options
        MMenuItem { text: "Connection settings"; onTriggered: toolbar.connectRequested() }
        MMenuItem { text: "Music library"; enabled: app.server.connected && app.server.folders.length>0; onTriggered: folders.popup(serverActions,serverActions.width-folders.width,serverActions.height+4) }
        MDivider {}
        MMenuItem { text: "New server playlist"; enabled: app.server.connected; onTriggered: {name.clear();newPlaylist.open()} }
        MMenuItem { text: "Save queue to server"; enabled: app.server.connected && app.queue.count>0; onTriggered: app.saveServerQueue() }
        MMenuItem { text: "Restore server queue"; enabled: app.server.connected; onTriggered: restoreQueue.open() }
    }
    MMenu {
        id: folders
        MMenuItem { text: "All libraries"; checkable:true;checked:!app.server.folder;onTriggered:{app.server.folder="";app.browseServer()} }
        Repeater { model:app.server.folders; MMenuItem { required property var modelData;text:modelData.name;checkable:true;checked:app.server.folder===String(modelData.id);onTriggered:{app.server.folder=String(modelData.id);app.browseServer()} } }
    }
    MDialog {
        id: newPlaylist; objectName: "newServerPlaylistDialog"; initialFocus: name; acceptText: "Create"; parent: Overlay.overlay; anchors.centerIn: parent; width: 360; modal: true
        title: "New server playlist"; standardButtons: Dialog.Save | Dialog.Cancel; acceptEnabled: name.text.trim().length>0
        MTextField { id: name; width: parent.width; label: "Playlist name"; maximumLength:120; onAccepted: if(newPlaylist.acceptEnabled)newPlaylist.accept() }
        onAccepted: app.server.createPlaylist(name.text)
    }
    MDialog {
        id: restoreQueue; objectName: "restoreServerQueueDialog"; acceptText: "Replace"; parent: Overlay.overlay; anchors.centerIn: parent; width: 360; modal: true
        title: "Replace current queue?"; standardButtons: Dialog.Ok | Dialog.Cancel
        SungText { width: parent.width; text: "Load the saved server queue and stop current playback."; wrapMode: Text.Wrap }
        onAccepted: app.restoreServerQueue()
    }
    Connections { target: app.server; function onChanged(){ if(toolbar.visible && app.server.connected && app.serverRequest.mode==="playlists")app.refresh() } }
}
