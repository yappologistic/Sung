import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore

ApplicationWindow {
    id: window
    objectName: "sungWindow"
    visible: true
    width: 1180; height: 800
    minimumWidth: 780; minimumHeight: 580
    font.family: Theme.fontFamily
    title: app.current.title ? app.current.title + " · Sung" : "Sung"
    color: Theme.background
    property string destination: "home"
    property string filter: "songs"
    property string libraryTab: "favorites"
    property string localPlaylist: ""
    property string side: ""
    property bool collectionTools: false
    property var miniPlayer: null
    readonly property bool uiActive: (visible && visibility!==Window.Minimized) || (miniPlayer!==null && miniPlayer.visible && miniPlayer.visibility!==Window.Minimized)
    onUiActiveChanged: app.setUiActive(uiActive)
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
    property var homeSections: app.page==="home" && app.pins.length ? [{title:"Pinned",items:app.pins}].concat(app.sections) : app.sections
    property bool hasSongCollection: app.sections.length===0 && app.results.count>0 && !!(app.results.get(0).videoId || app.results.get(0).localPath)
    property var bulkView: null
    property var batchItems: []
    property var menuItem: ({})
    property int menuIndex: -1
    property bool menuQueue: false
    property string playlistAction: "create"
    property string editPlaylistId: ""
    property string toastText: ""
    property bool searchFocused: searchField.activeFocus || (window.activeFocusItem && window.activeFocusItem.objectName==="lyricSearchField")
    property bool modalOpen: musicFoldersDialog.opened || cleanupDialog.opened || bulkActions.opened || volumeStepMenu.opened || rateDialog.opened || lyricTimingDialog.opened || settingsDialog.opened || playlistDialog.opened || addPlaylistDialog.opened || deletePlaylistDialog.opened || actions.opened || playlistActions.opened || sleepMenu.opened || (fileDialogs!==null && fileDialogs.visible) || audioDeviceDialog.opened || collectionSortMenu.opened
    property bool sliderFocused: window.activeFocusItem && window.activeFocusItem.objectName === "seekBar"
    function selectedView() {var item=window.activeFocusItem;while(item){if(item.sourceRows!==undefined)return item;item=item.parent;}return tracks;}
    function addBatch(view) {batchItems=view.selection.items();addPlaylistDialog.open();}
    QtObject {
        id: trackDrag
        property var owner: null
        property var rows: []
        property var items: []
        function begin(view,indices,songs,point) {owner=view;rows=indices;items=songs;dragGhost.x=point.x;dragGhost.y=point.y;dragGhost.Drag.active=true;}
        function move(point) {dragGhost.x=point.x;dragGhost.y=point.y;}
        function finish() {dragGhost.Drag.drop();cancel();}
        function cancel() {dragGhost.Drag.cancel();owner=null;rows=[];items=[];}
    }
    Rectangle {
        id: dragGhost; parent: window.contentItem; z: 1000
        width: 130; height: 40; radius: 16; color: Theme.primary
        visible: Drag.active
        Drag.source: trackDrag; Drag.keys: ["sung-tracks"]; Drag.hotSpot.x: 0; Drag.hotSpot.y: 0
        SungText { anchors.centerIn: parent; text: trackDrag.items.length+" selected"; color: Theme.primaryText; font.pixelSize: 13 }
    }
    function countText(n) { return n + (n === 1 ? " track" : " tracks"); }
    property real previousVolume: 0.65
    function toggleMute() { if(app.volume>0){previousVolume=app.volume;app.volume=0;}else app.volume=previousVolume; }
    Settings { id: geometry; category: "Window"; property int width: 1180; property int height: 800 }
    Component.onCompleted: { windowResources.manage(window);width=geometry.width;height=geometry.height;geometryReady=true;app.setUiActive(uiActive); }
    onWidthChanged: {if(geometryReady && !immersive && visibility===Window.Windowed)geometry.width=width;}
    onHeightChanged: {if(geometryReady && !immersive && visibility===Window.Windowed)geometry.height=height;}
    function toggleImmersive() {
        if(immersive){
            if(wasMaximized)showMaximized();else {showNormal();width=geometry.width;height=geometry.height;}
            immersive=false;content.forceActiveFocus();
        }else if(app.currentIndex>=0){
            wasMaximized=visibility===Window.Maximized;immersive=true;showFullScreen();app.fetchLyrics();
        }
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
    function focusSearch() { if(immersive)toggleImmersive();side="";app.startSearch();destination="search";searchField.forceActiveFocus();searchField.selectAll(); }
    function chooseLibrary(kind) { destination="library";libraryTab=kind;localPlaylist="";side="";app.library(kind); }
    function activateSide(which) { side=side===which?"":which;if(side==="lyrics")app.fetchLyrics(); }

    Shortcut { sequence: "Ctrl+M"; enabled: !window.modalOpen; onActivated: window.openMiniPlayer() }
    Shortcut { sequence: "Ctrl+K"; enabled: !window.modalOpen; onActivated: window.focusSearch() }
    Shortcut { sequence: "Ctrl+F"; enabled: !window.modalOpen; onActivated: window.focusSearch() }
    Shortcut { sequence: "Space"; enabled: !window.searchFocused && !window.modalOpen && (!window.activeFocusItem || window.activeFocusItem===content); onActivated: app.toggle() }
    Shortcut { sequence: "Ctrl+Right"; enabled: !window.modalOpen; onActivated: app.next() }
    Shortcut { sequence: "Ctrl+Left"; enabled: !window.modalOpen; onActivated: app.previous() }
    Shortcut { sequence: "Right"; enabled: !window.searchFocused && !collectionSearch.activeFocus && !window.modalOpen && !window.sliderFocused; onActivated: app.seek(app.position+10000) }
    Shortcut { sequence: "Left"; enabled: !window.searchFocused && !collectionSearch.activeFocus && !window.modalOpen && !window.sliderFocused; onActivated: app.seek(app.position-10000) }
    Shortcut { sequence: "Ctrl+L"; enabled: !window.modalOpen && !window.immersive; onActivated: window.activateSide("queue") }
    Shortcut { sequence: "Ctrl+Y"; enabled: !window.modalOpen && !window.immersive; onActivated: window.activateSide("lyrics") }
    Shortcut { sequence: "Alt+Left"; enabled: !window.modalOpen && !window.immersive; onActivated: app.back() }
    Shortcut { sequence: "Escape"; enabled: !window.modalOpen && !(window.activeFocusItem && window.activeFocusItem.objectName==="lyricSearchField"); onActivated: { if(trackDrag.owner)trackDrag.cancel();else if(window.selectedView().selection.count)window.selectedView().selection.clear();else if(window.immersive)window.toggleImmersive();else {window.side="";content.forceActiveFocus();} } }
    Shortcut { sequence: "Ctrl+Q"; onActivated: window.close() }

    Shortcut { sequence: "F11"; enabled: !window.modalOpen; onActivated: window.toggleImmersive() }
    Loader { anchors.fill: parent; active: window.immersive; sourceComponent: Component { ImmersivePlayer { onExitRequested: window.toggleImmersive(); onSpeedRequested: rateDialog.open(); onTimingRequested: lyricTimingDialog.open() } } }
    RowLayout {
        visible: !window.compactMode && !window.immersive
        anchors.fill: parent; spacing: 0
        ColumnLayout {
            objectName: "navigationRail"; Layout.preferredWidth: 88; Layout.minimumWidth: 88; Layout.maximumWidth: 88; Layout.fillHeight: true; Layout.topMargin: 24; spacing: 12
            Repeater {
                model: [{key:"home",icon:"home",label:"Home"},{key:"search",icon:"search",label:"Search"},{key:"library",icon:"library",label:"Library"}]
                Item {
                    required property var modelData
                    Layout.fillWidth: true; Layout.preferredHeight: 68
                    MButton {
                        id: navButton; objectName: "nav_"+modelData.key
                        anchors.horizontalCenter: parent.horizontalCenter; width: 64; height: 40
                        symbol: modelData.icon; tip: modelData.label; selected: window.destination===modelData.key
                        onClicked: {window.destination=modelData.key;if(modelData.key==="home")app.home();else if(modelData.key==="search")window.focusSearch();else window.chooseLibrary("favorites");}
                    }
                    SungText { anchors.top: navButton.bottom; anchors.topMargin: 2; anchors.horizontalCenter: parent.horizontalCenter; text: modelData.label; font.pixelSize: 12; font.weight: window.destination===modelData.key?Font.DemiBold:Font.Medium; color: window.destination===modelData.key?Theme.primary:Theme.muted }
                }
            }
            Item { Layout.fillHeight: true }
            MButton { objectName: "miniPlayerButton"; symbol: "mini"; tip: "Mini player · Ctrl+M"; Layout.alignment: Qt.AlignHCenter; onClicked: window.openMiniPlayer() }
            MButton { objectName: "settingsButton"; symbol: "settings"; tip: "Settings"; Layout.alignment: Qt.AlignHCenter; Layout.bottomMargin: 20; onClicked: settingsDialog.open() }
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.rightMargin: 16; Layout.topMargin: 16; Layout.bottomMargin: 16; spacing: 12
            RowLayout {
                Layout.fillWidth: true; spacing: 12
                MButton { symbol: "back"; tip: "Back"; enabled: app.canBack; onClicked: app.back() }
                Rectangle {
                    id: searchBox; z: 20
                    Layout.fillWidth: true; Layout.preferredHeight: 56; Layout.maximumWidth: 640
                    color: searchField.activeFocus ? Theme.high : Theme.container; radius: 28
                    border.width: searchField.activeFocus?2:0; border.color: Theme.primary
                    Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 18; anchors.rightMargin: 8; spacing: 12
                        Icon { name: "search"; ink: Theme.muted }
                        TextField {
                            font.family: Theme.fontFamily; id: searchField; objectName: "searchField"; Layout.fillWidth: true; Layout.fillHeight: true
                            placeholderText: "Search music"; placeholderTextColor: Theme.muted
                            color: Theme.text; selectionColor: Theme.primaryContainer; selectedTextColor: Theme.text
                            font.pixelSize: 16; background: null; selectByMouse: true
                            Accessible.name: "Search songs, albums, artists, playlists, or paste a YouTube link"
                            property var suggestions: []
                            property int highlighted: -1
                            property bool dismissed: false
                            function updateSuggestions() {
                                highlighted=-1;
                                suggestions=text.trim()?app.localMatches(text):app.recentSearches.map(q=>({title:q,recent:true}));
                            }
                            function submit() {dismissed=true;window.destination="search";window.localPlaylist="";app.search(text,window.filter);content.forceActiveFocus();}
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
                        y: searchBox.height+6; width: searchBox.width; height: Math.min(window.height-220,suggestionList.contentHeight+16)
                        visible: searchField.activeFocus && !searchField.dismissed && searchField.suggestions.length>0
                        focus: false; padding: 8; closePolicy: Popup.CloseOnPressOutside
                        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: app.motion?Theme.fast:0; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
                        exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: app.motion?Theme.fast:0; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
                        onClosed: searchField.dismissed=true
                        background: Rectangle { radius: 20; color: Theme.high; border.width: 1; border.color: Theme.outline }
                        contentItem: ListView {
                            id: suggestionList; objectName: "suggestionList"; clip: true; model: searchField.suggestions; currentIndex: searchField.highlighted
                            ScrollBar.vertical: ScrollBar {}
                            delegate: Item {
                                required property var modelData; required property int index
                                width: suggestionList.width; height: 56
                                Rectangle { anchors.fill: parent; radius: 12; color: index===searchField.highlighted?Theme.primaryContainer:"transparent" }
                                AbstractButton {
                                    anchors.fill: parent; anchors.rightMargin: modelData.recent?40:0; focusPolicy: Qt.NoFocus
                                    leftPadding: 12; rightPadding: 12
                                    Accessible.name: modelData.title+(modelData.origin?", "+modelData.origin:"")
                                    onClicked: searchField.choose(index)
                                    contentItem: Column {
                                        width: parent.width; anchors.verticalCenter: parent.verticalCenter; spacing: 2
                                        SungText { width: parent.width; text: modelData.title; font.pixelSize: 14 }
                                        SungText { width: parent.width; visible: !!modelData.origin; text: (modelData.artist?modelData.artist+" · ":"")+(modelData.origin||""); font.pixelSize: 12; color: Theme.muted }
                                    }
                                }
                                MButton { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; implicitWidth: 36; implicitHeight: 36; symbol: "close"; tip: "Remove recent search"; focusPolicy: Qt.NoFocus; visible: modelData.recent===true; onClicked: app.removeRecentSearch(modelData.title) }
                            }
                        }
                    }
                }
                Item { Layout.fillWidth: true }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 12
                Rectangle {
                    id: content
                    visible: !(window.width < 1000 && window.side)
                    Layout.fillWidth: true; Layout.fillHeight: true
                    radius: 28; color: Theme.surface; clip: true
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 28; spacing: 18
                        RowLayout {
                            Layout.fillWidth: true
                            Artwork { visible: !!app.cover; url: app.cover; Layout.preferredWidth: 76; Layout.preferredHeight: 76; radius: app.page==="artist" ? 38 : 18; pixels: 180 }
                            SungText { text: window.destination==="library"&&window.libraryTab==="playlists"&&!window.localPlaylist ? "Playlists" : app.title; font.pixelSize: app.page==="home"?40:28; font.weight: Font.Medium; Layout.fillWidth: true; wrapMode: Text.Wrap; maximumLineCount: 2 }
                            MButton { objectName: "pinCollectionButton"; symbol: "pin"; visible: !!app.collectionItem.id; selected: {app.pins;return app.isPinned(app.collectionItem);} tip: selected?"Unpin from Home":"Pin to Home"; onClicked: app.togglePin(app.collectionItem) }
                            MButton { symbol: "refresh"; tip: "Refresh"; visible: app.page!=="library"&&app.page!=="local"; enabled: !app.busy; onClicked: app.refresh() }
                            MButton { objectName: "musicFoldersButton"; text: "Folders"; tip: "Manage music folders"; visible: window.destination==="library" && window.libraryTab==="files"; onClicked: musicFoldersDialog.open() }
                            MButton { objectName: "rescanFoldersButton"; symbol: "refresh"; tip: "Rescan music folders"; visible: window.destination==="library" && window.libraryTab==="files" && app.musicFolders.length>0; enabled: !app.importingLocal; onClicked: app.rescanMusicFolders() }
                            MButton { objectName: "playlistCleanupButton"; text: "Clean up"; visible: !!window.localPlaylist; onClicked: window.openCleanup(window.localPlaylist) }
                            MButton { objectName: "addLocalFilesButton"; symbol: "plus"; tip: "Add local audio files"; visible: window.destination==="library" && window.libraryTab==="files"; enabled: !app.importingLocal; tonal: true; onClicked: window.openFileDialog("audio") }
                            MButton { objectName: "newPlaylistButton"; symbol: "plus"; tip: "New playlist"; visible: window.destination==="library" && window.libraryTab==="playlists" && !window.localPlaylist; tonal: true; onClicked: {window.playlistAction="create";playlistName.clear();playlistDialog.open();} }
                        }
                        RowLayout {
                            visible: app.page==="search"; Layout.fillWidth: true; spacing: 8
                            Repeater {
                                model: [{label:"Songs",key:"songs"},{label:"Albums",key:"albums"},{label:"Artists",key:"artists"},{label:"Playlists",key:"playlists"},{label:"Videos",key:"videos"}]
                                MButton { required property var modelData; objectName: "filter_"+modelData.key; text: modelData.label; selected: window.filter===modelData.key; implicitHeight: 36; onClicked: {window.filter=modelData.key;if(searchField.text.trim())app.search(searchField.text,window.filter);} }
                            }
                            Item { Layout.fillWidth: true }
                        }
                        RowLayout {
                            visible: app.importingLocal; Layout.fillWidth: true
                            SungText { text: app.localImportStatus; color: Theme.muted; Layout.fillWidth: true }
                            MButton { text: "Cancel import"; onClicked: app.cancelLocalImport() }
                        }
                        Flow {
                            visible: window.destination==="library"; Layout.fillWidth: true; spacing: 8
                            MButton { objectName: "likedTab"; text: "Liked songs"; selected: window.libraryTab==="favorites"; implicitHeight: 36; onClicked: window.chooseLibrary("favorites") }
                            MButton { objectName: "playlistsTab"; text: "Playlists"; selected: window.libraryTab==="playlists"; implicitHeight: 36; onClicked: window.chooseLibrary("playlists") }
                            MButton { objectName: "localFilesTab"; text: "Local files"; selected: window.libraryTab==="files"; implicitHeight: 36; onClicked: window.chooseLibrary("files") }
                            MButton { objectName: "mixesTab"; text: "Mixes"; selected: window.libraryTab==="mixes" || window.libraryTab.startsWith("mix-"); implicitHeight: 36; onClicked: window.chooseLibrary("mixes") }
                            MButton { objectName: "historyTab"; text: "History"; selected: window.libraryTab==="history"; implicitHeight: 36; onClicked: window.chooseLibrary("history") }
                        }
                        RowLayout {
                            visible: app.page==="home" && window.destination!=="library"; Layout.fillWidth: true; spacing: 8
                            Repeater {
                                model: [{title:"Feel good",q:"feel good songs"},{title:"Focus",q:"instrumental focus"},{title:"Unwind",q:"chill evening"},{title:"Energize",q:"workout energy"}]
                                MButton { required property var modelData; text: modelData.title; tonal: true; implicitHeight: 40; onClicked: {searchField.text=modelData.q;window.destination="search";app.search(modelData.q,"songs");} }
                            }
                        }
                        RowLayout {
                            visible: tracks.selection.count===0 && app.results.count>0 && app.sections.length===0 && !(window.destination==="library" && window.libraryTab==="playlists" && !window.localPlaylist) && (app.results.get(0).videoId || app.results.get(0).localPath)
                            Layout.fillWidth: true; spacing: 10
                            MButton { text: "Play"; symbol: "play"; filled: true; enabled: app.collection.count>0; onClicked: app.playCollection(0) }
                            MButton { symbol: "queue"; tip: "Add displayed songs to queue"; tonal: true; enabled: app.collection.count>0; onClicked: app.enqueueCollection() }
                            Item { Layout.fillWidth: true }
                            MButton { objectName: "collectionToolsButton"; symbol: "filter"; tip: "Find and sort songs"; selected: window.collectionTools || !!app.collection.query || app.collection.sortKey!=="original"; onClicked: {window.collectionTools=!window.collectionTools;if(window.collectionTools)Qt.callLater(()=>collectionSearch.forceActiveFocus());} }
                            SungText { text: app.collection.query ? app.collection.count+" / "+app.results.count : window.countText(app.results.count); color: Theme.muted; font.pixelSize: 12 }
                        }
                        RowLayout {
                            visible: window.hasSongCollection && window.collectionTools
                            Layout.fillWidth: true; spacing: 8
                            TextField {
                                id: collectionSearch; objectName: "collectionSearch"; Layout.fillWidth: true; implicitHeight: 44
                                font.family: Theme.fontFamily; font.pixelSize: 14; color: Theme.text
                                placeholderText: "Find in this list"; placeholderTextColor: Theme.muted
                                selectionColor: Theme.primaryContainer; selectedTextColor: Theme.text
                                leftPadding: 14; rightPadding: 14; selectByMouse: true
                                text: app.collection.query
                                onTextEdited: app.collection.query=text
                                onAccepted: {if(app.collection.count>0)app.playCollection(0);content.forceActiveFocus();}
                                background: Rectangle { radius: 16; color: Theme.container; border.width: collectionSearch.activeFocus?2:0; border.color: Theme.primary }
                                Accessible.name: "Find songs in this list"
                            }
                            MButton { symbol: "close"; tip: "Clear list filter"; visible: !!app.collection.query; onClicked: app.collection.query="" }
                            MButton { objectName: "collectionSortButton"; symbol: "sort"; tip: "Sort songs"; text: app.collection.sortKey==="original"?"Order":app.collection.sortKey==="title"?"Title":app.collection.sortKey==="artist"?"Artist":"Duration"; tonal: true; onClicked: collectionSortMenu.popup(this,width-collectionSortMenu.width,height+4) }
                        }
                        SelectionBar { Layout.fillWidth: true; view: tracks; canRemove: !!window.localPlaylist }
                        Item {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            BusyIndicator { anchors.centerIn: parent; running: app.busy && app.results.count===0 && window.homeSections.length===0; visible: running; palette.dark: Theme.primary }
                            ListView {
                                id: shelves; anchors.fill: parent
                                visible: window.homeSections.length>0 && !(window.destination==="library"&&window.libraryTab==="playlists")
                                clip: true; spacing: 26; reuseItems: true; cacheBuffer: 0
                                model: window.homeSections; boundsBehavior: Flickable.StopAtBounds
                                ScrollBar.vertical: ScrollBar {}
                                delegate: ColumnLayout {
                                    required property var modelData
                                    width: shelves.width; height: implicitHeight; spacing: 12
                                    RowLayout {
                                        Layout.fillWidth: true
                                        SungText { text: modelData.title; font.pixelSize: 22; font.weight: Font.Medium; Layout.fillWidth: true }
                                        MButton { symbol: "back"; tip: "Previous covers"; enabled: !shelf.atXBeginning; implicitWidth: 40; implicitHeight: 40; onClicked: shelf.flick(1300,0) }
                                        MButton { symbol: "chevron"; tip: "More covers"; enabled: !shelf.atXEnd; implicitWidth: 40; implicitHeight: 40; onClicked: shelf.flick(-1300,0) }
                                    }
                                    ListView {
                                        id: shelf
                                        Layout.fillWidth: true; Layout.preferredHeight: cellWidth+68
                                        property real cellWidth: Math.max(142,Math.min(190,(width-40)/3.35))
                                        orientation: ListView.Horizontal; spacing: 20; clip: true; boundsBehavior: Flickable.StopAtBounds
                                        model: modelData.items; reuseItems: true; cacheBuffer: 0
                                        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                                        delegate: ArtCard { required property var modelData; width: ListView.view.cellWidth; track: modelData }
                                    }
                                }
                            }
                            TrackList {
                                id: tracks; objectName: "tracksView"; anchors.fill: parent; clip: true; spacing: 4
                                visible: app.sections.length===0 && !(window.destination==="library"&&window.libraryTab==="playlists"&&!window.localPlaylist)
                                model: app.collection; reuseItems: true; cacheBuffer: 100; boundsBehavior: Flickable.StopAtBounds
                                queueMode: false; reorderEnabled: app.page==="local" && app.collection.sortKey==="original" && !app.collection.query
                                playlistId: window.localPlaylist; dragHub: trackDrag
                                onActivate: (row,item)=>{if(item.videoId || item.localPath)app.playCollection(row);else app.open(item);}
                                onMenuRequested: (item,index,anchor)=>window.trackMenu(item,index,anchor,false)
                                onRemoveSelected: {if(window.localPlaylist)app.removePlaylistRows(window.localPlaylist,sourceRows());}
                                onAddSelected: window.addBatch(tracks)
                                footer: Item {
                                    width: tracks.width; height: app.canMore?64:0
                                    MButton { anchors.centerIn: parent; text: app.busy ? "Loading…" : "Load more"; enabled: !app.busy; tonal: true; visible: app.canMore; onClicked: app.more() }
                                }
                                SungText { anchors.centerIn: parent; visible: app.collection.count===0 && !app.busy; text: app.collection.query ? "No matching songs" : app.error ? "Couldn’t load music" : app.page==="library" ? (window.libraryTab==="files"?"Add your music with +":window.libraryTab==="history"?"Nothing played yet":window.libraryTab.startsWith("mix-")?"No matching songs yet":"No liked songs yet") : app.page==="local" ? "No songs yet" : app.page==="search" && !app.query ? "Search music" : "No results"; color: Theme.muted; font.pixelSize: 18 }
                            }
                            ListView {
                                id: localPlaylists; anchors.fill: parent; clip: true; spacing: 8
                                visible: window.destination==="library"&&window.libraryTab==="playlists"&&!window.localPlaylist
                                model: app.playlists
                                delegate: Rectangle {
                                    required property var modelData; width: localPlaylists.width; height: 76; radius: 18; color: Theme.container
                                    RowLayout {
                                        anchors.fill: parent; anchors.margins: 12; spacing: 12
                                        MButton { symbol: "queue"; tonal: true; tip: "Open playlist"; onClicked: {window.localPlaylist=modelData.id;app.openPlaylist(modelData.id);} }
                                        AbstractButton { Layout.fillWidth: true; Layout.fillHeight: true; focusPolicy: Qt.StrongFocus; Accessible.name: modelData.title; background: Rectangle { color: "transparent"; radius: 8; border.width: parent.activeFocus?2:0; border.color: Theme.primary } onClicked: {window.localPlaylist=modelData.id;app.openPlaylist(modelData.id);} contentItem: Column { spacing: 4; SungText { text: modelData.title; font.pixelSize: 16; width: parent.width } SungText { text: window.countText(modelData.count); color: Theme.muted; font.pixelSize: 12 } } }
                                        MButton { symbol: "more"; tip: "Playlist actions"; onClicked: {window.editPlaylistId=modelData.id;playlistName.text=modelData.title;playlistActions.popup(this,width-playlistActions.width,height+4);} }
                                    }
                                }
                                Column {
                                    anchors.centerIn: parent; spacing: 18; visible: app.playlists.length===0
                                    SungText { anchors.horizontalCenter: parent.horizontalCenter; text: "No playlists yet"; color: Theme.muted; font.pixelSize: 18 }
                                    MButton { anchors.horizontalCenter: parent.horizontalCenter; text: "New playlist"; symbol: "plus"; filled: true; onClicked: {window.playlistAction="create";playlistName.clear();playlistDialog.open();} }
                                }
                            }
                        }
                    }
                    Connections { target: app; function onCatalogChanged(){ if(!app.busy)entrance.restart(); } }
                    NumberAnimation { id: entrance; target: content; property: "opacity"; from: app.motion?0.5:1; to: 1; duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve }
                }
                Rectangle {
                    id: sidePanel; objectName: "sidePanel"
                    Layout.fillWidth: window.width < 1000
                    property real revealWidth: window.side ? (window.width < 1000 ? window.width-104 : 360) : 0
                    Layout.preferredWidth: Math.max(0,revealWidth)
                    Layout.fillHeight: true; radius: 28; color: Theme.surface
                    visible: Layout.preferredWidth>1; clip: true
                    Behavior on revealWidth { NumberAnimation { duration: app.motion?350:0; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 18; spacing: 14
                        RowLayout {
                            Layout.fillWidth: true
                            SungText { text: window.side==="queue"?"Up next":window.side==="lyrics"?"Lyrics":"Now playing"; font.pixelSize: 20; font.weight: Font.Medium; Layout.fillWidth: true }
                            MButton { objectName: "lyricSearchButton"; symbol: "search"; tip: "Find in lyrics"; visible: window.side==="lyrics"; enabled: !!app.lyrics; onClicked: {if(sideLoader.item)sideLoader.item.openSearch();} }
                            MButton { objectName: "lyricTimingButton"; symbol: "settings"; tip: "Lyric timing · saved for this song"; visible: window.side==="lyrics" && !!app.current.id; onClicked: lyricTimingDialog.open() }
                            MButton { objectName: "immersiveButton"; symbol: "expand"; tip: "Immersive player · F11"; visible: window.side!=="queue"; enabled: app.currentIndex>=0; onClicked: window.toggleImmersive() }
                            MButton { symbol: "close"; tip: "Close panel"; onClicked: window.side="" }
                        }
                        Loader {
                            id: sideLoader
                            Layout.fillWidth: true; Layout.fillHeight: true
                            active: sidePanel.visible && !!window.side
                            sourceComponent: window.side==="queue"?queuePanel:window.side==="lyrics"?lyricsPanel:nowPanel
                        }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 112; color: Theme.container; radius: 28
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 16
                    AbstractButton { objectName: "nowButton"; Layout.preferredWidth: 64; Layout.preferredHeight: 64; enabled: app.currentIndex>=0; focusPolicy: Qt.StrongFocus; Accessible.name: "Now playing"; onClicked: window.activateSide("now")
                        contentItem: Artwork { url: app.current.art || ""; radius: 12; pixels: 150 }
                        background: Rectangle { anchors.fill: parent; anchors.margins: -3; color: "transparent"; radius: 15; border.width: parent.activeFocus?2:0; border.color: Theme.primary }
                    }
                    ColumnLayout {
                        Layout.preferredWidth: Math.max(100,Math.min(220,window.width*0.17)); spacing: 6
                        AbstractButton { Layout.fillWidth: true; implicitHeight: 24; focusPolicy: Qt.StrongFocus; enabled: app.currentIndex>=0; Accessible.name: "Now playing: " + (app.current.title || "Nothing playing"); onClicked: window.activateSide("now"); contentItem: SungText { text: app.current.title || "Nothing playing"; font.pixelSize: 15; font.weight: Font.DemiBold } background: Rectangle { color: "transparent"; radius: 4; border.width: parent.activeFocus?1:0; border.color: Theme.primary } }
                        AbstractButton { Layout.fillWidth: true; implicitHeight: 24; focusPolicy: Qt.StrongFocus; enabled: !!app.current.artistId; Accessible.name: "Go to " + (app.current.artist || "artist"); onClicked: app.open({kind:"artist",browseId:app.current.artistId,title:app.current.artist}); contentItem: SungText { text: app.current.artist || ""; color: Theme.muted; font.pixelSize: 13 } background: Rectangle { color: "transparent"; radius: 4; border.width: parent.activeFocus?1:0; border.color: Theme.primary } }
                    }
                    MButton { symbol: "heart"; tip: app.liked?"Unlike":"Like"; selected: app.liked; enabled: app.currentIndex>=0; visible: window.width>=1050; onClicked: app.toggleLike(app.current) }
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.maximumWidth: 520; spacing: 0
                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter; spacing: 6
                            MButton { symbol: "shuffle"; tip: "Shuffle"; selected: app.shuffle; onClicked: app.shuffle=!app.shuffle; visible: window.width>=980 }
                            MButton { symbol: "previous"; tip: "Previous · Ctrl+←"; enabled: app.queue.count>0; onClicked: app.previous() }
                            MButton { objectName: "playButton"; symbol: app.playing||app.resolving?"pause":"play"; tip: app.playing||app.resolving?"Pause · Space":"Play · Space"; filled: true; implicitWidth: 64; implicitHeight: 48; enabled: app.queue.count>0; onClicked: app.toggle(); BusyIndicator { anchors.centerIn: parent; width: 40; height: 40; running: app.resolving; visible: running } }
                            MButton { symbol: "next"; tip: "Next · Ctrl+→"; enabled: app.queue.count>0; onClicked: app.next() }
                            MButton { symbol: app.repeat===2?"repeat_one":"repeat"; tip: app.repeat===0?"Repeat off":app.repeat===1?"Repeat queue":"Repeat song"; selected: app.repeat>0; onClicked: app.repeat=(app.repeat+1)%3; visible: window.width>=980 }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 10
                            SungText { text: app.formatTime(app.position); color: Theme.muted; font.pixelSize: 11; Layout.preferredWidth: 34 }
                            SeekBar { Layout.fillWidth: true; objectName: "seekBar" }
                            SungText { text: app.formatTime(app.duration); color: Theme.muted; font.pixelSize: 11; Layout.preferredWidth: 34; horizontalAlignment: Text.AlignRight }
                        }
                    }
                    Item { Layout.fillWidth: true; visible: window.width>=1320 }
                    MButton { symbol: "lyrics"; tip: "Lyrics · Ctrl+Y"; selected: window.side==="lyrics"; enabled: app.currentIndex>=0; onClicked: window.activateSide("lyrics") }
                    MButton { objectName: "queueButton"; symbol: "queue"; tip: "Queue · Ctrl+L"; selected: window.side==="queue"; onClicked: window.activateSide("queue") }
                    RowLayout { visible: window.width>=1160; spacing: 0; MButton { symbol: app.volume>0?"volume":"mute"; tip: "Mute"; onClicked: window.toggleMute() } SeekBar { volumeMode: true; Layout.preferredWidth: 66 } }
                }
            }
        }
    }
    Component {
        id: queuePanel
        ColumnLayout {
            spacing: 8
            TrackList {
                id: queueList; objectName: "queueView"; Layout.fillWidth: true; Layout.fillHeight: true; model: app.queue; clip: true; reuseItems: true; cacheBuffer: 80; spacing: 4
                queueMode: true; reorderEnabled: true; dragHub: trackDrag
                onActivate: (row,item)=>app.playAt(row)
                onMenuRequested: (item,index,anchor)=>window.trackMenu(item,index,anchor,true)
                onRemoveSelected: app.removeQueueRows(sourceRows())
                onAddSelected: window.addBatch(queueList)
                SungText { anchors.centerIn: parent; text: "Your queue is empty"; color: Theme.muted; visible: app.queue.count===0 }
            }
            SelectionBar { Layout.fillWidth: true; view: queueList; canRemove: true }
            RowLayout {
                visible: queueList.selection.count===0; Layout.fillWidth: true
                MButton { objectName: "saveQueueButton"; symbol: "plus"; tip: "Save queue as playlist"; enabled: app.queue.count>0; onClicked: {window.playlistAction="queue";playlistName.clear();playlistDialog.open();} }
                MButton { objectName: "smartShuffleQueueButton"; symbol: "shuffle"; tip: "Shuffle upcoming · spread out artists"; enabled: app.queue.count-Math.max(0,app.currentIndex+1)>1; onClicked: app.smartShuffleQueue() }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 2
                    SungText { text: window.countText(app.queue.count); color: Theme.muted; Layout.fillWidth: true }
                    SungText { objectName: "queueTimeLabel"; text: app.queueTime; visible: !!text; color: Theme.muted; font.pixelSize: 12; Layout.fillWidth: true; HoverHandler { id: queueTimeHover } ToolTip.visible: queueTimeHover.hovered && !!app.queueEnd; ToolTip.text: app.queueEnd }
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
                width: nowScroll.availableWidth; spacing: 18
                Artwork { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: Math.min(320,nowScroll.availableWidth); Layout.preferredHeight: width; url: app.current.art || ""; radius: 24; pixels: 650 }
                SungText { text: app.current.title || "Nothing playing"; Layout.fillWidth: true; font.pixelSize: 24; font.weight: Font.Medium; wrapMode: Text.Wrap; elide: Text.ElideNone }
                SungText { text: app.current.artist || ""; Layout.fillWidth: true; font.pixelSize: 16; color: Theme.muted }
                RowLayout {
                    Layout.fillWidth: true
                    MButton { symbol: "heart"; tip: app.liked?"Unlike":"Like"; selected: app.liked; onClicked: app.toggleLike(app.current) }
                    MButton { symbol: "radio"; tip: "Start radio"; enabled: !!app.current.videoId; onClicked: app.radio(app.current) }
                    MButton { symbol: "more"; tip: "Track actions"; enabled: app.currentIndex>=0; onClicked: window.trackMenu(app.current,app.currentIndex,this,true) }
                }
                RowLayout { Layout.fillWidth: true; MButton { symbol: app.volume>0?"volume":"mute"; tip: "Mute"; onClicked: window.toggleMute() } SeekBar { volumeMode: true; Layout.fillWidth: true } }
            }
        }
    }

    MMenu {
        id: bulkActions; objectName: "bulkActions"
        MMenuItem { text: "Play selected next"; onTriggered: app.enqueueItems(window.bulkView.selection.items(),true) }
        MMenuItem { text: "Add selected to queue"; onTriggered: app.enqueueItems(window.bulkView.selection.items()) }
        MMenuItem { text: "Add selected to playlist"; onTriggered: window.addBatch(window.bulkView) }
        MMenuItem { text: "Remove selected"; visible: window.bulkView && (window.bulkView.queueMode || !!window.localPlaylist); height: visible?44:0; onTriggered: window.bulkView.removeSelected() }
        MMenuItem { text: "Select all"; onTriggered: window.bulkView.selection.selectAll() }
        MMenuItem { text: "Clear selection"; onTriggered: window.bulkView.selection.clear() }
    }
    MMenu {
        id: actions; objectName: "trackActions"
        width: 230; padding: 8
        background: Rectangle { color: Theme.high; radius: 20; border.color: Theme.outline }
        MMenuItem { text: "Open"; visible: !(window.menuItem.videoId || window.menuItem.localPath); onTriggered: app.open(window.menuItem) }
        MMenuItem { objectName: "playKeepQueueAction"; text: "Play now, keep queue"; visible: !!(window.menuItem.videoId || window.menuItem.localPath); onTriggered: app.playKeepingQueue(window.menuItem) }
        MMenuItem { text: "Play next"; visible: !!(window.menuItem.videoId || window.menuItem.localPath); onTriggered: app.enqueue(window.menuItem,true) }
        MMenuItem { text: "Add to queue"; visible: !!(window.menuItem.videoId || window.menuItem.localPath); onTriggered: app.enqueue(window.menuItem) }
        MMenuItem { text: "Start radio"; visible: !!window.menuItem.videoId; onTriggered: app.radio(window.menuItem) }
        MDivider { visible: !!(window.menuItem.videoId || window.menuItem.localPath); height: visible ? implicitHeight : 0 }
        MMenuItem { text: app.isLiked(window.menuItem.id || "")?"Remove from liked songs":"Like song"; visible: !!(window.menuItem.videoId || window.menuItem.localPath); onTriggered: app.toggleLike(window.menuItem) }
        MMenuItem { text: "Add to playlist"; visible: !!(window.menuItem.videoId || window.menuItem.localPath); onTriggered: addPlaylistDialog.open() }
        MMenuItem { text: "Go to artist"; visible: !!window.menuItem.artistId; onTriggered: app.open({kind:"artist",browseId:window.menuItem.artistId,title:window.menuItem.artist}) }
        MMenuItem { text: "Go to album"; visible: !!window.menuItem.albumId; onTriggered: app.open({kind:"album",browseId:window.menuItem.albumId,title:window.menuItem.album}) }
        MMenuItem { text: window.menuItem.localPath?"Copy file path":"Copy link"; onTriggered: app.copyLink(window.menuItem) }
        MMenuItem { text: "Locate file…"; visible: !!window.menuItem.localPath; onTriggered: window.openFileDialog("locate") }
        MMenuItem { text: "Remove from local files"; visible: app.libraryId==="files" && !!window.menuItem.localPath && !window.menuQueue; onTriggered: app.removeLocalFile(window.menuItem.id) }
        MDivider { visible: window.menuQueue || !!window.localPlaylist; height: visible?implicitHeight:0 }
        MMenuItem { text: "Move up"; visible: window.menuQueue; height: visible?44:0; enabled: window.menuIndex>0; onTriggered: app.moveQueue(window.menuIndex,window.menuIndex-1) }
        MMenuItem { text: "Move down"; visible: window.menuQueue; height: visible?44:0; enabled: window.menuIndex<app.queue.count-1; onTriggered: app.moveQueue(window.menuIndex,window.menuIndex+1) }
        MMenuItem { text: "Remove from queue"; visible: window.menuQueue; height: visible?44:0; onTriggered: app.removeQueue(window.menuIndex) }
        MMenuItem { text: "Move up in playlist"; visible: !window.menuQueue && !!window.localPlaylist; height: visible?44:0; enabled: window.menuIndex>0 && app.collection.sortKey==="original" && !app.collection.query; onTriggered: app.movePlaylistTrack(window.localPlaylist,window.menuIndex,window.menuIndex-1) }
        MMenuItem { text: "Move down in playlist"; visible: !window.menuQueue && !!window.localPlaylist; height: visible?44:0; enabled: window.menuIndex<app.results.count-1 && app.collection.sortKey==="original" && !app.collection.query; onTriggered: app.movePlaylistTrack(window.localPlaylist,window.menuIndex,window.menuIndex+1) }
        MMenuItem { text: "Remove from playlist"; visible: !window.menuQueue && !!window.localPlaylist; height: visible?44:0; onTriggered: app.removeFromPlaylist(window.localPlaylist,window.menuIndex) }
    }
    MMenu {
        id: playlistActions
        MMenuItem { text: {app.pins;return app.isPinned({kind:"local",id:window.editPlaylistId})?"Unpin from Home":"Pin to Home";} onTriggered: app.togglePin({kind:"local",id:window.editPlaylistId,title:playlistName.text}) }
        MMenuItem { text: "Clean up…"; onTriggered: window.openCleanup(window.editPlaylistId) }
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
            MButton { objectName: "addMusicFolderButton"; text: "Add folder…"; symbol: "plus"; tonal: true; enabled: !app.importingLocal; onClicked: {musicFoldersDialog.close();window.openFileDialog("folder");} }
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
                    MButton { objectName: "cleanupDuplicates"; text: "Duplicates"; selected: cleanupContent.removeDuplicates; onClicked: cleanupContent.removeDuplicates=!cleanupContent.removeDuplicates }
                    MButton { objectName: "cleanupMissing"; text: "Missing files"; selected: cleanupContent.removeMissing; onClicked: cleanupContent.removeMissing=!cleanupContent.removeMissing }
                    Item { Layout.fillWidth: true }
                    MButton { symbol: "refresh"; tip: "Check again"; enabled: !app.cleanupBusy&&!app.importingLocal; onClicked: app.inspectPlaylist(cleanupDialog.playlistId) }
                }
                ListView {
                    objectName: "cleanupList"; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; reuseItems: true; spacing: 4
                    model: app.cleanupItems
                    ScrollBar.vertical: ScrollBar {}
                    delegate: RowLayout {
                        required property var modelData; width: ListView.view.width; height: 76; spacing: 12
                        opacity: (cleanupContent.removeDuplicates&&modelData.duplicate)||(cleanupContent.removeMissing&&modelData.missing)?1:0.6
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 3
                            SungText { text: modelData.title; Layout.fillWidth: true; elide: Text.ElideRight }
                            SungText { text: [modelData.duplicate?"Duplicate":"",modelData.missing?"Missing file":""].filter(Boolean).join(" · "); color: Theme.muted; font.pixelSize: 13 }
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
        id: playlistDialog; objectName: "playlistDialog"; anchors.centerIn: parent; width: 380; modal: true; acceptEnabled: playlistName.text.trim().length>0; title: window.playlistAction==="rename"?"Rename playlist":"New playlist"
        background: Rectangle { color: Theme.container; radius: 28 }
        palette.windowText: Theme.text; palette.text: Theme.text; palette.buttonText: Theme.text
        standardButtons: Dialog.Save | Dialog.Cancel
        TextField { font.family: Theme.fontFamily; id: playlistName; objectName: "playlistName"; width: parent.width; implicitHeight: 48; leftPadding: 14; rightPadding: 14; placeholderText: "Playlist name"; placeholderTextColor: Theme.muted; selectionColor: Theme.primaryContainer; selectedTextColor: Theme.text; color: Theme.text; selectByMouse: true; maximumLength: 120; background: Rectangle { color: Theme.surface; radius: 12; border.width: playlistName.activeFocus?2:1; border.color: playlistName.activeFocus?Theme.primary:Theme.outline } onAccepted: {if(text.trim())playlistDialog.accept();} }
        onOpened: playlistName.forceActiveFocus()
        onAccepted: { if(window.playlistAction==="queue")app.saveQueue(playlistName.text);else if(window.playlistAction==="rename")app.renamePlaylist(window.editPlaylistId,playlistName.text);else {const id=app.createPlaylist(playlistName.text);if(id&&window.playlistAction==="add"){if(window.batchItems.length)app.addItemsToPlaylist(id,window.batchItems);else app.addToPlaylist(id,window.menuItem);}} }
    }
    MDialog {
        id: addPlaylistDialog; objectName: "addPlaylistDialog"; anchors.centerIn: parent; width: 360; height: Math.min(window.height-64,Math.min(500,268+app.playlists.length*52)); modal: true; title: "Add to playlist"; standardButtons: Dialog.Cancel
        background: Rectangle { color: Theme.container; radius: 28 }
        palette.windowText: Theme.text
        property bool contentReady: false
        onAboutToShow: contentReady=true
        contentItem: Loader {
            active: addPlaylistDialog.contentReady
            sourceComponent: Component {
        ColumnLayout {
            anchors.fill: parent
            ListView { objectName: "playlistChoices"; Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: Math.min(52,app.playlists.length*52); clip: true; model: app.playlists; delegate: MButton { objectName: "playlistChoice"; required property var modelData; width: ListView.view.width; text: modelData.title; leftAligned: true; onClicked: {if(window.batchItems.length)app.addItemsToPlaylist(modelData.id,window.batchItems);else app.addToPlaylist(modelData.id,window.menuItem);addPlaylistDialog.close();} } }
            MButton { Layout.fillWidth: true; text: "New playlist"; symbol: "plus"; tonal: true; onClicked: {addPlaylistDialog.close();window.playlistAction="add";playlistName.clear();playlistDialog.open();} }
        }
            }
        }
    }
    MDialog {
        id: deletePlaylistDialog; objectName: "deletePlaylistDialog"; anchors.centerIn: parent; width: 380; modal: true; title: "Delete playlist?"; standardButtons: Dialog.Yes | Dialog.No; onOpened: standardButton(Dialog.Yes).text="Delete"
        background: Rectangle { color: Theme.container; radius: 24 }
        palette.windowText: Theme.text; palette.buttonText: Theme.text
        onAccepted: {app.deletePlaylist(window.editPlaylistId);window.localPlaylist="";}
    }
    MDialog {
        id: settingsDialog; objectName: "settingsDialog"; anchors.centerIn: parent; width: 460; height: Math.min(window.height-64,660); modal: true; title: "Settings"
        padding: 24; background: Rectangle { color: Theme.container; radius: 28 }
        palette.windowText: Theme.text; palette.buttonText: Theme.text; palette.text: Theme.text
        standardButtons: Dialog.Close
        property bool contentReady: false
        onAboutToShow: contentReady=true
        contentItem: Loader {
            active: settingsDialog.contentReady
            sourceComponent: Component {
        ScrollView {
            anchors.fill: parent; contentWidth: availableWidth; clip: true
            ColumnLayout {
                width: parent.width; spacing: 18
                SungText { text: "Appearance"; font.pixelSize: 16; font.weight: Font.Medium }
                RowLayout { spacing: 8; Repeater { model: ["system","light","dark"]; MButton { required property string modelData; text: modelData==="system" && desktopTheme.available?"Noctalia":modelData.charAt(0).toUpperCase()+modelData.slice(1); selected: app.theme===modelData; onClicked: app.theme=modelData } } }
                MSwitch { text: "Animations"; checked: app.motion; onToggled: app.motion=checked; palette.windowText: Theme.text; palette.highlight: Theme.primary }
                MSwitch { text: "Autoplay similar songs"; checked: app.autoplay; onToggled: app.autoplay=checked; palette.windowText: Theme.text; palette.highlight: Theme.primary }
                MSwitch { objectName: "historyPauseSwitch"; text: "Pause history this session"; checked: app.historyPaused; onToggled: app.historyPaused=checked }
                MSwitch { objectName: "trackNotificationsSwitch"; text: "Track notifications"; checked: app.trackNotifications; onToggled: app.trackNotifications=checked }
                RowLayout { Layout.fillWidth: true; SungText { text: "Volume step"; Layout.fillWidth: true } MButton { objectName: "volumeStepButton"; text: app.volumeStep+"%"; tonal: true; onClicked: volumeStepMenu.popup(this,width-volumeStepMenu.width,height+4) } }
                RowLayout { Layout.fillWidth: true; SungText { text: "Playback speed"; Layout.fillWidth: true } MButton { objectName: "playbackSpeedButton"; text: Number(app.playbackRate.toFixed(2))+"×"; tonal: true; onClicked: rateDialog.open() } }
                RowLayout { Layout.fillWidth: true; SungText { text: "Sleep timer"; Layout.fillWidth: true } MButton { text: app.sleepStatus; symbol: "chevron"; tonal: true; onClicked: sleepMenu.popup(this,width-sleepMenu.width,height+4) } }

                MButton { objectName: "audioDeviceButton"; text: app.audioDeviceName; symbol: "volume"; tip: "Audio output"; tonal: true; Layout.fillWidth: true; leftAligned: true; onClicked: audioDeviceDialog.open() }
                MSwitch { text: "Prepare next track"; checked: app.prepareNext; onToggled: app.prepareNext=checked }
                MSwitch { text: "Find missing lyrics on LRCLIB"; checked: app.lyricsFallback; onToggled: app.lyricsFallback=checked }
                SungText { text: "YouTube"; font.pixelSize: 16; font.weight: Font.Medium; Layout.topMargin: 8 }
                MButton { text: app.cookies?"Replace cookies":"Import cookies"; symbol: "folder"; tonal: true; onClicked: window.openFileDialog("cookies") }
                MButton { text: "Remove cookies"; visible: !!app.cookies; onClicked: app.clearCookies() }
                SungText { text: "Optional cookies.txt for tracks that require sign-in. Your library stays on this device."; wrapMode: Text.Wrap; Layout.fillWidth: true; color: Theme.muted; font.pixelSize: 12 }
                RowLayout { MButton { text: "Clear artwork cache"; onClicked: app.clearCache() } MButton { text: "Clear history"; onClicked: app.clearHistory() } }
                RowLayout { MButton { text: "Export library"; onClicked: window.openFileDialog("export") } MButton { text: "Import library"; onClicked: window.openFileDialog("import") } }
                SungText { text: "Sung 0.11.0"; color: Theme.muted; font.pixelSize: 12; Layout.topMargin: 8 }
            }
        }
            }
        }
    }
    MMenu {
        id: sleepMenu
        MMenuItem { text: "Off"; onTriggered: app.setSleep(0) }
        MMenuItem { text: "End of track"; enabled: app.currentIndex>=0; onTriggered: app.setSleep(-1) }
        MMenuItem { text: "15 minutes"; onTriggered: app.setSleep(15) }
        MMenuItem { text: "30 minutes"; onTriggered: app.setSleep(30) }
        MMenuItem { text: "60 minutes"; onTriggered: app.setSleep(60) }
        MMenuItem { text: "90 minutes"; onTriggered: app.setSleep(90) }
    }
    Rectangle {
        anchors.bottom: parent.bottom; anchors.bottomMargin: 140; anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(window.width-120,errorText.implicitWidth+(app.canRetry?180:100)); height: Math.min(150,errorText.implicitHeight+32)
        radius: 18; color: Theme.containerText; visible: !!app.error; z: 50
        SungText { id: errorText; anchors.fill: parent; anchors.margins: 16; anchors.rightMargin: app.canRetry?140:58; text: app.error; wrapMode: Text.Wrap; elide: Text.ElideRight; maximumLineCount: 5; color: Theme.primaryContainer; font.pixelSize: 13 }
        MButton { anchors.right: parent.right; anchors.rightMargin: 48; anchors.verticalCenter: parent.verticalCenter; text: "Retry"; visible: app.canRetry; ink: Theme.primaryContainer; onClicked: app.retry() }
        MButton { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; symbol: "close"; ink: Theme.primaryContainer; tip: "Dismiss error"; onClicked: app.dismissError() }
    }
    Rectangle {
        anchors.bottom: parent.bottom; anchors.bottomMargin: 140; anchors.horizontalCenter: parent.horizontalCenter; z: 40
        width: toastLabel.implicitWidth+(window.toastText===app.undoMessage?120:40); height: 48; radius: 16; color: Theme.text
        opacity: toastTimer.running?1:0; visible: opacity>0
        Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        MButton { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; text: "Undo"; ink: Theme.background; visible: window.toastText===app.undoMessage && !!app.undoMessage; onClicked: app.undo() }
        SungText { id: toastLabel; anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 20; text: window.toastText; color: Theme.background }
    }
    Timer { id: toastTimer; interval: window.toastText===app.undoMessage ? 6000 : 2400 }
    Connections { target: app; function onToast(message){window.toastText=message;toastTimer.restart();} function onTrackChanged(){if(window.side==="lyrics" || window.compactMode || window.immersive)app.fetchLyrics();}
        function onCatalogChanged(){
            if(!app.collection.query && app.collection.sortKey==="original")window.collectionTools=false;
            if(app.page==="home"){window.destination="home";window.localPlaylist="";searchField.clear();}
            else if(app.page==="library" || app.page==="local"){
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
    MMenu {
        id: collectionSortMenu; objectName: "collectionSortMenu"
        Repeater {
            model: [{key:"original",label:"Original order"},{key:"title",label:"Title"},{key:"artist",label:"Artist"},{key:"duration",label:"Duration"}]
            MMenuItem { required property var modelData; objectName: "sort_"+modelData.key; text: modelData.label; checkable: true; checked: app.collection.sortKey===modelData.key; onTriggered: app.collection.sortKey=modelData.key }
        }
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
            anchors.fill: parent; spacing: 10
            SungText { text: Number(app.playbackRate.toFixed(2))+"×"; font.pixelSize: 32; Layout.alignment: Qt.AlignHCenter }
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
            SungText { text: (app.lyricOffset>0?"+":"")+(app.lyricOffset/1000).toFixed(2)+" s"; font.pixelSize: 28; Layout.alignment: Qt.AlignHCenter }
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
