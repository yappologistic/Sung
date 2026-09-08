import QtQuick
import QtQuick.Controls
import Sung.Native 1.0

ListView {
    id: list
    property bool queueMode: false
    property bool reorderEnabled: false
    property string playlistId: ""
    property string matchQuery: ""
    readonly property int dropIndex: drop.containsDrag?drop.before:-1
    required property var dragHub
    property alias selection: selection
    signal activate(int row, var item)
    signal menuRequested(var item, int row, var anchor)
    signal removeSelected()
    signal addSelected()
    clip: true; spacing: 4; reuseItems: true; cacheBuffer: 80
    boundsBehavior: Flickable.StopAtBounds
    readonly property bool animateEdits: queueMode && app.motion && visible && Window.window && Window.window.visible && Window.window.visibility!==Window.Minimized
    onAnimateEditsChanged: if(!animateEdits){for(const child of contentItem.children)if(child.motionRaised!==undefined)child.motionRaised=false;}
    displaced: Transition { enabled: list.animateEdits; NumberAnimation { properties: "x,y"; duration: 220; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve } }
    move: Transition { enabled: list.animateEdits; SequentialAnimation {
        PropertyAction { property: "motionRaised"; value: true }
        NumberAnimation { properties: "x,y"; duration: 220; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve }
        PropertyAction { property: "motionRaised"; value: false }
    } }
    add: Transition { enabled: list.animateEdits; NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve } }
    remove: Transition { enabled: list.animateEdits; NumberAnimation { property: "opacity"; to: 0; duration: 120; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve } }
    section.property: queueMode ? "musicSource" : ""
    section.criteria: ViewSection.FullString
    section.delegate: Item {
        required property string section
        width: list.width; height: 32
        SungText { anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter; width: parent.width-24; text: parent.section; color: Theme.muted; font.pixelSize: 12; elide: Text.ElideRight }
    }
    RowSelection { id: selection; model: list.model }
    Connections {
        target: list.model
        function cancelDrag(){if(list.dragHub.owner===list)list.dragHub.cancel();}
        function onModelReset(){cancelDrag();}
        function onRowsInserted(){cancelDrag();}
        function onRowsRemoved(){cancelDrag();}
        function onRowsMoved(){cancelDrag();}
        function onLayoutChanged(){cancelDrag();}
    }
    function sourceRows() {
        return selection.rows.map(i=>list.queueMode?i:app.collection.sourceIndex(i));
    }
    function beginDrag(row,point) {
        if(!selection.contains(row))selection.select(row,0);
        list.dragHub.begin(list,sourceRows(),selection.items(),point);
    }
    function insertion(y) {
        const row=indexAt(1,contentY+y);
        if(row<0){
            const at=y+contentY;
            // Section headings and spacing are not model rows. Find the next visible song.
            let next=count;
            for(let i=0;i<contentItem.children.length;++i){const child=contentItem.children[i];if(child.selectionIndex!==undefined && child.y>=at)next=Math.min(next,child.selectionIndex);}
            return at<=originY?0:next;
        }
        const item=itemAtIndex(row);
        return row+(item && contentY+y>item.y+item.height/2?1:0);
    }
    Keys.onPressed: event=> {
        if(event.key===Qt.Key_A && (event.modifiers&Qt.ControlModifier)){selection.selectAll();event.accepted=true;}
        else if(event.key===Qt.Key_Escape && selection.count){selection.clear();event.accepted=true;}
        else if(event.key===Qt.Key_Delete && selection.count){list.removeSelected();event.accepted=true;}
        else if([Qt.Key_Up,Qt.Key_Down,Qt.Key_Home,Qt.Key_End,Qt.Key_PageUp,Qt.Key_PageDown].indexOf(event.key)>=0){
            const previous=currentIndex;
            const page=Math.max(1,Math.floor(height/((currentItem?currentItem.height:72)+spacing)));
            let next=currentIndex+(event.key===Qt.Key_Down?1:event.key===Qt.Key_Up?-1:event.key===Qt.Key_PageDown?page:-page);
            if(event.key===Qt.Key_Home)next=0;
            else if(event.key===Qt.Key_End)next=count-1;
            currentIndex=count ? Math.max(0,Math.min(count-1,next)) : -1;
            if(currentIndex>=0)positionViewAtIndex(currentIndex,ListView.Contain);
            if((event.modifiers&Qt.ShiftModifier) && !selection.count && previous>=0)selection.select(previous,0);
            if(event.modifiers&Qt.ShiftModifier)selection.select(currentIndex,event.modifiers);
            forceActiveFocus();event.accepted=true;
        } else if((event.key===Qt.Key_Menu || (event.key===Qt.Key_F10 && (event.modifiers&Qt.ShiftModifier))) && currentIndex>=0 && currentItem){
            const item=model.get(currentIndex);
            if(item.kind!=="smart")list.menuRequested(item,list.queueMode?currentIndex:app.collection.sourceIndex(currentIndex),currentItem);
            event.accepted=true;
        } else if((event.key===Qt.Key_Space)&&(event.modifiers&Qt.ControlModifier)){selection.select(currentIndex,Qt.ControlModifier);event.accepted=true;}
        else if((event.key===Qt.Key_Return||event.key===Qt.Key_Enter)&&currentIndex>=0){list.activate(currentIndex,model.get(currentIndex));event.accepted=true;}
    }
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    delegate: TrackRow {
        required property var entry; required property int index
        objectName: (list.queueMode?"queueRow_":"trackRow_")+index
        matchQuery: list.matchQuery
        transform: Translate { y: list.dropIndex<0?0:index>=list.dropIndex?10:-10
            Behavior on y { NumberAnimation { duration: app.motion?130:0; easing.type: Easing.OutCubic } }
        }
        track: entry; rowIndex: list.queueMode?index:app.collection.sourceIndex(index); queueMode: list.queueMode
        selection: list.selection; selectionIndex: index; listOwner: list; dragHub: list.dragHub
        onClicked: list.activate(index,entry)
        onMenuRequested: (item,row,anchor)=>list.menuRequested(item,row,anchor)
    }
    DropArea {
        id: drop; objectName: "trackDropArea"; parent: list; anchors.fill: parent
        keys: ["sung-tracks"]
        property int before: -1
        property real pointerY: 0
        function compatible() {return list.queueMode || (list.reorderEnabled && list.dragHub.owner===list);}
        onEntered: drag=> {drag.accepted=compatible();if(drag.accepted){pointerY=drag.y;before=list.insertion(drag.y);}}
        onPositionChanged: drag=> {pointerY=drag.y;before=list.insertion(drag.y);}
        onExited: before=-1
        onDropped: event=> {
            if(!compatible())return;
            const at=list.insertion(event.y);
            const hub=list.dragHub;
            if(hub.owner===list){if(list.queueMode)app.moveQueueRows(hub.rows,at);else if(app.serverPlaylistEditable)app.moveServerRows(hub.rows,at);else app.movePlaylistRows(list.playlistId,hub.rows,at);}
            else if(list.queueMode)app.enqueueItems(hub.items,false,at);
            event.acceptProposedAction();before=-1;
        }
        Timer {
            interval: 30; repeat: true; running: drop.containsDrag && (drop.pointerY<36 || drop.pointerY>list.height-36)
            onTriggered: {
                list.contentY=Math.max(list.originY,Math.min(Math.max(list.originY,list.contentHeight-list.height+list.originY),list.contentY+(drop.pointerY<36?-12:12)));
                drop.before=list.insertion(drop.pointerY);
            }
        }
    }
    Rectangle {
        parent: list; anchors.fill: parent; z: 9; radius: 16; color: "transparent"; border.color: Theme.primary; border.width: 2
        visible: drop.containsDrag
    }
    Rectangle {
        objectName: "dropInsertionLine"
        parent: list; z: 10; height: 3; radius: 1.5; color: Theme.primary; width: list.width
        visible: drop.containsDrag && drop.before>=0
        y: {const item=list.itemAtIndex(drop.before);return Math.max(0,Math.min(list.height-3,item?item.y-list.contentY:list.contentHeight-list.contentY));}
    }
}
