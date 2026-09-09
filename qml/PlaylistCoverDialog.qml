import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

MDialog {
    id: dialog; objectName: "playlistCoverDialog"; title: "Playlist cover"; modal: true
    width: Math.min(440,parent.width-48); height: Math.min(650,parent.height-32)
    property string playlistId: ""
    property string preview: ""
    property string failure: ""
    signal chooseFile()
    onPreviewChanged: {horizontal.value=0.5;vertical.value=0.5;zoom.value=1;failure=preview?"":"Choose a PNG, JPEG or WebP image";}
    onClosed: preview=""
    standardButtons: Dialog.NoButton
    ColumnLayout {
        anchors.fill: parent; spacing: 10
        Item {
            id: crop; Layout.fillWidth:true; Layout.fillHeight:true; clip:true
            Item {
                id: square; width:Math.min(parent.width,parent.height); height:width; anchors.centerIn:parent; clip:true
                Rectangle {anchors.fill:parent;color:Theme.high;radius:20}
                Image {
                    id: picture; source:dialog.preview; cache:false; visible:!!dialog.preview
                    readonly property real ratio:sourceSize.height>0?sourceSize.width/sourceSize.height:1
                    width:square.width*Math.max(1,ratio)*zoom.value; height:width/ratio
                    x:-(width-square.width)*horizontal.value; y:-(height-square.height)*vertical.value
                }
                SungText {anchors.centerIn:parent;visible:!dialog.preview;text:"Choose a cover";color:Theme.muted}
            }
        }
        RowLayout {Layout.fillWidth:true; visible:!!dialog.preview
            SungText {text:"Horizontal"; Layout.preferredWidth:82}
            SettingSlider {id:horizontal; objectName:"coverCropX"; Layout.fillWidth:true; from:0;to:1;value:0.5;Accessible.name:"Horizontal crop position"}
        }
        RowLayout {Layout.fillWidth:true; visible:!!dialog.preview
            SungText {text:"Vertical"; Layout.preferredWidth:82}
            SettingSlider {id:vertical; objectName:"coverCropY"; Layout.fillWidth:true; from:0;to:1;value:0.5;Accessible.name:"Vertical crop position"}
        }
        RowLayout {Layout.fillWidth:true; visible:!!dialog.preview
            SungText {text:"Zoom"; Layout.preferredWidth:82}
            SettingSlider {id:zoom; objectName:"coverCropZoom"; Layout.fillWidth:true; from:1;to:3;value:1;Accessible.name:"Cover zoom"}
        }
        SungText {text:dialog.failure; visible:!!text; Layout.fillWidth:true; wrapMode:Text.Wrap; font.pixelSize:12; color:Theme.muted}
        RowLayout {Layout.fillWidth:true
            MButton {text:"Choose image…"; onClicked:dialog.chooseFile()}
            Item {Layout.fillWidth:true}
            MButton {text:"Cancel"; onClicked:dialog.close()}
            MButton {objectName:"savePlaylistCover";text:"Save";filled:true;enabled:!!dialog.preview;onClicked:{if(app.setPlaylistCover(dialog.playlistId,dialog.preview,horizontal.value,vertical.value,zoom.value))dialog.close();else dialog.failure="Couldn’t save this cover";}}
        }
    }
}
