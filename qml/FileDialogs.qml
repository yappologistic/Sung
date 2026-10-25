import QtQuick
import QtQuick.Dialogs

QtObject {
    id: dialogs
    required property Window ownerWindow
    readonly property bool visible: m3uExportPicker.visible || m3uImportPicker.visible || playlistCoverPicker.visible || exportPicker.visible || importPicker.visible || cookiePicker.visible || lyricPicker.visible || audioPicker.visible || locatePicker.visible || folderPicker.visible || artworkPicker.visible
    function open(kind) {
        if(kind === "playlist-cover") playlistCoverPicker.open();
        else if(kind === "artwork") {artworkPicker.songId=app.current.id || "";artworkPicker.open();}
        else if(kind === "m3u-export") {m3uExportPicker.playlistId=dialogs.ownerWindow.editPlaylistId;m3uExportPicker.open();}
        else if(kind === "m3u-import") m3uImportPicker.open();
        else if(kind === "folder") folderPicker.open();
        else if(kind === "audio") audioPicker.open();
        else if(kind === "locate") {locatePicker.songId=app.current.id || "";locatePicker.songId=dialogs.ownerWindow.menuItem.id || locatePicker.songId;locatePicker.open();}
        else if(kind === "export") exportPicker.open();
        else if(kind === "import") importPicker.open();
        else if(kind === "lyrics") { lyricPicker.songId=app.current.id || ""; lyricPicker.open(); }
        else if(kind === "cookies") cookiePicker.open();
    }
    property FileDialog playlistCoverPicker: FileDialog { parentWindow:dialogs.ownerWindow; title:qsTranslate("Main", "Choose playlist cover"); nameFilters:[qsTranslate("Main", "Images (*.png *.jpg *.jpeg *.webp)")]; onAccepted:dialogs.ownerWindow.previewPlaylistCover(selectedFile) }
    property FileDialog artworkPicker: FileDialog { parentWindow: dialogs.ownerWindow; property string songId; title: "Choose animated cover"; nameFilters: ["Animated covers (*.gif *.webp *.mp4 *.webm)"]; onAccepted: app.chooseArtwork(selectedFile,songId) }
    readonly property var playlistFilters: [qsTranslate("Main", "Playlists (*.m3u8 *.m3u)")]
    property FileDialog m3uExportPicker: FileDialog { objectName: "m3uExportPicker"; parentWindow: dialogs.ownerWindow; property string playlistId; title: qsTranslate("Main", "Export playlist"); fileMode: FileDialog.SaveFile; defaultSuffix: "m3u8"; nameFilters: dialogs.playlistFilters; onAccepted: app.exportPlaylistM3u(playlistId,selectedFile) }
    property FileDialog m3uImportPicker: FileDialog { objectName: "m3uImportPicker"; parentWindow: dialogs.ownerWindow; title: qsTranslate("Main", "Import playlist"); nameFilters: dialogs.playlistFilters; onAccepted: app.importPlaylistM3u(selectedFile) }
    readonly property var audioFilters: [qsTranslate("Main", "Audio files (*.mp3 *.flac *.ogg *.opus *.m4a *.aac *.wav *.aiff *.aif *.wma)")]
    property FolderDialog folderPicker: FolderDialog { objectName: "folderPicker"; parentWindow: dialogs.ownerWindow; title: qsTranslate("Main", "Add music folder"); onAccepted: dialogs.ownerWindow.finishFolderPick(selectedFolder); onRejected: dialogs.ownerWindow.returnToFolderEntry() }
    property FileDialog audioPicker: FileDialog { objectName: "audioPicker"; parentWindow: dialogs.ownerWindow; title: qsTranslate("Main", "Add music"); fileMode: FileDialog.OpenFiles; nameFilters: dialogs.audioFilters; onAccepted: app.importLocalFiles(selectedFiles) }
    property FileDialog locatePicker: FileDialog { parentWindow: dialogs.ownerWindow; property string songId; title: qsTranslate("Main", "Locate audio file"); nameFilters: dialogs.audioFilters; onAccepted: app.locateLocalFile(selectedFile,songId) }
    property FileDialog exportPicker: FileDialog { parentWindow: dialogs.ownerWindow; objectName: "exportPicker"; title: qsTranslate("Main", "Export library"); fileMode: FileDialog.SaveFile; defaultSuffix: "json"; nameFilters: [qsTranslate("Main", "Sung library (*.json)")]; onAccepted: app.exportLibrary(selectedFile) }
    property FileDialog importPicker: FileDialog { parentWindow: dialogs.ownerWindow; objectName: "importPicker"; title: qsTranslate("Main", "Import library"); nameFilters: [qsTranslate("Main", "Sung library (*.json)")]; onAccepted: app.importLibrary(selectedFile) }
    property FileDialog lyricPicker: FileDialog { parentWindow: dialogs.ownerWindow; property string songId; title: qsTranslate("Main", "Import lyrics"); nameFilters: [qsTranslate("Main", "Timed lyrics (*.lrc)")]; onAccepted: app.importLyrics(selectedFile,songId) }
    property FileDialog cookiePicker: FileDialog { parentWindow: dialogs.ownerWindow; objectName: "cookiePicker"; title: qsTranslate("Main", "Import YouTube cookies"); nameFilters: [qsTranslate("Main", "Cookie files (*.txt)")]; onAccepted: app.setCookieFile(selectedFile) }
}
