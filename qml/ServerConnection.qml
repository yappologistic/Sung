import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
MDialog {
    id: dialog
    objectName: "serverConnectionDialog"
    anchors.centerIn: parent; width: Math.min(460,parent.width-48)
    height: Math.min(parent.height-48, implicitHeaderHeight + implicitFooterHeight + topPadding + bottomPadding + connectionFields.implicitHeight + 8)
    // Cider is an Apple Music player on this computer, reached by address and
    // the token it issues, with no account name of its own.
    readonly property bool cider: app.server.provider==="cider"
    initialFocus: address.text.length ? (username.text.length || cider ? password : username) : address
    title: qsTranslate("Main", "Music server"); modal: true; standardButtons: Dialog.NoButton
    function fill() { address.text=app.server.address || (cider?"http://localhost:10767":"");username.text=app.server.username;password.clear() }
    onAboutToShow: fill()
    onClosed: password.clear()
    contentItem: ScrollView {
        id: connectionScroll; clip: true; contentWidth: availableWidth; contentHeight: connectionFields.implicitHeight; rightPadding: 12; topPadding: 8
        ScrollBar.vertical: MScrollBar { parent: connectionScroll; x: connectionScroll.width-width; y: connectionScroll.topPadding; height: connectionScroll.availableHeight; orientation: Qt.Vertical }
    ColumnLayout {
        id: connectionFields; objectName: "connectionFields"; width: connectionScroll.availableWidth; spacing: 16
        MSegmentedControl {
            // Three types outgrow a phone-width dialog at their natural width,
            // so the control takes the column's width and shares it evenly,
            // as the other segmented controls in Settings do.
            objectName: "serverProvider"; accessibleName: qsTranslate("Main", "Server type"); Layout.fillWidth: true; Layout.minimumWidth: 0
            options: [{key:"subsonic",label:"Subsonic",name:"serverType_subsonic"},{key:"jellyfin",label:"Jellyfin",name:"serverType_jellyfin"},{key:"cider",label:"Cider",name:"serverType_cider"}]
            value: app.server.provider; enabled: !app.server.connecting
            onChosen: value=>{app.server.selectProvider(value);dialog.fill()}
        }
        MTextField { id: address; objectName: "serverAddress"; Layout.fillWidth: true; Layout.topMargin: 8; label: dialog.cider?qsTranslate("Main", "Cider address"):qsTranslate("Main", "Server address"); inputMethodHints: Qt.ImhUrlCharactersOnly; enabled: !app.server.connecting; onAccepted: (dialog.cider?password:username).forceActiveFocus() }
        MTextField { id: username; objectName: "serverUsername"; Layout.fillWidth: true; label: qsTranslate("Main", "Username"); visible: !dialog.cider; enabled: !app.server.connecting; onAccepted: password.forceActiveFocus() }
        // Material's supporting text says where the value comes from. Cider
        // makes the token itself, in the one place this names.
        MTextField { id: password; objectName: "serverPassword"; Layout.fillWidth: true; label: dialog.cider?qsTranslate("Main", "Cider token"):qsTranslate("Main", "Password"); supporting: dialog.cider?qsTranslate("Main", "In Cider: Settings, Connectivity, Manage External Application Access"):""; echoMode: TextInput.Password; enabled: !app.server.connecting; onAccepted: if(connectButton.enabled)connectButton.clicked() }
        SungText { text: qsTranslate("Main", "HTTP sends traffic without encryption."); visible: address.text.startsWith("http://") && !address.text.startsWith("http://localhost:") && !address.text.startsWith("http://127.0.0.1:"); Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.muted; font.pixelSize: Theme.bodySmall }
        MSwitch { id: remember; objectName: "rememberServer"; Layout.fillWidth: true; Layout.minimumWidth: 0; text: qsTranslate("Main", "Remember in desktop keyring"); checked: app.server.keyringAvailable; enabled: app.server.keyringAvailable }
        SungText { objectName: "serverStatus"; text: app.server.error || (app.server.connecting?qsTranslate("Main", "Connecting…"):app.server.connected?qsTranslate("Main", "Connected"):app.server.provider==="jellyfin"?"Jellyfin":dialog.cider?qsTranslate("Main", "Apple Music through Cider"):"Navidrome / Subsonic"); Layout.fillWidth: true; wrapMode: Text.Wrap; color: app.server.error ? Theme.error : app.server.connected?Theme.primary:Theme.muted }
        MButton { text: qsTranslate("Main", "Disconnect"); visible: app.server.connected; onClicked: {app.server.disconnectServer();password.clear()} }
        MDivider { Layout.fillWidth: true; visible: app.server.connected && !app.server.remotePlayback }
        MSwitch { Layout.fillWidth: true; Layout.minimumWidth: 0; text: qsTranslate("Main", "Update server listening history"); visible: app.server.connected && !app.server.remotePlayback; checked: app.server.scrobbling; onToggled: app.server.scrobbling=checked }
        RowLayout {
            visible: app.server.connected && !app.server.remotePlayback; Layout.fillWidth: true
            SungText { text: qsTranslate("Main", "Audio quality"); Layout.fillWidth: true }
            MButton { text: app.server.bitrate?qsTranslate("Main", "%1 kbps").arg(app.server.bitrate):qsTranslate("Main", "Original"); onClicked: quality.popup(this,0,height) }
        }
    }
    }
    scrollSource: connectionScroll.contentItem
    footer: Item {
        implicitHeight: 96
        Rectangle {
            objectName: "dialogScrollDivider"
            anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
            height: 1; color: Theme.outlineVariant; visible: dialog.moreBelow
        }
        Row {
            anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 24; spacing: 8
            MButton { text: qsTranslate("Main", "Close"); ink: Theme.primary; onClicked: dialog.close() }
            MButton {
                id: connectButton; objectName: "connectServerButton"; text: qsTranslate("Main", "Connect"); filled: true
                visible: !app.server.connected || password.text.length>0 || app.server.connecting
                busy: app.server.connecting
                enabled: !app.server.connecting && address.text.trim().length>0 && (dialog.cider || (username.text.trim().length>0 && (app.server.provider==="jellyfin" || password.text.length>0)))
                onClicked: {app.server.connectServer(address.text,username.text,password.text,remember.checked);password.clear()}
            }
        }
    }
    MMenu {
        id: quality
        Repeater { model: [0,128,192,320]; MMenuItem { required property int modelData; text: modelData?qsTranslate("Main", "%1 kbps").arg(modelData)+" MP3":qsTranslate("Main", "Original"); checkable: true; checked: app.server.bitrate===modelData; onTriggered: app.server.bitrate=modelData } }
    }
}
