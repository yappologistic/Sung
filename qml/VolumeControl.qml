import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
RowLayout {
    id:control
    property bool showSlider:true
    property string buttonName:"exactVolumeButton"
    property string sliderName:"popupVolumeSlider"
    readonly property bool popupVisible:popup.visible
    spacing:0
    MButton {id:button;objectName:control.buttonName;symbol:app.volume>0?"volume":"mute";tip:"Volume · "+Math.round(app.volume*100)+"%";selected:popup.visible;onClicked:popup.open()}
    SeekBar {volumeMode:true;visible:control.showSlider;Layout.preferredWidth:66}
    Popup {
        id:popup;objectName:"volumePopup";parent:Overlay.overlay;width:264;height:184;padding:16;focus:true
        x:Math.max(12,Math.min(parent.width-width-12,button.mapToItem(parent,0,0).x-width+button.width))
        y:Math.max(12,Math.min(parent.height-height-12,button.mapToItem(parent,0,0).y-height-10))
        onOpened:{percent.text=String(Math.round(app.volume*100));percent.forceActiveFocus();percent.selectAll();}
        function apply(){if(percent.acceptableInput){app.volume=Number(percent.text)/100;close();}}
        closePolicy:Popup.CloseOnEscape|Popup.CloseOnPressOutside
        background:Rectangle {radius:24;color:Theme.high;border.width:1;border.color:Theme.outline}
        enter:Transition {NumberAnimation {property:"opacity";from:0;to:1;duration:Theme.enterDuration}}
        exit:Transition {NumberAnimation {property:"opacity";to:0;duration:Theme.exitDuration}}
        ColumnLayout {anchors.fill:parent;spacing:8
            RowLayout {Layout.fillWidth:true
                SungText {text:"Volume";Layout.fillWidth:true;font.pixelSize:18}
                MTextField {id:percent;objectName:"volumePercent";Layout.preferredWidth:72;implicitHeight:40;topPadding:10;bottomPadding:10;maximumLength:3;validator:IntValidator {bottom:0;top:100} inputMethodHints:Qt.ImhDigitsOnly;Accessible.name:"Volume percent";onAccepted:popup.apply()}
                SungText {text:"%";color:Theme.muted}
            }
            SeekBar {objectName:control.sliderName;volumeMode:true;inactiveColor:Theme.outline;Layout.fillWidth:true;implicitHeight:24;onMoved:percent.text=String(Math.round(value*100))}
            RowLayout {Layout.fillWidth:true
                MButton {objectName:"volumeMute";text:app.volume>0?"Mute":"Unmute";onClicked:{if(app.volume>0){control.previousVolume=app.volume;app.volume=0;}else app.volume=control.previousVolume;percent.text=String(Math.round(app.volume*100));}}
                Item {Layout.fillWidth:true}
                MButton {objectName:"volumeApply";text:"Apply";enabled:percent.acceptableInput;onClicked:popup.apply()}
            }
        }
    }
    property real previousVolume:0.7
}
