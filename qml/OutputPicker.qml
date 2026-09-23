import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Popup {
    id:popup;objectName:"outputPicker";width:Math.min(330,parent.width-32);height:Math.min(380,parent.height-32,devices.contentHeight+96);padding:16;focus:true
    property point anchorPosition:Qt.point(0,0)
    x:Math.max(16,Math.min(parent.width-width-16,anchorPosition.x-width))
    y:Math.max(16,Math.min(parent.height-height-16,anchorPosition.y-height-10))
    function showAt(anchor){const at=anchor.mapToItem(parent,0,0);anchorPosition=Qt.point(at.x+anchor.width,at.y);open();}
    onOpened: {devices.currentIndex=app.audioDevices.findIndex(d=>d.id===app.audioDeviceId);devices.forceActiveFocus();}
    closePolicy:Popup.CloseOnEscape|Popup.CloseOnPressOutside
    background:Rectangle {radius:Theme.shapeExtraLarge;color:Theme.high;border.width:1;border.color:Theme.outlineVariant}
    enter:Transition {NumberAnimation {property:"opacity";from:0;to:1;duration:Theme.enterDuration;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.enterCurve}}
    exit:Transition {NumberAnimation {property:"opacity";to:0;duration:Theme.exitDuration;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.exitCurve}}
    contentItem:ColumnLayout {spacing:8
        SungText {heading:true;text:"Audio output";font.pixelSize:Theme.titleMedium;font.weight:Font.Medium;Layout.fillWidth:true}
        ListView {id:devices;objectName:"outputDevices";Layout.fillWidth:true;Layout.fillHeight:true;clip:true;spacing:4;model:popup.visible?app.audioDevices:[]
            ScrollBar.vertical:MScrollBar {}
            Keys.onReturnPressed: {const device=app.audioDevices[currentIndex];if(device){app.audioDeviceId=device.id;popup.close();}}
            // Material draws a choice of one out of a set as radio buttons in
            // the leading slot, not as a check that appears once it is made.
            delegate:MButton {
                required property var modelData;required property int index
                objectName:"outputChoice_"+index;width:devices.width-8;height:48
                text:modelData.name;leftAligned:true;contentInset:44
                selected:app.audioDeviceId===modelData.id;tip:modelData.name
                onClicked:{app.audioDeviceId=modelData.id;popup.close();}
                MRadioButton {
                    objectName:"outputRadio_"+parent.index
                    anchors.left:parent.left;anchors.leftMargin:-4;anchors.verticalCenter:parent.verticalCenter
                    checked:parent.selected;enabled:false;presentational:true
                    Accessible.ignored:true
                }
            }
        }
    }
}
