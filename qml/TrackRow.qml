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
    // A mouse press also gives this row active focus. Its title tooltip then
    // follows pointer Enter until focus leaves, instead of reopening on Exit.
    property bool tooltipFocusFromPointer: false
    onKeyboardCurrentChanged: if (!keyboardCurrent) tooltipFocusFromPointer = false
    property bool selectionVisible: selectable && (hovered || pointer.containsMouse || keyboardCurrent || selection.count>0)
    property bool motionRaised: false
    z: motionRaised && app.motion ? 2 : 0
    property string matchQuery: ""
    readonly property bool titleRevealAllowed: !dragging && (!dragHub || !dragHub.owner) && (!listOwner || (!listOwner.moving && y>=listOwner.contentY && y+height<=listOwner.contentY+listOwner.height))
    property bool dragging: false
    // ReorderListTokens.kt:22-44 and ListItemDefaults.kt:231-241 give a
    // dragged row the tertiary pair. Over warm covers that turns green, away
    // from the cover's colour family. The primary container pair keeps the
    // gesture tied to the cover; selected rows retain the secondary pair.
    readonly property bool carried: dragging
    readonly property color titleInk: carried ? Theme.containerText
                                    : selected ? Theme.secondaryContainerText
                                    : active ? Theme.primary : Theme.text
    readonly property color supportInk: carried ? Theme.containerText
                                      : selected ? Theme.secondaryContainerText : Theme.muted
    // ListTokens.ItemTrailingIconColor: a row's trailing action is in the
    // variant ink, like its supporting text, not in the title's.
    readonly property color actionInk: carried ? Theme.containerText
                                     : selected ? Theme.secondaryContainerText : Theme.muted
    property point pressPoint
    property int pressModifiers: 0
    property bool queueMode: false
    property bool albumMode: false
    property string albumArtist: ""
    readonly property bool repeatedAlbumArtist: albumMode &&
        String(track.artist || "").trim().toLocaleLowerCase() === albumArtist.trim().toLocaleLowerCase()
    // Material's segmented list style: a run drawn as one group, its items set
    // apart rather than divided by a rule, round at the ends of the run and
    // nearly square inside it.
    property bool segmented: false
    property bool firstInRun: true
    property bool lastInRun: true
    readonly property bool pointerOver: pointer.containsMouse
    function pointerInside(item) {
        const p = item.mapFromItem(pointer, pointer.mouseX, pointer.mouseY)
        return pointer.containsMouse && p.x >= 0 && p.x < item.width && p.y >= 0 && p.y < item.height
    }
    // Material's swipe to dismiss. A queue row can be pushed aside to drop it,
    // revealing the action behind it as it goes; past a third of the row the
    // release commits. Reordering still owns any drag that is mostly vertical.
    property real swipe: 0
    property bool swiping: false
    readonly property real dismissThreshold: width/3
    signal dismissRequested()
    property bool active: queueMode ? rowIndex===app.currentIndex : app.current.id !== undefined && app.current.id === track.id
    signal menuRequested(var item, int index, var anchor)
    // With reuseItems, Qt 6.11 culls a pooled delegate rather than hiding
    // it, and Tab still stops in a culled item: focus could rest on a row off
    // screen that still describes the track it held before, and revealing it
    // positions the list at that stale index. Every reused delegate with a
    // Tab stop turns invisible while pooled, which Tab does respect.
    property bool pooled: false
    ListView.onPooled: pooled=true
    ListView.onReused: {pooled=false;motionRaised=false;tooltipFocusFromPointer=false;opacity=Qt.binding(()=>enabled?1:Theme.disabledContentOpacity);}
    // A row's one Tab stop is its actions button; from there Tab and
    // Shift+Tab move to the next or previous row in list order (TrackList.qml).
    function firstTabStop(){return actionsButton.visible?actionsButton:null}
    function lastTabStop(){return firstTabStop()}
    function stepTab(forward){return !!listOwner && !!listOwner.tabFrom && listOwner.tabFrom(selectionIndex,false,forward)}
    // PaneMotion.kt:150-177 uses DefaultSpatial for bounds changes.
    Behavior on implicitHeight {enabled:app.motion && visible && !dragging;NumberAnimation {id:rowResize;objectName:"trackRowResizeMotion";duration:Theme.springSpatialMs;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.springSpatial}}
    Connections {target:app;function onSettingsChanged(){if(!app.motion)rowResize.complete();}}
    // ListTokens.ItemLeadingImageWidth/Height is 56dp. The compact option
    // uses ItemLeadingAvatarSize (40dp), including in queue rows.
    readonly property int leadingSize: queueMode ? (app.compactDensity?40:56) : Theme.rowArtwork
    // ListItem.kt:1275 pads a one- or two-line item 8dp above and below its
    // content (ListItemVerticalPadding); Basic's 12dp left a 56dp cover no
    // room in a 72dp row, so it hung below the container.
    topPadding: 8
    bottomPadding: 8
    // ListItem.kt:1172 sizes an item as the larger of its container token and
    // its padded content. ListTokens.ItemOneLineContainerHeight is 56dp when
    // the album heading already supplies the artist, but the 56dp cover then
    // makes the row 72dp, the same as an ordinary two-line row.
    implicitHeight: Math.max(queueMode ? (app.compactDensity?56:72) : repeatedAlbumArtist ? 56 : Theme.rowHeight,
                             leadingSize + topPadding + bottomPadding)
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
            Behavior on color { ColorAnimation { duration: Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
            Icon {
                anchors.centerIn: parent
                name: "remove"; size: 20
                ink: parent.parent.committing ? Theme.primaryText : Theme.secondaryContainerText
            }
        }
    }
    NumberAnimation { id: swipeReturn; target: row; property: "swipe"; to: 0; duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial }
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
        color: row.carried ? Theme.primaryContainer : row.selected ? Theme.secondaryContainer : row.motionRaised ? Theme.container : row.active ? Theme.high : row.hovered ? Theme.container : row.segmented ? Theme.container : "transparent"
        // Rows meet their neighbours, so the ring is drawn on the row's own
        // edge rather than outside it, at Material's focus-ring width.
        border.width: row.keyboardCurrent ? Theme.focusRingWidth : 0; border.color: Theme.focusRing
        // ListItem.kt:1502-1505 animates an interactive item's colour on
        // DefaultEffects and its shape on FastSpatial: the corners move, so
        // they may ring; the colour may not.
        Behavior on color { ColorAnimation { duration: Theme.springEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springEffects } }
        Behavior on topLeftRadius { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
        Behavior on bottomLeftRadius { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
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
        onPressed: mouse=> {titleLabel.dismissTooltip();supportLabel.dismissTooltip();if(mouse.button===Qt.RightButton){if(row.track.kind!=="smart")row.menuRequested(row.track,row.rowIndex,row);return;}row.tooltipFocusFromPointer=true;row.pressPoint=Qt.point(mouse.x,mouse.y);row.pressModifiers=mouse.modifiers;row.dragging=false;row.forceActiveFocus();row.listOwner.currentIndex=row.selectionIndex;}
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
        // ListItem.kt:1976-1977 reads ListTokens.ItemBetweenSpace (12dp) as
        // the leading-to-text gap; ItemLeadingSpace is outer padding.
        spacing: 12
        transform: Translate { x: row.swipe }
        Item {
            objectName: "trackLeading"
            Layout.preferredWidth: row.leadingSize; Layout.preferredHeight: row.leadingSize
            Icon { objectName: "mixKindIcon"; anchors.centerIn: parent; name: "filter"; size: 24; ink: Theme.primary; visible: row.track.kind==="smart" }
            // Every row leads with its cover, album pages included: a track
            // number is not always known (YouTube Music sends none), and an
            // empty slot read as missing artwork. RoundedArt's cache decodes
            // an album's shared cover once for all its rows.
            Artwork { objectName: "trackLeadingArtwork"; visible: row.track.kind!=="smart"; anchors.fill: parent; url: row.track.art || ""; radius: Theme.shapeSmall; pixels: 112 }
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
            // ListTokens.ItemLabelTextFont is BodyLarge at its own weight.
            MatchText { id: titleLabel; objectName: "trackTitle"; query: row.matchQuery; tooltipEnabled: row.titleRevealAllowed; revealFocused: row.keyboardCurrent && !row.tooltipFocusFromPointer; pointerHovered: row.pointerInside(titleLabel); sourceText: row.track.title || ""; Layout.fillWidth: true; font.pixelSize: Theme.bodyLarge; typeRole: "bodyLarge"; color: row.titleInk }
            // ListTokens.ItemSupportingTextFont is BodyMedium. Album rows omit
            // only the artist already stated in the album heading.
            MatchText { id: supportLabel; objectName: "trackSupport"; visible: row.track.kind==="smart" || !row.repeatedAlbumArtist; query: row.matchQuery; tooltipEnabled: row.titleRevealAllowed; revealFocused: row.keyboardCurrent && !row.tooltipFocusFromPointer; pointerHovered: row.pointerInside(supportLabel); sourceText: row.track.kind==="smart" ? (row.track.description || "") : row.track.artist || (row.track.kind === "artist" ? "Artist" : row.track.kind === "album" ? "Album" : row.track.kind === "playlist" ? "Playlist" : ""); Layout.fillWidth: true; color: row.supportInk; font.pixelSize: Theme.bodyMedium; typeRole: "bodyMedium" }
        }
        // Qt Loader.active releases the inactive indicator. Keep this Loader
        // invisible too, so RowLayout adds no spacing before the duration.
        Loader {
            objectName: "trailingIndicatorLoader"
            active: row.active
            visible: active
            sourceComponent: PlayingIndicator { ink: row.titleInk }
        }
        // A row's trailing supporting text is label small
        // (ListTokens.ItemTrailingSupportingTextFont), not body small.
        SungText { font.features: {"tnum": 1}; visible: !row.queueMode || row.width>350; text: row.track.duration || (row.track.seconds ? app.formatTime(row.track.seconds*1000) : ""); font.pixelSize: Theme.labelSmall; labelRole: true; color: row.supportInk; Layout.rightMargin: 2 }
        MButton { id: actionsButton; Keys.onTabPressed: event => event.accepted=row.stepTab(!(event.modifiers&Qt.ShiftModifier)); Keys.onBacktabPressed: event => event.accepted=row.stepTab(false); visible: row.track.kind!=="smart"; symbol: "more"; tip: "Track actions"; Accessible.name: "Actions for "+(row.track.title||"track"); ink: row.actionInk; onClicked: row.menuRequested(row.track,row.rowIndex,this) }
    }
}
