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
    background:Rectangle {radius:24;color:Theme.high;border.width:1;border.color:Theme.outline}
    enter:Transition {NumberAnimation {property:"opacity";from:0;to:1;duration:Theme.enterDuration}}
    exit:Transition {NumberAnimation {property:"opacity";to:0;duration:Theme.exitDuration}}
    contentItem:ColumnLayout {spacing:8
        SungText {text:"Audio output";font.pixelSize:18;font.weight:Font.Medium;Layout.fillWidth:true}
        ListView {id:devices;objectName:"outputDevices";Layout.fillWidth:true;Layout.fillHeight:true;clip:true;spacing:4;model:popup.visible?app.audioDevices:[]
            ScrollBar.vertical:MScrollBar {}
            Keys.onReturnPressed: {const device=app.audioDevices[currentIndex];if(device){app.audioDeviceId=device.id;popup.close();}}
            delegate:MButton {required property var modelData;required property int index;objectName:"outputChoice_"+index;width:devices.width-8;height:48;text:modelData.name;leftAligned:true;selected:app.audioDeviceId===modelData.id;symbol:selected?"check":"";tip:modelData.name;onClicked:{app.audioDeviceId=modelData.id;popup.close();}}
        }
    }
}
