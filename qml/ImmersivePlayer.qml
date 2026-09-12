import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Item {
    id: player
    objectName: "immersivePlayer"
    TrackPresentation { id: presentation }
    property alias artwork: immersiveArt
    property string preferredLayout: "split"
    property bool externalModalOpen: false
    readonly property bool popupVisible: immersiveVolume.popupVisible || layoutMenu.visible
    readonly property bool volumePopupVisible: immersiveVolume.popupVisible
    readonly property bool hasLyrics: !!app.lyrics || app.lyricLines.length>0
    readonly property string effectiveLayout: hasLyrics && ["split","lyrics"].indexOf(preferredLayout)>=0 ? preferredLayout : "artwork"
    property string displayedLayout: effectiveLayout
    property bool ready: false
    property bool autoHideControls: false
    property bool controlsShown: true
    readonly property bool keyboardFocus: hasKeyboardFocus(player.Window.window ? player.Window.window.activeFocusItem : null)
    readonly property bool hideBlocked: !autoHideControls || !player.Window.window || !player.Window.window.active || !app.playing || app.buffering || externalModalOpen || popupVisible || immersiveLyrics.searchOpen || keyboardFocus
    property bool coverHidden: false
    property real detailsOpacity: coverHidden ? 0 : 1
    readonly property var artistTarget: app.relatedCollection(app.current,"artist")
    readonly property var albumTarget: app.relatedCollection(app.current,"album")
    readonly property real coverSize: Math.max(80,Math.min(displayedLayout==="artwork"?520:420,width*(displayedLayout==="artwork"?0.55:0.34),height-500))
    signal exitRequested()
    signal speedRequested()
    signal timingRequested()
    signal artworkRequested()
    signal autoHideRequested(bool enabled)
    signal layoutRequested(string layout)
    signal queueRequested()
    signal collectionRequested(var item)
    function hasKeyboardFocus(item) {
        for(let p=item;p && p!==player;p=p.parent)
            if(p.visualFocus===true || p.handlesTextInput===true)return true;
        return false;
    }
    function wake() { controlsShown=true;idle.restart(); }
    function showLyricsSearch() {
        if(preferredLayout==="artwork")layoutRequested("lyrics");
        immersiveLyrics.openSearch();wake();
    }
    onHideBlockedChanged: wake()
    onEffectiveLayoutChanged: {
        layoutChange.stop();
        if(!ready || !app.motion){displayedLayout=effectiveLayout;body.opacity=1;}
        else layoutChange.start();
    }
    Component.onCompleted: {ready=true;displayedLayout=effectiveLayout;forceActiveFocus();wake();}
    Keys.onPressed: event=>{wake();event.accepted=false;}
    HoverHandler {
        property point previousPosition: Qt.point(-1,-1)
        onPointChanged: {
            if(!hovered)return;
            const position=point.position;
            if(position.x===previousPosition.x && position.y===previousPosition.y)return;
            previousPosition=position;player.wake();
        }
    }
    Timer { id: idle; interval: 3500; onTriggered: {if(!player.hideBlocked)player.controlsShown=false;} }
    Connections { target: app; function onSettingsChanged(){if(!app.motion){layoutChange.stop();player.displayedLayout=player.effectiveLayout;body.opacity=1;}} }
    SequentialAnimation {
        id: layoutChange
        NumberAnimation {target:body;property:"opacity";to:0;duration:Theme.exitDuration;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.effectsCurve}
        ScriptAction {script:player.displayedLayout=player.effectiveLayout}
        NumberAnimation {target:body;property:"opacity";to:1;duration:Theme.enterDuration;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.effectsCurve}
    }
    Behavior on detailsOpacity { NumberAnimation { duration: Theme.exitDuration; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve } }
    NumberAnimation on opacity { from: 0; to: 1; duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: player.width<900?24:40; spacing: 20
        RowLayout {
            objectName: "immersiveTopControls"
            Layout.fillWidth: true; opacity: player.controlsShown?1:0
            Behavior on opacity {NumberAnimation {duration:Theme.normal;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.effectsCurve}}
            MButton { objectName: "exitImmersiveButton"; symbol: "back"; tip: "Exit immersive · Esc"; onClicked: player.exitRequested() }
            Item { Layout.fillWidth: true }
            MButton {id:layoutButton;objectName:"immersiveLayoutButton";symbol:"more";tip:"Immersive layout";selected:layoutMenu.visible;onClicked:layoutMenu.popup(layoutButton,width-layoutMenu.width,height+4)}
            MButton { objectName: "immersiveLyricSearchButton"; symbol: "search"; tip: "Find in lyrics"; enabled: player.hasLyrics; onClicked: player.showLyricsSearch() }
            MButton { text: Number(app.playbackRate.toFixed(2))+"×"; tip: "Playback speed"; onClicked: player.speedRequested() }
            MButton { symbol: "settings"; tip: "Lyric timing · saved for this song"; visible: app.lyricLines.length>0; onClicked: player.timingRequested() }
            MButton { symbol: "heart"; selected: app.liked; tip: app.liked?"Unlike":"Like"; enabled: app.currentIndex>=0; onClicked: app.toggleLike(app.current) }
        }
        RowLayout {
            id: body; objectName:"immersiveBody"
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0; spacing: Math.max(24,player.width*0.055)
            Item {Layout.fillWidth:true;visible:player.displayedLayout==="artwork"}
            ColumnLayout {
                visible:player.displayedLayout!=="lyrics"
                Layout.preferredWidth: player.coverSize; Layout.minimumWidth: player.coverSize; Layout.maximumWidth: player.coverSize; Layout.fillHeight: true; Layout.minimumHeight: 0; spacing: 12
                Item { Layout.fillHeight: true }
                Artwork { id: immersiveArt; objectName: "immersiveArtwork"; Layout.preferredWidth: player.coverSize; Layout.preferredHeight: player.coverSize; Layout.maximumHeight: player.coverSize; url: app.current.art || ""; motionUrl: app.currentMotionArt; crossfade:true; opacity: player.coverHidden?0:1; radius: 28; pixels: 850; highResolution: true; fit:app.currentArtworkFit
                    AbstractButton {anchors.fill:parent;Accessible.name:"View artwork";focusPolicy:Qt.StrongFocus;onClicked:player.artworkRequested();background:Rectangle {color:"transparent";radius:28;border.width:parent.visualFocus?2:0;border.color:Theme.primary}}
                }
                SungText { text: presentation.shown.title || "Nothing playing"; opacity: presentation.fade*player.detailsOpacity; transform: Translate { x: presentation.offset } Layout.fillWidth: true; font.pixelSize: player.width<900?22:30; font.weight: Font.DemiBold; wrapMode: Text.Wrap; maximumLineCount: 2 }
                AbstractButton {
                    objectName:"immersiveArtistButton";Layout.fillWidth:true;implicitHeight:48;leftPadding:0;rightPadding:8
                    enabled:!!player.artistTarget.kind && presentation.shown.id===app.current.id;focusPolicy:Qt.StrongFocus
                    Accessible.name: "Open artist · "+(app.current.artist || "")
                    onClicked: player.collectionRequested(player.artistTarget)
                    contentItem:SungText {text:presentation.shown.artist || "";font.pixelSize:18;color:parent.hovered&&parent.enabled?Theme.primary:Theme.muted;opacity:presentation.fade*player.detailsOpacity}
                    background:Rectangle {color:parent.down?Theme.high:parent.hovered&&parent.enabled?Qt.rgba(Theme.primary.r,Theme.primary.g,Theme.primary.b,Theme.hoverOpacity):"transparent";radius:12;border.width:parent.visualFocus?2:0;border.color:Theme.primary}
                }
                AbstractButton {
                    objectName:"immersiveAlbumButton";Layout.fillWidth:true;implicitHeight:40;leftPadding:0;rightPadding:8;visible:!!app.current.album
                    enabled:!!player.albumTarget.kind && presentation.shown.id===app.current.id;focusPolicy:Qt.StrongFocus
                    Accessible.name: "Open album · "+(app.current.album || "")
                    onClicked: player.collectionRequested(player.albumTarget)
                    contentItem:SungText {text:presentation.shown.album || "";font.pixelSize:14;color:parent.hovered&&parent.enabled?Theme.primary:Theme.muted;opacity:presentation.fade*player.detailsOpacity}
                    background:Rectangle {color:parent.down?Theme.high:parent.hovered&&parent.enabled?Qt.rgba(Theme.primary.r,Theme.primary.g,Theme.primary.b,Theme.hoverOpacity):"transparent";radius:12;border.width:parent.visualFocus?2:0;border.color:Theme.primary}
                }
                Item { Layout.fillHeight: true }
            }
            Item {Layout.fillWidth:true;visible:player.displayedLayout==="artwork"}
            LyricsView { id: immersiveLyrics; expanded: true; visible:player.displayedLayout!=="artwork"; Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.fillHeight: true; Layout.minimumHeight: 0 }
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter; spacing: 16; opacity:player.controlsShown?1:0
            Behavior on opacity {NumberAnimation {duration:Theme.normal;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.effectsCurve}}
            MButton { symbol: "shuffle"; selected: app.shuffle; tip: app.shuffle?"Shuffle on":"Shuffle off"; onClicked: app.shuffle=!app.shuffle }
            MButton { symbol: "previous"; tip: "Previous"; enabled: app.queue.count>0; onClicked: app.previous() }
            MButton { objectName: "immersivePlayButton"; busy: app.buffering; morphPlayback:true; symbol: app.playing||app.resolving?"pause":"play"; filled: true; implicitWidth: 80; implicitHeight: 56; tip: app.playing||app.resolving?"Pause":"Play"; enabled: app.queue.count>0; onClicked: app.toggle() }
            MButton { symbol: "next"; tip: "Next"; enabled: app.queue.count>0; onClicked: app.next() }
            MButton { symbol: app.repeat===2?"repeat_one":"repeat"; selected: app.repeat>0; tip: app.repeat===0?"Repeat off":app.repeat===1?"Repeat queue":"Repeat song"; onClicked: app.repeat=(app.repeat+1)%3 }
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: Math.min(800,player.width-80); spacing: 12; opacity:player.controlsShown?1:0
            Behavior on opacity {NumberAnimation {duration:Theme.normal;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.effectsCurve}}
            SungText { font.features: {"tnum": 1}; text: app.formatTime(app.position); color: Theme.muted; font.pixelSize: 12; Layout.preferredWidth: 40 }
            SeekBar { Layout.fillWidth: true }
            SungText { font.features: {"tnum": 1}; text: app.formatTime(app.duration); color: Theme.muted; font.pixelSize: 12; Layout.preferredWidth: 40; horizontalAlignment: Text.AlignRight }
            MButton {objectName:"immersiveQueueButton";symbol:"queue";tip:player.externalModalOpen?"":"Queue · Ctrl+L";onClicked:player.queueRequested()}
            VolumeControl {id:immersiveVolume;showSlider:false}
        }
    }
    MMenu {
        id:layoutMenu;objectName:"immersiveLayoutMenu"
        onClosed:{layoutButton.forceActiveFocus(Qt.PopupFocusReason);player.wake();}
        Repeater {
            model:[{key:"artwork",label:"Artwork"},{key:"lyrics",label:"Lyrics"},{key:"split",label:"Split"}]
            MMenuItem {required property var modelData;objectName:"immersiveLayout_"+modelData.key;text:modelData.label;checkable:true;checked:player.preferredLayout===modelData.key;onTriggered:player.layoutRequested(modelData.key)}
        }
        MDivider {}
        MMenuItem {objectName:"immersiveAutoHide";text:"Auto-hide controls";checkable:true;checked:player.autoHideControls;onTriggered:player.autoHideRequested(!player.autoHideControls)}
    }
}
