import QtQuick
import QtQml
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import QtCore
import Sung.Native 1.0

ApplicationWindow {
    id: window
    objectName: "sungWindow"
    visible: true
    width: 1180; height: 800
    minimumWidth: 480; minimumHeight: 580
    font.family: Theme.fontFamily
    title: app.current.title ? app.current.title + " · Sung" : "Sung"
    color: Theme.background
    RoundedArt {
        id: accentSample; objectName: "accentSample"; visible: false; pixels: 48
        source: app.artworkAccent ? (app.current.art || "") : ""
        onReadyChanged: {const color=seedColor();Theme.artworkSeed=ready ? color : "transparent";}
    }
    property string destination: "home"
    property string filter: "songs"
    property string libraryTab: "favorites"
    property string localPlaylist: ""
    property string side: ""
    // The queue panel shows what is coming, or what has just gone.
    property string queueTab: "next"
    property bool collectionTools: false
    // Where the library's primary action applies. The rail's slot and the FAB
    // itself both ask this rather than each other: a slot sized by whether its
    // child is visible, holding a child whose visibility is its parent's, never
    // opens.
    readonly property bool libraryAddApplies: destination==="library" && !localPlaylist
                                              && ["files","local-albums","local-artists","playlists"].indexOf(libraryTab)>=0
    property var miniPlayer: null
    readonly property bool uiActive: (visible && visibility!==Window.Minimized) || (miniPlayer!==null && miniPlayer.visible && miniPlayer.visibility!==Window.Minimized)
    onUiActiveChanged: {app.setUiActive(uiActive);if(!uiActive){cancelCoverFlight();cancelAlbumFlight();}}
    Binding { target: motionArtwork; property: "source"; value: window.uiActive && app.motion && app.animatedArtwork ? (app.currentMotionArt || "") : "" }
    Binding { target: motionArtwork; property: "running"; value: window.uiActive && app.playing && app.motion && app.animatedArtwork }
    property var fileDialogs: null
    function openFileDialog(kind) {
        if(!fileDialogs) {
            const component=Qt.createComponent("FileDialogs.qml");
            fileDialogs=component.createObject(window,{ownerWindow:window});
            if(!fileDialogs){console.error(component.errorString());return;}
        }
        fileDialogs.open(kind);
    }
    property bool compactMode: false
    property bool immersive: false
    property bool wasMaximized: false
    property bool geometryReady: false
    property var homeSections: {app.sections;app.pins;app.homeOrder;app.hiddenHomeSections;return app.homeSections();}
    // Whether this page has something other than a list of songs to show: the
    // shelves themselves, or the prompt that offers them back once they have
    // all been hidden. It is not the same question as whether the catalogue
    // returned sections. The feed also carries the pinned row, and asking the
    // catalogue put the list's empty state on top of the pins whenever the
    // catalogue came back with nothing, which is what an offline home is.
    readonly property bool feedShowing: homeSections.length>0
                                      || (app.page==="home" && app.homeSections(true).length>0)
    // Home has no cover of its own, so it borrows one: the playing track when
    // there is one, otherwise the first artwork its shelves have loaded.
    readonly property string homeArtwork: {
        if(app.current.art)return app.current.art;
        for(const section of window.homeSections){
            for(const item of (section.items || []))
                if(item.art)return item.art;
        }
        return "";
    }
    property bool hasSongCollection: !window.feedShowing && app.results.count>0 && !!(app.results.get(0).videoId || app.results.get(0).localPath || app.results.get(0).serverSong)
    property var bulkView: null
    property var batchItems: []
    property var menuItem: ({})
    property int menuIndex: -1
    property bool menuQueue: false
    property string playlistAction: "create"
    property string editPlaylistId: ""
    property string toastText: ""
    property bool toastPending: false
    readonly property bool toastHasUndo: !!app.undoMessage && toastText === app.undoMessage
    readonly property bool serverDisconnected: app.page === "server" && !app.server.connected && !app.server.connecting
    // Material's window size classes, and the pane margins and grid gutter it
    // keys to them. The feed on Home reads its card size off the same classes,
    // so a wider window shows more of the library rather than the same amount
    // stretched across it.
    readonly property string sizeClass: width<600?"compact":width<840?"medium":width<1200?"expanded":width<1600?"large":"extraLarge"
    // The classes as questions a layout can ask. Every decision about what fits
    // reads one of these, so it names a breakpoint Material publishes rather
    // than a number that happens to look about right.
    readonly property bool atLeastMedium: width >= 600
    readonly property bool atLeastExpanded: width >= 840
    readonly property bool atLeastLarge: width >= 1200
    readonly property int paneMargin: sizeClass==="compact"?16:24
    readonly property int paneGutter: sizeClass==="compact"||sizeClass==="medium"?16:24
    readonly property int feedCardWidth: sizeClass==="large"?210:sizeClass==="extraLarge"?230:190
    // Material scrims the page while the search view is open, so what the view
    // offers is plainly in front of the page rather than part of it.
    readonly property bool searchViewOpen: searchSuggestions.visible
    property bool searchFocused: (window.activeFocusItem && window.activeFocusItem.handlesTextInput===true) || searchField.activeFocus || (window.activeFocusItem && window.activeFocusItem.objectName==="lyricSearchField")
    readonly property bool editableLocal: {app.playlists;return !!localPlaylist && !app.smartPlaylist(localPlaylist).id;}
    readonly property bool modalOpen: immersiveQueue.visible || otherModalOpen
    readonly property bool otherModalOpen: trimDialog.visible || onboarding.visible || artworkViewer.visible || (immersiveLoader.item && immersiveLoader.item.popupVisible) || viewLayoutDialog.visible || volumeControl.popupVisible || homeEditor.visible || outputPicker.visible || sessionsDialog.visible || statsDialog.visible || playlistVersionsDialog.visible || playlistCoverDialog.visible || commandPalette.visible || artworkControls.visible || smartDialog.visible || trackDetails.visible || shortcutHelp.visible || duplicateDialog.visible || serverToolbar.dialogOpen || serverConnection.visible || serverAddDialog.visible || serverRenameDialog.visible || serverDeleteDialog.visible || serverRatingDialog.visible || musicFoldersDialog.visible || musicFolderEntry.visible || cleanupDialog.visible || navigationDrawer.visible || bulkActions.visible || volumeStepMenu.visible || rateDialog.visible || lyricTimingDialog.visible || settingsDialog.visible || playlistDialog.visible || addPlaylistDialog.visible || deletePlaylistDialog.visible || actions.visible || playlistActions.visible || sleepMenu.visible || (fileDialogs!==null && fileDialogs.visible) || audioDeviceDialog.visible || collectionSort.menuOpen
    property bool sliderFocused: window.activeFocusItem && window.activeFocusItem.handlesArrowKeys === true
    function selectedView() {var item=window.activeFocusItem;while(item){if(item.sourceRows!==undefined)return item;item=item.parent;}return tracks;}
    function addBatch(view) {batchItems=view.selection.items();addPlaylistDialog.open();}
    property var viewPositions: ({})
    property var viewPositionOrder: []
    property bool restoreViewPending: false
    function rememberView() {
        viewPositions[app.viewKey]={tracks:tracks.contentY-tracks.originY,shelves:shelves.contentY-shelves.originY,playlists:localPlaylists.contentY-localPlaylists.originY,tools:collectionTools};
        viewPositionOrder=viewPositionOrder.filter(k=>k!==app.viewKey).concat([app.viewKey]);
        while(viewPositionOrder.length>32)delete viewPositions[viewPositionOrder.shift()];
        restoreViewPending=true;
    }
    function restoreView() {
        if(!restoreViewPending || app.busy)return;
        restoreViewPending=false;
        const saved=viewPositions[app.viewKey] || {};
        function place(view,y) {view.forceLayout();view.contentY=view.originY+Math.max(0,Math.min(y || 0,Math.max(0,view.contentHeight-view.height)));}
        place(tracks,saved.tracks);place(shelves,saved.shelves);place(localPlaylists,saved.playlists);
        collectionTools=!!saved.tools || !!app.collection.query || app.collection.sortKey!=="original";
    }
    property bool revealPending: false
    function revealPlaying() {
        if(app.currentIndex<0)return;
        if(immersive){immersiveQueue.open();return;}
        revealPending=true;side="queue";
        Qt.callLater(()=>{if(sideLoader.item && sideLoader.item.revealCurrent)sideLoader.item.revealCurrent();});
    }
    function addPlaylistSelection(id) {
        const items=batchItems.length?batchItems.slice():[menuItem];
        const info=app.playlistAdditionInfo(id,items);
        if(info.duplicates>0 && info.added>0){duplicateDialog.playlistId=id;duplicateDialog.items=items;duplicateDialog.duplicates=info.duplicates;duplicateDialog.open();}
        else app.addItemsToPlaylist(id,items);
    }
    QtObject {
        id: trackDrag
        property bool dropping: false
        property var owner: null
        property var rows: []
        property var items: []
        function begin(view,indices,songs,point) {owner=view;rows=indices;items=songs;dragGhost.x=point.x;dragGhost.y=point.y;dragGhost.Drag.active=true;}
        function move(point) {dragGhost.x=point.x;dragGhost.y=point.y;}
        function finish() {dropping=true;try {dragGhost.Drag.drop();} finally {dropping=false;cancel();}}
        function cancel() {if(dropping)return;dragGhost.Drag.cancel();owner=null;rows=[];items=[];}
    }
    Rectangle {
        id: dragGhost; parent: window.contentItem; z: 1000
        objectName: "trackDragPreview"
        width: 180; height: 68; radius: Theme.shapeLargeIncreased; color: Theme.high; border.color: Theme.outlineVariant
        Repeater { model: dragGhost.visible?Math.min(3,trackDrag.items.length):0
            Artwork { required property int index; x: 12+(2-index)*6; y: 10+(2-index)*3; width: 42; height: 42; radius: Theme.shapeSmall; pixels: 96; rotation: (2-index)*5; url: trackDrag.items[index].art || "" }
        }
        visible: Drag.active
        Drag.source: trackDrag; Drag.keys: ["sung-tracks"]; Drag.hotSpot.x: 0; Drag.hotSpot.y: 0
        SungText { x: 80; width: 90; anchors.verticalCenter: parent.verticalCenter; text: window.countText(trackDrag.items.length); color: Theme.text; font.pixelSize: Theme.labelLarge }
    }
    function countText(n) { return n + (n === 1 ? " track" : " tracks"); }
    TrackPresentation { id: nowPresentation; visible: !window.immersive && !window.compactMode }
    property real previousVolume: 0.65
    function toggleMute() { if(app.volume>0){previousVolume=app.volume;app.volume=0;}else app.volume=previousVolume; }
    Settings { id: listeningSettings; category: "Listening"; property string layout: "split"; property bool autoHide: false; property bool coverflow: false }
    property string immersiveReturnView: ""
    function openImmersiveCollection(item) {
        if(!item.kind)return;
        immersiveReturnView=app.viewKey;
        if(immersive)toggleImmersive();
        app.open(item);
    }
    function showQueue() {
        if(immersive)immersiveQueue.open();else activateSide("queue");
    }
    function playbackSeek(delta) {
        if(app.currentIndex<0 || app.duration<=0)return;
        const target=Math.max(0,Math.min(app.duration,app.position+delta));
        app.seek(target);playbackHud.show(delta>0?"next":"previous",app.formatTime(target));
        if(immersiveLoader.item)immersiveLoader.item.wake();
    }
    function playbackVolume(delta) {
        app.volume=Math.max(0,Math.min(1,app.volume+delta*app.volumeStep/100));
        playbackHud.show(app.volume>0?"volume":"mute",Math.round(app.volume*100)+"%");
        if(immersiveLoader.item)immersiveLoader.item.wake();
    }
    // A panel width of nought means nobody has dragged it, so it follows the
    // canonical supporting pane proportion instead.
    Settings { id: geometry; category: "Window"; property int width: 1180; property int height: 800; property real panelWidth: 0 }
    Settings { id: railSettings; category: "Navigation"; property bool expanded: false }
    Component.onCompleted: { windowResources.manage(window);width=geometry.width;height=geometry.height;geometryReady=true;app.setUiActive(uiActive);if(!app.onboarded)Qt.callLater(()=>{if(!app.onboarded)onboarding.open();}); }
    onWidthChanged: {if(albumFlying)cancelAlbumFlight();if(geometryReady && !immersive && visibility===Window.Windowed)geometry.width=width;}
    onHeightChanged: {if(albumFlying)cancelAlbumFlight();if(geometryReady && !immersive && visibility===Window.Windowed)geometry.height=height;}
    property bool albumFlying: false
    property bool albumOpening: false
    property bool albumReturning:false
    property string albumOriginView:""
    property string albumOriginId:""
    property string albumFlightView: ""
    function cancelAlbumFlight(){albumSettle.stop();albumTimeout.stop();albumFlight.stop();albumFlying=false;albumReturning=false;albumFly.url="";}
    function openCollection(item,source){
        cancelAlbumFlight();
        // Material's container transform carries a card into the page it opens,
        // and it is the shape that carries it: the cover keeps its own corner
        // at the start and takes the detail's at the end. Any collection with a
        // cover can make that move, not only an album.
        if(app.motion && window.uiActive && source && item.art
           && ["album","local-album","artist","local-artist","playlist","local-playlist"].indexOf(item.kind)>=0){
            const at=source.mapToItem(window.contentItem,0,0);
            albumFly.x=at.x;albumFly.y=at.y;albumFly.width=source.width;albumFly.height=source.height;
            albumFly.radius=source.radius!==undefined?source.radius:20;
            albumFly.shape=source.shape!==undefined?source.shape:"";
            albumFly.url=item.art;albumFlying=true;
        }
        albumOriginView=app.viewKey;albumOriginId=item.id || item.browseId || "";
        const step=function(){
            albumOpening=true;app.open(item);albumOpening=false;albumFlightView=app.viewKey;
            if(albumFlying){albumTimeout.restart();albumSettle.restart();}
        }
        // A cover that flies carries the relationship on its own; without one
        // the move is Material's forward step through the hierarchy.
        if(albumFlying)step()
        else destinationTransition.forwardBackward(step,true)
    }
    function findAlbumCard(root){
        if(root.albumCardId!==undefined && root.albumCardId===albumOriginId && root.visible){
            const at=root.mapToItem(window.contentItem,0,0);
            if(at.x>=0 && at.y>=0 && at.x+root.width<=window.width && at.y+root.width<=window.height-100)return root;
        }
        for(const child of root.children){const found=findAlbumCard(child);if(found)return found;}
        return null;
    }
    function navigateBack(){
        const canReturn=app.motion && window.uiActive && app.viewKey===albumFlightView && albumOriginId && collectionArtwork.visible;
        cancelAlbumFlight();
        if(canReturn){const at=collectionArtwork.mapToItem(window.contentItem,0,0);albumFly.x=at.x;albumFly.y=at.y;albumFly.width=collectionArtwork.width;albumFly.height=collectionArtwork.height;albumFly.radius=collectionArtwork.radius;albumFly.shape=collectionArtwork.shape;albumFly.url=app.cover;albumFlying=true;albumReturning=true;}
        const step=function(){
            albumOpening=true;app.back();albumOpening=false;
            if(immersiveReturnView && app.viewKey===immersiveReturnView){
                immersiveReturnView="";cancelAlbumFlight();if(!immersive)toggleImmersive();return;
            }
            if(albumFlying){albumTimeout.restart();albumSettle.restart();}
        }
        if(albumFlying || immersiveReturnView)step()
        else destinationTransition.forwardBackward(step,false)
    }
    Timer {id:albumTimeout;interval:4000;onTriggered:window.cancelAlbumFlight()}
    Timer {id:albumSettle;interval:48;onTriggered:{
        if(!window.albumFlying || app.busy)return;
        if(window.albumReturning){
            if(app.viewKey!==window.albumOriginView){window.cancelAlbumFlight();return;}
            const card=window.findAlbumCard(content);
            if(!card){window.cancelAlbumFlight();return;}
            const at=card.mapToItem(window.contentItem,0,0);albumFly.endX=at.x;albumFly.endY=at.y;albumFly.endSize=card.width;albumFly.endRadius=card.corner!==undefined?card.corner:20;albumFlight.start();return;
        }
        if(app.viewKey!==window.albumFlightView || !collectionArtwork.visible || !app.cover || !app.albumInfo.summary){window.cancelAlbumFlight();return;}
        const at=collectionArtwork.mapToItem(window.contentItem,0,0);
        albumFly.endX=at.x;albumFly.endY=at.y;albumFly.endSize=collectionArtwork.width;albumFly.endRadius=collectionArtwork.radius;albumFlight.start();
    }}
    Artwork {id:albumFly;objectName:"albumFlightArtwork";z:79;visible:window.albumFlying;pixels:480
        property real endX:0;property real endY:0;property real endSize:0;property real endRadius:24
    }
    ParallelAnimation {id:albumFlight
        NumberAnimation {target:albumFly;property:"x";to:albumFly.endX;duration:350;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.curve}
        NumberAnimation {target:albumFly;property:"y";to:albumFly.endY;duration:350;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.curve}
        NumberAnimation {target:albumFly;property:"width";to:albumFly.endSize;duration:350;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.curve}
        NumberAnimation {target:albumFly;property:"height";to:albumFly.endSize;duration:350;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.curve}
        NumberAnimation {target:albumFly;property:"radius";to:albumFly.endRadius;duration:350;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.curve}
        onFinished:window.cancelAlbumFlight()
    }
    Connections {target:app;function onSettingsChanged(){if(!app.motion)window.cancelAlbumFlight();}}
    property bool coverFlying: false
    property real coverDetailsOpacity: coverFlying?0:1
    Behavior on coverDetailsOpacity { NumberAnimation { duration: app.motion?120:0; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve } }
    property point flightGlobal: Qt.point(0,0)
    function cancelCoverFlight() {coverFlightAnimation.stop();flightSettle.stop();coverFlying=false;flyingCover.url="";}
    function prepareCoverFlight(source) {
        cancelCoverFlight();
        if(!app.motion || !source || !source.visible || !app.current.art || !window.visible || window.visibility===Window.Minimized)return;
        flightGlobal=source.mapToGlobal(0,0);
        const at=source.mapToItem(window.contentItem,0,0);
        flyingCover.x=at.x;flyingCover.y=at.y;flyingCover.width=source.width;flyingCover.height=source.height;flyingCover.radius=source.radius || 12;
        flyingCover.url=app.current.art;coverFlying=true;
    }
    Timer {
        id: flightSettle; interval: 48
        onTriggered: {
            if(!window.coverFlying)return;
            const target=window.immersive && immersiveLoader.item?immersiveLoader.item.artwork:nowArtwork;
            if(!target || !target.visible || target.width<=0){window.cancelCoverFlight();return;}
            const start=window.contentItem.mapFromGlobal(window.flightGlobal.x,window.flightGlobal.y);
            const end=target.mapToItem(window.contentItem,0,0);
            flyingCover.x=start.x;flyingCover.y=start.y;
            flyingCover.endX=end.x;flyingCover.endY=end.y;flyingCover.endWidth=target.width;flyingCover.endHeight=target.height;flyingCover.endRadius=target.radius;
            coverFlightAnimation.start();
        }
    }
    Artwork {
        id: flyingCover; objectName: "flyingArtwork"; z: 80; visible: window.coverFlying; pixels: 800; fit:app.currentArtworkFit
        property real endX: 0; property real endY: 0; property real endWidth: 0; property real endHeight: 0; property real endRadius: 0
    }
    ParallelAnimation {
        id: coverFlightAnimation
        NumberAnimation { target: flyingCover; property: "x"; to: flyingCover.endX; duration: 350; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve }
        NumberAnimation { target: flyingCover; property: "y"; to: flyingCover.endY; duration: 350; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve }
        NumberAnimation { target: flyingCover; property: "width"; to: flyingCover.endWidth; duration: 350; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve }
        NumberAnimation { target: flyingCover; property: "height"; to: flyingCover.endHeight; duration: 350; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve }
        NumberAnimation { target: flyingCover; property: "radius"; to: flyingCover.endRadius; duration: 350; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve }
        onFinished: {window.coverFlying=false;flyingCover.url="";}
    }
    Connections { target: app; function onSettingsChanged(){if(!app.motion)window.cancelCoverFlight();} }
    function toggleImmersive() {
        cancelAlbumFlight();
        if(immersive){
            immersiveQueue.close();
            prepareCoverFlight(immersiveLoader.item?immersiveLoader.item.artwork:null);
            if(wasMaximized)showMaximized();else {showNormal();width=geometry.width;height=geometry.height;}
            immersive=false;content.forceActiveFocus();
        }else if(app.currentIndex>=0){
            prepareCoverFlight(nowArtwork);wasMaximized=visibility===Window.Maximized;immersive=true;showFullScreen();app.fetchLyrics();
        }
        if(coverFlying)flightSettle.restart();
    }

    function trackMenu(item,index,anchor,queueMode) {
        var view=anchor;
        while(view && view.sourceRows===undefined)view=view.parent;
        if(view && view.selection.count>1 && view.sourceRows().indexOf(index)>=0){bulkView=view;bulkActions.popup(anchor,anchor.width-bulkActions.width,anchor.height+4);return;}
        batchItems=[];menuItem=item;menuIndex=index;menuQueue=queueMode;
        const p=anchor.mapToItem(window.contentItem,anchor.width,anchor.height);
        actions.x=Math.max(12,Math.min(p.x-actions.width,window.width-actions.width-12));
        actions.y=Math.max(12,Math.min(p.y,window.height-actions.height-12));
        actions.open();
    }
    function openMiniPlayer() {
        if(immersive)toggleImmersive();
        if(!miniPlayer) miniPlayer=miniComponent.createObject(window);
        if(!miniPlayer)return;
        compactMode=true;miniPlayer.show();miniPlayer.raise();miniPlayer.requestActivate();window.hide();
        if(app.currentIndex>=0)app.fetchLyrics();
    }
    function restorePlayer() {
        compactMode=false;if(immersive)window.showFullScreen();else window.showNormal();window.raise();window.requestActivate();
        if(miniPlayer)miniPlayer.hide();
    }
    function relatedItem(item,kind) {
        return {kind:kind,title:kind==="artist"?item.artist:item.album,browseId:item[kind+"Id"],remoteId:item[kind+"Id"],source:item.source || "",server:item.server || ""}
    }
    function openServerConnection() {serverConnection.open()}
    // The field is part of the Search page rather than the window, so reaching
    // it means arriving there first; an item that is not on screen yet cannot
    // take the focus.
    function focusSearch() {
        if(app.page==="server"){searchField.forceActiveFocus();searchField.selectAll();return;}
        if(immersive)toggleImmersive();
        side="";app.startSearch();destination="search";
        searchField.forceActiveFocus();searchField.selectAll();
        Qt.callLater(()=>{if(!searchField.activeFocus){searchField.forceActiveFocus();searchField.selectAll();}});
    }
    // Material 3 navigation motion. Rail destinations fade through; the library
    // tabs are peers on one line, so they share the X axis in the direction of
    // travel through them.
    NavigationTransition { id: destinationTransition; objectName: "destinationTransition"; target: contentColumn }
    NavigationTransition { id: tabTransition; objectName: "tabTransition"; target: contentBody }
    readonly property var libraryOrder: ["favorites","playlists","files","local-albums","local-artists","mixes","history","server"]
    // Material's compact window class. Below it a rail would be taking room the
    // content needs, so navigation moves to a bar along the bottom.
    readonly property bool compactWindow: !atLeastMedium
    // Navigation down the leading edge instead of across the top bar, which
    // is the arrangement the top bar replaced. The rail needs a window wide
    // enough to sit beside the content; below that the same bar Material puts
    // against the bottom edge takes over, as it did before.
    // What a snackbar has to clear at the foot of the window. Material puts it
    // above whatever is anchored there rather than over it, and what is
    // anchored there is not the same thing in every view.
    readonly property real bottomChrome: immersive
        ? (immersiveLoader.item ? immersiveLoader.item.bottomChrome : 0)
        : compactMode ? 0 : playbackBar.height + 16
    readonly property bool sidebarNav: app.sidebarNavigation
    readonly property bool railShowing: sidebarNav && !compactWindow
    readonly property bool bottomBarShowing: sidebarNav && compactWindow
    // Material's supporting pane sits beside the content from the expanded
    // class up, and drops into a bottom sheet below it. This was 1000, which
    // is not a breakpoint.
    readonly property bool sheetMode: !atLeastExpanded
    readonly property bool artistPage: app.page==="artist" || app.page==="local-artist" || (app.page==="server" && app.serverRequest.mode==="artist")
    function confirmQueued() { toastPending=false;toastText="Added to queue";toastPending=true; }
    // Which destination navigation marks. Pressing one marks it at once, while
    // the page it leads to changes on the transition's own timing. The two were
    // the same property, so the indicator sat still for the whole of the
    // outgoing fade and then finished moving long after the page had settled,
    // which is what made a destination change feel like two separate events.
    property string markedDestination: destination
    onDestinationChanged: markedDestination = destination
    // Both arrangements offer the same three destinations, so they reach them
    // through the same function rather than each keeping its own copy of what
    // arriving somewhere means.
    function goToDestination(key) {
        if(destination===key)return
        markedDestination=key
        destinationTransition.fadeThrough(() => {
            destination=key
            if(key==="home")app.home()
            else if(key==="search"){app.startSearch();focusSearch();}
            else applyLibrary("favorites")
        })
    }
    function applyLibrary(kind) { destination="library";libraryTab=kind;localPlaylist="";side="";app.library(kind); }
    function chooseLibrary(kind) {
        const from=libraryOrder.indexOf(libraryTab), to=libraryOrder.indexOf(kind)
        // Stepping between library tabs is travel between peers. Reaching the
        // library from elsewhere is a change of destination, so it fades.
        if(destination!=="library" || from<0 || to<0)destinationTransition.fadeThrough(() => applyLibrary(kind))
        else if(from===to)applyLibrary(kind)
        else tabTransition.sharedAxisX(() => applyLibrary(kind),to>from)
    }
    function activateSide(which) { side=side===which?"":which;if(side==="lyrics")app.fetchLyrics(); }

    function quickCommands() {
        let rows=[{id:"view-layout",title:"Current view layout"},{id:"home-layout",title:"Customize Home"},{id:"sessions",title:"Listening sessions"},{id:"stats",title:"Listening statistics"},{id:"search",title:"Search music"},{id:"queue",title:"Show queue"},{id:"lyrics",title:"Show lyrics"},{id:"playing",title:"Show playing song"},{id:"mini",title:"Open mini player"},{id:"settings",title:"Open settings"},{id:"folders",title:"Manage music folders"},{id:"rescan",title:"Rescan music folders"},{id:"files",title:"Browse local music"},{id:"favorites",title:"Browse liked songs"}];
        if(app.currentIndex>=0)rows.push({id:"artwork",title:"Change animated cover"},{id:"play",title:app.playing?"Pause playback":"Resume playback"},{id:"immersive",title:"Toggle immersive player"});
        for(const p of app.playlists)rows.push({id:"playlist:"+p.id,title:"Open playlist · "+p.title,value:p.id});
        for(const device of app.audioDevices)rows.push({id:"device:"+device.id,title:"Audio output · "+device.name,value:device.id});
        return rows;
    }
    CommandPalette {
        id: commandPalette; anchors.centerIn: parent
        commands: visible?window.quickCommands():[]
        onChosen: c => {
            if(c.id.startsWith("playlist:")){window.destination="library";window.libraryTab="playlists";window.localPlaylist=c.value;app.openPlaylist(c.value);}
            else if(c.id.startsWith("device:"))app.audioDeviceId=c.value;
            else if(c.id==="search")window.focusSearch();
            else if(c.id==="queue" || c.id==="lyrics"){if(window.immersive)window.toggleImmersive();window.side=c.id;if(c.id==="lyrics")app.fetchLyrics();}
            else if(c.id==="playing")window.revealPlaying();
            else if(c.id==="mini")window.openMiniPlayer();
            else if(c.id==="settings")settingsDialog.open();
            else if(c.id==="sessions")sessionsDialog.open();
            else if(c.id==="stats")statsDialog.open();
            else if(c.id==="view-layout")viewLayoutDialog.open();
            else if(c.id==="home-layout"){app.home();homeEditor.open();}
            else if(c.id==="folders")musicFoldersDialog.open();
            else if(c.id==="rescan")app.rescanMusicFolders();
            else if(c.id==="artwork")artworkControls.open();
            else if(c.id==="play")app.toggle();
            else if(c.id==="immersive")window.toggleImmersive();
            else window.chooseLibrary(c.id);
        }
    }
    function previewPlaylistCover(url) {playlistCoverDialog.preview=app.preparePlaylistCover(url);playlistCoverDialog.open();}
    PlaylistCoverDialog { id: playlistCoverDialog; anchors.centerIn: parent; onChooseFile: window.openFileDialog("playlist-cover") }
    ViewLayoutDialog {id:viewLayoutDialog;anchors.centerIn:parent}
    HomeEditor {id:homeEditor;anchors.centerIn:parent}
    OutputPicker {id:outputPicker;parent:window.contentItem}
    ListeningSessions {id:sessionsDialog;anchors.centerIn:parent}
    ListeningStats {id:statsDialog;anchors.centerIn:parent}
    PlaylistVersions {id:playlistVersionsDialog;anchors.centerIn:parent}
    ArtworkViewer {id:artworkViewer}
    ArtworkControls { id: artworkControls;onInspectRequested:url=>artworkViewer.inspect(url); anchors.centerIn: parent; onChooseFile: window.openFileDialog("artwork") }
    Shortcut { sequence: "Ctrl+Shift+P"; enabled: !window.modalOpen; onActivated: commandPalette.open() }
    Shortcut { sequence: "Ctrl+J"; enabled: !window.modalOpen; onActivated: window.revealPlaying() }
    Shortcut { sequences: ["?", "F1"]; enabled: !window.modalOpen && !window.searchFocused; onActivated: shortcutHelp.open() }
    Shortcut { sequence: "Ctrl+M"; enabled: !window.modalOpen; onActivated: window.openMiniPlayer() }
    Shortcut { sequence: "Ctrl+K"; enabled: !window.modalOpen; onActivated: window.focusSearch() }
    Shortcut { sequence: "Ctrl+F"; enabled: !window.modalOpen; onActivated: window.focusSearch() }
    Shortcut { sequence: "Space"; enabled: !window.searchFocused && !window.modalOpen && (!window.activeFocusItem || window.activeFocusItem===content || window.activeFocusItem===immersiveLoader.item); onActivated: app.toggle() }
    Shortcut { sequence: "Ctrl+Right"; enabled: !window.modalOpen; onActivated: app.next() }
    Shortcut { sequence: "Ctrl+Left"; enabled: !window.modalOpen; onActivated: app.previous() }
    Shortcut { sequence: "Right"; enabled: !window.searchFocused && !collectionSearch.activeFocus && !window.modalOpen && !window.sliderFocused && !(window.activeFocusItem && window.activeFocusItem.libraryNavigation===true); onActivated: window.playbackSeek(10000) }
    Shortcut { sequence: "Left"; enabled: !window.searchFocused && !collectionSearch.activeFocus && !window.modalOpen && !window.sliderFocused && !(window.activeFocusItem && window.activeFocusItem.libraryNavigation===true); onActivated: window.playbackSeek(-10000) }
    Shortcut { sequence: "Ctrl+L"; enabled: !window.modalOpen; onActivated: window.showQueue() }
    Shortcut { sequence: "Ctrl+Up"; enabled: !window.modalOpen && !window.searchFocused; onActivated: window.playbackVolume(1) }
    Shortcut { sequence: "Ctrl+Down"; enabled: !window.modalOpen && !window.searchFocused; onActivated: window.playbackVolume(-1) }
    Shortcut { sequence: "M"; enabled: !window.modalOpen && !window.searchFocused && (window.activeFocusItem===content || window.activeFocusItem===immersiveLoader.item); onActivated: {window.toggleMute();playbackHud.show(app.volume>0?"volume":"mute",Math.round(app.volume*100)+"%");if(immersiveLoader.item)immersiveLoader.item.wake();} }
    Shortcut { sequence: "Ctrl+Y"; enabled: !window.modalOpen && !window.immersive; onActivated: window.activateSide("lyrics") }
    Shortcut { sequence: "Alt+Left"; enabled: !window.modalOpen && !window.immersive; onActivated: window.navigateBack() }
    Shortcut { sequence: "Escape"; enabled: !window.modalOpen && !(window.activeFocusItem && window.activeFocusItem.objectName==="lyricSearchField"); onActivated: { if(trackDrag.owner)trackDrag.cancel();else if(window.selectedView().selection.count)window.selectedView().selection.clear();else if(window.immersive)window.toggleImmersive();else {window.side="";content.forceActiveFocus();} } }
    Shortcut { sequence: "Ctrl+Q"; onActivated: window.close() }

    Instantiator {
        model: 10
        delegate: Shortcut {
            required property int index
            sequence: String(index)
            // Same reach as Space: never while typing or inside a list.
            enabled: !window.searchFocused && !window.modalOpen && app.duration>0 && (!window.activeFocusItem || window.activeFocusItem===content || window.activeFocusItem===immersiveLoader.item)
            onActivated: {
                const target=Math.round(app.duration*index/10);
                app.seek(target);
                playbackHud.show("next",app.formatTime(target));
                if(immersiveLoader.item)immersiveLoader.item.wake();
            }
        }
    }
    Shortcut { sequence: "F11"; enabled: !window.modalOpen; onActivated: window.toggleImmersive() }
    Loader { id: immersiveLoader; anchors.fill: parent; active: window.immersive; sourceComponent: Component { ImmersivePlayer { coverHidden: window.coverFlying;
                preferredLayout:listeningSettings.layout;autoHideControls:listeningSettings.autoHide;externalModalOpen:window.modalOpen
                coverflow:listeningSettings.coverflow
                onCoverflowRequested:enabled=>listeningSettings.coverflow=enabled
                onAutoHideRequested:enabled=>listeningSettings.autoHide=enabled
                onLayoutRequested:layout=>listeningSettings.layout=layout
                onQueueRequested:window.showQueue()
                onCollectionRequested:item=>window.openImmersiveCollection(item); onExitRequested: window.toggleImmersive(); onSpeedRequested: rateDialog.open(); onArtworkRequested:artworkViewer.inspect(app.current.art)
                onTimingRequested: lyricTimingDialog.open() } } }
    PlaybackHud {id:playbackHud;anchors.horizontalCenter:parent.horizontalCenter;anchors.bottom:parent.bottom;anchors.bottomMargin:window.immersive?172:128;z:90}
    Drawer {
        id:immersiveQueue;objectName:"immersiveQueueSheet";edge:Qt.RightEdge
        width:Math.min(420,window.width-32);height:window.height;modal:true;dim:true;focus:true;interactive:false
        padding:24;leftPadding:24;rightPadding:24;topPadding:24;bottomPadding:24;closePolicy:Popup.CloseOnEscape|Popup.CloseOnPressOutside
        background:Rectangle {color:Theme.container;radius:Theme.shapeExtraLarge}
        Overlay.modal:Rectangle {color:Theme.scrimColor()}
        enter:Transition {NumberAnimation {property:"position";to:1;duration:app.motion?350:0;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.curve}}
        exit:Transition {NumberAnimation {property:"position";to:0;duration:Theme.normal;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.exitCurve}}
        onOpened:{if(immersiveQueueLoader.item)immersiveQueueLoader.item.revealCurrent();}
        onClosed:{trackDrag.cancel();if(immersiveLoader.item){immersiveLoader.item.forceActiveFocus(Qt.PopupFocusReason);immersiveLoader.item.wake();}}
        contentItem:ColumnLayout {spacing:16
    Shortcut { sequence:"Escape";enabled:immersiveQueue.visible && !window.otherModalOpen;onActivated:{
        if(trackDrag.owner)trackDrag.cancel();
        else if(window.selectedView().selection.count)window.selectedView().selection.clear();
        else immersiveQueue.close();
    } }
            RowLayout {Layout.fillWidth:true
                SungText {text:"Queue";font.pixelSize:Theme.headlineSmall;Layout.fillWidth:true}
                MButton {objectName:"closeImmersiveQueue";symbol:"close";tip:"Close queue";onClicked:immersiveQueue.close()}
            }
            Loader {id:immersiveQueueLoader;Layout.fillWidth:true;Layout.fillHeight:true;active:immersiveQueue.visible;sourceComponent:queuePanel}
        }
    }
    // The cover the window takes its wash from: what is playing, then whatever
    // the page itself is showing, then a cover Home has borrowed.
    readonly property string windowArtwork: app.current.art || app.cover || window.homeArtwork || ""
    // Material keeps hero imagery behind the surfaces rather than on them. The
    // wash sits at the very back of the window and every panel floats over it,
    // so the colour of what is playing reaches the whole app rather than one
    // panel of it.
    AmbientBackdrop {
        objectName: "windowBackdrop"
        anchors.fill: parent
        visible: !window.compactMode && !window.immersive && active
        url: window.windowArtwork
        scrim: Theme.background
        dim: 0.80
        corner: 0
    }
    // How much of the wash the floating surfaces let through. Opaque when there
    // is no wash, so nothing changes for anyone who has turned it off.
    readonly property bool windowWashed: app.ambientBackdrop && !!window.windowArtwork && !window.compactMode && !window.immersive
    readonly property real washAlpha: windowWashed ? 0.74 : 1
    function washed(surface) { return Qt.rgba(surface.r,surface.g,surface.b,window.washAlpha) }
    Item {
        visible: !window.compactMode && !window.immersive
        anchors.fill: parent
        RowLayout {
        anchors.fill: parent; spacing: 0
        NavigationRail {
            id: navigationRail
            visible: window.railShowing
            expandedPreference: railSettings.expanded
            roomToExpand: window.atLeastExpanded
            current: window.markedDestination
            pins: app.pins
            libraryPending: app.importingLocal
            fabApplies: window.libraryAddApplies
            fabWidth: libraryFab.width; fabHeight: libraryFab.height
            pinSelected: item => app.libraryId===item.id || app.collectionItem.id===item.id
            onToggleRequested: railSettings.expanded=!railSettings.expanded
            onChosen: key => window.goToDestination(key)
            onPinChosen: item => app.open(item)
        }
        ColumnLayout {
            // The rail supplies the inset on the side it occupies, and the
            // bottom bar reaches two edges of the window, so the column gives
            // those up to whichever of them is on screen.
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.leftMargin: window.railShowing ? 0 : window.compactWindow ? 12 : 16
            Layout.rightMargin: window.bottomBarShowing ? 0 : window.compactWindow ? 12 : 16
            Layout.topMargin: window.compactWindow ? 12 : 16
            Layout.bottomMargin: window.bottomBarShowing ? 0 : window.compactWindow ? 12 : 16
            spacing: 12
            // Material's small top app bar, serving both arrangements. The
            // back action leads either way. The top bar arrangement centres
            // the navigation capsule on the bar and trails the window's own
            // actions; the sidebar arrangement gives the room to the search
            // bar and keeps those actions at the foot of the rail.
            Item {
                id: topBar
                objectName: "topBar"
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                Row {
                    id: topBarLeading
                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    MButton {
                        objectName: "drawerButton"; symbol: "menu"; tip: "Navigation"
                        visible: window.bottomBarShowing
                        onClicked: navigationDrawer.open()
                    }
                    MButton {
                        objectName: "backButton"; symbol: "back"; tip: "Back"
                        enabled: app.canBack; onClicked: window.navigateBack()
                    }
                }
                // Where the search bar sits in the sidebar arrangement, which
                // keeps it in the window's chrome on every page.
                Item {
                    id: topBarSearchHost
                    objectName: "topBarSearchHost"
                    visible: window.sidebarNav
                    anchors.left: topBarLeading.right; anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    height: 56
                    // 360dp to 720dp is the width Material gives a search bar.
                    width: Math.max(0, Math.min(720, parent.width - topBarLeading.width - 12))
                }
                Item { id: topBarCentre; anchors.fill: parent; visible: !window.sidebarNav }
                Row {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    visible: !window.sidebarNav
                    Item { id: topBarMiniHost; width: 48; height: 48 }
                    Item { id: topBarSettingsHost; width: 48; height: 48 }
                }
            }
            // A compact window cannot centre the capsule between the two sides
            // and still clear them, so the bar spans the column there, which is
            // the width Material gives it in the first place.
            Item {
                id: narrowNavHost
                objectName: "narrowNavHost"
                visible: !window.sidebarNav && !window.atLeastMedium
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? 64 : 0
            }
            RowLayout {
                id: contentRow
                // Material's list and detail layout: the list a detail was
                // opened from stays beside it once there is room for both. The
                // supporting pane can still take the far side, which is the
                // arrangement Material allows at this width.
                readonly property bool listDetail: app.listPaneId.length>0 && window.atLeastLarge && !window.sheetMode
                // Material's supporting pane layout splits the row two thirds
                // to one; the third is where the panel starts before anyone
                // drags it somewhere else.
                readonly property real supportingWidth: Math.max(320,Math.min(480,Math.round(width/3)))
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 12
                Rectangle {
                    id: listPane
                    objectName: "listPane"
                    Layout.preferredWidth: contentRow.listDetail ? 320 : 0
                    Layout.fillHeight: true
                    visible: Layout.preferredWidth > 1
                    clip: true
                    radius: Theme.shapeExtraLarge
                    color: window.washed(Theme.surfaceLow)
                    Behavior on Layout.preferredWidth { enabled: app.motion; NumberAnimation { duration: Theme.springSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial } }
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 16; spacing: 12
                        RowLayout {
                            Layout.fillWidth: true
                            SungText { objectName: "listPaneTitle"; text: app.listPaneTitle; font.pixelSize: Theme.titleLarge; Layout.fillWidth: true }
                            MButton { objectName: "listPaneClose"; symbol: "close"; tip: "Close list"; onClicked: window.chooseLibrary(app.listPaneId) }
                        }
                        GridView {
                            id: listPaneGrid
                            objectName: "listPaneGrid"
                            Layout.fillWidth: true; Layout.fillHeight: true
                            clip: true; reuseItems: true; cacheBuffer: 0
                            model: app.listPane
                            cellWidth: width/Math.max(1,Math.floor(width/140))
                            cellHeight: cellWidth+52
                            ScrollBar.vertical: MScrollBar {}
                            MSmoothWheel { flick: listPaneGrid }
                            delegate: ArtCard {
                                // A delegate that requires one property requires
                                // them all: index stops being handed over.
                                required property var entry
                                required property int index
                                objectName: "listPaneCard_"+index
                                width: listPaneGrid.cellWidth-12
                                track: entry
                                openHandler: window.openCollection
                            }
                        }
                    }
                }
                Rectangle {
                    id: content
                    Accessible.role: Accessible.Pane
                    Accessible.name: window.title || "Content"
                    readonly property real headerCollapse: tracks.visible ? Math.max(0,Math.min(1,(tracks.contentY-tracks.originY)/160)) : 0
                    readonly property bool compactHeader: headerCollapse>0.7
                    property real headerExtent: (app.albumInfo.summary?Math.min(156,window.height*0.19):76)*(1-headerCollapse)+40*headerCollapse
                    Behavior on headerExtent {enabled:!tracks.moving;NumberAnimation {duration:app.motion?120:0;easing.type:Easing.OutCubic}}
                    visible: true
                    Layout.fillWidth: true; Layout.fillHeight: true
                    radius: Theme.shapeExtraLarge; color: window.washed(Theme.surfaceLow); clip: true
                    Rectangle {
                        objectName: "searchScrim"
                        anchors.fill: parent; radius: parent.radius; z: 40
                        color: Theme.scrimColor()
                        visible: opacity>0
                        opacity: window.searchViewOpen ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
                    }
                    AmbientBackdrop {
                        objectName: "homeBackdrop"; anchors.fill: parent
                        // The window wash already carries this cover; repeating
                        // it inside the panel would only double the scrim.
                        url: app.page==="home" && !window.windowWashed ? window.homeArtwork : ""
                        scrim: Theme.surfaceLow; dim: 0.88; corner: parent.radius
                    }
                    // Material moves an app bar from the plain surface to a
                    // container, and lifts it two levels, as soon as content
                    // scrolls underneath it. Without that the list slides under
                    // a header that gives no sign it is in front.
                    Rectangle {
                        id: appBar
                        objectName: "appBarSurface"
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                        height: contentColumn.y + contentBody.y
                        topLeftRadius: parent.radius; topRightRadius: parent.radius
                        color: Theme.container
                        opacity: Math.min(1, content.headerCollapse*4)
                        visible: opacity > 0
                        // A rounded panel clips its children to its bounds, not
                        // to its corners, so a shadow left to reach around the
                        // bar is drawn outside the panel and rings its top
                        // corners. The bar is flush with the top and both
                        // sides, so the only part of its shadow that can be
                        // seen is the part below it, and that is all it draws.
                        Item {
                            objectName: "appBarLift"
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.bottom
                            height: 16
                            clip: true
                            MElevation {
                                width: parent.width; height: appBar.height; y: -appBar.height
                                radius: 0; level: 2
                            }
                        }
                    }
                    ColumnLayout {
                        id: contentColumn
                        objectName: "contentColumn"
                        // Material moves the arriving view along an axis; a
                        // transform keeps that off the layout's own geometry.
                        property real shift: 0
                        transform: Translate { x: contentColumn.shift }
                        // One margin and one rhythm for every tab. The album and
                        // artist grids used to tighten both, which moved the
                        // title and the tabs each time one of them came up. A
                        // grid's first cover starts at its cell's edge, so at
                        // the shared margin it lines up with the title.
                        anchors.fill: parent; anchors.margins: window.paneMargin; spacing: app.page==="server"?8:16
                        ArtistHero {
                            objectName: "artistHero"
                            Layout.fillWidth: true
                            visible: window.artistPage
                            collapse: content.headerCollapse
                        }
                        RowLayout {
                            visible: !window.artistPage
                            Layout.fillWidth: true; spacing: 16
                            Artwork { id:collectionArtwork;objectName:"collectionArtwork";opacity:window.albumFlying?0:1;visible: !!app.cover; url: app.cover; Layout.preferredWidth: content.headerExtent; Layout.preferredHeight: content.headerExtent; radius: (app.page==="artist" || app.page==="local-artist") ? width/2 : app.albumInfo.summary?24:12; shape: (app.page==="artist" || app.page==="local-artist") ? "cookie9Sided" : ""; pixels: app.albumInfo.summary?384:180
                                AbstractButton {anchors.fill:parent;objectName:"inspectCollectionArtwork";Accessible.name:"View artwork";focusPolicy:Qt.StrongFocus;onClicked:artworkViewer.inspect(app.cover)
                                    background:Rectangle {color:"transparent";radius:Theme.shapeExtraLarge;border.width:parent.visualFocus?2:0;border.color:Theme.primary}
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 6
                                // The slot the search bar fills on its own page.
                                Item {
                                    id: pageSearchHost
                                    objectName: "pageSearchHost"
                                    visible: !window.sidebarNav && searchBox.onItsPage
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0; Layout.maximumWidth: 720
                                    Layout.preferredHeight: visible ? 56 : 0
                                }
                            SungText {heading: true; visible: !pageSearchHost.visible; text: window.serverDisconnected ? "Music server" : window.destination==="library"&&window.libraryTab==="playlists"&&!window.localPlaylist ? "Playlists" : app.title; objectName: "collectionHeaderTitle"; emphasized: true; scaled: true; font.pixelSize: app.page==="home"?Theme.displaySmall:Theme.headlineMedium-(Theme.headlineMedium-Theme.titleLarge)*content.headerCollapse; Behavior on font.pixelSize { NumberAnimation { duration: app.motion?Theme.normal:0; easing.type: Easing.OutCubic } } Layout.fillWidth: true; wrapMode: Text.Wrap; maximumLineCount: 2 }
                                SungText { objectName: "albumArtist"; visible: !!app.albumInfo.artist;opacity:1-content.headerCollapse;Layout.maximumHeight:implicitHeight*(1-content.headerCollapse);clip:true; Layout.fillWidth: true; text: app.albumInfo.artist || ""; font.pixelSize: Theme.bodyLarge; color: Theme.muted; maximumLineCount: 2; wrapMode: Text.Wrap }
                                SungText { objectName: "albumSummary"; visible: !!app.albumInfo.summary;opacity:1-content.headerCollapse;Layout.maximumHeight:implicitHeight*(1-content.headerCollapse);clip:true; Layout.fillWidth: true; text: app.albumInfo.summary || ""; font.pixelSize: Theme.appBarSubtitle.medium; color: Theme.muted; wrapMode: Text.Wrap }
                            }
                            // The app bar's own actions. Material measures them
                            // against the room the title leaves and folds the
                            // rest into a menu rather than dropping any.
                            MAppBarRow {
                                objectName: "collectionActions"
                                // The actions take the width they need and the
                                // title gives way, which is the order Material
                                // puts them in. Folding is what happens when
                                // even that cannot be met.
                                Layout.preferredWidth: implicitWidth
                                Layout.preferredHeight: 48
                                Layout.alignment: Qt.AlignVCenter
                                actions: [
                                    {key:"editHome", name:"editHomeButton", symbol:"settings", label:"Customize Home", visible:app.page==="home",
                                     trigger:function(){homeEditor.open()}},
                                    {key:"pin", name:"pinCollectionButton", symbol:"pin", toggle:true, checked:(app.pins, app.isPinned(app.collectionItem)),
                                     label:app.isPinned(app.collectionItem)?"Unpin from Home":"Pin to Home",
                                     visible:!!app.collectionItem.id, trigger:function(){app.togglePin(app.collectionItem)}},
                                    {key:"refresh", symbol:"refresh", label:"Refresh", enabled:!app.busy,
                                     visible:app.page!=="library"&&app.page!=="local", trigger:function(){app.refresh()}},
                                    {key:"folders", name:"musicFoldersButton", symbol:"folder", text:"Folders", label:"Manage music folders",
                                     visible:window.destination==="library" && window.libraryTab==="files",
                                     trigger:function(){musicFoldersDialog.open()}},
                                    {key:"rescan", name:"rescanFoldersButton", symbol:"refresh", label:"Rescan music folders", enabled:!app.importingLocal,
                                     visible:window.destination==="library" && window.libraryTab==="files" && app.musicFolders.length>0,
                                     trigger:function(){app.rescanMusicFolders()}},
                                    {key:"cleanup", name:"playlistCleanupButton", text:"Clean up", label:"Clean up", visible:window.editableLocal,
                                     trigger:function(){window.openCleanup(window.localPlaylist)}},
                                    {key:"editRules", name:"editSmartPlaylistButton", text:"Edit rules", label:"Edit rules",
                                     visible:!!window.localPlaylist && !window.editableLocal,
                                     trigger:function(){smartDialog.edit(window.localPlaylist)}}
                                ]
                            }
                        }
                        Flow {
                            objectName: "searchFilters"
                            visible: app.page==="search"; Layout.fillWidth: true; spacing: 8
                            Repeater {
                                model: [{label:"Songs",key:"songs"},{label:"Albums",key:"albums"},{label:"Artists",key:"artists"},{label:"Playlists",key:"playlists"},{label:"Videos",key:"videos"}]
                                MChip { required property var modelData; objectName: "filter_"+modelData.key; text: modelData.label; selected: window.filter===modelData.key; onClicked: {window.filter=modelData.key;if(searchField.text.trim())app.search(searchField.text,window.filter);} }
                            }
                        }
                        RowLayout {
                            visible: app.importingLocal; Layout.fillWidth: true
                            MLoadingIndicator { Layout.preferredWidth: 24; Layout.preferredHeight: 24; running: app.importingLocal && app.localImportProgress < 0; label: "Importing music" }
                            SungText { text: app.localImportStatus; color: Theme.muted }
                            MWavyProgress {
                                objectName: "importProgress"
                                Layout.fillWidth: true; Layout.maximumWidth: 320
                                progress: app.localImportProgress
                                label: "Importing music"
                            }
                            Item { Layout.fillWidth: true }
                            MButton { text: "Cancel import"; onClicked: app.cancelLocalImport() }
                        }
                        LibraryTabs {
                            objectName: "libraryTabs"
                            visible: window.destination==="library"; Layout.fillWidth: true
                            currentKey: window.libraryTab
                            dotKey: app.importingLocal ? "files" : ""
                            onChosen: key => window.chooseLibrary(key)
                        }
                        RowLayout {
                            visible: window.destination==="library" && ["files","local-albums","local-artists"].indexOf(window.libraryTab)>=0
                            Layout.fillWidth: true; spacing: 8
                            LibraryTabs {
                                objectName: "localFacetTabs"
                                secondary: true
                                Layout.preferredWidth: 320
                                entries: [{label:"Songs", key:"files", name:"localView_files"},
                                          {label:"Albums", key:"local-albums", name:"localView_local-albums"},
                                          {label:"Artists", key:"local-artists", name:"localView_local-artists"}]
                                currentKey: window.libraryTab
                                onChosen: key => window.chooseLibrary(key)
                            }
                            MSearchField {
                                objectName: "localGroupSearch"; Layout.fillWidth:true
                            visible:app.page==="library" && (app.libraryId==="local-albums" || app.libraryId==="local-artists")
                            placeholderText:app.libraryId==="local-albums"?"Find albums":"Find artists"
                            text:app.collection.query; onTextEdited:app.collection.query=text
                            implicitHeight:48
                            Accessible.name:placeholderText
                        }
                        }
                        ServerToolbar { id: serverToolbar; Layout.fillWidth: true; visible: app.page==="server" && !window.serverDisconnected; onConnectRequested: serverConnection.open() }
                        Flow {
                            visible: app.page==="home" && window.destination!=="library"; Layout.fillWidth: true; spacing: 8
                            Repeater {
                                model: [{title:"Feel good",q:"feel good songs"},{title:"Focus",q:"instrumental focus"},{title:"Unwind",q:"chill evening"},{title:"Energize",q:"workout energy"}]
                                MChip { required property var modelData; text: modelData.title; selectable: false; onClicked: {searchField.text=modelData.q;window.destination="search";app.search(modelData.q,"songs");} }
                            }
                        }
                        RowLayout {
                            // An artist's hero owns these actions while it is
                            // open; the row takes them back as it collapses, so
                            // exactly one Play is ever on screen.
                            visible: tracks.selection.count===0 && app.results.count>0 && !window.feedShowing && !(window.destination==="library" && window.libraryTab==="playlists" && !window.localPlaylist) && !!(app.results.get(0).videoId || app.results.get(0).localPath || app.results.get(0).serverSong) && (!window.artistPage || content.compactHeader)
                            Layout.fillWidth: true; spacing: 12
                            MSplitButton {
                                objectName: "collectionPlay"
                                text: "Play"; symbol: "play"; filled: true
                                enabled: app.collection.count>0
                                onClicked: app.playCollection(0)
                                menu: MMenu {
                                    MMenuItem { objectName: "collectionShuffle"; symbol: "shuffle"; text: "Shuffle"; enabled: app.collection.count>1; onTriggered: {app.shuffle=true;app.playCollection(Math.floor(Math.random()*app.collection.count));} }
                                    MMenuItem { objectName: "collectionQueue"; symbol: "queue"; text: "Add to queue"; enabled: app.collection.count>0; onTriggered: {const before=app.queue.count;app.enqueueCollection();if(app.queue.count>before)window.confirmQueued();} }
                                    MMenuItem { objectName: "collectionPlayNext"; symbol: "next"; text: "Play next"; enabled: app.collection.count>0; onTriggered: app.enqueueItems(app.collection.rows(),true) }
                                }
                            }
                            // Material's input chip: a filter the reader typed
                            // stands on its own and carries the means to take
                            // it back out, rather than living only inside a row
                            // that can be folded away with it still applied.
                            MChip {
                                objectName: "collectionFilterChip"
                                variant: "input"
                                selectable: false
                                symbol: "filter"
                                visible: !!app.collection.query && !window.collectionTools
                                text: app.collection.query
                                Accessible.name: "Filter: " + app.collection.query
                                onClicked: {window.collectionTools=true;Qt.callLater(()=>collectionSearch.forceActiveFocus());}
                                onRemoved: app.collection.query=""
                            }
                            Item { Layout.fillWidth: true }
                            MButton { objectName: "collectionToolsButton"; symbol: "filter"; tip: "Find and sort songs"; selected: window.collectionTools || !!app.collection.query || app.collection.sortKey!=="original"; onClicked: {window.collectionTools=!window.collectionTools;if(window.collectionTools)Qt.callLater(()=>collectionSearch.forceActiveFocus());} }
                            SungText { visible: !app.albumInfo.summary || content.compactHeader || !!app.collection.query; text: app.collection.query ? app.collection.count+" / "+app.results.count : window.countText(app.results.count); color: Theme.muted; font.pixelSize: Theme.bodySmall }
                        }
                        RowLayout {
                            visible: window.hasSongCollection && window.collectionTools
                            Layout.fillWidth: true; spacing: 8
                            TextField {
                                id: collectionSearch; objectName: "collectionSearch"; Layout.fillWidth: true; implicitHeight: 44
                                font.family: Theme.fontFamily; font.pixelSize: Theme.bodyMedium; color: Theme.text
                                placeholderText: "Find in this list"; placeholderTextColor: Theme.muted
                                selectionColor: Theme.primaryContainer; selectedTextColor: Theme.text
                                leftPadding: 14; rightPadding: 14; selectByMouse: true
                                text: app.collection.query
                                onTextEdited: app.collection.query=text
                                onAccepted: {if(app.collection.count>0)app.playCollection(0);content.forceActiveFocus();}
                                background: Rectangle { radius: Theme.shapeLarge; color: Theme.container; border.width: collectionSearch.activeFocus?2:0; border.color: Theme.primary }
                                Accessible.name: "Find songs in this list"
                            }
                            MButton { symbol: "close"; tip: "Clear list filter"; visible: !!app.collection.query; onClicked: app.collection.query="" }
                            // What a list is sorted by is a value, not an
                            // action, so Material draws it as a field that
                            // opens its options rather than as a button.
                            MExposedDropdown {
                                id: collectionSort
                                objectName: "collectionSortControl"
                                fieldName: "collectionSortButton"
                                label: "Sort"
                                implicitHeight: 48
                                options: [{key:"original",label:"Original order",name:"sort_original"},
                                          {key:"title",label:"Title",name:"sort_title"},
                                          {key:"artist",label:"Artist",name:"sort_artist"},
                                          {key:"duration",label:"Duration",name:"sort_duration"}]
                                    .concat(app.page==="library" && app.libraryId==="files"
                                            ? [{key:"folder",label:"Folder",name:"sort_folder"}] : [])
                                value: app.collection.sortKey
                                onChosen: key => app.collection.sortKey = key
                            }
                        }
                        // A docked toolbar is docked: it runs the width of the
                        // surface it belongs to rather than sitting inside its
                        // margins with square corners.
                        SelectionBar {
                            Layout.fillWidth: true
                            Layout.leftMargin: -contentColumn.anchors.margins
                            Layout.rightMargin: -contentColumn.anchors.margins
                            view: tracks
                            canRemove: window.editableLocal || app.serverPlaylistEditable
                        }
                        Item {
                            id: contentBody
                            objectName: "contentBody"
                            // Material names pull to refresh as the interaction
                            // its loading indicator is for. The gesture acts on
                            // whichever list is on screen.
                            MPullToRefresh {
                                objectName: "contentRefresh"
                                target: shelves.visible ? shelves : tracks.visible ? tracks : localGroups.visible ? localGroups : null
                                enabled: !!target && (app.page!=="library" || window.libraryTab!=="playlists")
                                busy: app.busy || app.importingLocal
                                onTriggered: {
                                    if(window.destination==="library" && window.libraryTab.startsWith("local")) app.rescanMusicFolders()
                                    else if(window.destination==="library" && window.libraryTab==="files") app.rescanMusicFolders()
                                    else app.refresh()
                                }
                            }
                            Item {
                                id: contentFabHost
                                objectName: "contentFabHost"
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.margins: 8
                                // The button asks to sit above the content, but
                                // its own z counts among this slot's children,
                                // not the slot's siblings. Without this the
                                // lists declared below it take the presses,
                                // and the button is visible but dead.
                                z: libraryFab.z
                                width: window.railShowing ? 0 : libraryFab.width
                                height: window.railShowing ? 0 : libraryFab.height
                            }
                            MFabMenu {
                                id: libraryFab
                                objectName: "libraryFab"
                                // Material puts the surface's primary action at
                                // the head of the rail where there is a rail,
                                // and on the surface it acts on where there is
                                // not. Either way a slot reserves its room.
                                parent: window.railShowing ? navigationRail.fabSlot : contentFabHost
                                extended: window.railShowing && navigationRail.expanded
                                downward: window.railShowing
                                leadingEdge: window.railShowing
                                anchors.left: parent.left
                                anchors.top: parent.top
                                visible: window.libraryAddApplies
                                onVisibleChanged: if(!visible)close()
                                label: "Add to your library"
                                actions: window.libraryTab==="playlists"
                                    ? [{label:"New playlist",symbol:"plus",action:function(){window.playlistAction="create";playlistName.clear();playlistDialog.open();}},
                                       {label:"Smart playlist",symbol:"filter",action:function(){smartDialog.edit("")}},
                                       {label:"Import M3U",symbol:"folder",action:function(){window.openFileDialog("m3u-import")}}]
                                    : [{label:"Add files",symbol:"plus",action:function(){window.openFileDialog("audio")}},
                                       {label:"Add folder",symbol:"folder",action:function(){musicFoldersDialog.open()}}]
                            }
                            property real shift: 0
                            transform: Translate { x: contentBody.shift }
                            Layout.fillWidth: true; Layout.fillHeight: true
                            Column {
                                objectName: "serverEmptyState"; anchors.centerIn: parent; spacing: 16
                                visible: window.serverDisconnected
                                SungText { anchors.horizontalCenter: parent.horizontalCenter; text: "Connect your music library"; font.pixelSize: Theme.titleLarge }
                                MButton { objectName: "serverEmptyConnect"; anchors.horizontalCenter: parent.horizontalCenter; text: "Connect server"; filled: true; onClicked: serverConnection.open() }
                            }
                            Column {anchors.centerIn:parent;spacing:12;visible:app.page==="home"&&!app.busy&&window.homeSections.length===0&&app.homeSections(true).length>0
                                SungText {text:"Home sections are hidden";color:Theme.muted;anchors.horizontalCenter:parent.horizontalCenter}
                                MButton {objectName:"restoreHomeSections";text:"Restore sections";tonal:true;anchors.horizontalCenter:parent.horizontalCenter;onClicked:app.resetHomeLayout()}
                            }
                            CatalogSkeleton { anchors.fill: parent; loading: app.busy && app.results.count===0 && window.homeSections.length===0; cards: app.page==="home" || app.page==="artist" }
                            ListView {
                                id: shelves; objectName: "homeShelves"; anchors.fill: parent
                                visible: window.homeSections.length>0 && !(window.destination==="library"&&window.libraryTab==="playlists")
                                clip: true; spacing: window.paneGutter+2; reuseItems: true; cacheBuffer: 0
                                model: window.homeSections; boundsBehavior: Flickable.StopAtBounds
                                ScrollBar.vertical: ScrollBar {}
                                MSmoothWheel { flick: shelves }
                                delegate: ColumnLayout {
                                    required property var modelData
                                    width: shelves.width; height: implicitHeight; spacing: 12
                                    RowLayout {
                                        Layout.fillWidth: true
                                        SungText { heading: true; text: modelData.title; font.pixelSize: Theme.titleLarge; font.weight: Font.Medium; Layout.fillWidth: true }
                                        MButton { symbol: "back"; tip: "Previous covers"; enabled: !shelf.atXBeginning; implicitWidth: 40; implicitHeight: 40; onClicked: shelf.flick(1300,0) }
                                        MButton { symbol: "chevron"; tip: "More covers"; enabled: !shelf.atXEnd; implicitWidth: 40; implicitHeight: 40; onClicked: shelf.flick(-1300,0) }
                                    }
                                    MCarousel {
                                        id: shelf
                                        Layout.fillWidth: true; Layout.preferredHeight: cellWidth+68
                                        Behavior on cellWidth {enabled:app.motion && visible;NumberAnimation {duration:Theme.springSpatialMs;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.springSpatial}}
                                        cellWidth: Math.max(app.viewCompactDensity?112:142,Math.min(app.viewCompactDensity?148:window.feedCardWidth,(width-40)/(app.viewCompactDensity?4.35:3.35)))
                                        model: modelData.items
                                        openHandler: window.openCollection
                                    }
                                }
                            }
                            GridView {
                                id: localGroups; objectName: "localGroups"; anchors.fill: parent; clip: true; reuseItems: true; cacheBuffer: 0
                                bottomMargin: libraryFab.visible ? libraryFab.height+24 : 0
                                visible: app.page==="library" && app.viewMode==="grid" && (app.libraryId==="local-albums" || app.libraryId==="local-artists")
                                model: visible ? app.collection : null
                                cellWidth: width/Math.max(2,Math.floor(width/Theme.gridCell));
                                Behavior on cellWidth {enabled:app.motion && localGroups.visible;NumberAnimation {duration:260;easing.type:Easing.InOutCubic}}
                                cellHeight: coverExtent+64
                                readonly property real coverExtent:Math.min(cellWidth-16,Math.max(96,height-64))
                                ScrollBar.vertical: ScrollBar {}
                                MSmoothWheel { flick: localGroups }
                                delegate: ArtCard {required property var entry; width: localGroups.coverExtent; track: entry;openHandler:window.openCollection}
                                SungText {anchors.centerIn: parent; visible: localGroups.count===0; text:app.collection.query?"No matches":"Import music to browse here"; color:Theme.muted}
                            }
                            TrackList {
                                id: tracks; groupDiscs: !!app.albumInfo.multipleDiscs && app.collection.sortKey==="original"; objectName: "tracksView"; anchors.fill: parent; clip: true
                                bottomMargin: libraryFab.visible ? libraryFab.height+24 : 0
                                visible: !localGroups.visible && !window.feedShowing && !(window.destination==="library"&&window.libraryTab==="playlists"&&!window.localPlaylist)
                                groupFolders: app.page==="library" && app.libraryId==="files" && app.collection.sortKey==="folder"
                                model: app.collection; reuseItems: true; cacheBuffer: 100; boundsBehavior: Flickable.StopAtBounds
                                queueMode: false; reorderEnabled: (window.editableLocal || app.serverPlaylistEditable) && app.collection.sortKey==="original" && !app.collection.query
                                playlistId: window.localPlaylist; dragHub: trackDrag
                                matchQuery: app.collection.query || (app.page==="search"?app.query:app.page==="server"?(app.serverRequest.query || ""):"")
                                onActivate: (row,item)=>{if(item.videoId || item.localPath || item.serverSong)app.playCollection(row);else app.open(item);}
                                onMenuRequested: (item,index,anchor)=>window.trackMenu(item,index,anchor,false)
                                onRemoveSelected: {if(app.serverPlaylistEditable)app.removeServerRows(sourceRows());else if(window.localPlaylist)app.removePlaylistRows(window.localPlaylist,sourceRows());}
                                onAddSelected: window.addBatch(tracks)
                                footer: Item {
                                    width: tracks.width; height: app.canMore && tracks.count>0 ? 64 : 0
                                    MButton { objectName:"loadMoreButton"; anchors.centerIn: parent; text: app.busy ? "Loading…" : "Load more"; busy: app.busy; enabled: !app.busy; tonal: true; visible: app.canMore && tracks.count>0; onClicked: app.more() }
                                }
                                Column {
                                    objectName: "collectionEmptyState"; anchors.centerIn: parent; width: Math.min(parent.width,320); spacing: 16
                                    visible: app.collection.count===0 && !app.busy && !window.serverDisconnected
                                    Icon { anchors.horizontalCenter: parent.horizontalCenter; name: app.error?"refresh":app.collection.query || app.page==="search"?"search":"library"; size: 36; ink: Theme.muted }
                                    SungText { width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; text: app.collection.query ? "No matching songs" : app.error ? "Couldn’t load music" : app.page==="library" ? (window.libraryTab==="files"?"No local music yet":window.libraryTab==="history"?"Nothing played yet":window.libraryTab.startsWith("mix-")?"No matching songs yet":"No liked songs yet") : app.page==="local" ? "No songs yet" : app.page==="search" && !app.query ? "Search music" : "No results"; color: Theme.muted; font.pixelSize: Theme.bodyLarge }
                                    MButton {
                                        objectName: "emptyStateAction"; anchors.horizontalCenter: parent.horizontalCenter; tonal: true
                                        text: app.collection.query?"Clear filters":app.error && app.canRetry?"Retry":window.libraryTab==="files" && app.page==="library"?"Add music":"Search music"
                                        onClicked: {if(app.collection.query)app.collection.query="";else if(app.error && app.canRetry)app.retry();else if(window.libraryTab==="files" && app.page==="library")window.openFileDialog("audio");else window.focusSearch();}
                                    }
                                }
                            }
                            GridView {
                                id:playlistGrid;objectName:"playlistGrid";anchors.fill:parent;clip:true;reuseItems:true;cacheBuffer:0
                                bottomMargin: libraryFab.visible ? libraryFab.height+24 : 0
                                visible:window.destination==="library"&&window.libraryTab==="playlists"&&!window.localPlaylist&&app.viewMode==="grid"
                                model:visible?app.playlists:[];cellWidth:width/Math.max(2,Math.floor(width/Theme.gridCell));
                                Behavior on cellWidth {enabled:app.motion && playlistGrid.visible;NumberAnimation {duration:260;easing.type:Easing.InOutCubic}}
                                cellHeight:cellWidth+56
                                ScrollBar.vertical:MScrollBar {}
                                MSmoothWheel { flick: playlistGrid }
                                delegate:ArtCard {required property var modelData;width:playlistGrid.cellWidth-16
                                    track:Object.assign({},modelData,{kind:"local",art:modelData.customCover||"",artworks:modelData.customCover?[]:modelData.artworks})
                                    openHandler:window.openCollection
                                    MButton {anchors.left:parent.left;anchors.top:parent.top;anchors.margins:8;symbol:"more";tonal:true;tip:"Playlist actions";onClicked:{window.editPlaylistId=modelData.id;playlistName.text=modelData.title;playlistActions.popup(this,width-playlistActions.width,height+4);}}
                                }
                                SungText {anchors.centerIn:parent;visible:playlistGrid.count===0;text:"Create your first playlist";color:Theme.muted}
                            }
                            ListView {
                                id: localPlaylists; anchors.fill: parent; clip: true; spacing: 8
                                visible: window.destination==="library"&&window.libraryTab==="playlists"&&!window.localPlaylist&&app.viewMode!=="grid"
                                model: app.playlists
                                MSmoothWheel { flick: localPlaylists }
                                delegate: Rectangle {
                                    required property var modelData; width: localPlaylists.width; height:app.viewCompactDensity?64:76; radius: Theme.shapeLarge; color: Theme.container
                                    RowLayout {
                                        anchors.fill: parent; anchors.margins: 12; spacing: 12
                                        AbstractButton { Layout.preferredWidth: 48; Layout.preferredHeight: 48; focusPolicy: Qt.StrongFocus; Accessible.name: "Open "+modelData.title; contentItem: PlaylistCover { artworks: modelData.artworks || [] } background: Rectangle { color: "transparent"; radius: Theme.shapeMedium; border.width: parent.activeFocus?2:0; border.color: Theme.focusRing } onClicked: {window.localPlaylist=modelData.id;app.openPlaylist(modelData.id);} }
                                        AbstractButton { Layout.fillWidth: true; Layout.fillHeight: true; focusPolicy: Qt.StrongFocus; Accessible.name: modelData.title; background: Rectangle { color: "transparent"; radius: Theme.shapeSmall; border.width: parent.activeFocus?2:0; border.color: Theme.focusRing } onClicked: {window.localPlaylist=modelData.id;app.openPlaylist(modelData.id);} contentItem: Column { spacing: 4; SungText { text: modelData.title; font.pixelSize: Theme.bodyLarge; width: parent.width } SungText { text: modelData.smart?"Smart playlist":window.countText(modelData.count); color: Theme.muted; font.pixelSize: Theme.bodySmall } } }
                                        MButton { symbol: "more"; tip: "Playlist actions"; onClicked: {window.editPlaylistId=modelData.id;playlistName.text=modelData.title;playlistActions.popup(this,width-playlistActions.width,height+4);} }
                                    }
                                }
                                Column {
                                    anchors.centerIn: parent; spacing: 16; visible: app.playlists.length===0
                                    Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "library"; size: 36; ink: Theme.muted }
                                    SungText { anchors.horizontalCenter: parent.horizontalCenter; text: "No playlists yet"; color: Theme.muted; font.pixelSize: Theme.bodyLarge }
                                    MButton { anchors.horizontalCenter: parent.horizontalCenter; text: "New playlist"; symbol: "plus"; filled: true; onClicked: {window.playlistAction="create";playlistName.clear();playlistDialog.open();} }
                                }
                            }
                        }
                    }
                    Connections { target: app
                        property string previousMode:""
                        function onPresentationChanged(){if(previousMode!==app.viewMode){previousMode=app.viewMode;if(window.uiActive && !app.busy)entrance.restart();}}
                        function onCatalogChanged(){previousMode=app.viewMode;if(!app.busy && !window.albumFlying)entrance.restart();}
                    }
                    NumberAnimation { id: entrance; target: content; property: "opacity"; from: app.motion?0.5:1; to: 1; duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve }
                }
                Item {
                    id: panelGrip; objectName: "panelResizeHandle"
                    visible: !!window.side && !window.sheetMode
                    Layout.preferredWidth: 24; Layout.fillHeight: true
                    activeFocusOnTab: true; Accessible.role: Accessible.Grip; Accessible.name: "Resize side panel"
                    Keys.onLeftPressed: geometry.panelWidth=Math.min(window.width-560,sidePanel.chosenWidth+24)
                    Keys.onRightPressed: geometry.panelWidth=Math.max(320,sidePanel.chosenWidth-24)
                    MDragHandle {
                        objectName: "panelDragHandle"
                        anchors.centerIn: parent
                        pressed: panelGrip.activeFocus
                        dragging: gripMouse.pressed || gripMouse.containsMouse
                    }
                    MouseArea {
                        id: gripMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.SplitHCursor
                        property real startX; property real startWidth
                        onPressed: mouse => {startX=mapToItem(window.contentItem,mouse.x,mouse.y).x;startWidth=sidePanel.width;}
                        onPositionChanged: mouse => {if(pressed)geometry.panelWidth=Math.max(320,Math.min(window.width-560,startWidth+startX-mapToItem(window.contentItem,mouse.x,mouse.y).x));}
                        // Back to the canonical third.
                        onDoubleClicked: geometry.panelWidth=0
                    }
                }
                Rectangle {
                    id: sidePanel; objectName: "sidePanel"
                    Accessible.role: Accessible.Pane
                    Accessible.name: "Supporting pane"
                    Layout.fillWidth: false
                    readonly property real chosenWidth: geometry.panelWidth>0 ? geometry.panelWidth : contentRow.supportingWidth
                    property real revealWidth: window.side && !window.sheetMode ? Math.max(320,Math.min(chosenWidth,window.width-560)) : 0
                    Layout.preferredWidth: Math.max(0,revealWidth)
                    Layout.fillHeight: true; radius: Theme.shapeExtraLarge; color: window.washed(Theme.surfaceLow)
                    visible: Layout.preferredWidth>1; clip: true
                    Rectangle {
                        objectName: "searchScrimPanel"
                        anchors.fill: parent; radius: parent.radius; z: 40
                        color: Theme.scrimColor()
                        visible: opacity>0
                        opacity: window.searchViewOpen ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
                    }
                    Behavior on revealWidth { enabled: !gripMouse.pressed; NumberAnimation { duration: app.motion?350:0; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
                    AmbientBackdrop {
                        objectName: "nowBackdrop"; anchors.fill: parent
                        url: window.side==="now" ? (app.current.art || "") : ""
                        scrim: Theme.surfaceLow; dim: 0.86; corner: parent.radius
                    }
                    // The panel's contents live once and move between here and
                    // the bottom sheet, so a narrow window changes where the
                    // pane is, not what it is.
                    Item { id: sidePanelHost; anchors.fill: parent; anchors.margins: Theme.sideSheetPadding }
                }
            }
            Rectangle {
                id: playbackBar
                objectName: "playbackBar"
                Accessible.role: Accessible.Pane
                Accessible.name: "Playback"
                Layout.fillWidth: true; Layout.preferredHeight: 112; color: window.washed(Theme.container); radius: Theme.shapeExtraLarge
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 16
                    AbstractButton { id: nowButton; objectName: "nowButton"; Layout.preferredWidth: 64; Layout.preferredHeight: 64; enabled: app.currentIndex>=0; focusPolicy: Qt.StrongFocus; Accessible.name: "Now playing"; onClicked: window.activateSide("now")
                        contentItem: Artwork { id: nowArtwork; objectName: "nowArtwork"; url: app.current.art || ""; motionUrl: app.currentMotionArt; crossfade:true; radius: Theme.shapeMedium; pixels: 150; fit:app.currentArtworkFit; opacity: window.coverFlying?0:1 }
                        background: Rectangle { anchors.fill: parent; anchors.margins: -3; color: "transparent"; radius: Theme.shapeLarge; border.width: parent.activeFocus?2:0; border.color: Theme.focusRing }
                    }
                    ColumnLayout {
                        Layout.preferredWidth: Math.max(100,Math.min(220,window.width*0.17)); spacing: 6; opacity: nowPresentation.fade*window.coverDetailsOpacity; transform: Translate { x: nowPresentation.offset }
                        AbstractButton { Layout.fillWidth: true; implicitHeight: 24; focusPolicy: Qt.StrongFocus; enabled: app.currentIndex>=0; Accessible.name: "Now playing: " + (app.current.title || "Nothing playing"); onClicked: window.activateSide("now"); contentItem: MatchText { revealFocused: parent.activeFocus; sourceText: nowPresentation.shown.title || "Nothing playing"; font.pixelSize: Theme.titleMedium; font.weight: Font.DemiBold } background: Rectangle { color: "transparent"; radius: Theme.shapeExtraSmall; border.width: parent.activeFocus?1:0; border.color: Theme.focusRing } }
                        AbstractButton { Layout.fillWidth: true; implicitHeight: 24; focusPolicy: Qt.StrongFocus; enabled: !!app.current.artistId; Accessible.name: "Go to " + (app.current.artist || "artist"); onClicked: app.open(window.relatedItem(app.current,"artist")); contentItem: MatchText { revealFocused: parent.activeFocus; sourceText: nowPresentation.shown.artist || ""; color: Theme.muted; font.pixelSize: Theme.bodyMedium } background: Rectangle { color: "transparent"; radius: Theme.shapeExtraSmall; border.width: parent.activeFocus?1:0; border.color: Theme.focusRing } }
                    }
                    MButton { symbol: "heart"; tip: app.liked?"Unlike":"Like"; toggle: true; selected: app.liked; enabled: app.currentIndex>=0; visible: window.width>=1050; onClicked: app.toggleLike(app.current) }
                    ColumnLayout {
                        // The transport and the seek bar share this column. It
                        // was held to 520 and a spacer beside it took the rest,
                        // so on a wide window the bar stayed a few hundred
                        // pixels long while the bar it sits in was over a
                        // thousand. A minute of music was a handful of pixels.
                        Layout.fillWidth: true; Layout.maximumWidth: 960; spacing: 0
                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter; spacing: 6
                            MButton { objectName: "playerShuffle"; symbol: "shuffle"; tip: "Shuffle"; toggle: true; selected: app.shuffle; onClicked: app.shuffle=!app.shuffle; visible: window.width>=980 }
                            MButton { symbol: "previous"; tip: "Previous · Ctrl+←"; enabled: app.queue.count>0; onClicked: app.previous() }
                            MButton { objectName: "playButton"; morphPlayback:true; symbol: app.playing||app.resolving?"pause":"play"; tip: app.playing||app.resolving?"Pause · Space":"Play · Space"; filled: true; size: "medium"; implicitWidth: 72; enabled: app.queue.count>0; onClicked: app.toggle(); busy: app.buffering }
                            MButton { symbol: "next"; tip: "Next · Ctrl+→"; enabled: app.queue.count>0; onClicked: app.next() }
                            MButton { symbol: app.repeat===2?"repeat_one":"repeat"; tip: app.repeat===0?"Repeat off":app.repeat===1?"Repeat queue":"Repeat song"; toggle: true; selected: app.repeat>0; onClicked: app.repeat=(app.repeat+1)%3; visible: window.width>=980 }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 12
                            SungText { font.features: {"tnum": 1}; text: app.formatTime(app.position); color: Theme.muted; font.pixelSize: Theme.labelSmall; labelRole: true; Layout.preferredWidth: 34 }
                            SeekBar { Layout.fillWidth: true; objectName: "seekBar" }
                            SungText { font.features: {"tnum": 1}; text: app.formatTime(app.duration); color: Theme.muted; font.pixelSize: Theme.labelSmall; labelRole: true; Layout.preferredWidth: 34; horizontalAlignment: Text.AlignRight }
                        }
                    }

                    MButton { symbol: "lyrics"; tip: "Lyrics · Ctrl+Y"; toggle: true; selected: window.side==="lyrics"; enabled: app.currentIndex>=0; onClicked: window.activateSide("lyrics") }
                    MButton {
                        objectName: "queueButton"; symbol: "queue"; tip: "Queue · Ctrl+L"
                        toggle: true; selected: window.side==="queue"; onClicked: window.activateSide("queue")
                        MBadge {
                            objectName: "queueBadge"
                            // Redundant once the queue itself is on screen.
                            present: window.side!=="queue"
                            count: app.queue.count-app.currentIndex-1
                            subject: "songs waiting"
                            x: parent.width/2+12-inset; y: (parent.height-24)/2-height+lift
                        }
                    }
                    // Material moves an action that will not fit into an
                    // overflow menu rather than dropping it, so the controls the
                    // bar has no room for are still one click away.
                    MAppBarRow {
                        objectName: "playerOverflow"
                        // The bar keeps room for two of these before it folds
                        // the rest into the menu.
                        Layout.preferredWidth: Math.min(implicitWidth, itemWidth*2 + spacing)
                        Layout.preferredHeight: 48
                        visible: live.length > 0
                        actions: [
                            {key:"like", symbol:"heart", label:app.liked?"Unlike":"Like", toggle:true, checked:app.liked,
                             enabled:app.currentIndex>=0, visible:window.width<1050, trigger:function(){app.toggleLike(app.current)}},
                            {key:"shuffle", symbol:"shuffle", label:"Shuffle", toggle:true, checked:app.shuffle,
                             visible:window.width<980, trigger:function(){app.shuffle=!app.shuffle}},
                            {key:"repeat", symbol:app.repeat===2?"repeat_one":"repeat",
                             label:app.repeat===0?"Repeat off":app.repeat===1?"Repeat queue":"Repeat song",
                             toggle:true, checked:app.repeat>0, visible:window.width<980,
                             trigger:function(){app.repeat=(app.repeat+1)%3}}
                        ]
                    }
                    MButton {id:outputButton;objectName:"playerOutputButton";symbol:"chevron";iconWidth:"narrow";tip:"Audio output · "+app.audioDeviceName;selected:outputPicker.visible;onClicked:outputPicker.showAt(outputButton)}
                    VolumeControl {id:volumeControl;showSlider:window.width>=1160}
                }
            }
            // Material puts the bar against the bottom edge, which is where
            // the sidebar arrangement keeps it on a window too narrow for the
            // rail. The top bar arrangement has no use for this slot.
            Item {
                id: bottomNavHost
                objectName: "bottomNavHost"
                visible: window.bottomBarShowing
                Layout.fillWidth: true
                Layout.leftMargin: -parent.Layout.leftMargin
                Layout.topMargin: 4
                Layout.preferredHeight: visible ? 64 : 0
            }
        }
        }
    }
    // Material's search bar. The top bar arrangement puts it on the page it
    // belongs to: Search is one of the three destinations, so a bar that is
    // always in the chrome is a second door into the same room and says
    // nothing on the two pages that are not it. The sidebar arrangement keeps
    // it in the top bar on every page, which is what it did before. One bar
    // either way, moved between the two slots.
    Rectangle {
        id: searchBox; objectName: "searchBar"; z: 20
        readonly property bool onItsPage: app.page==="search" || (app.page==="server" && !window.serverDisconnected)
        parent: window.sidebarNav ? topBarSearchHost : pageSearchHost
        anchors.fill: parent
        visible: window.sidebarNav || onItsPage
        // Material's search bar sits on surfaceContainerHigh whether
        // or not it holds focus; the focus ring does the rest.
        color: Theme.high; radius: Theme.shapeExtraLarge
        border.width: searchField.activeFocus?2:0; border.color: Theme.focusRing
        Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        // Material raises the search bar three levels, so it holds
        // its own against whatever scrolls beneath it rather than
        // sitting in the page with it.
        MElevation { objectName: "searchBarShade"; anchors.fill: parent; radius: parent.radius; level: 3 }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 8; spacing: 12
            // The leading icon says what the bar is for, so it is
            // drawn in the surface ink; the trailing clear button
            // is an action on it and stays in the variant.
            Icon { name: "search"; ink: Theme.text }
            TextField {
                font.family: Theme.fontFamily; id: searchField; objectName: "searchField"; Layout.fillWidth: true; Layout.fillHeight: true
                placeholderText: app.page==="server"?"Search server":"Search music"; placeholderTextColor: Theme.muted
                color: Theme.text; selectionColor: Theme.primaryContainer; selectedTextColor: Theme.text
                font.pixelSize: Theme.bodyLarge; background: null; selectByMouse: true
                Accessible.name: app.page==="server"?"Search music server":"Search songs, albums, artists, playlists, or paste a YouTube link"
                property var suggestions: []
                property int highlighted: -1
                property bool dismissed: false
                function updateSuggestions() {
                    highlighted=-1;
                    if(app.page==="server"){suggestions=[];return;}
                    // Material's search view groups what it offers under
                    // category labels rather than running it together.
                    suggestions=text.trim()
                        ? app.localMatches(text).map(m=>Object.assign({},m,{group:m.kind==="local"?"Playlists":"Songs"}))
                        : app.recentSearches.map(q=>({title:q,recent:true,group:"Recent"}));
                }
                function submit() {if(app.page==="server"){dismissed=true;app.browseServer("search",text,serverToolbar.searchFilter);content.forceActiveFocus();return;}dismissed=true;window.destination="search";window.localPlaylist="";app.search(text,window.filter);content.forceActiveFocus();}
                function choose(index) {
                    if(index<0 || index>=suggestions.length){submit();return;}
                    const item=suggestions[index];dismissed=true;
                    if(item.recent){text=item.title;submit();}
                    else {app.rememberSearch(text);content.forceActiveFocus();if(item.kind==="local")app.openPlaylist(item.id);else if(item.queueIndex!==undefined && app.queue.get(item.queueIndex).id===item.id)app.playAt(item.queueIndex);else app.playKeepingQueue(item);}
                }
                onTextEdited: {highlighted=-1;dismissed=false;suggestionDelay.restart();}
                onActiveFocusChanged: {if(activeFocus){dismissed=false;updateSuggestions();}else suggestionDelay.stop();}
                onAccepted: choose(highlighted)
                Keys.onPressed: event=> {
                    if((event.key===Qt.Key_Down || event.key===Qt.Key_Up) && suggestionDelay.running){suggestionDelay.stop();updateSuggestions();}
                    if(event.key===Qt.Key_Delete && (event.modifiers&Qt.ShiftModifier) && highlighted>=0 && suggestions[highlighted].recent){
                        const previous=highlighted;app.removeRecentSearch(suggestions[highlighted].title);highlighted=Math.min(previous,suggestions.length-1);event.accepted=true;return;
                    }
                    if(event.key===Qt.Key_Down && suggestions.length){dismissed=false;highlighted=Math.min(suggestions.length-1,highlighted+1);suggestionList.positionViewAtIndex(highlighted,ListView.Contain);event.accepted=true;}
                    else if(event.key===Qt.Key_Up && suggestions.length){highlighted=Math.max(-1,highlighted-1);if(highlighted>=0)suggestionList.positionViewAtIndex(highlighted,ListView.Contain);event.accepted=true;}
                    else if(event.key===Qt.Key_Escape){dismissed=true;highlighted=-1;event.accepted=true;}
                }
                Timer { id: suggestionDelay; interval: 90; onTriggered: searchField.updateSuggestions() }
                Connections { target: app; function onRecentSearchesChanged(){if(searchField.activeFocus)searchField.updateSuggestions();} function onLibraryChanged(){if(searchField.activeFocus)suggestionDelay.restart();} }
                Connections { target: app.queue; function onCountChanged(){if(searchField.activeFocus)suggestionDelay.restart();} }
            }
            MButton { symbol: "close"; tip: "Clear search"; visible: searchField.text.length>0; onClicked: {searchField.clear();searchField.forceActiveFocus();searchField.dismissed=false;searchField.updateSuggestions();} }
        }
        Popup {
            id: searchSuggestions; objectName: "searchSuggestions"; parent: searchBox
            // Material docks the search view under the bar when there
            // is room and gives it the whole screen when there is not.
            readonly property bool fullScreen: window.compactWindow
            y: searchBox.height+6; width: searchBox.width
            height: fullScreen ? Math.max(120,window.height-190)
                               : Math.min(window.height-220,suggestionList.contentHeight+16)
            visible: searchField.activeFocus && !searchField.dismissed && searchField.suggestions.length>0
            focus: false; padding: 8; closePolicy: Popup.CloseOnPressOutside
            enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: app.motion?Theme.fast:0; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
            exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: app.motion?Theme.fast:0; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
            onClosed: searchField.dismissed=true
            background: Rectangle {
                // Material's docked search view is the extra large
                // corner, not the step below it.
                radius: Theme.shapeExtraLarge; color: Theme.high; border.width: 1; border.color: Theme.outlineVariant
                MElevation { anchors.fill: parent; radius: parent.radius; level: 3 }
            }
            contentItem: ListView {
                id: suggestionList; objectName: "suggestionList"; clip: true; model: searchField.suggestions; currentIndex: searchField.highlighted
                ScrollBar.vertical: ScrollBar {}
                delegate: Item {
                    id: suggestionRow
                    readonly property bool highlighted: index===searchField.highlighted
                    required property var modelData; required property int index
                    // The first row of a category carries its label.
                    readonly property bool opensGroup: !!modelData.group && (index===0 || searchField.suggestions[index-1].group!==modelData.group)
                    width: suggestionList.width; height: 56+(opensGroup?28:0)
                    SungText {
                        objectName: "suggestionGroup_"+index
                        visible: suggestionRow.opensGroup
                        x: 12; width: parent.width-24; height: 28
                        verticalAlignment: Text.AlignVCenter
                        text: modelData.group || ""; font.pixelSize: Theme.labelMedium; color: Theme.muted
                    }
                    Item {
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 56
                        Rectangle { anchors.fill: parent; radius: Theme.shapeMedium; color: suggestionRow.highlighted?Theme.secondaryContainer:"transparent" }
                        Rectangle {
                            objectName: "suggestionStateLayer"; anchors.fill: parent; radius: Theme.shapeMedium
                            color: suggestionRow.highlighted?Theme.secondaryContainerText:Theme.text
                            opacity: suggestionButton.down?Theme.pressedOpacity:suggestionButton.hovered?Theme.hoverOpacity:0
                            Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
                        }
                        AbstractButton {
                            id: suggestionButton; objectName: "suggestion_"+index; hoverEnabled: true
                            anchors.fill: parent; anchors.rightMargin: modelData.recent?40:0; focusPolicy: Qt.NoFocus
                            leftPadding: 64; rightPadding: 12
                            Accessible.name: modelData.title+(modelData.artist?", "+modelData.artist:"")+(modelData.origin?", "+modelData.origin:"")
                            Accessible.selected: suggestionRow.highlighted
                            onClicked: searchField.choose(index)
                            contentItem: Item {
                                Column {
                                    objectName: "suggestionLabels"
                                    width: parent.width; anchors.verticalCenter: parent.verticalCenter; spacing: 2
                                    SungText { width: parent.width; text: modelData.title; color: suggestionRow.highlighted?Theme.secondaryContainerText:Theme.text; font.pixelSize: Theme.bodyMedium }
                                    SungText { width: parent.width; visible: !!modelData.origin; text: (modelData.artist?modelData.artist+" · ":"")+(modelData.origin||""); font.pixelSize: Theme.bodySmall; color: suggestionRow.highlighted?Theme.secondaryContainerText:Theme.muted }
                                }
                            }
                        }
                        // Material gives every row in the view a leading
                        // element: the cover where there is one, and the
                        // icon for what the row is where there is not.
                        Item {
                            objectName: "suggestionLeading_"+index
                            x: 12; anchors.verticalCenter: parent.verticalCenter; width: 40; height: 40
                            Artwork { anchors.fill: parent; visible: !!modelData.art; radius: Theme.shapeSmall; pixels: 120; url: modelData.art || "" }
                            Icon {
                                anchors.centerIn: parent; visible: !modelData.art
                                name: modelData.recent ? "history" : modelData.kind==="local" ? "library" : "disc"
                                ink: suggestionRow.highlighted?Theme.secondaryContainerText:Theme.muted
                            }
                        }
                        MButton { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; implicitWidth: 36; implicitHeight: 36; symbol: "close"; tip: "Remove recent search · Shift+Delete"; focusPolicy: Qt.NoFocus; visible: modelData.recent===true; onClicked: app.removeRecentSearch(modelData.title) }
                    }
                }
            }
        }
    }

    // The sidebar arrangement's modal drawer, for a window too narrow to hold
    // the rail beside the content. It carries what the rail carries, which is
    // the only way to reach the pins and the window's actions at that size.
    MNavigationDrawer {
        id: navigationDrawer; objectName: "navigationDrawer"
        parent: window.contentItem
        ColumnLayout {
            anchors.fill: parent
            spacing: 4
            Repeater {
                model: [{key:"home",icon:"home",label:"Home"},{key:"search",icon:"search",label:"Search"},{key:"library",icon:"library",label:"Library"}]
                MNavigationItem {
                    required property var modelData
                    objectName: "drawerNav_"+modelData.key
                    expanded: true
                    Layout.fillWidth: true; Layout.preferredHeight: 56
                    symbol: modelData.icon; text: modelData.label
                    selected: window.destination===modelData.key
                    badged: modelData.key==="library" && app.importingLocal
                    onClicked: { navigationDrawer.close(); window.goToDestination(modelData.key) }
                }
            }
            MDivider { objectName: "drawerDivider"; inset: 16; visible: app.pins.length>0; Layout.fillWidth: true; Layout.topMargin: 4 }
            SungText { visible: app.pins.length>0; text: "Pinned"; color: Theme.muted; font.pixelSize: Theme.titleSmall; typeRole: "titleSmall"; Layout.leftMargin: 16 }
            Repeater {
                model: app.pins.slice(0,6)
                MNavigationItem {
                    required property var modelData
                    required property int index
                    objectName: "drawerPin_"+index
                    expanded: true
                    Layout.fillWidth: true; Layout.preferredHeight: 48
                    artUrl: modelData.art || ""; text: modelData.title || ""
                    selected: app.libraryId===modelData.id
                    onClicked: { navigationDrawer.close(); app.open(modelData) }
                }
            }
            Item { Layout.fillHeight: true }
            MButton { objectName: "drawerMiniPlayer"; symbol: "mini"; text: "Mini player"; leftAligned: true; Layout.fillWidth: true; onClicked: { navigationDrawer.close(); window.openMiniPlayer() } }
            MButton { objectName: "drawerSettings"; symbol: "settings"; text: "Settings"; leftAligned: true; Layout.fillWidth: true; onClicked: { navigationDrawer.close(); settingsDialog.open() } }
        }
    }
    // One navigation bar for every window size and both arrangements. The top
    // bar arrangement centres it as a capsule while there is room and spans it
    // across the column once there is not; the sidebar arrangement uses it
    // only on a window too narrow for the rail, against the bottom edge where
    // Material places it.
    MNavigationBar {
        objectName: "navigationBar"
        parent: window.bottomBarShowing ? bottomNavHost
              : window.atLeastMedium ? topBarCentre : narrowNavHost
        visible: !window.compactMode && !window.immersive && !window.railShowing
        anchors.centerIn: parent
        hugsContent: !window.sidebarNav && window.atLeastMedium
        edgeToEdge: window.bottomBarShowing
        width: hugsContent ? implicitWidth : parent.width
        height: implicitHeight
        destinations: [{key:"home",icon:"home",label:"Home"},{key:"search",icon:"search",label:"Search"},{key:"library",icon:"library",label:"Library"}]
        // Importing is pending work inside the library, so the destination
        // says so while it runs.
        badgedKeys: app.importingLocal ? ["library"] : []
        current: window.markedDestination
        onChosen: key => window.goToDestination(key)
    }
    // The window's two actions. They trail the top bar in one arrangement and
    // sit at the foot of the rail in the other, so there is one of each in the
    // window rather than a pair per arrangement.
    MButton {
        objectName: "miniPlayerButton"; symbol: "mini"; tip: "Mini player · Ctrl+M"
        parent: window.railShowing ? navigationRail.miniHost : topBarMiniHost
        anchors.fill: parent
        text: window.railShowing && navigationRail.expanded ? "Mini player" : ""
        leftAligned: window.railShowing && navigationRail.expanded
        onClicked: window.openMiniPlayer()
    }
    MButton {
        objectName: "settingsButton"; symbol: "settings"; tip: "Settings"
        parent: window.railShowing ? navigationRail.settingsHost : topBarSettingsHost
        anchors.fill: parent
        text: window.railShowing && navigationRail.expanded ? "Settings" : ""
        leftAligned: window.railShowing && navigationRail.expanded
        onClicked: settingsDialog.open()
    }
    Component {
        id: queuePanel
        ColumnLayout {
            function revealCurrent() {
                window.revealPending=false;
                queueList.currentIndex=app.currentIndex;
                queueList.forceActiveFocus(Qt.ShortcutFocusReason);
                queueList.forceLayout();queueList.positionViewAtIndex(app.currentIndex,ListView.Center);
                revealSettle.targetIndex=app.currentIndex;revealSettle.restart();
            }
            Timer {
                id: revealSettle; interval: 32
                property int targetIndex: -1
                onTriggered: {
                    if(targetIndex!==app.currentIndex || window.side!=="queue" && !immersiveQueue.visible)return;
                    // Center using actual row geometry once section headers have been laid out.
                    queueList.forceLayout();queueList.positionViewAtIndex(targetIndex,ListView.Center);queueList.forceLayout();
                    const row=queueList.itemAtIndex(targetIndex);
                    if(row)queueList.contentY=Math.max(queueList.originY,Math.min(queueList.originY+Math.max(0,queueList.contentHeight-queueList.height),row.y-(queueList.height-row.height)/2));
                }
            }
            spacing: 8
            // Material puts a choice between two views of the same place on a
            // segmented button rather than a second destination.
            MSegmentedControl {
                objectName: "queueTabs"
                Layout.fillWidth: true
                accessibleName: "Queue or recently played"
                options: [{key:"next",label:"Up next",name:"queueTab_next"},{key:"history",label:"History",name:"queueTab_history"}]
                value: window.queueTab
                onChosen: key => window.queueTab=key
            }
            TrackList {
                id: recentList; objectName: "recentlyPlayedView"
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true; reuseItems: true; cacheBuffer: 80
                segmented: true
                visible: window.queueTab==="history"
                model: app.recentlyPlayed; dragHub: trackDrag
                onActivate: (row,item)=>app.playKeepingQueue(item)
                onMenuRequested: (item,index,anchor)=>window.trackMenu(item,index,anchor,false)
                onAddSelected: window.addBatch(recentList)
                Column { anchors.centerIn: parent; spacing: 16; visible: app.recentlyPlayed.count===0
                    Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "history"; size: 36; ink: Theme.muted }
                    SungText { anchors.horizontalCenter: parent.horizontalCenter; text: "Nothing played yet"; color: Theme.muted }
                }
            }
            RowLayout {
                visible: window.queueTab==="history" && recentList.selection.count===0
                Layout.fillWidth: true
                SungText { objectName: "recentlyPlayedCount"; text: window.countText(app.recentlyPlayed.count); color: Theme.muted; Layout.fillWidth: true }
                MButton { objectName: "openFullHistory"; text: "Open history"; onClicked: {window.side="";window.chooseLibrary("history");} }
            }
            TrackList {
                id: queueList; objectName: "queueView"; visible: window.queueTab==="next"; Layout.fillWidth: true; Layout.fillHeight: true; model: app.queue; clip: true; reuseItems: true; cacheBuffer: 80
                queueMode: true; reorderEnabled: true; dragHub: trackDrag
                onActivate: (row,item)=>app.playAt(row)
                onMenuRequested: (item,index,anchor)=>window.trackMenu(item,index,anchor,true)
                onRemoveSelected: app.removeQueueRows(sourceRows())
                onAddSelected: window.addBatch(queueList)
                Column { anchors.centerIn: parent; spacing: 16; visible: app.queue.count===0
                    Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "queue"; size: 36; ink: Theme.muted }
                    SungText { anchors.horizontalCenter: parent.horizontalCenter; text: "Your queue is empty"; color: Theme.muted }
                    MButton { anchors.horizontalCenter: parent.horizontalCenter; text: "Search music"; tonal: true; onClicked: window.focusSearch() }
                }
            }
            SelectionBar { Layout.fillWidth: true; view: window.queueTab==="history"?recentList:queueList; canRemove: window.queueTab==="next" }
            RowLayout {
                visible: window.queueTab==="next" && queueList.selection.count===0; Layout.fillWidth: true
                MButton { objectName: "saveQueueButton"; symbol: "plus"; tip: "Save queue as playlist"; enabled: app.queue.count>0; onClicked: {window.playlistAction="queue";playlistName.clear();playlistDialog.open();} }
                MButton { objectName: "smartShuffleQueueButton"; symbol: "shuffle"; tip: "Shuffle upcoming · spread out artists"; enabled: app.queue.count-Math.max(0,app.currentIndex+1)>1; onClicked: app.smartShuffleQueue() }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 2
                    SungText { text: window.countText(app.queue.count); color: Theme.muted; Layout.fillWidth: true }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 2; visible: !!app.queueTime
                        SungText {objectName:"queueTimeLabel"; text:app.queueTime; color:Theme.muted; font.pixelSize:Theme.bodySmall; Layout.fillWidth:true}
                        SungText {objectName:"queueEndLabel"; text:app.queueEnd; visible:!!text; color:Theme.muted; font.pixelSize:Theme.bodySmall; Layout.fillWidth:true}
                    }
                }
                MButton { text: "Clear"; enabled: app.queue.count>0; onClicked: app.clearQueue() }
            }
        }
    }
    Component { id: lyricsPanel; LyricsView {} }
    Component {
        id: nowPanel
        ScrollView {
            id: nowScroll
            contentWidth: availableWidth; clip: true
            ColumnLayout {
                width: nowScroll.availableWidth; spacing: 16
                Artwork { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: Math.min(320,nowScroll.availableWidth); Layout.preferredHeight: width; url: app.current.art || ""; motionUrl: app.currentMotionArt || ""; radius: Theme.shapeExtraLarge; pixels: 650; highResolution:true;crossfade:true; fit:app.currentArtworkFit }
                SungText { text: app.current.title || "Nothing playing"; Layout.fillWidth: true; font.pixelSize: Theme.headlineSmall; font.weight: Font.Medium; wrapMode: Text.Wrap; elide: Text.ElideNone }
                SungText { text: app.current.artist || ""; Layout.fillWidth: true; font.pixelSize: Theme.bodyLarge; color: Theme.muted }
                RowLayout {
                    Layout.fillWidth: true
                    MButton { symbol: "heart"; tip: app.liked?"Unlike":"Like"; toggle: true; selected: app.liked; onClicked: app.toggleLike(app.current) }
                    MButton { symbol: "radio"; tip: "Start radio"; enabled: !!app.current.videoId; onClicked: app.radio(app.current) }
                    MButton { symbol: "more"; tip: "Track actions"; enabled: app.currentIndex>=0; onClicked: window.trackMenu(app.current,app.currentIndex,this,true) }
                }
                RowLayout { Layout.fillWidth: true; MButton { symbol: app.volume>0?"volume":"mute"; tip: app.volume>0?"Mute":"Unmute"; onClicked: window.toggleMute() } SeekBar { volumeMode: true; Layout.fillWidth: true } }
            }
        }
    }

    // What the rail holds when there is room for a rail. On a compact window
    // the rail stands down, and without this its destinations, pins, mini
    // player and settings would have no way in at all.
    ColumnLayout {
        id: panelBody
        objectName: "sidePanelBody"
        parent: window.sheetMode ? sheetHost : sidePanelHost
        anchors.fill: parent
        // Material sets a side sheet's headline apart from what it holds by
        // this much, and no more.
        spacing: Theme.sideSheetTopSpacing
        RowLayout {
            Layout.fillWidth: true
            SungText {heading: true; objectName: "sidePanelTitle"; text: window.side==="queue"?(window.queueTab==="history"?"Recently played":"Up next"):window.side==="lyrics"?"Lyrics":"Now playing"; font.pixelSize: Theme.titleLarge; font.weight: Font.Medium; Layout.fillWidth: true }
            MButton { objectName: "revealPlayingButton"; text: "Playing"; tip: "Show playing song · Ctrl+J"; visible: window.side==="queue" && window.queueTab==="next"; enabled: app.currentIndex>=0; onClicked: window.revealPlaying() }
            MButton { objectName: "lyricSearchButton"; symbol: "search"; tip: "Find in lyrics"; visible: window.side==="lyrics"; enabled: !!app.lyrics; onClicked: {if(sideLoader.item)sideLoader.item.openSearch();} }
            MButton { objectName: "lyricTimingButton"; symbol: "settings"; tip: "Lyric timing · saved for this song"; visible: window.side==="lyrics" && !!app.current.id; onClicked: lyricTimingDialog.open() }
            MButton { objectName: "immersiveButton"; symbol: "expand"; tip: "Immersive player · F11"; visible: window.side!=="queue"; enabled: app.currentIndex>=0; onClicked: window.toggleImmersive() }
            MButton { symbol: "close"; tip: "Close panel"; onClicked: window.side="" }
        }
        Loader {
            id: sideLoader
            onLoaded: if(window.revealPending && item.revealCurrent)Qt.callLater(()=>{if(item && item.revealCurrent)item.revealCurrent();})
            Layout.fillWidth: true; Layout.fillHeight: true
            active: !!window.side && (sidePanel.visible || window.sheetMode)
            sourceComponent: window.side==="queue"?queuePanel:window.side==="lyrics"?lyricsPanel:nowPanel
        }
    }
    MBottomSheet {
        id: panelSheet; objectName: "panelSheet"
        parent: window.contentItem
        // At this width the sheet is most of the window, so it takes the
        // screen over rather than sitting alongside what it covers.
        modal: true
        open: window.sheetMode && !!window.side && !window.immersive && !window.compactMode
        onClosed: window.side=""
        Item { id: sheetHost; anchors.fill: parent; anchors.margins: 16; anchors.topMargin: 6 }
    }


    MMenu {
        id: bulkActions; objectName: "bulkActions"
        MMenuItem { symbol: "next"; text: "Play selected next"; onTriggered: app.enqueueItems(window.bulkView.selection.items(),true) }
        MMenuItem { symbol: "queue"; text: "Add selected to queue"; onTriggered: app.enqueueItems(window.bulkView.selection.items()) }
        MMenuItem { symbol: "plus"; text: "Add selected to playlist"; onTriggered: window.addBatch(window.bulkView) }
        MMenuItem { symbol: "remove"; text: "Remove selected"; visible: window.bulkView && (window.bulkView.queueMode || window.editableLocal || app.serverPlaylistEditable); height: visible?44:0; onTriggered: window.bulkView.removeSelected() }
        MMenuItem { symbol: "check"; text: "Select all"; shortcut: "Ctrl+A"; onTriggered: window.bulkView.selection.selectAll() }
        MMenuItem { symbol: "close"; text: "Clear selection"; shortcut: "Esc"; onTriggered: window.bulkView.selection.clear() }
    }
    MMenu {
        id: actions; objectName: "trackActions"
        width: 230; padding: 8
        background: Rectangle { color: Theme.high; radius: Theme.shapeLargeIncreased; border.color: Theme.outlineVariant }
        MMenuItem { text: "Open"; visible: !(window.menuItem.videoId || window.menuItem.localPath || window.menuItem.serverSong); onTriggered: app.open(window.menuItem) }
        MMenuItem { objectName: "playKeepQueueAction"; symbol: "play"; text: "Play now, keep queue"; visible: !!(window.menuItem.videoId || window.menuItem.localPath || window.menuItem.serverSong); onTriggered: app.playKeepingQueue(window.menuItem) }
        MMenuItem { symbol: "next"; text: "Play next"; visible: !!(window.menuItem.videoId || window.menuItem.localPath || window.menuItem.serverSong); onTriggered: app.enqueue(window.menuItem,true) }
        MMenuItem { symbol: "queue"; text: "Add to queue"; visible: !!(window.menuItem.videoId || window.menuItem.localPath || window.menuItem.serverSong); onTriggered: app.enqueue(window.menuItem) }
        MMenuItem { symbol: "radio"; text: "Start radio"; visible: !!window.menuItem.videoId; onTriggered: app.radio(window.menuItem) }
        MMenuItem { objectName: "trimAction"; symbol: "volume"; text: "Adjust volume\u2026"; visible: !!(window.menuItem.videoId || window.menuItem.localPath || window.menuItem.serverSong); onTriggered: trimDialog.adjust(window.menuItem) }
        MDivider { visible: !!(window.menuItem.videoId || window.menuItem.localPath || window.menuItem.serverSong); height: visible ? implicitHeight : 0 }
        MMenuItem { symbol: "heart"; text: app.isLiked(window.menuItem.id || "")?"Remove from liked songs":"Like song"; visible: !!(window.menuItem.videoId || window.menuItem.localPath || window.menuItem.serverSong); onTriggered: app.toggleLike(window.menuItem) }
        MMenuItem { objectName: "trackDetailsAction"; symbol: "more"; text: "Track details"; visible: !!(window.menuItem.videoId || window.menuItem.localPath || window.menuItem.serverSong); onTriggered: trackDetails.inspect(window.menuItem) }
        MMenuItem { symbol: "plus"; text: "Add to playlist"; visible: !!(window.menuItem.videoId || window.menuItem.localPath || window.menuItem.serverSong); onTriggered: addPlaylistDialog.open() }
        MMenuItem { symbol: "chevron"; text: "Go to artist"; visible: !!window.menuItem.artistId; onTriggered: app.open(window.relatedItem(window.menuItem,"artist")) }
        MMenuItem { text: "Artwork…"; visible: window.menuItem.id===app.current.id && !!app.current.id; onTriggered: artworkControls.open() }
        MMenuItem { symbol: "disc"; text: "Go to album"; visible: !!window.menuItem.albumId; onTriggered: app.open(window.relatedItem(window.menuItem,"album")) }
        MMenuItem { text: ["subsonic","jellyfin"].indexOf(window.menuItem.source)>=0?"Copy song details":window.menuItem.localPath?"Copy file path":"Copy link"; onTriggered: app.copyLink(window.menuItem) }
        MMenuItem { text: "Add to server playlist"; visible: !!window.menuItem.serverSong; enabled: app.server.connected; onTriggered: {window.batchItems=[];serverAddDialog.open()} }
        MMenuItem { text: "Rate song"; visible: !!window.menuItem.serverSong && app.server.supportsRating; enabled: app.server.connected; onTriggered: serverRatingDialog.open() }
        MMenuItem { text: "Remove from server playlist"; visible: app.serverPlaylistEditable && !window.menuQueue; onTriggered: app.removeServerRows([window.menuIndex]) }
        MMenuItem { text: "Rename server playlist"; visible: ["subsonic","jellyfin"].indexOf(window.menuItem.source)>=0 && window.menuItem.kind==="playlist" && !!window.menuItem.editable; onTriggered: {serverName.text=window.menuItem.title;serverRenameDialog.open()} }
        MMenuItem { text: "Delete server playlist"; visible: ["subsonic","jellyfin"].indexOf(window.menuItem.source)>=0 && window.menuItem.kind==="playlist" && !!window.menuItem.editable && (window.menuItem.source!=="jellyfin" || !!window.menuItem.deletable); onTriggered: serverDeleteDialog.open() }
        MMenuItem { symbol: "folder"; text: "Locate file…"; visible: !!window.menuItem.localPath; onTriggered: window.openFileDialog("locate") }
        MMenuItem { text: "Remove from local files"; visible: app.libraryId==="files" && !!window.menuItem.localPath && !window.menuQueue; onTriggered: app.removeLocalFile(window.menuItem.id) }
        MDivider { visible: window.menuQueue || window.editableLocal; height: visible?implicitHeight:0 }
        MMenuItem { text: "Move up"; visible: window.menuQueue; height: visible?44:0; enabled: window.menuIndex>0; onTriggered: app.moveQueue(window.menuIndex,window.menuIndex-1) }
        MMenuItem { text: "Move down"; visible: window.menuQueue; height: visible?44:0; enabled: window.menuIndex<app.queue.count-1; onTriggered: app.moveQueue(window.menuIndex,window.menuIndex+1) }
        MMenuItem { symbol: "remove"; text: "Remove from queue"; visible: window.menuQueue; height: visible?44:0; onTriggered: app.removeQueue(window.menuIndex) }
        MMenuItem { text: "Move up in playlist"; visible: !window.menuQueue && window.editableLocal; height: visible?44:0; enabled: window.menuIndex>0 && app.collection.sortKey==="original" && !app.collection.query; onTriggered: app.movePlaylistTrack(window.localPlaylist,window.menuIndex,window.menuIndex-1) }
        MMenuItem { text: "Move down in playlist"; visible: !window.menuQueue && window.editableLocal; height: visible?44:0; enabled: window.menuIndex<app.results.count-1 && app.collection.sortKey==="original" && !app.collection.query; onTriggered: app.movePlaylistTrack(window.localPlaylist,window.menuIndex,window.menuIndex+1) }
        MMenuItem { text: "Remove from playlist"; visible: !window.menuQueue && window.editableLocal; height: visible?44:0; onTriggered: app.removeFromPlaylist(window.localPlaylist,window.menuIndex) }
    }
    MMenu {
        id: playlistActions
        MMenuItem { text: {app.pins;return app.isPinned({kind:"local",id:window.editPlaylistId})?"Unpin from Home":"Pin to Home";} onTriggered: app.togglePin({kind:"local",id:window.editPlaylistId,title:playlistName.text}) }
        MMenuItem { text: "Edit rules"; visible: !!app.smartPlaylist(window.editPlaylistId).id; onTriggered: smartDialog.edit(window.editPlaylistId) }
        MMenuItem { text: "Clean up…"; visible: !app.smartPlaylist(window.editPlaylistId).id; onTriggered: window.openCleanup(window.editPlaylistId) }
        MMenuItem { text: "Change cover…"; onTriggered: {playlistCoverDialog.playlistId=window.editPlaylistId;playlistCoverDialog.preview="";playlistCoverDialog.open();} }
        MMenuItem { text: "Restore cover collage"; onTriggered: app.resetPlaylistCover(window.editPlaylistId) }
        MMenuItem { objectName: "exportM3uItem"; text: "Export as M3U\u2026"; onTriggered: window.openFileDialog("m3u-export") }
        MMenuItem { objectName: "playlistVersionsItem"; text: "Version history\u2026"; visible: !app.smartPlaylist(window.editPlaylistId).id; onTriggered: playlistVersionsDialog.inspect(window.editPlaylistId,playlistName.text) }
        MMenuItem { text: "Rename"; onTriggered: {window.playlistAction="rename";playlistDialog.open();} }
        MMenuItem { text: "Delete"; onTriggered: deletePlaylistDialog.open() }
    }
    function openCleanup(id) { cleanupDialog.playlistId=id;app.inspectPlaylist(id);cleanupDialog.open(); }
    MDialog {
        id: musicFoldersDialog; objectName: "musicFoldersDialog"; anchors.centerIn: parent
        title: "Music folders"; modal: true; width: Math.min(560,window.width-48); height: Math.min(440,window.height-48,240+64*Math.max(1,app.musicFolders.length))
        standardButtons: Dialog.Close
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            ListView {
                objectName: "musicFoldersList"; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                model: musicFoldersDialog.visible?app.musicFolders:[]; reuseItems: true
                ScrollBar.vertical: ScrollBar {}
                delegate: RowLayout {
                    required property string modelData; width: ListView.view.width; height: 64; spacing: 8
                    SungText { text: modelData; Layout.fillWidth: true; elide: Text.ElideMiddle }
                    MButton { symbol: "close"; tip: "Forget folder; keep songs"; enabled: !app.importingLocal; onClicked: app.forgetMusicFolder(modelData) }
                }
                SungText { anchors.centerIn: parent; visible: app.musicFolders.length===0; text: "Add a folder to import its music"; color: Theme.muted }
            }
            MButton { objectName: "addMusicFolderButton"; text: "Add folder…"; symbol: "plus"; tonal: true; enabled: !app.importingLocal; onClicked: {musicFoldersDialog.close();musicFolderPath.clear();musicFolderEntry.pathError="";musicFolderEntry.open();} }
        }
    }
    property bool folderPickForOnboarding: false
    function finishFolderPick(url) {
        if(folderPickForOnboarding){folderPickForOnboarding=false;onboarding.setFolderPath(url.toString());return;}
        musicFolderPath.text=url.toString();musicFolderEntry.pathError="";musicFolderEntry.open();
    }
    function returnToFolderEntry() {
        if(folderPickForOnboarding){folderPickForOnboarding=false;return;}
        musicFolderEntry.open();
    }
    Onboarding {
        id: onboarding; objectName: "onboarding"
        onBrowseRequested: {window.folderPickForOnboarding=true;window.openFileDialog("folder");}
        onServerRequested: serverConnection.open()
    }
    MDialog {
        id: musicFolderEntry; objectName: "musicFolderEntry"; anchors.centerIn: parent
        title: "Add music folder"; modal: true; width: Math.min(560,window.width-48)
        implicitHeight: header.implicitHeight+contentItem.implicitHeight+footer.implicitHeight+topPadding+bottomPadding
        initialFocus: musicFolderPath
        property string pathError: ""
        function submit() {
            if(app.importingLocal)return;
            pathError=app.importMusicFolderPath(musicFolderPath.text);
            if(!pathError){musicFolderPath.clear();close();}else musicFolderPath.forceActiveFocus();
        }
        contentItem: ColumnLayout {
            spacing: 12
            MTextField {
                id: musicFolderPath; objectName: "musicFolderPath"; Layout.fillWidth: true
                label: "Folder path"; placeholderText: activeFocus?"/path/to/Music or ~/Music":""
                supporting: "A folder is scanned including everything inside it."
                errorText: musicFolderEntry.pathError
                onTextChanged: musicFolderEntry.pathError=""
                onAccepted: musicFolderEntry.submit()
            }
            MButton { objectName: "browseMusicFolderButton"; text: "Browse…"; symbol: "folder"; enabled: !app.importingLocal; onClicked: {musicFolderEntry.close();window.openFileDialog("folder");} }
        }
        footer: Item {
            implicitHeight: 88
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 24; anchors.rightMargin: 24; anchors.topMargin: 16; anchors.bottomMargin: 24; spacing: 8
                Item { Layout.fillWidth: true }
                MButton { text: "Cancel"; onClicked: musicFolderEntry.close() }
                MButton { objectName: "confirmMusicFolderButton"; text: "Add folder"; filled: true; enabled: !!musicFolderPath.text.trim() && !app.importingLocal; onClicked: musicFolderEntry.submit() }
            }
        }
    }
    MDialog {
        id: cleanupDialog; objectName: "cleanupDialog"; property string playlistId
        anchors.centerIn: parent; title: "Clean up playlist"; modal: true
        width: Math.min(620,window.width-48); height: Math.min(560,window.height-48,244+80*Math.max(1,app.cleanupItems.length))
        onClosed: app.closePlaylistCleanup()
        Loader {
            anchors.fill: parent; active: cleanupDialog.visible
            sourceComponent: ColumnLayout {
                id: cleanupContent; spacing: 12
                property bool removeDuplicates: true
                property bool removeMissing: false
                readonly property int selectedCount: app.cleanupItems.filter(r=>(removeDuplicates&&r.duplicate)||(removeMissing&&r.missing)).length
                RowLayout {
                    Layout.fillWidth: true
                    MChip { objectName: "cleanupDuplicates"; text: "Duplicates"; selected: cleanupContent.removeDuplicates; onClicked: cleanupContent.removeDuplicates=!cleanupContent.removeDuplicates }
                    MChip { objectName: "cleanupMissing"; text: "Missing files"; selected: cleanupContent.removeMissing; onClicked: cleanupContent.removeMissing=!cleanupContent.removeMissing }
                    Item { Layout.fillWidth: true }
                    MButton { symbol: "refresh"; busy: app.cleanupBusy; tip: "Check again"; enabled: !app.cleanupBusy&&!app.importingLocal; onClicked: app.inspectPlaylist(cleanupDialog.playlistId) }
                }
                ListView {
                    objectName: "cleanupList"; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; reuseItems: true; spacing: 4
                    model: app.cleanupItems
                    ScrollBar.vertical: ScrollBar {}
                    delegate: RowLayout {
                        required property var modelData; width: ListView.view.width; height: 76; spacing: 12
                        opacity: (cleanupContent.removeDuplicates&&modelData.duplicate)||(cleanupContent.removeMissing&&modelData.missing)?1:0.6
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 4
                            SungText { text: modelData.title; Layout.fillWidth: true; elide: Text.ElideRight }
                            SungText { text: [modelData.duplicate?"Duplicate":"",modelData.missing?"Missing file":""].filter(Boolean).join(" · "); color: Theme.muted; font.pixelSize: Theme.labelMedium }
                        }
                        MButton { text: "Locate…"; visible: modelData.missing; enabled: !app.importingLocal; onClicked: {window.menuItem=modelData;window.openFileDialog("locate");} }
                    }
                    SungText { anchors.centerIn: parent; text: app.cleanupBusy?"Checking playlist…":"No duplicates or missing files"; visible: app.cleanupBusy||app.cleanupItems.length===0; color: Theme.muted }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    MButton { text: "Done"; onClicked: cleanupDialog.close() }
                    MButton { objectName: "applyPlaylistCleanup"; text: "Remove "+cleanupContent.selectedCount; tonal: true; enabled: cleanupContent.selectedCount>0&&!app.cleanupBusy&&!app.importingLocal; onClicked: app.applyPlaylistCleanup(cleanupContent.removeDuplicates,cleanupContent.removeMissing) }
                }
                Connections { target: app; function onLocalImportChanged() { if(!app.importingLocal&&cleanupDialog.opened)app.inspectPlaylist(cleanupDialog.playlistId); } }
            }
        }
    }
    MDialog {
        id: playlistDialog; objectName: "playlistDialog"; initialFocus: playlistName; acceptText: window.playlistAction==="rename" ? "Save" : "Create"; anchors.centerIn: parent; width: 380; modal: true; acceptEnabled: playlistName.text.trim().length>0; title: window.playlistAction==="rename"?"Rename playlist":"New playlist"
        palette.windowText: Theme.text; palette.text: Theme.text; palette.buttonText: Theme.text
        standardButtons: Dialog.Save | Dialog.Cancel
        MTextField { id: playlistName; objectName: "playlistName"; variant: "filled"; width: parent.width; label: "Playlist name"; maximumLength: 120; onAccepted: if(playlistDialog.acceptEnabled)playlistDialog.accept() }
        onOpened: playlistName.forceActiveFocus()
        onAccepted: { if(window.playlistAction==="queue")app.saveQueue(playlistName.text);else if(window.playlistAction==="rename")app.renamePlaylist(window.editPlaylistId,playlistName.text);else {const id=app.createPlaylist(playlistName.text);if(id&&window.playlistAction==="add"){if(window.batchItems.length)app.addItemsToPlaylist(id,window.batchItems);else app.addToPlaylist(id,window.menuItem);}} }
    }
    MDialog {
        id: addPlaylistDialog; objectName: "addPlaylistDialog"; anchors.centerIn: parent; width: 360; height: Math.min(window.height-64,Math.min(500,268+app.playlists.length*52)); modal: true; title: "Add to playlist"; standardButtons: Dialog.Cancel
        palette.windowText: Theme.text
        property bool contentReady: false
        onAboutToShow: contentReady=true
        contentItem: Loader {
            active: addPlaylistDialog.contentReady
            sourceComponent: Component {
        ColumnLayout {
            anchors.fill: parent
            ListView { objectName: "playlistChoices"; Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: Math.min(52,app.playlists.length*52); clip: true; model: app.playlists.filter(p=>!p.smart); delegate: MButton { objectName: "playlistChoice"; required property var modelData; width: ListView.view.width; text: modelData.title; leftAligned: true; onClicked: {const playlistId=modelData.id;addPlaylistDialog.close();window.addPlaylistSelection(playlistId);} } }
            MButton { Layout.fillWidth: true; text: "Server playlist…"; visible: app.server.connected; onClicked: {addPlaylistDialog.close();serverAddDialog.open()} }
            MButton { Layout.fillWidth: true; text: "New playlist"; symbol: "plus"; tonal: true; onClicked: {addPlaylistDialog.close();window.playlistAction="add";playlistName.clear();playlistDialog.open();} }
        }
            }
        }
    }
    MDialog {
        // A prompt that cannot be undone takes Material's dialog icon, which
        // centres the headline under it so the question is read before it is
        // answered.
        id: deletePlaylistDialog; objectName: "deletePlaylistDialog"; anchors.centerIn: parent; width: 380; modal: true; symbol: "trash"; title: "Delete playlist?"; standardButtons: Dialog.Yes | Dialog.No; onOpened: {standardButton(Dialog.Yes).text="Delete";standardButton(Dialog.No).text="Cancel";}
        palette.windowText: Theme.text; palette.buttonText: Theme.text
        onAccepted: {app.deletePlaylist(window.editPlaylistId);window.localPlaylist="";}
    }
    MDialog {
        id: serverAddDialog; objectName: "serverAddDialog"; anchors.centerIn: parent; width: 380; height: Math.min(window.height-64,420); modal: true; title: "Add to server playlist"; standardButtons: Dialog.Cancel
        ListView { anchors.fill: parent; clip: true; model: app.server.playlists
            delegate: MButton { required property var modelData; width: ListView.view.width; text: modelData.title; leftAligned: true; enabled: !!modelData.editable; onClicked: {app.addServerPlaylist(modelData.remoteId,window.batchItems.length?window.batchItems:[window.menuItem]);serverAddDialog.close()} }
            SungText { anchors.centerIn: parent; visible: app.server.playlists.length===0; text: "Create a playlist in Music server"; color: Theme.muted }
        }
    }
    MDialog {
        id: serverRenameDialog; objectName: "serverRenameDialog"; initialFocus: serverName; acceptText: "Save"; anchors.centerIn: parent; width: 360; modal: true; title: "Rename server playlist"; standardButtons: Dialog.Save | Dialog.Cancel; acceptEnabled: serverName.text.trim().length>0
        MTextField { id: serverName; width: parent.width; label: "Playlist name"; maximumLength:120; onAccepted: if(serverRenameDialog.acceptEnabled)serverRenameDialog.accept() }
        onAccepted: app.renameServerPlaylist(window.menuItem,serverName.text)
    }
    MDialog {
        id: serverDeleteDialog; objectName: "serverDeleteDialog"; acceptText: "Delete"; anchors.centerIn: parent; width: 360; modal: true; title: "Delete server playlist?"; standardButtons: Dialog.Ok | Dialog.Cancel
        SungText { width: parent.width; text: "This removes the playlist from your server. Songs are kept."; wrapMode: Text.Wrap }
        onAccepted: app.deleteServerPlaylist(window.menuItem)
    }
    MDialog {
        id: serverRatingDialog; objectName: "serverRatingDialog"; anchors.centerIn: parent; width: 380; modal: true; title: "Rate song"; standardButtons: Dialog.Cancel
        Column { width: parent.width; spacing: 8
            Row { anchors.horizontalCenter: parent.horizontalCenter; spacing: 8; Repeater { model:5; MButton { required property int index; objectName: "rating_"+(index+1); text:String(index+1); tip: "Rate " + (index+1) + " out of 5"; width: 48; selected: window.menuItem.rating===index+1; onClicked:{app.rateServerSong(window.menuItem,index+1);serverRatingDialog.close()} } } }
            MButton { objectName: "clearRatingButton"; anchors.horizontalCenter: parent.horizontalCenter; text: "Clear rating"; enabled: !!window.menuItem.rating; onClicked: {app.rateServerSong(window.menuItem,0);serverRatingDialog.close()} }
        }
    }
    ServerConnection { id: serverConnection }
    MDialog {
        id: settingsDialog; objectName: "settingsDialog"; width: fitWidth(880); height: fitHeight(740); modal: true; title: "Settings"
        padding: 24
        palette.windowText: Theme.text; palette.buttonText: Theme.text; palette.text: Theme.text
        standardButtons: Dialog.Close
        scrollSource: contentItem.item ? contentItem.item.scrollFlickable : null
        property bool contentReady: false
        property string searchQuery: ""
        property int category: 0
        readonly property var categories: ["Appearance","Playback","Library","Connections","Privacy & data"]
        function matches(terms) {return searchQuery.trim().toLowerCase().split(/\s+/).every(word=>terms.toLowerCase().indexOf(word)>=0);}
        onAboutToShow: {contentReady=true;searchQuery="";}
        onOpened: if(contentItem.item)contentItem.item.focusSearch()
        contentItem: Loader {
            active: settingsDialog.contentReady
            sourceComponent: Component {
        Item {
        property Flickable scrollFlickable: settingsScrollView.contentItem
        function focusSearch() {settingsSearch.forceActiveFocus();}
        MSearchField { id: settingsSearch; objectName: "settingsSearch"; anchors.left: parent.left; anchors.right: parent.right; placeholderText: "Search settings"; clearTip: "Clear settings search"; text: settingsDialog.searchQuery; onTextEdited: {settingsDialog.searchQuery=text;settingsScrollView.contentItem.contentY=0;} }

        Column {
            id:settingsCategories;objectName:"settingsCategories";visible:parent.width>=720
            y:settingsSearch.height+20;width:180;spacing:6
            Repeater {model:settingsDialog.categories
                MButton {required property string modelData;required property int index;objectName:"settingsCategory_"+index
                    width:180;text:modelData;leftAligned:true;selected:!settingsDialog.searchQuery.trim() && settingsDialog.category===index
                    onClicked:{settingsDialog.category=index;settingsDialog.searchQuery="";settingsScrollView.contentItem.contentY=0;}
                }
            }
        }
        MButton {id:settingsCategoryPicker;objectName:"settingsCategoryPicker";visible:parent.width<720
            y:settingsSearch.height+12;width:parent.width;leftAligned:true;tonal:true
            text:settingsDialog.searchQuery.trim()?"Search results":settingsDialog.categories[settingsDialog.category];symbol:"chevron"
            onClicked:settingsCategoryMenu.popup(this,0,height+4)
            MMenu {id:settingsCategoryMenu;objectName:"settingsCategoryMenu";segmented:true;width:settingsCategoryPicker.width
                Repeater {model:settingsDialog.categories
                    // A segmented menu is a choice between peers, so it has to
                    // say which peer you are on. This one never did.
                    MMenuItem {required property string modelData;required property int index;text:modelData;checkable:true;checked:settingsDialog.category===index;onTriggered:{settingsDialog.category=index;settingsDialog.searchQuery="";settingsScrollView.contentItem.contentY=0;}}
                }
            }
        }
        ScrollView {
            id: settingsScrollView; objectName: "settingsScroll"
            rightPadding: 12
            ScrollBar.horizontal.policy:ScrollBar.AlwaysOff
            anchors.fill: parent; anchors.leftMargin: parent.width>=720?204:0; anchors.topMargin: settingsSearch.height+(parent.width>=720?20:76); contentWidth: availableWidth; contentHeight: settingsOptions.implicitHeight; clip: true
            ScrollBar.vertical: MScrollBar {
                objectName: "settingsScrollBar"
                parent: settingsScrollView; x: settingsScrollView.width-width
                y: settingsScrollView.topPadding; height: settingsScrollView.availableHeight
                orientation: Qt.Vertical
            }
            // A ScrollView's own Flickable is its contentItem, so that is what
            // the wheel has to drive.
            MSmoothWheel { flick: settingsScrollView.contentItem }
            ColumnLayout {
                id: settingsOptions; objectName: "settingsOptions"
                width: settingsScrollView.availableWidth; spacing: 28
                ColumnLayout {
                    id: settingsGroup0; objectName:"settingsGroup0"
                    Layout.fillWidth:true;Layout.minimumWidth:0; spacing:12
                    property bool hasMatches: settingsDialog.matches("Appearance theme system Noctalia light dark") || settingsDialog.matches("Appearance artwork accent color") || settingsDialog.matches("Accent color source palette") || settingsDialog.matches("Ambient artwork backdrop immersive now playing") || settingsDialog.matches("Backdrop follows the music audio") || settingsDialog.matches("Album covers for music videos YouTube Apple Music") || settingsDialog.matches("Color scheme variant neutral tonal spot vibrant expressive content") || settingsDialog.matches("Contrast standard medium high accessibility") || settingsDialog.matches("Density compact comfortable spacing") || settingsDialog.matches("Pointer density precise mouse touch target") || settingsDialog.matches("Current view layout density grid list") || (!!app.current.id && settingsDialog.matches("Current artwork")) || settingsDialog.matches("Animated album artwork") || settingsDialog.matches("Online animated covers YouTube Apple Music") || settingsDialog.matches("Animations")
                    visible: settingsDialog.searchQuery.trim() ? hasMatches : settingsDialog.category===0
                    SungText {heading: true;text:"Appearance";font.pixelSize:Theme.titleLarge;font.weight:Font.Medium;Layout.bottomMargin:8}
                    ColumnLayout {id:options0;objectName:"settingsRows0";Layout.fillWidth:true;Layout.minimumWidth:0;spacing:12
                // Two arrangements of the same three destinations. The option
                // labels name where they go, so the control needs no sentence
                // under it explaining what a rail is; it does need its own
                // heading, or it reads as a theme under the one below.
                SungText { visible: settingsDialog.matches("Navigation top bar sidebar rail destinations"); text: "Navigation"; font.pixelSize: Theme.titleMedium; typeRole: "titleMedium" }
                MSegmentedControl {Layout.fillWidth:true;Layout.minimumWidth:0;visible:settingsDialog.matches("Navigation top bar sidebar rail destinations");accessibleName:"Navigation";options:[{key:false,label:"Top bar",name:"navigationTop"},{key:true,label:"Sidebar",name:"navigationSidebar"}];value:app.sidebarNavigation;onChosen:value=>app.sidebarNavigation=value}
                SungText { visible: settingsDialog.matches("Appearance theme system Noctalia light dark"); text: "Theme"; font.pixelSize: Theme.titleMedium; typeRole: "titleMedium" }
                MSegmentedControl {Layout.fillWidth:true;Layout.minimumWidth:0; visible: settingsDialog.matches("Appearance theme system Noctalia light dark"); accessibleName:"Theme"; options:[{key:"system",label:desktopTheme.available?"Noctalia":"System",name:"themeSystem"},{key:"light",label:"Light",name:"themeLight"},{key:"dark",label:"Dark",name:"themeDark"}]; value:app.theme; onChosen:value=>app.theme=value }

                MSwitch { Layout.fillWidth:true;Layout.minimumWidth:0; objectName:"artworkAccentSwitch"; text:"Use artwork accent"; checked:app.artworkAccent; onToggled:app.artworkAccent=checked; visible:settingsDialog.matches("Appearance artwork accent color") }
                SungText {text:"Accent color";visible:settingsDialog.matches("Accent color source palette");font.pixelSize:Theme.bodyLarge;font.weight:Font.Medium;Layout.topMargin:4}
                AccentPicker {Layout.fillWidth:true;Layout.minimumWidth:0;visible:settingsDialog.matches("Accent color source palette")}
                MSwitch { Layout.fillWidth:true;Layout.minimumWidth:0; objectName:"ambientBackdropSwitch"; text:"Ambient artwork backdrop"; checked:app.ambientBackdrop; onToggled:app.ambientBackdrop=checked; visible:settingsDialog.matches("Ambient artwork backdrop immersive now playing") }
                MSwitch { Layout.fillWidth:true;Layout.minimumWidth:0; objectName:"backdropPulseSwitch"; text:"Backdrop follows the music"; checked:app.backdropPulse; enabled:app.ambientBackdrop; onToggled:app.backdropPulse=checked; visible:settingsDialog.matches("Backdrop follows the music audio") }
                // Material spreads the same five palettes differently for each of
                // its scheme variants, which is what decides how much of the
                // cover the interface takes on.
                SungText {text:"Color scheme";visible:settingsDialog.matches("Color scheme variant neutral tonal spot vibrant expressive content");font.pixelSize:Theme.bodyLarge;font.weight:Font.Medium;Layout.topMargin:4}
                MSegmentedControl {Layout.fillWidth:true;Layout.minimumWidth:0;
                    objectName:"colorVariantControl"
                    visible:settingsDialog.matches("Color scheme variant neutral tonal spot vibrant expressive content")
                    accessibleName:"Color scheme"
                    options:[{key:"neutral",label:"Neutral",name:"variantNeutral"},
                             {key:"tonalSpot",label:"Balanced",name:"variantTonalSpot"},
                             {key:"vibrant",label:"Vibrant",name:"variantVibrant"},
                             {key:"expressive",label:"Expressive",name:"variantExpressive"},
                             {key:"content",label:"Faithful",name:"variantContent"}]
                    value:app.colorVariant; onChosen:value=>app.colorVariant=value
                }
                SungText {text:"Contrast";visible:settingsDialog.matches("Contrast standard medium high accessibility");font.pixelSize:Theme.bodyLarge;font.weight:Font.Medium;Layout.topMargin:4}
                MSegmentedControl {Layout.fillWidth:true;Layout.minimumWidth:0;
                    objectName:"contrastControl"
                    visible:settingsDialog.matches("Contrast standard medium high accessibility")
                    accessibleName:"Contrast"
                    options:[{key:0,label:"Standard",name:"contrastStandard"},
                             {key:0.5,label:"Medium",name:"contrastMedium"},
                             {key:1,label:"High",name:"contrastHigh"}]
                    value:app.colorContrast; onChosen:value=>app.colorContrast=value
                }
                SungText {text:"Density";visible:settingsDialog.matches("Density compact comfortable spacing");font.pixelSize:Theme.bodyLarge;font.weight:Font.Medium}
                MSegmentedControl {Layout.fillWidth:true;Layout.minimumWidth:0;visible:settingsDialog.matches("Density compact comfortable spacing");accessibleName:"Density";options:[{key:false,label:"Comfortable",name:"densityComfortable"},{key:true,label:"Compact",name:"densityCompact"}];value:app.compactDensity;onChosen:value=>app.compactDensity=value}
                // Material draws its controls tighter when a precision pointer
                // is driving them, because the 48dp minimum is a rule about
                // disambiguating touches.
                SungText {text:"Pointer";visible:settingsDialog.matches("Pointer density precise mouse touch target");font.pixelSize:Theme.bodyLarge;font.weight:Font.Medium;Layout.topMargin:4}
                MSegmentedControl {Layout.fillWidth:true;Layout.minimumWidth:0;
                    objectName:"pointerControl"
                    visible:settingsDialog.matches("Pointer density precise mouse touch target")
                    accessibleName:"Pointer"
                    options:[{key:false,label:"Comfortable",name:"pointerTouch"},{key:true,label:"Precise",name:"pointerPrecise"}]
                    value:app.precisePointer; onChosen:value=>app.precisePointer=value
                }

                MSettingRow {opens:true;objectName:"viewLayoutButton";text:"Current view layout";visible:settingsDialog.matches("Current view layout density grid list");onClicked:{settingsDialog.close();viewLayoutDialog.open();}}
                MSettingRow {opens:true;text:"Current artwork";visible:!!app.current.id && settingsDialog.matches("Current artwork");onClicked:{settingsDialog.close();artworkControls.open();}}
                MSwitch { Layout.fillWidth:true;Layout.minimumWidth:0; objectName: "animatedArtworkSwitch"; visible: settingsDialog.matches("Animated album artwork"); text: "Animated album artwork"; checked: app.animatedArtwork; onToggled: app.animatedArtwork=checked }
                MSwitch { Layout.fillWidth:true;Layout.minimumWidth:0; objectName: "onlineArtworkSwitch"; visible: settingsDialog.matches("Online animated covers YouTube Apple Music"); text: "Online animated covers"; checked: app.onlineArtwork; onToggled: app.onlineArtwork=checked }
                MSwitch { Layout.fillWidth:true;Layout.minimumWidth:0; objectName: "albumCoversSwitch"; visible: settingsDialog.matches("Album covers for music videos YouTube Apple Music"); text: "Album covers for music videos"; checked: app.albumCovers; onToggled: app.albumCovers=checked }
                MSwitch { Layout.fillWidth:true;Layout.minimumWidth:0; visible: settingsDialog.matches("Animations"); text: "Animations"; checked: app.motion; onToggled: app.motion=checked }
                    }
                }
                ColumnLayout {
                    id: settingsGroup1; objectName:"settingsGroup1"
                    Layout.fillWidth:true;Layout.minimumWidth:0; spacing:12
                    property bool hasMatches: settingsDialog.matches("Autoplay similar songs") || settingsDialog.matches("Track notifications") || settingsDialog.matches("Volume step") || settingsDialog.matches("Playback speed rate") || settingsDialog.matches("Sleep timer") || settingsDialog.matches("Pause headphones audio output disconnects") || settingsDialog.matches("Audio output device speakers headphones") || settingsDialog.matches("Prepare next track") || settingsDialog.matches("Gapless playback join pause between songs") || settingsDialog.matches("Crossfade overlap songs fade") || settingsDialog.matches("Volume normalization loudness level ReplayGain") || settingsDialog.matches("Resume recordings over 20 minutes mixes sets position") || settingsDialog.matches("Find missing lyrics on LRCLIB") || settingsDialog.matches("Sleep timer fade out volume")
                    visible: settingsDialog.searchQuery.trim() ? hasMatches : settingsDialog.category===1
                    SungText {heading: true;text:"Playback";font.pixelSize:Theme.titleLarge;font.weight:Font.Medium;Layout.bottomMargin:8}
                    ColumnLayout {id:options1;objectName:"settingsRows1";Layout.fillWidth:true;Layout.minimumWidth:0;spacing:12
                MSwitch { Layout.fillWidth:true;Layout.minimumWidth:0; visible: settingsDialog.matches("Autoplay similar songs"); text: "Autoplay similar songs"; checked: app.autoplay; onToggled: app.autoplay=checked }
                MSwitch { Layout.fillWidth:true;Layout.minimumWidth:0; visible: settingsDialog.matches("Track notifications"); objectName: "trackNotificationsSwitch"; text: "Track notifications"; checked: app.trackNotifications; onToggled: app.trackNotifications=checked }
                RowLayout { visible: settingsDialog.matches("Volume step"); Layout.fillWidth: true;Layout.minimumWidth:0; SungText { text: "Volume step"; font.pixelSize: Theme.bodyLarge; Layout.fillWidth: true } MButton { objectName: "volumeStepButton"; text: app.volumeStep+"%"; tonal: true; onClicked: volumeStepMenu.popup(this,width-volumeStepMenu.width,height+4) } }
                RowLayout { visible: settingsDialog.matches("Playback speed rate"); Layout.fillWidth: true;Layout.minimumWidth:0; SungText { text: "Playback speed"; font.pixelSize: Theme.bodyLarge; Layout.fillWidth: true } MButton { objectName: "playbackSpeedButton"; text: Number(app.playbackRate.toFixed(2))+"×"; tonal: true; onClicked: rateDialog.open() } }
                RowLayout { visible: settingsDialog.matches("Audio output device speakers headphones"); Layout.fillWidth: true;Layout.minimumWidth:0; SungText { text: "Audio output"; font.pixelSize: Theme.bodyLarge; Layout.fillWidth: true } MButton { objectName: "audioDeviceButton"; text: app.audioDeviceName; tip: "Audio output"; tonal: true; onClicked: audioDeviceDialog.open() } }
                MSwitch { Layout.fillWidth:true;Layout.minimumWidth:0;objectName:"disconnectSwitch";text:"Pause when audio output disconnects";hint:"Unplugging headphones or losing a Bluetooth device stops playback instead of switching it to the speakers.";checked:app.pauseOnDisconnect;onToggled:app.pauseOnDisconnect=checked;visible:settingsDialog.matches("Pause headphones audio output disconnects")}
                RowLayout { visible: settingsDialog.matches("Sleep timer"); Layout.fillWidth: true;Layout.minimumWidth:0; SungText { text: "Sleep timer"; font.pixelSize: Theme.bodyLarge; Layout.fillWidth: true } MButton { objectName: "sleepTimerButton"; text: app.sleepStatus; tonal: true; onClicked: sleepMenu.popup(this,width-sleepMenu.width,height+4) } }
                MSwitch {objectName:"sleepFadeSwitch";Layout.fillWidth:true;Layout.minimumWidth:0;text:"Fade out before sleep";hint:"The last minute before the sleep timer ends fades the volume down rather than cutting it.";checked:app.sleepFade;onToggled:app.sleepFade=checked;visible:settingsDialog.matches("Sleep timer fade out volume")}
                MSwitch { Layout.fillWidth:true;Layout.minimumWidth:0; objectName:"volumeNormalizationSwitch"; visible: settingsDialog.matches("Volume normalization loudness level ReplayGain"); text: "Volume normalization"; hint: "Uses the ReplayGain loudness written into a file, where there is one, so songs from different albums play at a similar level."; checked: app.volumeNormalization; onToggled: app.volumeNormalization=checked }
                MSwitch { Layout.fillWidth:true;Layout.minimumWidth:0; objectName:"resumeLongTracksSwitch"; visible: settingsDialog.matches("Resume recordings over 20 minutes mixes sets position"); text: "Resume recordings over 20 minutes"; hint: "Mixes, sets and long recordings start again where you left them instead of from the beginning."; checked: app.resumeLongTracks; onToggled: app.resumeLongTracks=checked }
                MSwitch { Layout.fillWidth:true;Layout.minimumWidth:0; objectName:"gaplessSwitch"; visible: settingsDialog.matches("Gapless playback join pause between songs"); text: "Gapless playback"; hint: "Hands the next song straight to the output as the current one ends, so albums recorded without breaks keep none."; checked: app.gapless; onToggled: app.gapless=checked }
                ColumnLayout {
                    objectName: "crossfadeSetting"
                    visible: settingsDialog.matches("Crossfade overlap songs fade"); Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        SungText { text: "Crossfade"; font.pixelSize: Theme.bodyLarge; Layout.fillWidth: true }
                        SungText { objectName: "crossfadeValue"; text: app.crossfadeSeconds>0 ? app.crossfadeSeconds+" s" : "Off"; color: Theme.muted; font.pixelSize: Theme.bodyMedium }
                    }
                    SettingSlider {
                        objectName: "crossfadeSlider"; Layout.fillWidth: true
                        valueLabel: value>0 ? value+" s" : "Off"
                        from: 0; to: 12; stepSize: 1; snapMode: Slider.SnapAlways
                        value: app.crossfadeSeconds; onMoved: app.crossfadeSeconds=value
                        Accessible.name: "Crossfade seconds"
                    }
                    SungText { text: "Songs overlap as one ends and the next begins, except inside an album and into an autoplay suggestion, which are joined instead."; color: Theme.muted; font.pixelSize: Theme.bodyMedium; Layout.fillWidth: true; wrapMode: Text.Wrap; visible: app.crossfadeSeconds>0 }
                }
                MSwitch { Layout.fillWidth:true;Layout.minimumWidth:0; visible: settingsDialog.matches("Prepare next track"); text: "Prepare next track"; hint: "Resolves and buffers the next song while the current one plays, which is what makes the join between them immediate."; checked: app.prepareNext; onToggled: app.prepareNext=checked }
                MSwitch { Layout.fillWidth:true;Layout.minimumWidth:0; visible: settingsDialog.matches("Find missing lyrics on LRCLIB"); text: "Find missing lyrics on LRCLIB"; hint: "Songs with no lyrics of their own are looked up on LRCLIB, which sends the title and artist to that service."; checked: app.lyricsFallback; onToggled: app.lyricsFallback=checked }
                    }
                }
                ColumnLayout {
                    id: settingsGroup2; objectName:"settingsGroup2"
                    Layout.fillWidth:true;Layout.minimumWidth:0; spacing:12
                    property bool hasMatches: settingsDialog.matches("Music folders import manage") || settingsDialog.matches("Keyboard shortcuts keys help") || settingsDialog.matches("Quick actions commands playlists") || settingsDialog.matches("Update music folders automatically watch") || settingsDialog.matches("Type to jump in lists keyboard") || settingsDialog.matches("Start page Home local music server liked") || settingsDialog.matches("Customize Home sections order") || settingsDialog.matches("Listening sessions saved queues") || settingsDialog.matches("Listening statistics top artists albums time")
                    visible: settingsDialog.searchQuery.trim() ? hasMatches : settingsDialog.category===2
                    SungText {heading: true;text:"Library";font.pixelSize:Theme.titleLarge;font.weight:Font.Medium;Layout.bottomMargin:8}
                    ColumnLayout {id:options2;objectName:"settingsRows2";Layout.fillWidth:true;Layout.minimumWidth:0;spacing:12
                MSettingRow {opens:true;text:"Music folders";visible:settingsDialog.matches("Music folders import manage");onClicked:{settingsDialog.close();musicFoldersDialog.open();}}
                MSettingRow {opens:true;objectName:"shortcutHelpButton";text:"Keyboard shortcuts";visible:settingsDialog.matches("Keyboard shortcuts keys help");onClicked:{settingsDialog.close();shortcutHelp.open()}}
                MSettingRow {opens:true;text:"Quick actions · Ctrl+Shift+P";visible:settingsDialog.matches("Quick actions commands playlists");onClicked:{settingsDialog.close();commandPalette.open();}}
                MSwitch { Layout.fillWidth:true;Layout.minimumWidth:0; text: "Update music folders automatically"; visible: settingsDialog.matches("Update music folders automatically watch"); checked: app.watchMusicFolders; onToggled: app.watchMusicFolders=checked }
                MSwitch { Layout.fillWidth:true;Layout.minimumWidth:0; objectName:"typeAheadSwitch"; text: "Type to jump in lists"; visible: settingsDialog.matches("Type to jump in lists keyboard"); checked: app.typeAheadJump; onToggled: app.typeAheadJump=checked }
                SungText {text:"Start page";visible:settingsDialog.matches("Start page Home local music server liked");font.pixelSize:Theme.bodyLarge;font.weight:Font.Medium}
                MSegmentedControl {Layout.fillWidth:true;Layout.minimumWidth:0;visible:settingsDialog.matches("Start page Home local music server liked");accessibleName:"Start page"; options:[{key:"home",label:"Home",name:"startPage_home"},{key:"files",label:"Local",name:"startPage_files"},{key:"server",label:"Server",name:"startPage_server"},{key:"favorites",label:"Liked",name:"startPage_favorites"}];value:app.startPage;onChosen:value=>app.startPage=value}
                MSettingRow {opens:true;text:"Customize Home";visible:settingsDialog.matches("Customize Home sections order");onClicked:{settingsDialog.close();app.home();homeEditor.open();}}
                MSettingRow {opens:true;objectName:"sessionsButton";text:"Listening sessions";visible:settingsDialog.matches("Listening sessions saved queues");onClicked:{settingsDialog.close();sessionsDialog.open();}}
                MSettingRow {opens:true;objectName:"listeningStatsButton";text:"Listening statistics";visible:settingsDialog.matches("Listening statistics top artists albums time");onClicked:{settingsDialog.close();statsDialog.open();}}
                    }
                }
                ColumnLayout {
                    id: settingsGroup3; objectName:"settingsGroup3"
                    Layout.fillWidth:true;Layout.minimumWidth:0; spacing:12
                    property bool hasMatches: settingsDialog.matches("Music server library") || settingsDialog.matches("YouTube cookies import replace remove sign in") || settingsDialog.matches("YouTube streaming quality standard data saver bitrate")
                    visible: settingsDialog.searchQuery.trim() ? hasMatches : settingsDialog.category===3
                    SungText {heading: true;text:"Connections";font.pixelSize:Theme.titleLarge;font.weight:Font.Medium;Layout.bottomMargin:8}
                    ColumnLayout {id:options3;objectName:"settingsRows3";Layout.fillWidth:true;Layout.minimumWidth:0;spacing:12
                MSettingRow {opens:true;objectName:"musicServerButton";text:"Music server";visible:settingsDialog.matches("Music server library");onClicked:{settingsDialog.close();serverConnection.open()}}
                SungText { visible: settingsDialog.matches("YouTube cookies import replace remove sign in") || settingsDialog.matches("YouTube streaming quality standard data saver bitrate"); text: "YouTube"; font.pixelSize: Theme.labelLarge; font.weight: Font.Medium; color: Theme.muted; Layout.topMargin: 12 }
                SungText { visible: settingsDialog.matches("YouTube streaming quality standard data saver bitrate"); text: "Streaming quality"; font.pixelSize: Theme.bodyLarge; font.weight: Font.Medium }
                MSegmentedControl {Layout.fillWidth:true;Layout.minimumWidth:0;
                    objectName:"streamingQualityControl"
                    visible: settingsDialog.matches("YouTube streaming quality standard data saver bitrate")
                    accessibleName:"Streaming quality"
                    options:[{key:"standard",label:"Standard",name:"qualityStandard"},
                             {key:"saver",label:"Data saver",name:"qualitySaver"}]
                    value:app.streamingQuality; onChosen:value=>app.streamingQuality=value
                }
                MSettingRow {opens:true;objectName:"cookieButton";text:app.cookies?"Replace cookies":"Import cookies";visible:settingsDialog.matches("YouTube cookies import replace remove sign in");onClicked:window.openFileDialog("cookies")}
                MSettingRow {text:"Remove cookies";visible:!!app.cookies && settingsDialog.matches("YouTube cookies import replace remove sign in");onClicked:app.clearCookies()}
                SungText { visible: settingsDialog.matches("YouTube cookies import replace remove sign in"); text: "Optional cookies.txt for tracks that require sign-in. Your library stays on this device."; wrapMode: Text.Wrap; Layout.fillWidth: true;Layout.minimumWidth:0; color: Theme.muted; font.pixelSize: Theme.bodyMedium }
                    }
                }
                ColumnLayout {
                    id: settingsGroup4; objectName:"settingsGroup4"
                    Layout.fillWidth:true;Layout.minimumWidth:0; spacing:12
                    property bool hasMatches: settingsDialog.matches("Pause history this session privacy") || settingsDialog.matches("Keep played songs offline storage disk") || settingsDialog.matches("Clear kept songs offline storage") || settingsDialog.matches("Clear artwork cache") || settingsDialog.matches("Clear history") || settingsDialog.matches("Export library backup") || settingsDialog.matches("Import library restore") || settingsDialog.matches("Sung version")
                    visible: settingsDialog.searchQuery.trim() ? hasMatches : settingsDialog.category===4
                    SungText {heading: true;text:"Privacy & data";font.pixelSize:Theme.titleLarge;font.weight:Font.Medium;Layout.bottomMargin:8}
                    ColumnLayout {id:options4;objectName:"settingsRows4";Layout.fillWidth:true;Layout.minimumWidth:0;spacing:12
                MSwitch { Layout.fillWidth:true;Layout.minimumWidth:0; visible: settingsDialog.matches("Pause history this session privacy"); objectName: "historyPauseSwitch"; text: "Pause history this session"; checked: app.historyPaused; onToggled: app.historyPaused=checked }
                MExposedDropdown { Layout.fillWidth:true;Layout.minimumWidth:0; objectName:"keepPlayedPicker"; fieldName:"keepPlayedButton"; implicitHeight:48; label:"Keep played songs"; visible:settingsDialog.matches("Keep played songs offline storage disk"); options:[{key:0,label:"Off",name:"keepPlayedOff"},{key:512,label:"512 MB",name:"keepPlayed512"},{key:1024,label:"1 GB",name:"keepPlayed1024"},{key:4096,label:"4 GB",name:"keepPlayed4096"},{key:16384,label:"16 GB",name:"keepPlayed16384"}]; value:app.keepPlayedMb; onChosen:key=>app.keepPlayedMb=key }
                MSettingRow {objectName:"clearKeptSongsButton";text:"Clear kept songs · "+app.keptSongsSize;visible:app.keptSongsSize.length>0 && settingsDialog.matches("Clear kept songs offline storage");onClicked:app.clearKeptSongs()}
                MSettingRow {objectName:"clearCacheButton";text:"Clear artwork cache";visible:settingsDialog.matches("Clear artwork cache");onClicked:app.clearCache()}
                MSettingRow {objectName:"clearHistoryButton";text:"Clear history";visible:settingsDialog.matches("Clear history");onClicked:app.clearHistory()}
                MSettingRow {opens:true;objectName:"exportLibraryButton";text:"Export library";visible:settingsDialog.matches("Export library backup");onClicked:window.openFileDialog("export")}
                MSettingRow {opens:true;objectName:"importLibraryButton";text:"Import library";visible:settingsDialog.matches("Import library restore");onClicked:window.openFileDialog("import")}
                SungText { objectName:"settingsVersion"; visible: settingsDialog.matches("Sung version"); text: "Sung " + Qt.application.version; color: Theme.muted; font.pixelSize: Theme.labelMedium; Layout.topMargin: 12 }
                    }
                }
                SungText { objectName: "settingsNoResults"; text: "No settings found"; color: Theme.muted; visible: {settingsDialog.searchQuery;return !!settingsDialog.searchQuery.trim() && !settingsGroup0.hasMatches && !settingsGroup1.hasMatches && !settingsGroup2.hasMatches && !settingsGroup3.hasMatches && !settingsGroup4.hasMatches;} }
            }
        }
        }
            }
        }
    }
    MDialog {
        id: trimDialog; objectName: "trimDialog"; anchors.centerIn: parent
        width: 380; title: "Adjust volume"; modal: true; standardButtons: Dialog.Close
        // The footer is MDialog's button bar, which arrives on first open.
        implicitHeight: header.implicitHeight+contentItem.implicitHeight+(footer ? footer.implicitHeight : 0)+topPadding+bottomPadding
        property var track: ({})
        property string trackId: ""
        property real trim: 0
        function adjust(item) {
            track=item || {};trackId=track.id || "";
            trim=app.trackTrim(trackId);open();
        }
        contentItem: ColumnLayout {
            spacing: 12
            SungText { objectName: "trimTitle"; text: trimDialog.track.title || ""; Layout.fillWidth: true; elide: Text.ElideRight; color: Theme.muted; font.pixelSize: Theme.bodyMedium }
            SungText { objectName: "trimValue"; text: (trimDialog.trim>0?"+":"")+Number(trimDialog.trim).toFixed(1)+" dB"; font.pixelSize: Theme.headlineLarge; Layout.alignment: Qt.AlignHCenter }
            SettingSlider {
                objectName: "trimSlider"; from: -12; to: 12; stepSize: 0.5
                value: trimDialog.trim; Layout.fillWidth: true
                onMoved: {trimDialog.trim=value;app.setTrackTrim(trimDialog.trackId,value);}
                Accessible.name: "Volume trim in decibels"
            }
            MButton { objectName: "trimReset"; text: "Reset"; Layout.alignment: Qt.AlignHCenter; enabled: !!trimDialog.trim; onClicked: {trimDialog.trim=0;app.setTrackTrim(trimDialog.trackId,0);} }
            SungText { Layout.fillWidth: true; text: "Kept for this song alone, on top of any automatic levelling."; color: Theme.muted; font.pixelSize: Theme.labelMedium; wrapMode: Text.Wrap }
        }
    }
    MMenu {
        id: sleepMenu
        MMenuItem { text: "Off"; onTriggered: app.setSleep(0) }
        MMenuItem { text: "End of track"; enabled: app.currentIndex>=0; onTriggered: app.setSleep(-1) }
        MMenuItem { objectName: "sleepQueueEnd"; text: "End of queue"; enabled: app.queue.count>0 && !app.shuffle && app.repeat===0; onTriggered: app.setSleep(-2) }
        MMenuItem { text: "15 minutes"; onTriggered: app.setSleep(15) }
        MMenuItem { text: "30 minutes"; onTriggered: app.setSleep(30) }
        MMenuItem { text: "60 minutes"; onTriggered: app.setSleep(60) }
        MMenuItem { text: "90 minutes"; onTriggered: app.setSleep(90) }
    }
    Rectangle {
        objectName: "errorBar"
        anchors.bottom: parent.bottom; anchors.bottomMargin: 140; anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(window.width-120,errorText.implicitWidth+(app.canRetry?180:100)); height: Math.min(150,errorText.implicitHeight+32)
        // Material has a pair of roles for this and nothing else in the app
        // wears them: an error is the one thing on screen that should not look
        // like everything else.
        radius: Theme.shapeLarge; color: Theme.errorContainer; visible: !!app.error; z: 50
        SungText { id: errorText; anchors.fill: parent; anchors.margins: 16; anchors.rightMargin: app.canRetry?140:58; text: app.error; wrapMode: Text.Wrap; elide: Text.ElideRight; maximumLineCount: 5; color: Theme.errorContainerText; font.pixelSize: Theme.bodyMedium }
        MButton { anchors.right: parent.right; anchors.rightMargin: 48; anchors.verticalCenter: parent.verticalCenter; text: "Retry"; visible: app.canRetry; ink: Theme.errorContainerText; onClicked: app.retry() }
        MButton { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; symbol: "close"; ink: Theme.errorContainerText; tip: "Dismiss error"; onClicked: app.dismissError() }
    }
    // Material's snackbar. It sits against the theme rather than in it, so the
    // container and everything on it come from the inverse roles, and it takes
    // the smallest corner on the scale rather than a panel's rounding. A
    // snackbar can also be pushed aside, which is what the drag below is for.
    Rectangle {
        anchors.bottom: parent.bottom; anchors.bottomMargin: 16 + window.bottomChrome; anchors.horizontalCenter: parent.horizontalCenter; z: 40
        Behavior on anchors.bottomMargin { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
        id: toastBar; objectName: "toastBar"
        width: Math.min(window.width-48,720,toastLabel.implicitWidth+(window.toastHasUndo?168:40))
        // Material brings a component in by expanding it away from the edge it
        // sits against rather than by scaling it, because scale reads as a
        // change of elevation. A snackbar sits at the bottom, so it grows
        // upwards, and what it holds is clipped by the frame on its way in.
        readonly property real restingHeight: toastLabel.lineCount>1 ? 68 : 48
        readonly property bool shown: window.toastPending && !app.error && !window.modalOpen
        height: shown ? restingHeight : 0
        clip: true
        Behavior on height { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
        radius: Theme.shapeExtraSmall; color: Theme.inverseSurface
        readonly property real shoveFade: 1-Math.min(0.95, Math.abs(toastShove.x)/(width/2))
        opacity: (shown ? 1 : 0)*shoveFade
        visible: height > 1
        Accessible.role: Accessible.AlertMessage; Accessible.name: window.toastText
        transform: Translate { id: toastShove }
        MElevation { anchors.fill: parent; radius: parent.radius; level: 3 }
        HoverHandler { id: toastHover }
        DragHandler {
            objectName: "toastSwipe"
            target: null; yAxis.enabled: false
            onActiveChanged: if(!active) {
                if(Math.abs(toastShove.x) > toastBar.width/3) window.toastPending=false
                toastReturn.restart()
            }
            onActiveTranslationChanged: if(active) toastShove.x = activeTranslation.x
        }
        NumberAnimation { id: toastReturn; target: toastShove; property: "x"; to: 0; duration: Theme.springFastEffectsMs }
        onVisibleChanged: if(!visible) toastShove.x = 0
        Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        MButton { id: toastUndo; objectName: "toastUndo"; anchors.right: toastDismiss.left; anchors.bottom: parent.bottom; anchors.bottomMargin: (toastBar.restingHeight-height)/2; text: "Undo"; ink: Theme.inversePrimary; visible: window.toastHasUndo; onClicked: app.undo() }
        MButton { id: toastDismiss; objectName: "toastDismiss"; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.bottomMargin: (toastBar.restingHeight-height)/2; symbol: "close"; tip: "Dismiss notification"; ink: Theme.inverseSurfaceText; visible: window.toastHasUndo; onClicked: window.toastPending=false }
        SungText { id: toastLabel; anchors.bottom: parent.bottom; anchors.bottomMargin: (toastBar.restingHeight-height)/2; anchors.left: parent.left; anchors.leftMargin: 16; anchors.right: parent.right; anchors.rightMargin: window.toastHasUndo?148:16; wrapMode: Text.Wrap; maximumLineCount: 2; text: window.toastText; font.pixelSize: Theme.bodyMedium; color: Theme.inverseSurfaceText }
    }
    Timer { id: toastTimer; interval: 5000; running: window.toastPending && !window.toastHasUndo && !app.error && !window.modalOpen && !toastHover.hovered && !toastUndo.activeFocus && !toastDismiss.activeFocus; onTriggered: window.toastPending=false }
    Connections { target: app; function onToast(message){window.toastPending=false;window.toastText=message;window.toastPending=true;} function onTrackChanged(){if(window.coverFlying)window.cancelCoverFlight();if(window.side==="lyrics" || window.compactMode || window.immersive)app.fetchLyrics();}
        function onViewAboutToChange(){window.rememberView();if(!window.albumOpening)window.cancelAlbumFlight();}
        function onCatalogChanged(){
            Qt.callLater(window.restoreView);if(window.albumFlying && !window.albumOpening && !app.busy)albumSettle.restart();
            if(!app.collection.query && app.collection.sortKey==="original")window.collectionTools=false;
            if(app.page==="home"){window.destination="home";window.localPlaylist="";searchField.clear();app.clearListPane();}
            else if(app.page==="server"){window.destination="library";window.libraryTab="server";window.localPlaylist="";searchField.suggestions=[];searchField.text=app.serverRequest.query || "";}
            else if(app.page==="library" || app.page==="local" || app.page==="local-album" || app.page==="local-artist"){
                window.destination="library";
                window.localPlaylist=app.page==="local"?app.libraryId:"";
                if(app.libraryId)window.libraryTab=app.page==="local"?"playlists":app.libraryId;
            }else{window.destination="search";window.localPlaylist="";}
            if(app.page==="search" && app.query){searchField.text=app.query;window.filter=app.searchFilter;}
        } }
    Component { id: miniComponent; MiniPlayer { onRestoreRequested: window.restorePlayer() } }
    MMenu {
        id: volumeStepMenu; objectName: "volumeStepMenu"
        Repeater {
            model: [1,2,5,10]
            MMenuItem { required property int modelData; objectName: "volumeStep_"+modelData; text: modelData+"%"; checkable: true; checked: app.volumeStep===modelData; onTriggered: app.volumeStep=modelData }
        }
    }
    SmartPlaylistDialog { id: smartDialog; anchors.centerIn: parent }
    TrackDetailsDialog { id: trackDetails; anchors.centerIn: parent }
    ShortcutHelp { id: shortcutHelp; anchors.centerIn: parent }
    MDialog {
        id: duplicateDialog; objectName: "duplicateDialog"; anchors.centerIn: parent
        width: Math.min(440,window.width-48); implicitHeight: implicitHeaderHeight+implicitFooterHeight+duplicateLabel.implicitHeight+48
        title: "Skip duplicates?"; modal: true; standardButtons: Dialog.Cancel | Dialog.Ok; acceptText: "Skip duplicates"
        property string playlistId: ""
        property var items: []
        property int duplicates: 0
        contentItem: SungText { id: duplicateLabel; text: duplicateDialog.duplicates+(duplicateDialog.duplicates===1?" song is already included.":" songs are already included."); wrapMode: Text.Wrap }
        onAccepted: {app.addItemsToPlaylist(playlistId,items);items=[];playlistId="";}
        onRejected: {items=[];playlistId="";}
    }
    MDialog {
        id: audioDeviceDialog; objectName: "audioDeviceDialog"; anchors.centerIn: parent
        width: 430; height: Math.min(window.height-80,Math.min(440,160+app.audioDevices.length*56))
        modal: true; title: "Audio output"; standardButtons: Dialog.Close
        property bool contentReady: false
        onAboutToShow: contentReady=true
        contentItem: Loader {
            active: audioDeviceDialog.contentReady
            sourceComponent: Component {
        ListView {
            anchors.fill: parent; clip: true; spacing: 4; model: app.audioDevices
            ScrollBar.vertical: ScrollBar {}
            delegate: MButton {
                required property var modelData
                objectName: "audioDeviceChoice"; width: ListView.view.width; height: 52
                text: modelData.name; leftAligned: true; selected: app.audioDeviceId===modelData.id
                tip: modelData.name; onClicked: {app.audioDeviceId=modelData.id;audioDeviceDialog.close();}
            }
        }
            }
        }
    }

    MDialog {
        id: rateDialog; objectName: "rateDialog"; anchors.centerIn: parent
        width: 380; height: Math.min(window.height-64,420); title: "Playback speed"; modal: true; standardButtons: Dialog.Close
        property bool contentReady: false
        onAboutToShow: contentReady=true
        contentItem: Loader {
            active: rateDialog.contentReady
            sourceComponent: Component {
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            SungText { text: Number(app.playbackRate.toFixed(2))+"×"; font.pixelSize: Theme.headlineLarge; Layout.alignment: Qt.AlignHCenter }
            SettingSlider { objectName: "playbackRateSlider"; from: 0.5; to: 2; stepSize: 0.05; value: app.playbackRate; Layout.fillWidth: true; onMoved: app.playbackRate=value; Accessible.name: "Playback speed" }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter; spacing: 8
                Repeater { model: [0.75,1,1.5]; MButton { required property real modelData; text: modelData+"×"; selected: Math.abs(app.playbackRate-modelData)<0.01; implicitHeight: 40; onClicked: app.playbackRate=modelData } }
            }
            MSwitch { text: "Preserve pitch"; visible: app.pitchAdjustable; checked: app.preservePitch; onToggled: app.preservePitch=checked; Layout.fillWidth: true }
        }
            }
        }
    }
    MDialog {
        id: lyricTimingDialog; objectName: "lyricTimingDialog"; anchors.centerIn: parent
        width: 380; height: Math.min(window.height-64,600); title: "Lyrics"; modal: true; standardButtons: Dialog.Close
        property bool contentReady: false
        onAboutToShow: contentReady=true
        contentItem: Loader {
            active: lyricTimingDialog.contentReady
            sourceComponent: Component {
        ScrollView {
            id: lyricControls; anchors.fill: parent; contentWidth: availableWidth; clip: true
        ColumnLayout {
            width: lyricControls.availableWidth; spacing: 16
            RowLayout {
                Layout.fillWidth: true
                SungText { text: "Text size"; Layout.fillWidth: true }
                MButton { objectName: "lyricsSizeSmaller"; text: "A−"; tip: "Smaller lyrics"; enabled: app.lyricTextSize>20; onClicked: app.lyricTextSize-- }
                MButton { objectName: "lyricsSizeReset"; text: "Reset"; tip: "Default lyric size"; enabled: app.lyricTextSize!==25; onClicked: app.lyricTextSize=25 }
                MButton { objectName: "lyricsSizeLarger"; text: "A+"; tip: "Larger lyrics"; enabled: app.lyricTextSize<32; onClicked: app.lyricTextSize++ }
            }
            SungText { text: app.lyricsSource; visible: text.length>0; color: Theme.muted }
            RowLayout {
                MButton { objectName: "importLyricsButton"; text: "Import LRC"; enabled: !!app.current.id; onClicked: window.openFileDialog("lyrics") }
                MButton { text: "Use automatic"; visible: app.lyricsSource==="Imported LRC"; onClicked: app.resetLyrics() }
            }
            MSwitch { objectName: "completedLyricsSwitch"; text: "Show completed lines"; checked: app.keepCompletedLyrics; onToggled: app.keepCompletedLyrics=checked; Layout.fillWidth: true }
            SungText { text: (app.lyricOffset>0?"+":"")+(app.lyricOffset/1000).toFixed(2)+" s"; font.pixelSize: Theme.headlineMedium; Layout.alignment: Qt.AlignHCenter }
            SettingSlider { objectName: "lyricTimingSlider"; from: -10000; to: 10000; stepSize: 250; value: app.lyricOffset; Layout.fillWidth: true; onMoved: app.lyricOffset=value; Accessible.name: "Lyric timing; positive shows lyrics earlier" }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter; spacing: 8
                MButton { objectName: "lyricsLaterButton"; text: "Later"; enabled: app.lyricOffset> -10000; onClicked: app.lyricOffset-=250 }
                MButton { text: "Reset"; tonal: true; enabled: app.lyricOffset!==0; onClicked: app.lyricOffset=0 }
                MButton { objectName: "lyricsEarlierButton"; text: "Earlier"; enabled: app.lyricOffset<10000; onClicked: app.lyricOffset+=250 }
            }
        }
        }
            }
        }
    }

}
