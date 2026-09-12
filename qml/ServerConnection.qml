import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
MDialog {
    id: dialog
    objectName: "serverConnectionDialog"
    anchors.centerIn: parent; width: Math.min(460,parent.width-48)
    height: Math.min(parent.height-48, implicitHeaderHeight + implicitFooterHeight + topPadding + bottomPadding + connectionFields.implicitHeight + 8)
    initialFocus: address.text.length ? (username.text.length ? password : username) : address
    title: "Music server"; modal: true; standardButtons: Dialog.NoButton
    onAboutToShow: { address.text=app.server.address;username.text=app.server.username;password.clear() }
    onClosed: password.clear()
    contentItem: ScrollView {
        id: connectionScroll; clip: true; contentWidth: availableWidth; contentHeight: connectionFields.implicitHeight; rightPadding: 12; topPadding: 8
        ScrollBar.vertical: MScrollBar { parent: connectionScroll; x: connectionScroll.width-width; y: connectionScroll.topPadding; height: connectionScroll.availableHeight; orientation: Qt.Vertical }
    ColumnLayout {
        id: connectionFields; objectName: "connectionFields"; width: connectionScroll.availableWidth; spacing: 16
        MSegmentedControl {
            objectName: "serverProvider"; accessibleName: "Server type"
            options: [{key:"subsonic",label:"Subsonic",name:"serverType_subsonic"},{key:"jellyfin",label:"Jellyfin",name:"serverType_jellyfin"}]
            value: app.server.provider; enabled: !app.server.connecting
            onChosen: value=>{app.server.selectProvider(value);address.text=app.server.address;username.text=app.server.username;password.clear()}
        }
        MTextField { id: address; objectName: "serverAddress"; Layout.fillWidth: true; Layout.topMargin: 8; label: "Server address"; inputMethodHints: Qt.ImhUrlCharactersOnly; enabled: !app.server.connecting; onAccepted: username.forceActiveFocus() }
        MTextField { id: username; objectName: "serverUsername"; Layout.fillWidth: true; label: "Username"; enabled: !app.server.connecting; onAccepted: password.forceActiveFocus() }
        MTextField { id: password; objectName: "serverPassword"; Layout.fillWidth: true; label: "Password"; echoMode: TextInput.Password; enabled: !app.server.connecting; onAccepted: if(connectButton.enabled)connectButton.clicked() }
        SungText { text: "HTTP sends traffic without encryption."; visible: address.text.startsWith("http://") && !address.text.startsWith("http://localhost:") && !address.text.startsWith("http://127.0.0.1:"); Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.muted; font.pixelSize: 12 }
        MSwitch { id: remember; objectName: "rememberServer"; text: "Remember in desktop keyring"; checked: app.server.keyringAvailable; enabled: app.server.keyringAvailable }
        SungText { objectName: "serverStatus"; text: app.server.error || (app.server.connecting?"Connecting…":app.server.connected?"Connected":app.server.provider==="jellyfin"?"Jellyfin":"Navidrome / Subsonic"); Layout.fillWidth: true; wrapMode: Text.Wrap; color: app.server.error ? Theme.error : app.server.connected?Theme.primary:Theme.muted }
        MButton { text: "Disconnect"; visible: app.server.connected; onClicked: {app.server.disconnectServer();password.clear()} }
        MDivider { Layout.fillWidth: true; visible: app.server.connected }
        MSwitch { text: "Update server listening history"; visible: app.server.connected; checked: app.server.scrobbling; onToggled: app.server.scrobbling=checked }
        RowLayout {
            visible: app.server.connected; Layout.fillWidth: true
            SungText { text: "Audio quality"; Layout.fillWidth: true }
            MButton { text: app.server.bitrate?app.server.bitrate+" kbps":"Original"; onClicked: quality.popup(this,0,height) }
        }
    }
    }
    footer: Item {
        implicitHeight: 96
        Row {
            anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 24; spacing: 8
            MButton { text: "Close"; ink: Theme.primary; onClicked: dialog.close() }
            MButton {
                id: connectButton; objectName: "connectServerButton"; text: "Connect"; filled: true
                visible: !app.server.connected || password.text.length>0 || app.server.connecting
                busy: app.server.connecting
                enabled: !app.server.connecting && address.text.trim().length>0 && username.text.trim().length>0 && (app.server.provider==="jellyfin" || password.text.length>0)
                onClicked: {app.server.connectServer(address.text,username.text,password.text,remember.checked);password.clear()}
            }
        }
    }
    MMenu {
        id: quality
        Repeater { model: [0,128,192,320]; MMenuItem { required property int modelData; text: modelData?modelData+" kbps MP3":"Original"; checkable: true; checked: app.server.bitrate===modelData; onTriggered: app.server.bitrate=modelData } }
    }
}
