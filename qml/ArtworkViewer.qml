import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
MDialog {
    id: viewer; objectName:"artworkViewer"
    title:"Artwork";modal:true;anchors.centerIn:parent
    width:Math.min(1040,parent.width-48);height:Math.min(1000,parent.height-48)
    standardButtons:Dialog.Close;initialFocus:canvas
    property string source:""
    property real zoom:1
    function inspect(url){source=url;zoom=1;open();}
    function zoomTo(value){
        const old=zoom, next=Math.max(1,Math.min(4,value));
        const cx=canvas.contentX+canvas.width/2, cy=canvas.contentY+canvas.height/2;
        zoom=next;
        canvas.contentX=Math.max(0,Math.min(canvas.contentWidth-canvas.width,cx*next/old-canvas.width/2));
        canvas.contentY=Math.max(0,Math.min(canvas.contentHeight-canvas.height,cy*next/old-canvas.height/2));
    }
    onClosed:{source="";zoom=1;}
    contentItem:ColumnLayout {
        spacing:12
        Flickable {
            id:canvas;objectName:"artworkCanvas";Layout.fillWidth:true;Layout.fillHeight:true
            clip:true;boundsBehavior:Flickable.StopAtBounds
            readonly property real extent:Math.min(width,height)*viewer.zoom
            contentWidth:Math.max(width,extent);contentHeight:Math.max(height,extent)
            Artwork {objectName:"inspectedArtwork";x:(canvas.contentWidth-width)/2;y:(canvas.contentHeight-height)/2;width:canvas.extent;height:width
                url:viewer.visible?viewer.source:"";fit:true;radius:0;highResolution:true
            }
            WheelHandler {onWheel:event=>{viewer.zoomTo(viewer.zoom*Math.pow(1.0015,event.angleDelta.y));event.accepted=true;}}
        }
        RowLayout {Layout.alignment:Qt.AlignHCenter;spacing:8
            MButton {objectName:"artworkZoomOut";text:"−";tip:"Zoom out";enabled:viewer.zoom>1;onClicked:viewer.zoomTo(viewer.zoom/1.25)}
            MButton {objectName:"artworkZoomReset";text:Math.round(viewer.zoom*100)+"%";tip:"Reset zoom";onClicked:viewer.zoomTo(1)}
            MButton {objectName:"artworkZoomIn";text:"+";tip:"Zoom in";enabled:viewer.zoom<4;onClicked:viewer.zoomTo(viewer.zoom*1.25)}
        }
        Keys.onPressed:event=>{
            if(event.key===Qt.Key_Plus || event.key===Qt.Key_Equal){viewer.zoomTo(viewer.zoom*1.25);event.accepted=true;}
            else if(event.key===Qt.Key_Minus){viewer.zoomTo(viewer.zoom/1.25);event.accepted=true;}
            else if(event.key===Qt.Key_0){viewer.zoomTo(1);event.accepted=true;}
        }
    }
}
