import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// First run, in three steps. Nothing here is required: every step can be passed
// over, and the same controls all live in Settings afterwards.
MDialog {
    id: root
    objectName: "onboardingDialog"
    property int step: 0
    readonly property int steps: 3
    readonly property bool lastStep: step===steps-1
    property string folderError: ""
    property bool folderAdded: false
    signal browseRequested()
    signal serverRequested()
    title: ["Welcome to Sung","Add your music","Where should Sung open?"][step]
    modal: true
    closePolicy: Popup.NoAutoClose
    anchors.centerIn: parent
    width: fitWidth(560)
    standardButtons: Dialog.NoButton
    function finish() {
        app.onboarded=true;
        close();
    }
    function setFolderPath(path) {
        built=true;
        body.item.folderField.text=path;
        folderError="";
        folderAdded=false;
    }
    function addFolder() {
        if(app.importingLocal)return;
        folderError=app.importMusicFolderPath(body.item.folderField.text);
        folderAdded=!folderError;
        if(folderError)body.item.folderField.forceActiveFocus();
    }
    onOpened: step=0
    contentItem: Loader {
        id: body
        active: root.built
        sourceComponent: ColumnLayout {
            readonly property Item folderField: folderPath
            spacing: 16
            SungText {
                objectName: "onboardingBlurb"
                Layout.fillWidth: true; Layout.minimumWidth: 0
                text: [
                    "Pick how Sung should look. You can change any of this later in Settings.",
                    "Point Sung at a folder of music and it will keep it up to date. Skip this if you only stream.",
                    "Choose the page Sung opens on. That is everything — enjoy."
                ][root.step]
                color: Theme.muted; font.pixelSize: Theme.bodyMedium; wrapMode: Text.Wrap
            }
            ColumnLayout {
                objectName: "onboardingAppearance"
                visible: root.step===0
                Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 12
                SungText { text: "Theme"; emphasized: true }
                MSegmentedControl {
                    accessibleName: "Theme"
                    options: [{key:"system",label:desktopTheme.available?"Noctalia":"System",name:"onboardTheme_system"},{key:"light",label:"Light",name:"onboardTheme_light"},{key:"dark",label:"Dark",name:"onboardTheme_dark"}]
                    value: app.theme; onChosen: value=>app.theme=value
                }
                SungText { text: "Accent color"; emphasized: true; Layout.topMargin: 4 }
                AccentPicker { Layout.fillWidth: true; Layout.minimumWidth: 0 }
            }
            ColumnLayout {
                objectName: "onboardingMusic"
                visible: root.step===1
                Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 12
                MTextField {
                    id: folderPath; objectName: "onboardingFolderPath"
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    label: "Folder path"; placeholderText: activeFocus?"/path/to/Music or ~/Music":""
                    onTextChanged: {root.folderError="";root.folderAdded=false;}
                    onAccepted: root.addFolder()
                    Accessible.description: root.folderError
                }
                SungText { objectName: "onboardingFolderError"; Layout.fillWidth: true; Layout.minimumWidth: 0; visible: !!root.folderError; text: root.folderError; color: Theme.error; wrapMode: Text.Wrap }
                SungText { objectName: "onboardingImporting"; Layout.fillWidth: true; Layout.minimumWidth: 0; visible: app.importingLocal; text: app.localImportStatus; color: Theme.muted; font.pixelSize: Theme.bodyMedium; wrapMode: Text.Wrap }
                MWavyProgress { objectName: "onboardingImportProgress"; Layout.fillWidth: true; Layout.minimumWidth: 0; visible: app.importingLocal; progress: app.localImportProgress; label: "Importing music" }
                SungText { objectName: "onboardingFolderDone"; Layout.fillWidth: true; Layout.minimumWidth: 0; visible: root.folderAdded && !app.importingLocal; text: "Folder added. Sung keeps it up to date while it is running."; color: Theme.muted; font.pixelSize: Theme.bodyMedium; wrapMode: Text.Wrap }
                RowLayout {
                    spacing: 8
                    MButton { objectName: "onboardingBrowse"; text: "Browse…"; symbol: "folder"; outlined: true; enabled: !app.importingLocal; onClicked: root.browseRequested() }
                    MButton { objectName: "onboardingAddFolder"; text: "Add folder"; tonal: true; enabled: folderPath.text.trim().length>0 && !app.importingLocal; busy: app.importingLocal; onClicked: root.addFolder() }
                }
            }
            ColumnLayout {
                objectName: "onboardingStart"
                visible: root.step===2
                Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 12
                MSegmentedControl {
                    accessibleName: "Start page"
                    options: [{key:"home",label:"Home",name:"onboardStart_home"},{key:"files",label:"Local",name:"onboardStart_files"},{key:"server",label:"Server",name:"onboardStart_server"},{key:"favorites",label:"Liked",name:"onboardStart_favorites"}]
                    value: app.startPage; onChosen: value=>app.startPage=value
                }
                MButton { objectName: "onboardingServer"; Layout.fillWidth: true; Layout.minimumWidth: 0; leftAligned: true; text: "Connect a music server…"; symbol: "library"; onClicked: root.serverRequested() }
                SungText { Layout.fillWidth: true; Layout.minimumWidth: 0; text: "Subsonic, Navidrome and Jellyfin are supported. You can connect one at any time from Settings."; color: Theme.muted; font.pixelSize: Theme.labelMedium; wrapMode: Text.Wrap }
            }
        }
    }
    footer: Loader {
        active: root.built
        sourceComponent: Item {
            implicitHeight: 76
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 24; anchors.rightMargin: 24; spacing: 8
                SungText { objectName: "onboardingProgress"; text: "Step "+(root.step+1)+" of "+root.steps; color: Theme.muted; font.pixelSize: Theme.labelMedium }
                Item { Layout.fillWidth: true }
                MButton { objectName: "onboardingSkip"; text: "Skip"; visible: !root.lastStep; onClicked: root.finish() }
                MButton { objectName: "onboardingBack"; text: "Back"; enabled: root.step>0; onClicked: root.step=Math.max(0,root.step-1) }
                MButton {
                    objectName: "onboardingNext"; text: root.lastStep?"Start listening":"Next"; filled: true
                    onClicked: {if(root.lastStep)root.finish();else root.step=Math.min(root.steps-1,root.step+1);}
                }
            }
        }
    }
}
