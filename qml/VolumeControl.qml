import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
RowLayout {
    id:control
    property bool showSlider:true
    property string buttonName:"exactVolumeButton"
    property string sliderName:"popupVolumeSlider"
    readonly property bool popupVisible:popup.visible
    // What the popup opens from: this control's own button, or the overflow
    // menu of a bar too narrow to keep the button on show.
    property Item anchorItem:button
    function openFrom(item){anchorItem=item||button;popup.open();}
    spacing:0
    MButton {id:button;objectName:control.buttonName;symbol:app.volume>0?"volume":"mute";tip:"Volume · "+Math.round(app.volume*100)+"%";selected:popup.visible;onClicked:control.openFrom(button)}
    SeekBar {volumeMode:true;visible:control.showSlider;Layout.preferredWidth:66}
    Popup {
        id:popup;objectName:"volumePopup";parent:Overlay.overlay;width:264;height:184;padding:16;focus:true
        x:Math.max(12,Math.min(parent.width-width-12,control.anchorItem.mapToItem(parent,0,0).x-width+control.anchorItem.width))
        y:Math.max(12,Math.min(parent.height-height-12,control.anchorItem.mapToItem(parent,0,0).y-height-10))
        property bool built: false
        onAboutToShow: built=true
        onOpened: body.item.beginEdit()
        function apply(){body.item.apply();}
        closePolicy:Popup.CloseOnEscape|Popup.CloseOnPressOutside
        background:Rectangle {radius:Theme.shapeExtraLarge;color:Theme.high;border.width:1;border.color:Theme.outlineVariant}
        enter:Transition {NumberAnimation {property:"opacity";from:0;to:1;duration:Theme.enterDuration}}
        exit:Transition {NumberAnimation {property:"opacity";to:0;duration:Theme.exitDuration}}
        Loader {
            id: body
            anchors.fill: parent
            active: popup.built
            sourceComponent: ColumnLayout {
                function beginEdit(){percent.text=String(Math.round(app.volume*100));percent.forceActiveFocus();percent.selectAll();}
                function apply(){if(percent.acceptableInput){app.volume=Number(percent.text)/100;popup.close();}}
                anchors.fill:parent;spacing:8
                RowLayout {Layout.fillWidth:true
                    SungText {heading:true;text:"Volume";Layout.fillWidth:true;font.pixelSize:Theme.titleMedium}
                    MTextField {id:percent;objectName:"volumePercent";Layout.preferredWidth:72;implicitHeight:40;topPadding:10;bottomPadding:10;maximumLength:3;validator:IntValidator {bottom:0;top:100} inputMethodHints:Qt.ImhDigitsOnly;Accessible.name:"Volume percent";onAccepted:popup.apply()}
                    SungText {text:"%";color:Theme.muted}
                }
                SeekBar {objectName:control.sliderName;volumeMode:true;inactiveColor:Theme.outlineVariant;Layout.fillWidth:true;onMoved:percent.text=String(Math.round(value*100))}
                RowLayout {Layout.fillWidth:true
                    MButton {objectName:"volumeMute";text:app.volume>0?"Mute":"Unmute";onClicked:{if(app.volume>0){control.previousVolume=app.volume;app.volume=0;}else app.volume=control.previousVolume;percent.text=String(Math.round(app.volume*100));}}
                    Item {Layout.fillWidth:true}
                    MButton {objectName:"volumeApply";text:"Apply";enabled:percent.acceptableInput;onClicked:popup.apply()}
                }
            }
        }
    }
    property real previousVolume:0.7
}
