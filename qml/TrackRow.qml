import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ItemDelegate {
    id: row
    property var track: ({})
    property int rowIndex: -1
    property var selection: null
    property int selectionIndex: -1
    property var listOwner: null
    property var dragHub: null
    property bool selected: selection ? (selection.revision,selection.contains(selectionIndex)) : false
    property bool selectable: selection && !!(track.videoId || track.localPath || track.serverSong) && track.available!==false
    property bool keyboardCurrent: activeFocus || (listOwner && listOwner.activeFocus && listOwner.currentIndex===selectionIndex)
    property bool selectionVisible: selectable && (hovered || pointer.containsMouse || keyboardCurrent || selection.count>0)
    property bool motionRaised: false
    z: motionRaised && app.motion ? 2 : 0
    property string matchQuery: ""
    readonly property bool titleRevealAllowed: !dragging && (!dragHub || !dragHub.owner) && (!listOwner || (!listOwner.moving && y>=listOwner.contentY && y+height<=listOwner.contentY+listOwner.height))
    property bool dragging: false
    // Material's reorder list recolours the row under the finger rather than
    // only tinting it: the carried row takes the tertiary container and the
    // ink that belongs on it, so it reads as the subject of the gesture and
    // not as a row that happens to be lit. These three inks are what the rest
    // of the row asks for, so the rule lives in one place.
    readonly property bool carried: dragging
    readonly property color titleInk: carried ? Theme.tertiaryContainerText
                                    : selected ? Theme.secondaryContainerText
                                    : active ? Theme.primary : Theme.text
    readonly property color supportInk: carried ? Theme.tertiaryContainerText
                                      : selected ? Theme.secondaryContainerText : Theme.muted
    readonly property color actionInk: carried ? Theme.tertiaryContainerText
                                     : selected ? Theme.secondaryContainerText : Theme.text
    property point pressPoint
    property int pressModifiers: 0
    property bool queueMode: false
    // Material's segmented list style: a run drawn as one group, its items set
    // apart rather than divided by a rule, round at the ends of the run and
    // nearly square inside it.
    property bool segmented: false
    property bool firstInRun: true
    property bool lastInRun: true
    readonly property bool pointerOver: pointer.containsMouse
    // Material's swipe to dismiss. A queue row can be pushed aside to drop it,
    // revealing the action behind it as it goes; past a third of the row the
    // release commits. Reordering still owns any drag that is mostly vertical.
    property real swipe: 0
    property bool swiping: false
    readonly property real dismissThreshold: width/3
    signal dismissRequested()
    property bool active: queueMode ? rowIndex===app.currentIndex : app.current.id !== undefined && app.current.id === track.id
    signal menuRequested(var item, int index, var anchor)
    ListView.onReused: {motionRaised=false;opacity=Qt.binding(()=>enabled?1:Theme.disabledContentOpacity);}
    Behavior on implicitHeight {enabled:app.motion && visible && !dragging;NumberAnimation {id:rowResize;duration:220;easing.type:Easing.InOutCubic}}
    Connections {target:app;function onSettingsChanged(){if(!app.motion)rowResize.complete();}}
    implicitHeight: queueMode ? (app.compactDensity?56:72) : Theme.rowHeight
    width: ListView.view ? ListView.view.width : 500
    hoverEnabled: true
    enabled: track.available !== false
    opacity: enabled ? 1 : Theme.disabledContentOpacity
    Accessible.description: selectable ? "Ctrl-click to toggle selection, Shift-click for a range. Drag selected songs to move them." : ""
    Accessible.name: (track.title || "") + ", " + (track.artist || "")
    Accessible.selected: selected
    // Material's reveal list uncovers a button rather than a coloured sheet:
    // the item's own surface stays behind it, and what appears is an icon
    // button. It is round while it is only on offer and takes the large corner
    // in the accent once the swipe has gone far enough to commit, so the shape
    // says what letting go will do.
    Item {
        objectName: "swipeReveal"
        anchors.fill: parent; z: -0.5
        visible: row.swipe !== 0
        readonly property bool committing: Math.abs(row.swipe) >= row.dismissThreshold
        Rectangle {
            anchors.fill: parent
            radius: Theme.listActive
            color: Theme.surface
        }
        Rectangle {
            objectName: "swipeAction"
            anchors.verticalCenter: parent.verticalCenter
            x: row.swipe > 0 ? 16 : parent.width-width-16
            width: 40; height: 40
            radius: parent.committing ? Theme.listActive : Theme.shapeFull(height)
            color: parent.committing ? Theme.primary : Theme.secondaryContainer
            Behavior on radius { enabled: app.motion; NumberAnimation { duration: Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastEffects } }
            Behavior on color { ColorAnimation { duration: Theme.springFastEffectsMs } }
            Icon {
                anchors.centerIn: parent
                name: "remove"; size: 20
                ink: parent.parent.committing ? Theme.primaryText : Theme.secondaryContainerText
            }
        }
    }
    NumberAnimation { id: swipeReturn; target: row; property: "swipe"; to: 0; duration: Theme.springFastEffectsMs }
    background: Rectangle {
        id: rowContainer
        transform: Translate { x: row.swipe }
        // A segmented run sets its items apart rather than dividing them.
        y: row.segmented ? Theme.listSegmentedGap/2 : 0
        height: row.height - (row.segmented ? Theme.listSegmentedGap : 0)
        // Material's expressive list item answers the pointer with its shape as
        // well as its colour: it rests nearly square, rounds as the pointer
        // arrives, and rounds fully while it is pressed, focused, carried or
        // picked out. In a segmented run the corners inside the run keep the
        // resting step and only the ends of the run are round.
        readonly property int stateCorner: row.dragging || row.motionRaised || row.down || row.keyboardCurrent ? Theme.listActive
                                         : row.selected || row.active ? Theme.listActive
                                         : row.hovered || row.pointerOver ? Theme.listHovered
                                         : Theme.listRest
        // Outside a run, or in any state but rest, the item is one shape. At
        // rest inside a run only the ends of the run are round.
        readonly property bool wholeShape: !row.segmented || stateCorner !== Theme.listRest
        topLeftRadius: wholeShape ? stateCorner : row.firstInRun ? Theme.listActive : Theme.listRest
        topRightRadius: topLeftRadius
        bottomLeftRadius: wholeShape ? stateCorner : row.lastInRun ? Theme.listActive : Theme.listRest
        bottomRightRadius: bottomLeftRadius
        // Material marks a chosen list item with the secondary container, the
        // same role that marks a chosen anything else. Picking rows out is not
        // an action, so it does not take the accent an action is offered in.
        color: row.carried ? Theme.tertiaryContainer : row.selected ? Theme.secondaryContainer : row.motionRaised ? Theme.container : row.active ? Theme.high : row.hovered ? Theme.container : row.segmented ? Theme.container : "transparent"
        border.width: row.keyboardCurrent ? 2 : 0; border.color: Theme.focusRing
        Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        Behavior on topLeftRadius { enabled: app.motion; NumberAnimation { duration: Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastEffects } }
        Behavior on bottomLeftRadius { enabled: app.motion; NumberAnimation { duration: Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastEffects } }
        // A row being carried is lifted, not only tinted.
        MElevation { anchors.fill: parent; radius: parent.topLeftRadius; level: row.motionRaised || row.dragging ? 4 : 0 }
        // A row the list is moving for you takes Material's dragged state
        // layer, heavier than the one a press leaves. A row you are carrying
        // yourself does not: it has the reorder container instead, and laying
        // 16% of the surface ink over that would only muddy it.
        Rectangle {
            objectName: "rowDraggedLayer"
            anchors.fill: parent; radius: parent.radius
            color: Theme.text
            opacity: row.motionRaised ? Theme.draggedOpacity : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        }
    }
    MouseArea {
        id: pointer; anchors.fill: parent; anchors.rightMargin: 60
        enabled: !!row.selection; acceptedButtons: Qt.LeftButton | Qt.RightButton; hoverEnabled: true; preventStealing: true
        onPressed: mouse=> {if(mouse.button===Qt.RightButton){if(row.track.kind!=="smart")row.menuRequested(row.track,row.rowIndex,row);return;}row.pressPoint=Qt.point(mouse.x,mouse.y);row.pressModifiers=mouse.modifiers;row.dragging=false;row.forceActiveFocus();row.listOwner.currentIndex=row.selectionIndex;}
        onPositionChanged: mouse=> {
            if(!(pressedButtons&Qt.LeftButton) || !row.selectable)return;
            const dx=mouse.x-row.pressPoint.x, dy=mouse.y-row.pressPoint.y;
            if(row.queueMode && !row.dragging && !row.swiping && Math.abs(dx)>10 && Math.abs(dx)>Math.abs(dy)*1.5){swipeReturn.stop();row.swiping=true;}
            if(row.swiping){row.swipe=dx;return;}
            const p=mapToItem(null,mouse.x,mouse.y);
            if(!row.dragging && Math.hypot(dx,dy)>10){row.dragging=true;row.listOwner.beginDrag(row.selectionIndex,p);}
            if(row.dragging)row.dragHub.move(p);
        }
        onReleased: mouse=> {
            if(mouse.button===Qt.RightButton)return;
            if(row.swiping){row.swiping=false;if(Math.abs(row.swipe)>row.dismissThreshold)row.dismissRequested();swipeReturn.restart();return;}
            if(row.dragging){row.dragHub.finish();row.dragging=false;return;}
            if(row.selectable && (mouse.x<64 || row.pressModifiers&(Qt.ControlModifier|Qt.ShiftModifier)))row.selection.select(row.selectionIndex,mouse.x<64?Qt.ControlModifier:row.pressModifiers);
            else if(row.selection.count && row.selectable)row.selection.select(row.selectionIndex,0);
            else row.clicked();
        }
        onCanceled: {if(row.dragging)row.dragHub.cancel();row.dragging=false;if(row.swiping){row.swiping=false;swipeReturn.restart();}}
        onDoubleClicked: mouse=> {if(!(mouse.modifiers&(Qt.ControlModifier|Qt.ShiftModifier)))row.clicked();}
    }
    contentItem: RowLayout {
        // Material's list item keeps 12dp between the leading element and what
        // it introduces.
        spacing: 12
        transform: Translate { x: row.swipe }
        Item {
            Layout.preferredWidth: row.queueMode?(app.compactDensity?36:48):Theme.rowArtwork; Layout.preferredHeight: Layout.preferredWidth
            Icon { anchors.centerIn: parent; name: "shuffle"; size: 24; ink: Theme.primary; visible: row.track.kind==="smart" }
            Artwork { visible: row.track.kind!=="smart"; anchors.fill: parent; url: row.track.art || ""; radius: Theme.shapeSmall; pixels: 112 }
            // Material puts a selection control in a list item's leading slot,
            // and for a list you can take several rows from that is a checkbox.
            MCheckbox {
                objectName: "rowCheckbox"
                anchors.centerIn: parent
                visible: opacity>0
                opacity: row.selectionVisible?1:0
                checked: row.selected
                // The row owns the gesture: a press on its leading edge is
                // what selects it, and has been since before there was a box.
                enabled: false
                presentational: true
                Accessible.ignored: true
                Behavior on opacity { NumberAnimation { duration: app.motion?Theme.fast:0; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true; spacing: 4
            MatchText { objectName: "trackTitle"; query: row.matchQuery; tooltipEnabled: row.titleRevealAllowed; revealFocused: row.keyboardCurrent; sourceText: row.track.title || ""; Layout.fillWidth: true; font.pixelSize: Theme.bodyLarge; font.weight: row.active ? Font.DemiBold : Font.Medium; color: row.titleInk }
            MatchText { visible: row.track.kind!=="smart"; query: row.matchQuery; tooltipEnabled: row.titleRevealAllowed; revealFocused: row.keyboardCurrent; sourceText: row.track.artist || (row.track.kind === "artist" ? "Artist" : row.track.kind === "album" ? "Album" : row.track.kind === "playlist" ? "Playlist" : ""); Layout.fillWidth: true; color: row.supportInk; font.pixelSize: Theme.bodyMedium }
        }
        PlayingIndicator { ink: row.titleInk; visible: row.active }
        SungText { font.features: {"tnum": 1}; visible: !row.queueMode || row.width>350; text: row.track.duration || (row.track.seconds ? app.formatTime(row.track.seconds*1000) : ""); font.pixelSize: Theme.bodySmall; color: row.supportInk; Layout.rightMargin: 2 }
        MButton { visible: row.track.kind!=="smart"; symbol: "more"; tip: "Track actions"; Accessible.name: "Actions for "+(row.track.title||"track"); ink: row.actionInk; onClicked: row.menuRequested(row.track,row.rowIndex,this) }
    }
}
