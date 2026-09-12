import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
MDialog {
    id:dialog;objectName:"homeEditor";title:"Customize Home";modal:true;standardButtons:Dialog.Close
    width:Math.min(580,parent.width-48);height:Math.min(540,parent.height-48,Math.max(1,sections.length)*70+280)
    readonly property var sections: {app.sections;app.pins;app.homeOrder;app.hiddenHomeSections;return visible&&app.page==="home"?app.homeSections(true):[];}
    ColumnLayout {anchors.fill:parent;spacing:12
        ListView {id:list;objectName:"homeEditorList";Layout.fillWidth:true;Layout.fillHeight:true;clip:true;spacing:6;model:dialog.sections;reuseItems:true
            ScrollBar.vertical:MScrollBar {}
            delegate:Rectangle {required property var modelData;required property int index;width:list.width;height:64;radius:16;color:Theme.high
                RowLayout {anchors.fill:parent;anchors.margins:8;spacing:4
                    MSwitch {objectName:"homeVisible_"+index;text:modelData.title;Layout.fillWidth:true;checked:modelData.shown;onToggled:app.showHomeSection(modelData.title,checked)}
                    MButton {objectName:"homeUp_"+index;symbol:"back";rotation:90;tip:"Move up";Accessible.name:"Move "+modelData.title+" up";enabled:index>0;onClicked:app.moveHomeSection(modelData.title,-1)}
                    MButton {objectName:"homeDown_"+index;symbol:"back";rotation:-90;tip:"Move down";Accessible.name:"Move "+modelData.title+" down";enabled:index<list.count-1;onClicked:app.moveHomeSection(modelData.title,1)}
                }
            }
            SungText {anchors.centerIn:parent;text:app.busy?"Loading Home…":"No Home sections available";visible:list.count===0;color:Theme.muted}
        }
        MButton {objectName:"homeLayoutReset";text:"Reset layout";onClicked:app.resetHomeLayout()}
    }
}
