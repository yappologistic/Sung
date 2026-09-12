import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Sung.Native 1.0

ListView {
    id: list
    property bool queueMode: false
    property bool groupFolders: false
    property bool groupDiscs: false
    property bool groupReady:false
    function scheduleGroups(){if(groupReady)groupRefresh.restart();}
    Timer {id:groupRefresh;interval:0;onTriggered:list.rebuildGroups()}
    property var folded: ({})
    property var groupRows: ({})
    readonly property bool foldable: !queueMode && (groupFolders || groupDiscs)
    function groupKey(item){return groupFolders?String(item.localPath || "").slice(0,String(item.localPath || "").lastIndexOf("/")):"Disc "+Math.max(1,item.discNumber || 1);}
    function rebuildGroups(){
        const groups={};
        if(foldable && model)for(let i=0;i<model.count;++i){const key=groupKey(model.get(i));if(!groups[key])groups[key]=[];groups[key].push(i);}
        groupRows=groups;updateExcluded();
    }
    function updateExcluded(){
        let rows=[];
        if(foldable)for(const key of Object.keys(folded))if(folded[key] && groupRows[key])rows=rows.concat(groupRows[key]);
        selection.excludedRows=rows;
        if(currentIndex>=0 && rowFolded(currentIndex))currentIndex=-1;
    }
    function rowFolded(row){return foldable && model && !!folded[groupKey(model.get(row))];}
    function toggleGroup(key){const state=Object.assign({},folded);state[key]=!state[key];folded=state;updateExcluded();}
    onGroupFoldersChanged:{folded={};scheduleGroups();}
    onGroupDiscsChanged:{folded={};scheduleGroups();}
    onModelChanged:{folded={};scheduleGroups();}
    Component.onCompleted:{groupReady=true;rebuildGroups();}
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
    clip: true; spacing: foldable?0:4; reuseItems: true; cacheBuffer: 80
    boundsBehavior: Flickable.StopAtBounds
    readonly property bool animateEdits: queueMode && app.motion && visible && Window.window && Window.window.visible && Window.window.visibility!==Window.Minimized
    onAnimateEditsChanged: if(!animateEdits){for(const child of contentItem.children)if(child.motionRaised!==undefined)child.motionRaised=false;}
    displaced: Transition { enabled: list.animateEdits; NumberAnimation { properties: "x,y"; duration: 220; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
    move: Transition { enabled: list.animateEdits; SequentialAnimation {
        PropertyAction { property: "motionRaised"; value: true }
        NumberAnimation { properties: "x,y"; duration: 220; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve }
        PropertyAction { property: "motionRaised"; value: false }
    } }
    add: Transition { enabled: list.animateEdits; NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve } }
    remove: Transition { enabled: list.animateEdits; NumberAnimation { property: "opacity"; to: 0; duration: 120; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve } }
    section.property: queueMode ? "queueOrigin" : groupFolders ? "musicFolder" : groupDiscs ? "musicDisc" : ""
    section.criteria: ViewSection.FullString
    section.delegate: Item {
        id:heading;required property string section
        objectName: list.groupFolders ? "folderHeading" : "sourceHeading"
        width:list.width;height:list.foldable?48:32
        RowLayout {anchors.fill:parent;spacing:4
            MButton {objectName:"toggleGroup_"+heading.section;visible:list.foldable;Layout.fillWidth:true;leftAligned:true
                text:(list.groupFolders?app.musicFolderLabel(heading.section):heading.section)+" · "+(list.groupRows[heading.section] || []).length
                tip:(list.folded[heading.section]?"Expand ":"Collapse ")+heading.section
                symbol:"";contentInset:34;
                Icon {x:8;anchors.verticalCenter:parent.verticalCenter;name:"chevron";size:18;rotation:list.folded[heading.section]?0:90;Behavior on rotation {NumberAnimation {duration:app.motion?180:0}}}
                onClicked:list.toggleGroup(heading.section)
            }
            SungText {visible:!list.foldable;Layout.fillWidth:true;Layout.leftMargin:12;text:heading.section;color:Theme.muted;font.pixelSize:12;elide:Text.ElideRight}
            MButton {objectName:"playGroup_"+heading.section;visible:list.foldable;symbol:"play";tip:"Play "+heading.section;onClicked:app.playGroup(heading.section,list.groupFolders)}
        }
    }
    RowSelection { id: selection; model: list.model }
    Connections {
        target: list.model
        function cancelDrag(){if(list.dragHub.owner===list)list.dragHub.cancel();}
        function onModelReset(){cancelDrag();list.folded={};list.scheduleGroups();}
        function onRowsInserted(){cancelDrag();list.scheduleGroups();}
        function onRowsRemoved(){cancelDrag();list.scheduleGroups();}
        function onRowsMoved(){cancelDrag();list.scheduleGroups();}
        function onLayoutChanged(){cancelDrag();list.scheduleGroups();}
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
            next=Math.max(0,Math.min(count-1,next));
            const step=(event.key===Qt.Key_Up || event.key===Qt.Key_PageUp || event.key===Qt.Key_End)?-1:1;
            while(next>=0 && next<count && rowFolded(next))next+=step;
            currentIndex=next>=0 && next<count?next:-1;
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
        property bool foldedRow:list.foldable && !!list.folded[list.groupKey(entry)]
        height:foldedRow?0:implicitHeight
        visible:!foldedRow
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
