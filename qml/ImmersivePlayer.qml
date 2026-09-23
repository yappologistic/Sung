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
    // Singing along needs to know when each line starts, so it asks for more
    // than the reading layouts do and falls back when a song cannot give it.
    readonly property bool hasTimedLyrics: app.lyricLines.length>0
    readonly property string effectiveLayout: preferredLayout==="singalong" ? (hasTimedLyrics?"singalong":"artwork")
        : hasLyrics && ["split","lyrics"].indexOf(preferredLayout)>=0 ? preferredLayout : "artwork"
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
    // How wide the column holding the cover and its details is. Material has
    // nothing to say about a now-playing cover, so this is a share of the
    // window: most of it when the cover is the screen, about a third when it
    // is sharing with the words. The cover's own size is not set here. It is
    // the largest square that fits the room the column has left after the
    // title, artist and album, which the layout works out rather than a
    // constant guessing at it.
    // The metadata column needs room for ordinary words even when the split
    // view is narrow. The floor is a layout allowance, not an artwork size.
    readonly property real coverColumnWidth: Math.max(200,width*(displayedLayout==="artwork"?0.55:0.34))
    // Material has no now-playing text measure token. At tall desktop widths
    // 520px keeps the shared edge close to the height-limited square cover;
    // the column itself sets the floor when the window is narrow.
    readonly property real detailsMeasure: Math.min(520,coverColumn.width)
    // At the immersive 40px lyric size, 760px holds roughly 35 characters.
    // The measure is centred only where the window content also fits gutters.
    readonly property real lyricMeasure: 760
    readonly property real coverSize: immersiveArt.width
    // What sits against the bottom of this view, so anything that has to clear
    // it knows how much to clear. A snackbar is the one that has to.
    readonly property real bottomChrome: controlsShown
        ? transport.height + seekRow.height + shell.spacing + shell.anchors.margins : 0
    signal exitRequested()
    signal speedRequested()
    signal timingRequested()
    signal artworkRequested()
    signal autoHideRequested(bool enabled)
    signal layoutRequested(string layout)
    signal queueRequested()
    signal collectionRequested(var item)
    signal coverflowRequested(bool enabled)
    property bool coverflow: false
    // ImmersiveCoverflow.qml:16-20 computes this row from the window height.
    // The duplicate measure is needed before its Loader exists; reading the
    // loaded item's height here makes coverflowVisible depend on itself.
    readonly property real coverflowReserve: Math.round(Math.max(72,Math.min(104,
        (Window.window?Window.window.height:800)*0.11)))+58
    // There is no Material token for a now-playing cover. Below 160dp it
    // reads as a queue thumbnail, so the optional coverflow yields its row.
    // IconButton.kt:242-249 gives each link a 48dp target. Count four shell
    // gaps and the natural control heights before reserving the optional row.
    readonly property real coverflowCoverBudget: (Window.window?Window.window.height:height)-2*(width<600?16:24)
        -topControls.implicitHeight-transport.implicitHeight-seekRow.implicitHeight
        -coverflowReserve-4*shell.spacing-title.implicitHeight-2*48
    readonly property bool coverflowVisible: coverflow && app.queue.count>0 &&
        (displayedLayout==="lyrics" || displayedLayout==="singalong" || coverflowCoverBudget>=160)
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
        // FastEffects carries both halves of the layout fade without bounce.
        NumberAnimation {objectName:"immersiveFadeOutMotion";target:body;property:"opacity";to:0;duration:Theme.springFastEffectsMs;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.springFastEffects}
        ScriptAction {script:player.displayedLayout=player.effectiveLayout}
        NumberAnimation {objectName:"immersiveFadeInMotion";target:body;property:"opacity";to:1;duration:Theme.springFastEffectsMs;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.springFastEffects}
    }
    Behavior on detailsOpacity { NumberAnimation { objectName: "immersiveDetailFadeMotion"; duration: Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastEffects } }
    NumberAnimation on opacity { from: 0; to: 1; duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve }
    AmbientBackdrop { anchors.fill: parent; url: app.current.art || "" }
    ColumnLayout {
        id: shell
        // Material's page margins for the window size class, the same ones the
        // rest of the application insets its panes by.
        anchors.fill: parent; anchors.margins: player.width<600?16:24; spacing: 20
        RowLayout {
            id: topControls
            objectName: "immersiveTopControls"
            Layout.fillWidth: true; opacity: player.controlsShown?1:0
            // Qt Quick Item.enabled removes input and focus after the fade.
            // Each exposed control below also leaves the accessibility tree.
            enabled: opacity>0; Accessible.ignored: opacity===0
            Behavior on opacity {NumberAnimation {duration:Theme.normal;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.effectsCurve}}
            MButton { objectName: "exitImmersiveButton"; symbol: "back"; tip: "Exit immersive · Esc"; Accessible.ignored: topControls.opacity===0; onClicked: player.exitRequested() }
            Item { Layout.fillWidth: true }
            // Material keeps an app bar to a few trailing actions and folds the
            // rest behind one overflow. Five of them sat here, one of which
            // already wore the overflow glyph while opening a layout menu, so
            // the icon that means "more actions" did not. There is one now,
            // and it means it.
            MButton { objectName: "immersiveLyricSearchButton"; symbol: "search"; tip: "Find in lyrics"; enabled: player.hasLyrics && player.displayedLayout!=="singalong"; Accessible.ignored: topControls.opacity===0; onClicked: player.showLyricsSearch() }
            MButton { symbol: "heart"; selected: app.liked; tip: app.liked?"Unlike":"Like"; enabled: app.currentIndex>=0; Accessible.ignored: topControls.opacity===0; onClicked: app.toggleLike(app.current) }
            MButton {id:layoutButton;objectName:"immersiveLayoutButton";symbol:"more";tip:"More actions";selected:layoutMenu.visible;Accessible.ignored: topControls.opacity===0;onClicked:layoutMenu.popup(layoutButton,width-layoutMenu.width,height+4)}
        }
        RowLayout {
            id: body; objectName:"immersiveBody"
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0; spacing: Math.max(24,player.width*0.055)
            // shell is anchored to the window, so this decision cannot feed
            // back into the width of the RowLayout it sizes.
            readonly property bool lyricMeasureFits: shell.width >= player.lyricMeasure+2*spacing
            Item {Layout.fillWidth:true;visible:player.displayedLayout==="artwork"}
            ColumnLayout {
                id: coverColumn
                visible:player.displayedLayout!=="lyrics" && player.displayedLayout!=="singalong"
                Layout.preferredWidth: player.coverColumnWidth; Layout.minimumWidth: player.coverColumnWidth; Layout.maximumWidth: player.coverColumnWidth
                Layout.fillHeight: true; Layout.minimumHeight: 0; spacing: 0
                // The cover takes the room the details do not, as the largest
                // square that fits it. A window with more height to give
                // therefore reaches the cover instead of stopping at a cap.
                Item {
                    id: coverSlot; objectName: "immersiveCoverSlot"
                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0
                    Artwork { id: immersiveArt; objectName: "immersiveArtwork"; anchors.centerIn: parent
                        width: Math.max(80,Math.min(parent.width,parent.height)); height: width
                        url: app.current.art || ""; motionUrl: app.currentMotionArt; crossfade:true; opacity: player.coverHidden?0:1; radius: Theme.shapeExtraLarge; pixels: 850; highResolution: true; fit:app.currentArtworkFit
                        AbstractButton {anchors.fill:parent;Accessible.name:"View artwork";focusPolicy:Qt.StrongFocus;onClicked:player.artworkRequested();background:Rectangle {color:"transparent";radius:Theme.shapeExtraLarge;border.width:parent.visualFocus?2:0;border.color:Theme.focusRing}}
                    }
                }
                SungText {
                    id: title
                    objectName: "immersiveTitle"
                    text: presentation.shown.title || "Nothing playing"
                    opacity: presentation.fade*player.detailsOpacity
                    transform: Translate { x: presentation.offset }
                    Layout.fillWidth: true
                    Layout.maximumWidth: player.detailsMeasure
                    Layout.alignment: Qt.AlignHCenter
                    // Typography.kt:115-133 reads the TypeScaleTokens roles.
                    // TypeScaleTokens.kt:117-127,343-353 gives headline-large
                    // 32sp/40sp with medium emphasis, nearest the old 30px;
                    // :207-217,433-443 gives narrow title-large 22sp/28sp.
                    typeRole: player.width<900?"titleLarge":"headlineLarge"
                    emphasized: true
                    font.pixelSize: player.width<900?Theme.titleLarge:Theme.headlineLarge

                    // Qt Text.WordWrap keeps whole words; ElideRight marks the
                    // last line when the two-line limit omits the rest.
                    wrapMode: Text.WordWrap
                    elide: Text.ElideRight
                    maximumLineCount: 2
                    clip: true
                }
                AbstractButton {
                    objectName: "immersiveArtistButton"
                    Layout.fillWidth: true
                    Layout.maximumWidth: player.detailsMeasure
                    Layout.alignment: Qt.AlignHCenter
                    implicitHeight: 48
                    leftPadding: 0; rightPadding: 8
                    enabled:!!player.artistTarget.kind && presentation.shown.id===app.current.id;focusPolicy:Qt.StrongFocus
                    Accessible.name: "Open artist \u00b7 "+(app.current.artist || "")
                    onClicked: player.collectionRequested(player.artistTarget)
                    contentItem:SungText {text:presentation.shown.artist || "";font.pixelSize:Theme.bodyLarge;color:parent.hovered&&parent.enabled?Theme.primary:Theme.muted;opacity:presentation.fade*player.detailsOpacity}
                    background:Rectangle {color:parent.down?Theme.high:parent.hovered&&parent.enabled?Qt.rgba(Theme.primary.r,Theme.primary.g,Theme.primary.b,Theme.hoverOpacity):"transparent";radius:Theme.shapeMedium}
                    // MButton's ring sits 3px outside the control and follows
                    // its shape, with a 2px Theme.focusRing stroke.
                    Rectangle { objectName: "immersiveArtistFocusRing"; anchors.fill: parent; anchors.margins: -3; radius: Theme.shapeMedium+3; color: "transparent"; border.width: 2; border.color: Theme.focusRing; visible: parent.visualFocus }
                }
                AbstractButton {
                    // IconButton.kt:242-249 applies a 48dp minimum hit area.
                    // Zero column spacing keeps both 48dp slots contiguous;
                    // the text stays centred inside each larger target.
                    objectName: "immersiveAlbumButton"
                    Layout.fillWidth: true
                    Layout.maximumWidth: player.detailsMeasure
                    Layout.alignment: Qt.AlignHCenter
                    implicitHeight: 48
                    leftPadding: 0; rightPadding: 8
                    visible: !!app.current.album
                    enabled:!!player.albumTarget.kind && presentation.shown.id===app.current.id;focusPolicy:Qt.StrongFocus
                    Accessible.name: "Open album \u00b7 "+(app.current.album || "")
                    onClicked: player.collectionRequested(player.albumTarget)
                    contentItem:SungText {text:presentation.shown.album || "";font.pixelSize:Theme.labelLarge;labelRole:true;color:parent.hovered&&parent.enabled?Theme.primary:Theme.muted;opacity:presentation.fade*player.detailsOpacity}
                    background:Rectangle {color:parent.down?Theme.high:parent.hovered&&parent.enabled?Qt.rgba(Theme.primary.r,Theme.primary.g,Theme.primary.b,Theme.hoverOpacity):"transparent";radius:Theme.shapeMedium}
                    Rectangle { objectName: "immersiveAlbumFocusRing"; anchors.fill: parent; anchors.margins: -3; radius: Theme.shapeMedium+3; color: "transparent"; border.width: 2; border.color: Theme.focusRing; visible: parent.visualFocus }
                }
            }
            Item {Layout.fillWidth:true;visible:player.displayedLayout==="artwork"}
            // Matching flexible space centres the measure when it fits. On a
            // narrower window, lyrics fill the content width without gutters.
            Item { Layout.fillWidth: true; visible: player.displayedLayout==="lyrics" && body.lyricMeasureFits }
            LyricsView { id: immersiveLyrics; expanded: true; visible:player.displayedLayout!=="artwork" && player.displayedLayout!=="singalong"; Layout.fillWidth: player.displayedLayout!=="lyrics" || !body.lyricMeasureFits; Layout.minimumWidth: 0; Layout.fillHeight: true; Layout.minimumHeight: 0
                Layout.preferredWidth: player.displayedLayout==="lyrics" ? (body.lyricMeasureFits ? player.lyricMeasure : shell.width) : -1
                Layout.maximumWidth: player.displayedLayout==="lyrics" ? (body.lyricMeasureFits ? player.lyricMeasure : shell.width) : Infinity
                Layout.alignment: Qt.AlignHCenter }
            Item { Layout.fillWidth: true; visible: player.displayedLayout==="lyrics" && body.lyricMeasureFits }
            SingAlong { id: immersiveSingAlong; visible:player.displayedLayout==="singalong"; Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.fillHeight: true; Layout.minimumHeight: 0 }
        }
        Item {
            id: coverflowSlot
            Layout.fillWidth: true
            Layout.preferredHeight: player.coverflowReserve
            visible: player.coverflowVisible
            opacity: player.controlsShown?1:0
            enabled: opacity>0
            // Destroy the faded carousel's delegates after its opacity reaches
            // zero. The independent row measure keeps the layout slot in place.
            Behavior on opacity {NumberAnimation {duration:Theme.normal;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.effectsCurve}}
            Loader {
                id: upNext
                anchors.fill: parent
                active: coverflowSlot.opacity>0 && player.coverflowVisible
                sourceComponent: ImmersiveCoverflow { onShowAllRequested: player.queueRequested() }
            }
        }
        // Material replaced the bottom app bar with docked and floating
        // toolbars. The transport floats over the artwork rather than being
        // anchored into it, which is what a floating toolbar is for. It keeps
        // the standard colour style: a vibrant bar over an arbitrary cover
        // would fight whatever colour the artwork happens to be.
        MFloatingToolbar {
            id: transport
            objectName: "immersiveToolbar"
            Layout.alignment: Qt.AlignHCenter
            opacity: player.controlsShown?1:0
            enabled: opacity>0; Accessible.ignored: opacity===0
            Behavior on opacity {NumberAnimation {duration:Theme.normal;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.effectsCurve}}
            content: [
            MButton { symbol: "shuffle"; toggle: true; selected: app.shuffle; tip: app.shuffle?"Shuffle on":"Shuffle off"; Accessible.ignored: transport.opacity===0; onClicked: app.shuffle=!app.shuffle },
            MButton { objectName: "immersivePreviousButton"; symbol: "previous"; tip: "Previous"; enabled: app.queue.count>0; Accessible.ignored: transport.opacity===0; onClicked: app.previous() },
            MButton { objectName: "immersivePlayButton"; busy: app.buffering; morphPlayback:true; symbol: app.playing||app.resolving?"pause":"play"; filled: true; implicitWidth: 80; implicitHeight: 56; tip: app.playing||app.resolving?"Pause":"Play"; enabled: app.queue.count>0; Accessible.ignored: transport.opacity===0; onClicked: app.toggle() },
            MButton { objectName: "immersiveNextButton"; symbol: "next"; tip: "Next"; enabled: app.queue.count>0; Accessible.ignored: transport.opacity===0; onClicked: app.next() },
            MButton { symbol: app.repeat===2?"repeat_one":"repeat"; toggle: true; selected: app.repeat>0; tip: app.repeat===0?"Repeat off":app.repeat===1?"Repeat queue":"Repeat song"; Accessible.ignored: transport.opacity===0; onClicked: app.repeat=(app.repeat+1)%3 }
            ]
        }
        // The bar itself is what sits on the window's centre line, not the row
        // carrying it. Hanging the queue and volume actions off one end pushed
        // the bar off centre while the row stayed centred, which is the kind of
        // thing that reads as crooked without being obviously wrong. The empty
        // slot at the leading end mirrors them, so the two ends weigh the same.
        GridLayout {
            id: seekRow; objectName: "immersiveSeekRow"
            // SliderTokens.kt:88-111 sizes the handle and track, but gives no
            // minimum width. Below 700px the time/seek trio gets the whole
            // first row so a 480px window gives it over 300px; actions wrap
            // below. Seven columns retain the old centred wide arrangement.
            readonly property bool compact: player.width<700
            columns: compact?3:7; columnSpacing:12; rowSpacing:0
            Layout.fillWidth: true; opacity:player.controlsShown?1:0
            enabled: opacity>0; Accessible.ignored: opacity===0
            Behavior on opacity {NumberAnimation {duration:Theme.normal;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.effectsCurve}}
            Item { visible:!seekRow.compact; Layout.preferredWidth: seekTrailing.implicitWidth; Layout.preferredHeight: 1 }
            Item { visible:!seekRow.compact; Layout.fillWidth: true }
            SungText { font.features: {"tnum": 1}; text: app.formatTime(app.position); color: Theme.muted; font.pixelSize: Theme.labelMedium; labelRole: true; Layout.preferredWidth: 40; Accessible.ignored: seekRow.opacity===0 }
            SeekBar { objectName: "immersiveSeek"; Layout.fillWidth: true; Layout.maximumWidth: 640; Accessible.ignored: seekRow.opacity===0 }
            SungText { font.features: {"tnum": 1}; text: app.formatTime(app.duration); color: Theme.muted; font.pixelSize: Theme.labelMedium; labelRole: true; Layout.preferredWidth: 40; horizontalAlignment: Text.AlignRight; Accessible.ignored: seekRow.opacity===0 }
            Item { visible:!seekRow.compact; Layout.fillWidth: true }
            RowLayout {
                id: seekTrailing
                spacing: 12; Layout.columnSpan: seekRow.compact?3:1; Layout.alignment:Qt.AlignRight
                MButton {objectName:"immersiveQueueButton";symbol:"queue";tip:player.externalModalOpen?"":"Queue \u00b7 Ctrl+L";Accessible.name:"Queue \u00b7 Ctrl+L";Accessible.ignored:seekRow.opacity===0;onClicked:player.queueRequested()}
                Item {
                    id: volumeSlot
                    property real measuredWidth: 48
                    property real measuredHeight: 48
                    Layout.preferredWidth: measuredWidth
                    Layout.preferredHeight: measuredHeight
                    // Qt keeps invisible descendants in the accessible tree.
                    // Unload the volume control only after the fade, retaining
                    // its measured slot so the seek bar stays centred.
                    Loader {
                        id: immersiveVolume
                        anchors.fill: parent
                        active: seekRow.opacity>0
                        readonly property bool popupVisible: item ? item.popupVisible : false
                        sourceComponent: VolumeControl { showSlider:false }
                        onLoaded: {
                            volumeSlot.measuredWidth=item.implicitWidth;
                            volumeSlot.measuredHeight=item.implicitHeight;
                        }
                    }
                }
            }
        }
    }
    MMenu {
        id:layoutMenu;objectName:"immersiveLayoutMenu"
        onClosed:{layoutButton.forceActiveFocus(Qt.PopupFocusReason);player.wake();}
        Repeater {
            model:[{key:"artwork",label:"Artwork"},{key:"lyrics",label:"Lyrics"},{key:"split",label:"Split"},{key:"singalong",label:"Sing along"}]
            MMenuItem {required property var modelData;objectName:"immersiveLayout_"+modelData.key;text:modelData.label;checkable:true
                // Offered only where the song can actually drive it.
                enabled:modelData.key!=="singalong" || player.hasTimedLyrics
                checked:player.preferredLayout===modelData.key;onTriggered:player.layoutRequested(modelData.key)}
        }
        MDivider {}
        // The two actions that used to be buttons of their own. The speed one
        // carried its value on its face, so the line says it instead.
        // Menu.kt:324-347 makes the leading icon optional. There is no speed
        // glyph in the bundled set, and a history glyph misnames the action.
        MMenuItem {objectName:"immersiveSpeed";text:"Playback speed · "+Number(app.playbackRate.toFixed(2))+"×";onTriggered:player.speedRequested()}
        MMenuItem {objectName:"immersiveTiming";symbol:"settings";text:"Lyric timing";enabled:app.lyricLines.length>0;onTriggered:player.timingRequested()}
        MDivider {}
        MMenuItem {objectName:"immersiveCoverflowToggle";text:"Up next covers";checkable:true;checked:player.coverflow;onTriggered:player.coverflowRequested(!player.coverflow)}
        MMenuItem {objectName:"immersiveAutoHide";text:"Auto-hide controls";checkable:true;checked:player.autoHideControls;onTriggered:player.autoHideRequested(!player.autoHideControls)}
    }
}
