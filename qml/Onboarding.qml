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
    title: [qsTranslate("Main", "Welcome to Sung"),qsTranslate("Main", "Add your music"),qsTranslate("Main", "Where should Sung open?")][step]
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
                    qsTranslate("Main", "Pick how Sung should look. You can change any of this later in Settings."),
                    qsTranslate("Main", "Point Sung at a folder of music and it will keep it up to date. Skip this if you only stream."),
                    qsTranslate("Main", "Choose the page Sung opens on. That is everything — enjoy.")
                ][root.step]
                color: Theme.muted; font.pixelSize: Theme.bodyMedium; wrapMode: Text.Wrap
            }
            ColumnLayout {
                objectName: "onboardingAppearance"
                visible: root.step===0
                Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 12
                SungText { text: qsTranslate("Main", "Theme"); emphasized: true }
                MSegmentedControl {
                    accessibleName: qsTranslate("Main", "Theme")
                    options: [{key:"system",label:desktopTheme.available?"Noctalia":qsTranslate("Main", "System"),name:"onboardTheme_system"},{key:"light",label:qsTranslate("Main", "Light"),name:"onboardTheme_light"},{key:"dark",label:qsTranslate("Main", "Dark"),name:"onboardTheme_dark"}]
                    value: app.theme; onChosen: value=>app.theme=value
                }
                SungText { text: qsTranslate("Main", "Accent color"); emphasized: true; Layout.topMargin: 4 }
                AccentPicker { Layout.fillWidth: true; Layout.minimumWidth: 0 }
            }
            ColumnLayout {
                objectName: "onboardingMusic"
                visible: root.step===1
                Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 12
                MTextField {
                    id: folderPath; objectName: "onboardingFolderPath"
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    label: qsTranslate("Main", "Folder path"); placeholderText: activeFocus?qsTranslate("Main", "/path/to/Music or ~/Music"):""
                    onTextChanged: {root.folderError="";root.folderAdded=false;}
                    onAccepted: root.addFolder()
                    Accessible.description: root.folderError
                }
                SungText { objectName: "onboardingFolderError"; Layout.fillWidth: true; Layout.minimumWidth: 0; visible: !!root.folderError; text: root.folderError; color: Theme.error; wrapMode: Text.Wrap }
                SungText { objectName: "onboardingImporting"; Layout.fillWidth: true; Layout.minimumWidth: 0; visible: app.importingLocal; text: app.localImportStatus; color: Theme.muted; font.pixelSize: Theme.bodyMedium; wrapMode: Text.Wrap }
                MWavyProgress { objectName: "onboardingImportProgress"; Layout.fillWidth: true; Layout.minimumWidth: 0; visible: app.importingLocal; progress: app.localImportProgress; label: qsTranslate("Main", "Importing music") }
                SungText { objectName: "onboardingFolderDone"; Layout.fillWidth: true; Layout.minimumWidth: 0; visible: root.folderAdded && !app.importingLocal; text: qsTranslate("Main", "Folder added. Sung keeps it up to date while it is running."); color: Theme.muted; font.pixelSize: Theme.bodyMedium; wrapMode: Text.Wrap }
                RowLayout {
                    spacing: 8
                    MButton { objectName: "onboardingBrowse"; text: qsTranslate("Main", "Browse…"); symbol: "folder"; outlined: true; enabled: !app.importingLocal; onClicked: root.browseRequested() }
                    MButton { objectName: "onboardingAddFolder"; text: qsTranslate("Main", "Add folder"); tonal: true; enabled: folderPath.text.trim().length>0 && !app.importingLocal; busy: app.importingLocal; onClicked: root.addFolder() }
                }
            }
            ColumnLayout {
                objectName: "onboardingStart"
                visible: root.step===2
                Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 12
                MSegmentedControl {
                    accessibleName: qsTranslate("Main", "Start page")
                    options: [{key:"home",label:qsTranslate("Navigation", "Home"),name:"onboardStart_home"},{key:"files",label:qsTranslate("Main", "Local"),name:"onboardStart_files"},{key:"server",label:qsTranslate("Main", "Server"),name:"onboardStart_server"},{key:"favorites",label:qsTranslate("Main", "Liked"),name:"onboardStart_favorites"}]
                    value: app.startPage; onChosen: value=>app.startPage=value
                }
                MButton { objectName: "onboardingServer"; Layout.fillWidth: true; Layout.minimumWidth: 0; leftAligned: true; text: qsTranslate("Main", "Connect a music server…"); symbol: "library"; onClicked: root.serverRequested() }
                SungText { Layout.fillWidth: true; Layout.minimumWidth: 0; text: qsTranslate("Main", "Subsonic, Navidrome and Jellyfin are supported. You can connect one at any time from Settings."); color: Theme.muted; font.pixelSize: Theme.labelMedium; wrapMode: Text.Wrap }
            }
        }
    }
    footer: Loader {
        active: root.built
        sourceComponent: Item {
            implicitHeight: 76
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 24; anchors.rightMargin: 24; spacing: 8
                SungText { objectName: "onboardingProgress"; text: qsTranslate("Main", "Step %1 of %2").arg(root.step+1).arg(root.steps); color: Theme.muted; font.pixelSize: Theme.labelMedium }
                Item { Layout.fillWidth: true }
                MButton { objectName: "onboardingSkip"; text: qsTranslate("Main", "Skip"); visible: !root.lastStep; onClicked: root.finish() }
                MButton { objectName: "onboardingBack"; text: qsTranslate("Main", "Back"); enabled: root.step>0; onClicked: root.step=Math.max(0,root.step-1) }
                MButton {
                    objectName: "onboardingNext"; text: root.lastStep?qsTranslate("Main", "Start listening"):qsTranslate("Main", "Next"); filled: true
                    onClicked: {if(root.lastStep)root.finish();else root.step=Math.min(root.steps-1,root.step+1);}
                }
            }
        }
    }
}
