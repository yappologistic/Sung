import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

MDialog {
    id: dialog
    objectName: "artworkControls"
    signal chooseFile()
    signal inspectRequested(string url)
    title: "Artwork"; modal: true; standardButtons: Dialog.Close
    // AlertDialog.kt:147,409-412 measures content within a 280-560dp width;
    // this dialog prefers 440dp and the window limits its final size.
    // ScrollView takes over when the content is taller than the window.
    width: fitWidth(440); height: fitHeight(implicitHeight)
    contentHeight: bodyLoader.implicitHeight
    // The body is built the first time the dialog opens. MDialog sets
    // `built` on aboutToShow, which runs before the enter transition, so
    // the first frame of that transition already has the content in it.
    // Popup's contentItem is positioned below its header and inside padding.
    // Keep this Loader inside the default contentItem so anchors cannot replace that layout.
    Loader {
        id: bodyLoader
        anchors.fill: parent
        active: dialog.built
        sourceComponent: Component {
            ScrollView {
                implicitHeight: bodyColumn.implicitHeight
                contentWidth: availableWidth; clip: true
                ScrollBar.vertical: MScrollBar {}
                ColumnLayout {
                    id: bodyColumn
                    width: parent.width; spacing: 8
                    Artwork { objectName: "coverPreview"; Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 128; Layout.preferredHeight: 128; url: app.current.art || ""; motionUrl: dialog.visible?app.currentMotionArt:""; pixels: 384; radius: Theme.shapeExtraLarge; fit:app.currentArtworkFit
                        AbstractButton {objectName:"inspectArtworkButton";anchors.fill:parent;enabled:!!app.current.art;focusPolicy:Qt.StrongFocus;hoverEnabled:true;Accessible.name:"View artwork";onClicked:{dialog.close();dialog.inspectRequested(app.current.art);}
                            background:Rectangle {color:"transparent";radius:Theme.shapeExtraLarge;border.width:parent.visualFocus?2:0;border.color:Theme.focusRing}
                            Icon {anchors.right:parent.right;anchors.bottom:parent.bottom;anchors.margins:8;name:"expand";visible:parent.hovered || parent.visualFocus}
                            ToolTip.visible:hovered;ToolTip.text:"View artwork";ToolTip.delay:650
                        }
                    }
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
    }
}
