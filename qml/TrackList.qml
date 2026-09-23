import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Sung.Native 1.0

ListView {
    id: list
    property bool queueMode: false
    // Backend::albumInfo returns a summary only for an album detail page.
    // Keep the queue and every other collection in the ordinary list format.
    readonly property bool albumRows: !queueMode && !!app.albumInfo.summary
    // Backend::albumInfo traverses the result set. Read its artist once for
    // the list, rather than recomputing it in every delegate.
    readonly property string albumArtist: albumRows ? (app.albumInfo.artist || "") : ""
    // Material's segmented list style, which the panel lists are drawn in.
    property bool segmented: queueMode
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
    // Type-ahead jump. The buffer collects printable keys until a short pause,
    // so "bl" finds "Blue Hour" rather than every row starting with "b".
    property string typeAhead: ""
    property int typeAheadRow: -1
    Timer { id: typeAheadIdle; interval: 900; onTriggered: list.typeAhead="" }
    // One pass over the rows: a title match wins outright, an artist match is
    // only used when no title in the whole list starts with what was typed.
    function matchRow(prefix) {
        if(!prefix || !model)return -1;
        let artistRow=-1;
        for(let i=0;i<model.count;++i) {
            if(rowFolded(i))continue;
            const row=model.get(i);
            if(String(row.title || "").toLowerCase().startsWith(prefix))return i;
            if(artistRow<0 && String(row.artist || "").toLowerCase().startsWith(prefix))artistRow=i;
        }
        return artistRow;
    }
    function typeAheadKey(event) {
        if(!app.typeAheadJump || event.text.length!==1)return false;
        if(event.modifiers&(Qt.ControlModifier|Qt.AltModifier|Qt.MetaModifier))return false;
        const character=event.text.toLowerCase();
        // Space stays a playback shortcut unless it continues an active search.
        if(character<" " || (character===" " && !list.typeAhead))return false;
        const candidate=list.typeAhead+character;
        const row=matchRow(candidate);
        typeAheadIdle.restart();
        if(row<0)return true;
        list.typeAhead=candidate;list.typeAheadRow=row;
        currentIndex=row;positionViewAtIndex(row,ListView.Contain);forceActiveFocus();
        return true;
    }
    property bool reorderEnabled: false
    property string playlistId: ""
    property string matchQuery: ""
    readonly property int dropIndex: drop.containsDrag?drop.before:-1
    // How far the rows move aside for the row being carried. The gap they leave
    // is twice this, and it is the drop zone.
    readonly property int dropParting: 10
    required property var dragHub
    property alias selection: selection
    signal activate(int row, var item)
    signal menuRequested(var item, int row, var anchor)
    signal removeSelected()
    signal addSelected()
    clip: true; spacing: segmented?0:foldable?0:4; reuseItems: true; cacheBuffer: 80
    boundsBehavior: Flickable.StopAtBounds
    readonly property bool animateEdits: queueMode && app.motion && visible && Window.window && Window.window.visible && Window.window.visibility!==Window.Minimized
    onAnimateEditsChanged: if(!animateEdits){for(const child of contentItem.children)if(child.motionRaised!==undefined)child.motionRaised=false;}
    // PaneMotion.kt:150-177 uses DefaultSpatial when panes move.
    displaced: Transition { enabled: list.animateEdits && app.motion; NumberAnimation { objectName: "trackDisplaceMotion"; properties: "x,y"; duration: Theme.springSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial } }
    move: Transition { enabled: list.animateEdits; SequentialAnimation {
        PropertyAction { property: "motionRaised"; value: true }
        // DefaultSpatial moves a row to its new list position.
        NumberAnimation { objectName: "trackMoveMotion"; properties: "x,y"; duration: Theme.springSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial }
        PropertyAction { property: "motionRaised"; value: false }
    } }
    // DefaultEffects fades added rows without opacity overshoot.
    add: Transition { enabled: list.animateEdits && app.motion; NumberAnimation { objectName: "trackAddMotion"; property: "opacity"; from: 0; to: 1; duration: Theme.springEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springEffects } }
    // Menu.kt:1829-1831 uses FastEffects for content leaving the screen.
    remove: Transition { enabled: list.animateEdits && app.motion; NumberAnimation { objectName: "trackRemoveMotion"; property: "opacity"; to: 0; duration: Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastEffects } }
    section.property: queueMode ? "queueOrigin" : groupFolders ? "musicFolder" : groupDiscs ? "musicDisc" : ""
    section.criteria: ViewSection.FullString
    section.delegate: Item {
        id:heading;required property string section
        objectName: list.groupFolders ? "folderHeading" : "sourceHeading"
        width:list.width;height:list.foldable?48:32
        RowLayout {anchors.fill:parent;spacing:4
            MButton {objectName:"toggleGroup_"+heading.section;visible:list.foldable;Layout.fillWidth:true;Layout.minimumWidth:0;leftAligned:true
                text:(list.groupFolders?app.musicFolderLabel(heading.section):heading.section)+" · "+(list.groupRows[heading.section] || []).length
                tip:(list.folded[heading.section]?"Expand ":"Collapse ")+heading.section
                symbol:"";contentInset:12;
                onClicked:list.toggleGroup(heading.section)
            }
            // Material puts the expander at the trailing edge of the item it
            // opens, inside a container that is the item's own surface while it
            // is shut and a step above it once it is open, so the state is
            // readable without watching the chevron turn.
            Rectangle {
                objectName:"groupExpander_"+heading.section
                visible:list.foldable
                Layout.preferredWidth:32;Layout.preferredHeight:32
                radius:Theme.shapeFull(height)
                // Material's expandable list gives the control a container in
                // both states: the surface while the group is closed, the
                // surface container while it is open.
                color:list.folded[heading.section]?Theme.surface:Theme.container
                Behavior on color {ColorAnimation {duration:Theme.fast;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.fastEffectsCurve}}
                Icon {anchors.centerIn:parent;name:"chevron";size:18;rotation:list.folded[heading.section]?0:90;Behavior on rotation {NumberAnimation {duration:Theme.springFastSpatialMs;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.springFastSpatial}}}
                TapHandler {onTapped:list.toggleGroup(heading.section)}
                Accessible.ignored:true
            }
            SungText {objectName:"groupHeading";visible:!list.foldable;Layout.fillWidth:true;Layout.leftMargin:12;text:heading.section;color:Theme.muted;font.pixelSize:Theme.titleSmall;typeRole:"titleSmall";elide:Text.ElideRight}
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
        else if(list.typeAheadKey(event))event.accepted=true;
    }
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    MSmoothWheel { flick: list }
    delegate: TrackRow {
        required property var entry; required property int index
        objectName: (list.queueMode?"queueRow_":"trackRow_")+index
        property bool foldedRow:list.foldable && !!list.folded[list.groupKey(entry)]
        height:foldedRow?0:implicitHeight
        visible:!foldedRow && !pooled
        matchQuery: list.matchQuery
        transform: Translate { y: list.dropIndex<0?0:index>=list.dropIndex?list.dropParting:-list.dropParting
            // DefaultSpatial settles list row movement to its new position.
            Behavior on y { enabled: app.motion; NumberAnimation { objectName: "trackDropGapMotion"; duration: Theme.springSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial } }
        }
        track: entry; rowIndex: list.queueMode?index:app.collection.sourceIndex(index); queueMode: list.queueMode
        albumMode: list.albumRows; albumArtist: list.albumArtist
        // The queue is drawn as Material's segmented list: one run per group of
        // songs, round where the run ends and nearly square inside it.
        segmented: list.segmented
        firstInRun: index===0 || ListView.section !== ListView.previousSection
        lastInRun: index===list.count-1 || ListView.section !== ListView.nextSection
        selection: list.selection; selectionIndex: index; listOwner: list; dragHub: list.dragHub
        onClicked: list.activate(index,entry)
        onDismissRequested: if(list.queueMode)app.removeQueue(rowIndex)
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
        parent: list; anchors.fill: parent; z: 9; radius: Theme.shapeLarge; color: "transparent"; border.color: Theme.focusRing; border.width: 2
        visible: drop.containsDrag
    }
    // Material's reorder list says where the row will land by showing the place
    // rather than by drawing a line at it: the rows part, and the gap they open
    // is the drop zone, in the surface container a step below the list.
    Rectangle {
        objectName: "dropZone"
        parent: list; z: 10; width: list.width
        height: 2*list.dropParting; radius: Theme.listActive; color: Theme.surfaceLow
        visible: drop.containsDrag && drop.before>=0
        y: {const item=list.itemAtIndex(drop.before);return Math.max(0,Math.min(list.height-height,(item?item.y-list.contentY:list.contentHeight-list.contentY)-list.dropParting));}
    }
}
