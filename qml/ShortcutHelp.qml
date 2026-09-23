import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

MDialog {
    id: dialog
    objectName: "shortcutHelp"
    width: fitWidth(540); height: fitHeight(660)
    initialFocus: shortcuts
    title: "Keyboard shortcuts"; modal: true; standardButtons: Dialog.Close
    scrollSource: shortcuts
    contentItem: ListView {
        id: shortcuts; objectName: "shortcutList"; clip: true; spacing: 4
        boundsBehavior: Flickable.StopAtBounds
        model: dialog.visible ? [
            ["Search music","Ctrl+K / Ctrl+F"],
            ["Quick actions","Ctrl+Shift+P"],
            ["Show playing song","Ctrl+J"],
            ["Queue","Ctrl+L"],
            ["Lyrics","Ctrl+Y"],
            ["Play / pause¹","Space"],
            ["Previous / next track","Ctrl+← / →"],
            ["Seek 10 seconds¹","← / →"],
            ["Volume¹","Ctrl+↑ / ↓"],
            ["Mute¹","M"],
            ["Back","Alt+←"],
            ["Mini player","Ctrl+M"],
            ["Immersive player","F11"],
            ["Select all songs²","Ctrl+A"],
            ["Extend selection²","Shift+↑ / ↓"],
            ["Toggle selection²","Ctrl+Space"],
            ["Play focused song²","Enter"],
            ["Track menu²","Shift+F10"],
            ["Remove selection²","Delete"],
            ["Close / clear selection","Esc"],
            ["Keyboard shortcuts¹","? / F1"],
            ["Quit","Ctrl+Q"]
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
        footer: SungText { width: shortcuts.width-16; text: "¹ Outside text fields and controls. ² With the song list focused."; wrapMode: Text.Wrap; color: Theme.muted; font.pixelSize: Theme.bodySmall; topPadding: 16 }
    }
}
