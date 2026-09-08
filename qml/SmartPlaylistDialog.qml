import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
MDialog {
    id: dialog
    objectName: "smartPlaylistDialog"
    property string playlistId: ""
    property string sourceFilter: "any"
    property int daysFilter: 0
    title: playlistId ? "Edit rules" : "New smart playlist"
    width: Math.min(480,parent.width-48); height: Math.min(720,parent.height-48)
    modal: true; standardButtons: Dialog.Cancel | Dialog.Ok; acceptText: "Save"
    initialFocus: nameField; acceptEnabled: nameField.text.trim().length>0
    function edit(id) {
        const p=app.smartPlaylist(id), rules=p.rules || {};
        playlistId=id; nameField.text=p.title || ""; artistField.text=rules.artist || "";
        titleField.text=rules.title || ""; sourceFilter=rules.source || "any";
        daysFilter=rules.days || 0; likedSwitch.checked=!!rules.likedOnly; open();
    }
    onAccepted: {
        const id=app.saveSmartPlaylist(playlistId,nameField.text,{artist:artistField.text,title:titleField.text,source:sourceFilter,days:daysFilter,likedOnly:likedSwitch.checked});
        if(id)app.openPlaylist(id);
    }
    ScrollView {
        id: scroll; objectName: "smartScroll"; anchors.fill: parent; clip: true; contentWidth: availableWidth; contentHeight: fields.implicitHeight; rightPadding: 10
        ScrollBar.vertical: MScrollBar { parent: scroll; x: scroll.width-width; height: scroll.availableHeight; orientation: Qt.Vertical }
        ColumnLayout {
            id: fields; width: scroll.availableWidth; spacing: 12
            SungText { text: "Name" }
            MTextField { id: nameField; objectName: "smartName"; Layout.fillWidth: true; maximumLength: 120; placeholderText: "Playlist name" }
            SungText { text: "Artist contains" }
            MTextField { id: artistField; objectName: "smartArtist"; Layout.fillWidth: true; maximumLength: 120; placeholderText: "Any artist" }
            SungText { text: "Title contains" }
            MTextField { id: titleField; objectName: "smartTitle"; Layout.fillWidth: true; maximumLength: 120; placeholderText: "Any title" }
            RowLayout {
                Layout.fillWidth: true
                SungText { text: "Source"; Layout.fillWidth: true }
                MButton { objectName: "smartSource"; text: ({any:"Any source",local:"Local files",youtube:"YouTube Music",subsonic:"Music server"})[dialog.sourceFilter]; tonal: true; onClicked: sources.popup(this,0,height) }
            }
            RowLayout {
                Layout.fillWidth: true
                SungText { text: "Last played"; Layout.fillWidth: true }
                MButton { objectName: "smartPlayed"; text: dialog.daysFilter===0?"Any time":dialog.daysFilter===-1?"Never":"Over "+dialog.daysFilter+" days ago"; tonal: true; onClicked: played.popup(this,0,height) }
            }
            MSwitch { id: likedSwitch; objectName: "smartLiked"; text: "Liked songs only" }
            SungText { text: "Matches saved music. All rules apply."; color: Theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
    }
    MMenu { id: sources; Repeater { model: [{key:"any",title:"Any source"},{key:"local",title:"Local files"},{key:"youtube",title:"YouTube Music"},{key:"subsonic",title:"Music server"}]; MMenuItem { required property var modelData; text: modelData.title; checkable: true; checked: dialog.sourceFilter===modelData.key; onTriggered: dialog.sourceFilter=modelData.key } } }
    MMenu { id: played; Repeater { model: [0,-1,7,30,90,365]; MMenuItem { required property int modelData; text: modelData===0?"Any time":modelData===-1?"Never":"Over "+modelData+" days ago"; checkable: true; checked: dialog.daysFilter===modelData; onTriggered: dialog.daysFilter=modelData } } }
}
