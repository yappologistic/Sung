import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

MDialog {
    id: dialog
    objectName: "commandPalette"
    property var commands: []
    signal chosen(var command)
    readonly property var matches: visible ? commands.filter(c => field.text.trim().toLowerCase().split(/\s+/).every(t => c.title.toLowerCase().includes(t))).slice(0,100) : []
    title: "Quick actions"; modal: true
    width: Math.min(560,parent.width-48); height: Math.min(520,parent.height-64)
    initialFocus: field
    onOpened: {field.clear();list.currentIndex=0;}
    function choose(index) {if(index>=0 && index<matches.length){const c=matches[index];close();chosen(c);}}
    contentItem: ColumnLayout {
        spacing: 12
        MSearchField {
            id: field; objectName: "commandSearch"; Layout.fillWidth: true
            placeholderText: "Find an action or playlist"; Accessible.name: "Find an action or playlist"
            onTextChanged: list.currentIndex=0
            onAccepted: dialog.choose(list.currentIndex)
            Keys.onDownPressed: {list.currentIndex=Math.min(dialog.matches.length-1,list.currentIndex+1);list.positionViewAtIndex(list.currentIndex,ListView.Contain);}
            Keys.onUpPressed: {list.currentIndex=Math.max(0,list.currentIndex-1);list.positionViewAtIndex(list.currentIndex,ListView.Contain);}
        }
        ListView {
            id: list; objectName: "commandResults"; Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; model: dialog.matches; spacing: 4; boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: MScrollBar {}
            delegate: MButton {
                required property var modelData; required property int index
                objectName: "command_"+modelData.id; width: list.width-12; height: 48
                text: modelData.title; leftAligned: true; selected: list.currentIndex===index
                onClicked: dialog.choose(index)
            }
            SungText { anchors.centerIn: parent; visible: dialog.matches.length===0; text: "No matching actions"; color: Theme.muted }
        }
    }
}
