import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

MDialog {
    id: dialog
    objectName: "shortcutHelp"
    width: Math.min(540,parent.width-48); height: Math.min(660,parent.height-48)
    initialFocus: shortcuts
    title: "Keyboard shortcuts"; modal: true; standardButtons: Dialog.Close
    contentItem: ListView {
        id: shortcuts; objectName: "shortcutList"; clip: true; spacing: 4
        boundsBehavior: Flickable.StopAtBounds
        model: dialog.visible ? [
            ["Search music","Ctrl+K / Ctrl+F"],
            ["Show playing song","Ctrl+J"],
            ["Queue","Ctrl+L"],
            ["Lyrics","Ctrl+Y"],
            ["Play / pause¹","Space"],
            ["Previous / next track","Ctrl+← / →"],
            ["Seek 10 seconds¹","← / →"],
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
            SungText { id: description; text: modelData[0]; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 14 }
            SungText { text: modelData[1]; Layout.preferredWidth: 155; horizontalAlignment: Text.AlignRight; color: Theme.primary; font.pixelSize: 14 }
        }
        footer: SungText { width: shortcuts.width-16; text: "¹ Outside text fields and controls. ² With the song list focused."; wrapMode: Text.Wrap; color: Theme.muted; font.pixelSize: 12; topPadding: 16 }
    }
}
