import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

MDialog {
    id: dialog
    objectName: "shortcutHelp"
    width: fitWidth(540); height: fitHeight(660)
    initialFocus: shortcuts
    title: qsTranslate("Main", "Keyboard shortcuts"); modal: true; standardButtons: Dialog.Close
    scrollSource: shortcuts
    contentItem: ListView {
        id: shortcuts; objectName: "shortcutList"; clip: true; spacing: 4
        boundsBehavior: Flickable.StopAtBounds
        model: dialog.visible ? [
            [qsTranslate("Main", "Search music"),"Ctrl+K / Ctrl+F"],
            [qsTranslate("Main", "Quick actions"),"Ctrl+Shift+P"],
            [qsTranslate("Main", "Show playing song"),"Ctrl+J"],
            [qsTranslate("Main", "Queue"),"Ctrl+L"],
            [qsTranslate("Main", "Lyrics"),"Ctrl+Y"],
            [qsTranslate("Main", "Play / pause¹"),"Space"],
            [qsTranslate("Main", "Previous / next track"),"Ctrl+← / →"],
            [qsTranslate("Main", "Seek 10 seconds¹"),"← / →"],
            [qsTranslate("Main", "Volume")+"¹","Ctrl+↑ / ↓"],
            [qsTranslate("Main", "Mute¹"),"M"],
            [qsTranslate("Main", "Back"),"Alt+←"],
            [qsTranslate("Main", "Mini player"),"Ctrl+M"],
            [qsTranslate("Main", "Immersive player"),"F11"],
            [qsTranslate("Main", "Select all songs²"),"Ctrl+A"],
            [qsTranslate("Main", "Extend selection²"),"Shift+↑ / ↓"],
            [qsTranslate("Main", "Toggle selection²"),"Ctrl+Space"],
            [qsTranslate("Main", "Play focused song²"),"Enter"],
            [qsTranslate("Main", "Track menu²"),"Shift+F10"],
            [qsTranslate("Main", "Remove selection²"),"Delete"],
            [qsTranslate("Main", "Close / clear selection"),"Esc"],
            [qsTranslate("Main", "Keyboard shortcuts")+"¹","? / F1"],
            [qsTranslate("Main", "Quit Sung"),"Ctrl+Q"]
        ] : []
        ScrollBar.vertical: MScrollBar {}
        delegate: RowLayout {
            required property var modelData
            width: shortcuts.width-12; height: Math.max(48,description.implicitHeight+16); spacing: 16
            SungText { id: description; text: modelData[0]; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: Theme.bodyMedium }
            // ListTokens.kt:341-342 sets ItemTrailingSupportingTextColor to OnSurfaceVariant;
            // these key combinations support the action label at the row end.
            SungText { objectName: "shortcutKeys"; text: modelData[1]; Layout.preferredWidth: 155; horizontalAlignment: Text.AlignRight; color: Theme.muted; font.pixelSize: Theme.labelLarge; labelRole: true }
        }
        footer: SungText { width: shortcuts.width-16; text: qsTranslate("Main", "¹ Outside text fields and controls. ² With the song list focused."); wrapMode: Text.Wrap; color: Theme.muted; font.pixelSize: Theme.bodySmall; topPadding: 16 }
    }
}
