import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

MDialog {
    id: dialog
    objectName: "artworkControls"
    signal chooseFile()
    title: "Artwork"; modal: true; standardButtons: Dialog.Close
    width: Math.min(440,parent.width-48); height: Math.min(640,parent.height-48)
    contentItem: ScrollView {
        contentWidth: availableWidth; clip: true
        ScrollBar.vertical: MScrollBar {}
        ColumnLayout {
            width: parent.width; spacing: 8
            Artwork { objectName: "coverPreview"; Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 128; Layout.preferredHeight: 128; url: app.current.art || ""; motionUrl: dialog.visible?app.currentMotionArt:""; pixels: 384; radius: 24; fit:app.currentArtworkFit }
            RowLayout {Layout.alignment:Qt.AlignHCenter
                MChip {objectName:"artworkFill";text:"Fill";selected:!app.currentArtworkFit;onClicked:app.currentArtworkFit=false}
                MChip {objectName:"artworkFit";text:"Fit";selected:app.currentArtworkFit;onClicked:app.currentArtworkFit=true}
            }
            SungText { Layout.fillWidth: true; Layout.preferredHeight: 40; text: app.artworkStatus; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; color: Theme.muted }
            MButton { Layout.fillWidth: true; Layout.preferredHeight: 40; text: "View source album"; visible: !!app.artworkPage; onClicked: Qt.openUrlExternally(app.artworkPage) }
            MButton { objectName: "retryArtworkButton"; Layout.fillWidth: true; Layout.preferredHeight: 40; text: "Find cover again"; enabled: !!app.current.videoId; onClicked: app.retryArtwork() }
            MButton { objectName: "chooseArtworkButton"; tonal: true; Layout.fillWidth: true; Layout.preferredHeight: 40; text: "Choose local animation…"; onClicked: dialog.chooseFile() }
            MButton { objectName: "rejectArtworkButton"; Layout.fillWidth: true; Layout.preferredHeight: 40; text: "Disable for this song"; enabled: !!app.currentMotionArt; onClicked: app.rejectArtwork() }
            MButton { objectName: "resetArtworkButton"; Layout.fillWidth: true; Layout.preferredHeight: 40; text: "Use automatic cover"; onClicked: app.resetArtworkChoice() }
        }
    }
}
