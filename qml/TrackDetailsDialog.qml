import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
MDialog {
    id: dialog; objectName: "trackDetailsDialog"
    title: "Track details"; modal: true; standardButtons: Dialog.Close
    width: Math.min(520,parent.width-48); height: Math.min(560,parent.height-48)
    property var details: []
    function inspect(track) { details=app.trackDetails(track); open(); }
    onClosed: details=[]
    ScrollView {
        id: scroll; objectName: "detailsScroll"; anchors.fill: parent; clip: true; contentWidth: availableWidth; contentHeight: fields.implicitHeight; rightPadding: 10
        ScrollBar.vertical: MScrollBar { parent: scroll; x: scroll.width-width; height: scroll.availableHeight; orientation: Qt.Vertical }
        ColumnLayout {
            id: fields; width: scroll.availableWidth; spacing: 16
            Repeater {
                model: dialog.details
                ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true; spacing: 4
                    SungText { text: modelData.label; color: Theme.muted; font.pixelSize: 12 }
                    TextEdit { Layout.fillWidth: true; text: modelData.value; textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; color: Theme.text; selectionColor: Theme.primary; selectedTextColor: Theme.primaryText; font.family: Theme.fontFamily; font.pixelSize: 16; Accessible.name: modelData.label+": "+modelData.value }
                }
            }
        }
    }
}
