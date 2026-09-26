import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
MDialog {
    id: dialog; objectName: "trackDetailsDialog"
    title: "Track details"; modal: true; standardButtons: Dialog.Close
    width: fitWidth(520); height: fitHeight(560)
    property var details: []
    property var inspectedTrack: ({})
    function inspect(track) { inspectedTrack=track;details=app.trackDetails(track);open(); }
    Connections {target:app;function onQualityChanged(){if(dialog.visible)dialog.details=app.trackDetails(dialog.inspectedTrack);}function onTrackChanged(){if(dialog.visible)dialog.details=app.trackDetails(dialog.inspectedTrack);}}
    onClosed: details=[]
    // The body is built the first time the dialog opens. MDialog sets
    // `built` on aboutToShow, which runs before the enter transition, so
    // the first frame of that transition already has the content in it.
    Loader {
        anchors.fill: parent
        active: dialog.built
        sourceComponent: Component {
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
                            SungText { text: modelData.label; color: Theme.muted; font.pixelSize: Theme.labelMedium; labelRole: true }
                            TextEdit { Layout.fillWidth: true; text: modelData.value; textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; color: Theme.text; selectionColor: Theme.textSelection; selectedTextColor: Theme.text; font.family: Theme.fontFamily; font.pixelSize: Theme.bodyLarge; Accessible.name: modelData.label+": "+modelData.value }
                        }
                    }
                }
            }
        }
    }
}
