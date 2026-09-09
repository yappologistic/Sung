import QtQuick
import QtQuick.Dialogs

QtObject {
    id: dialogs
    required property Window ownerWindow
    readonly property bool visible: playlistCoverPicker.visible || exportPicker.visible || importPicker.visible || cookiePicker.visible || lyricPicker.visible || audioPicker.visible || locatePicker.visible || folderPicker.visible || artworkPicker.visible
    function open(kind) {
        if(kind === "playlist-cover") playlistCoverPicker.open();
        else if(kind === "artwork") {artworkPicker.songId=app.current.id || "";artworkPicker.open();}
        else if(kind === "folder") folderPicker.open();
        else if(kind === "audio") audioPicker.open();
        else if(kind === "locate") {locatePicker.songId=app.current.id || "";locatePicker.songId=dialogs.ownerWindow.menuItem.id || locatePicker.songId;locatePicker.open();}
        else if(kind === "export") exportPicker.open();
        else if(kind === "import") importPicker.open();
        else if(kind === "lyrics") { lyricPicker.songId=app.current.id || ""; lyricPicker.open(); }
        else if(kind === "cookies") cookiePicker.open();
    }
    property FileDialog playlistCoverPicker: FileDialog { parentWindow:dialogs.ownerWindow; title:"Choose playlist cover"; nameFilters:["Images (*.png *.jpg *.jpeg *.webp)"]; onAccepted:dialogs.ownerWindow.previewPlaylistCover(selectedFile) }
    property FileDialog artworkPicker: FileDialog { parentWindow: dialogs.ownerWindow; property string songId; title: "Choose animated cover"; nameFilters: ["Animated covers (*.gif *.webp *.mp4 *.webm)"]; onAccepted: app.chooseArtwork(selectedFile,songId) }
    readonly property var audioFilters: ["Audio files (*.mp3 *.flac *.ogg *.opus *.m4a *.aac *.wav *.aiff *.aif *.wma)"]
    property FolderDialog folderPicker: FolderDialog { objectName: "folderPicker"; parentWindow: dialogs.ownerWindow; title: "Add music folder"; onAccepted: dialogs.ownerWindow.finishFolderPick(selectedFolder); onRejected: dialogs.ownerWindow.returnToFolderEntry() }
    property FileDialog audioPicker: FileDialog { objectName: "audioPicker"; parentWindow: dialogs.ownerWindow; title: "Add music"; fileMode: FileDialog.OpenFiles; nameFilters: dialogs.audioFilters; onAccepted: app.importLocalFiles(selectedFiles) }
    property FileDialog locatePicker: FileDialog { parentWindow: dialogs.ownerWindow; property string songId; title: "Locate audio file"; nameFilters: dialogs.audioFilters; onAccepted: app.locateLocalFile(selectedFile,songId) }
    property FileDialog exportPicker: FileDialog { parentWindow: dialogs.ownerWindow; objectName: "exportPicker"; title: "Export library"; fileMode: FileDialog.SaveFile; defaultSuffix: "json"; nameFilters: ["Sung library (*.json)"]; onAccepted: app.exportLibrary(selectedFile) }
    property FileDialog importPicker: FileDialog { parentWindow: dialogs.ownerWindow; objectName: "importPicker"; title: "Import library"; nameFilters: ["Sung library (*.json)"]; onAccepted: app.importLibrary(selectedFile) }
    property FileDialog lyricPicker: FileDialog { parentWindow: dialogs.ownerWindow; property string songId; title: "Import lyrics"; nameFilters: ["Timed lyrics (*.lrc)"]; onAccepted: app.importLyrics(selectedFile,songId) }
    property FileDialog cookiePicker: FileDialog { parentWindow: dialogs.ownerWindow; objectName: "cookiePicker"; title: "Import YouTube cookies"; nameFilters: ["Cookie files (*.txt)"]; onAccepted: app.setCookieFile(selectedFile) }
}
