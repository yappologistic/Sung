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
    // The visualizer needs nothing from the song but its sound, so it is
    // always available.
    readonly property string effectiveLayout: preferredLayout==="visualizer" ? "visualizer"
        : preferredLayout==="singalong" ? (hasTimedLyrics?"singalong":"artwork")
        : hasLyrics && ["split","lyrics"].indexOf(preferredLayout)>=0 ? preferredLayout : "artwork"
    // The artwork and the visualizer both give the cover the screen; the
    // visualizer cuts it to a shape and rings it with the sound.
    readonly property bool coverAlone: displayedLayout==="artwork" || displayedLayout==="visualizer"
    readonly property bool visualizing: displayedLayout==="visualizer"
    // The ring's share of the square the cover would otherwise fill. A third
    // of the diameter leaves the bars a sixth of it on each side, long enough
    // to read a kick from across the room and short enough that the cover is
    // still the thing on screen.
    readonly property real visualizerScale: 1.5
    // The visualizer's cover changes shape every so often. Material keeps
    // abstract shapes for imagery and decorative moments, and names sound as
    // one of the changes a morph is for (Shape, "Morph shapes to connect
    // function and feeling", "Emphasize aesthetic moments with shape"). The
    // set is the part of the library that is round and even all the way
    // round: each fills its box both ways, so the picture stays centred and
    // the ring keeps its footprint, and none reads as a symbol ("Shape is
    // versatile, not semantic"), which rules out the heart, the arrow and the
    // triangle in a player whose buttons use them. Burst and boom are left
    // out too: their points cut away most of the picture.
    readonly property var visualizerShapes: ["cookie12Sided","cookie9Sided","cookie7Sided","cookie6Sided","cookie4Sided","sunny","verySunny","softBurst","clover8Leaf","clover4Leaf","flower","puffyDiamond"]
    property string visualizerShape: visualizerShapes[0]
    property string visualizerNextShape: ""
    property real visualizerMorph: 0
    onVisualizingChanged: {visualizerShapeMorph.stop();visualizerNextShape="";visualizerMorph=0;visualizerShape=visualizerShapes[0]}
    // Eight to sixteen seconds, a few bars of most songs: often enough to be
    // noticed, seldom enough that the cover stays a picture ("Use abstract
    // shapes sparingly"). It waits while the music is paused, since the change
    // belongs to the sound, and while the window cannot be seen.
    function visualizerShapeWait() { return 8000+Math.round(Math.random()*8000) }
    Timer {
        id: visualizerShapeTimer; objectName: "visualizerShapeTimer"
        interval: player.visualizerShapeWait()
        running: player.visualizing && app.playing && app.motion && player.visible && !!player.Window.window && player.Window.window.visible
                 && player.Window.window.visibility!==Window.Minimized && !visualizerShapeMorph.running
        onTriggered: {
            const others=player.visualizerShapes.filter(name => name!==player.visualizerShape)
            player.visualizerNextShape=others[Math.floor(Math.random()*others.length)]
            interval=player.visualizerShapeWait()
            visualizerShapeMorph.restart()
        }
    }
    // Shape morphs use the expressive motion scheme (Shape, Shape morph). The
    // cover is the largest thing on screen, so it takes the slow spatial
    // spring, whose stiffness of 200 is also what Compose's own sequence of
    // shapes morphs at (LoadingIndicator.kt). It rings, so a lobe overshoots
    // and settles; the cover, its ring and its focus outline all read this
    // one value.
    NumberAnimation {
        id: visualizerShapeMorph; objectName: "visualizerShapeMorph"
        target: player; property: "visualizerMorph"; from: 0; to: 1
        duration: Theme.springSlowSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSlowSpatial
        onFinished: {player.visualizerShape=player.visualizerNextShape;player.visualizerNextShape="";player.visualizerMorph=0}
    }
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
    readonly property real coverColumnWidth: Math.max(200,width*(coverAlone?0.55:0.34))
    // Material has no now-playing text measure token. 520px caps the title's
    // two lines; the column sets the floor when it is narrower.
    readonly property real detailsMeasure: Math.min(520,coverColumn.width)
    // With the cover alone, the title, artist and album centre under it as a
    // narrowing stack: the cover is the largest square that fits, so its edges
    // move with the window while its centre does not. Beside lyrics they stay
    // left-aligned, reading with the lines next to them.
    readonly property bool detailsCentred: coverAlone
    // A short window makes the cover the height it can have, so it sits in
    // the middle of a column wider than itself. Left-aligned details start
    // where the cover starts, not at the column's edge, or a small cover
    // leaves them hanging off to its left.
    readonly property real coverInset: detailsCentred ? 0 : Math.max(0,(coverSlot.width-immersiveArt.width)/2)
    readonly property real detailsWidth: detailsCentred ? detailsMeasure : Math.min(520,coverColumn.width-coverInset)
    readonly property int detailsAlignment: detailsCentred ? Qt.AlignHCenter : Qt.AlignLeft
    // The page margin (16dp compact, 24dp beyond) between the cover and the
    // title, so a height-limited cover gives way instead of meeting the text.
    readonly property real coverGap: width<600?Theme.spaceLarge:Theme.spaceExtraLarge
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
        -coverflowReserve-4*shell.spacing-title.implicitHeight-(detailsCentred?coverGap:0)-2*48
    readonly property bool coverflowVisible: coverflow && app.queue.count>0 &&
        (displayedLayout==="lyrics" || displayedLayout==="singalong" ||
         coverflowCoverBudget/(visualizing?visualizerScale:1)>=160)
    function hasKeyboardFocus(item) {
        for(let p=item;p && p!==player;p=p.parent)
            if(p.visualFocus===true || p.handlesTextInput===true)return true;
        return false;
    }
    function wake() { controlsShown=true;idle.restart(); }
    function showLyricsSearch() {
        if(["artwork","singalong","visualizer"].indexOf(preferredLayout)>=0)layoutRequested("lyrics");
        immersiveLyrics.openSearch();wake();
    }
    // A layout fade can reveal Lyrics after openSearch's first focus request.
    // Repeat the request when its pane enters the visible layout.
    onDisplayedLayoutChanged: if(displayedLayout==="lyrics" && immersiveLyrics.searchOpen)immersiveLyrics.openSearch()
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
            MButton { objectName: "immersiveLyricSearchButton"; symbol: "search"; tip: "Find in lyrics"; enabled: player.hasLyrics; Accessible.ignored: topControls.opacity===0; onClicked: player.showLyricsSearch() }
            MButton { symbol: "heart"; selected: app.liked; tip: app.liked?"Unlike":"Like"; enabled: app.currentIndex>=0; Accessible.ignored: topControls.opacity===0; onClicked: app.toggleLike(app.current) }
            MButton {id:layoutButton;objectName:"immersiveLayoutButton";symbol:"more";tip:"More actions";selected:layoutMenu.visible;Accessible.ignored: topControls.opacity===0;onClicked:layoutMenu.popup(layoutButton,width-layoutMenu.width,height+4)}
        }
        RowLayout {
            id: body; objectName:"immersiveBody"
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0; spacing: Math.max(24,player.width*0.055)
            // shell is anchored to the window, so this decision cannot feed
            // back into the width of the RowLayout it sizes.
            readonly property bool lyricMeasureFits: shell.width >= player.lyricMeasure+2*spacing
            Item {Layout.fillWidth:true;visible:player.coverAlone}
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
                    Loader {
                        id: spectrumRing
                        active: player.visualizing
                        // Centred on the cover itself, and wider than it by an
                        // even number of pixels, so the two centres round to
                        // the same pixel and the ring is exactly concentric.
                        anchors.centerIn: immersiveArt
                        width: immersiveArt.width+2*Math.floor((Math.min(parent.width,parent.height)-immersiveArt.width)/2); height: width
                        sourceComponent: SpectrumRing { shape: immersiveArt.shape; toShape: immersiveArt.toShape; morph: immersiveArt.morph; coverSize: immersiveArt.width; running: app.playing }
                    }
                    Artwork { id: immersiveArt; objectName: "immersiveArtwork"; anchors.centerIn: parent
                        // In the visualizer the cover is a whole number of
                        // pixels: centring a fractional cover rounds its
                        // position, and the ring centred on it then hung up to
                        // a pixel past the slot.
                        width: Math.max(80,player.visualizing ? Math.floor(Math.min(parent.width,parent.height)/player.visualizerScale) : Math.min(parent.width,parent.height)); height: width
                        // It starts as Material's twelve-sided cookie, close
                        // enough to a circle to read as a record, and moves
                        // through the set above.
                        shape: player.visualizing ? player.visualizerShape : ""
                        toShape: player.visualizing ? player.visualizerNextShape : ""
                        morph: player.visualizerMorph
                        url: app.current.art || ""; motionUrl: app.currentMotionArt; crossfade:true; opacity: player.coverHidden?0:1; radius: Theme.shapeExtraLarge; pixels: 850; highResolution: true; fit:app.currentArtworkFit
                        AbstractButton {anchors.fill:parent;Accessible.name:"View artwork";focusPolicy:Qt.StrongFocus;onClicked:player.artworkRequested()
                            background:Item {
                                Rectangle {anchors.fill:parent;visible:!immersiveArt.shape;color:"transparent";radius:Theme.shapeExtraLarge;border.width:parent.parent.visualFocus?2:0;border.color:Theme.focusRing}
                                MShape {objectName:"immersiveArtworkFocusRing";anchors.fill:parent;visible:!!immersiveArt.shape && parent.parent.visualFocus;shape:immersiveArt.shape||"circle";toShape:immersiveArt.toShape;progress:immersiveArt.morph;color:"transparent";strokeColor:Theme.focusRing;strokeWidth:2}
                            }}
                    }
                }
                SungText {
                    id: title
                    objectName: "immersiveTitle"
                    text: presentation.shown.title || "Nothing playing"
                    opacity: presentation.fade*player.detailsOpacity
                    transform: Translate { x: presentation.offset }
                    Layout.fillWidth: true
                    Layout.maximumWidth: player.detailsWidth
                    Layout.alignment: player.detailsAlignment
                    Layout.leftMargin: player.coverInset
                    Layout.topMargin: player.detailsCentred ? player.coverGap : 0
                    // Typography.kt:115-133 reads the TypeScaleTokens roles.
                    // TypeScaleTokens.kt:117-127,343-353 gives headline-large
                    // 32sp/40sp with medium emphasis, nearest the old 30px;
                    // :207-217,433-443 gives narrow title-large 22sp/28sp.
                    typeRole: player.width<900?"titleLarge":"headlineLarge"
                    emphasized: true
                    font.pixelSize: player.width<900?Theme.titleLarge:Theme.headlineLarge
                    horizontalAlignment: player.detailsCentred ? Text.AlignHCenter : Text.AlignLeft

                    // Qt Text.WordWrap keeps whole words; ElideRight marks the
                    // last line when the two-line limit omits the rest.
                    wrapMode: Text.WordWrap
                    elide: Text.ElideRight
                    maximumLineCount: 2
                    clip: true
                }
                // Button.kt:1015,1025 places 12dp inside a text button. Centred
                // under the cover, each link hugs its label the way a text
                // button does; left-aligned beside lyrics, it reaches 12dp into
                // the margin so its text keeps the title's edge.
                Item {
                    Layout.fillWidth: true
                    Layout.maximumWidth: player.detailsWidth
                    Layout.alignment: player.detailsAlignment
                    Layout.leftMargin: player.coverInset
                    implicitHeight: 48
                    AbstractButton {
                        objectName: "immersiveArtistButton"
                        x: player.detailsCentred ? (parent.width-width)/2 : -12
                        width: player.detailsCentred ? Math.min(parent.width+24,implicitContentWidth+24) : parent.width+24
                        height: parent.height
                        leftPadding: 12; rightPadding: 12
                        enabled:!!player.artistTarget.kind && presentation.shown.id===app.current.id;focusPolicy:Qt.StrongFocus
                        Accessible.name: "Open artist \u00b7 "+(app.current.artist || "")
                        onClicked: player.collectionRequested(player.artistTarget)
                        contentItem:SungText {text:presentation.shown.artist || "";font.pixelSize:Theme.bodyLarge;color:parent.hovered&&parent.enabled?Theme.primary:Theme.muted;opacity:presentation.fade*player.detailsOpacity}
                        background:Rectangle {color:parent.down?Theme.high:parent.hovered&&parent.enabled?Qt.rgba(Theme.primary.r,Theme.primary.g,Theme.primary.b,Theme.hoverOpacity):"transparent";radius:Theme.shapeMedium}
                        // MButton's ring sits 3px outside the control and follows
                        // its shape, with a 2px Theme.focusRing stroke.
                        Rectangle { objectName: "immersiveArtistFocusRing"; anchors.fill: parent; anchors.margins: -3; radius: Theme.shapeMedium+3; color: "transparent"; border.width: 2; border.color: Theme.focusRing; visible: parent.visualFocus }
                    }
                }
                Item {
                    // IconButton.kt:242-249 applies a 48dp minimum hit area.
                    // Zero column spacing keeps the 48dp slots contiguous.
                    Layout.fillWidth: true
                    Layout.maximumWidth: player.detailsWidth
                    Layout.alignment: player.detailsAlignment
                    Layout.leftMargin: player.coverInset
                    implicitHeight: 48
                    visible: !!app.current.album
                    AbstractButton {
                        objectName: "immersiveAlbumButton"
                        x: player.detailsCentred ? (parent.width-width)/2 : -12
                        width: player.detailsCentred ? Math.min(parent.width+24,implicitContentWidth+24) : parent.width+24
                        height: parent.height
                        leftPadding: 12; rightPadding: 12
                        enabled:!!player.albumTarget.kind && presentation.shown.id===app.current.id;focusPolicy:Qt.StrongFocus
                        Accessible.name: "Open album \u00b7 "+(app.current.album || "")
                        onClicked: player.collectionRequested(player.albumTarget)
                        contentItem:SungText {text:presentation.shown.album || "";font.pixelSize:Theme.labelLarge;labelRole:true;color:parent.hovered&&parent.enabled?Theme.primary:Theme.muted;opacity:presentation.fade*player.detailsOpacity}
                        background:Rectangle {color:parent.down?Theme.high:parent.hovered&&parent.enabled?Qt.rgba(Theme.primary.r,Theme.primary.g,Theme.primary.b,Theme.hoverOpacity):"transparent";radius:Theme.shapeMedium}
                        Rectangle { objectName: "immersiveAlbumFocusRing"; anchors.fill: parent; anchors.margins: -3; radius: Theme.shapeMedium+3; color: "transparent"; border.width: 2; border.color: Theme.focusRing; visible: parent.visualFocus }
                    }
                }
            }
            Item {Layout.fillWidth:true;visible:player.coverAlone}
            // Matching flexible space centres the measure when it fits. On a
            // narrower window, lyrics fill the content width without gutters.
            Item { Layout.fillWidth: true; visible: player.displayedLayout==="lyrics" && body.lyricMeasureFits }
            LyricsView { id: immersiveLyrics; expanded: true; visible:!player.coverAlone && player.displayedLayout!=="singalong"; Layout.fillWidth: player.displayedLayout!=="lyrics" || !body.lyricMeasureFits; Layout.minimumWidth: 0; Layout.fillHeight: true; Layout.minimumHeight: 0
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
        // Material's own music player, from the icon button guidelines (Size
        // and width): the main action is the most visually prominent, "like
        // playing and pausing a song". Previous and next are narrow tonal
        // buttons and play is a wide filled one, all one size in one standard
        // button group, so a press in one makes the others give way. Play is
        // the square shape and turns round while the song plays, the swap the
        // button group guidelines ask of a selected button. Each button brings
        // its own container, which is what holds it apart from the cover
        // behind ("use icons with a background to make them easy to see on any
        // surface"), so there is no toolbar under them.
        // Shuffle and repeat are toggles outside the group, one size and one
        // gap on either side of it, so the group is what sits on the centre
        // line and a press inside it never moves them.
        RowLayout {
            id: transport
            objectName: "immersiveTransport"
            // Button groups, Adaptive design: larger, wider buttons in large and
            // extra-large windows (1200dp and wider), smaller ones below. The
            // height floor is this view's own: under 800dp, 96dp of transport
            // takes the room the cover needs.
            readonly property string buttonSize: player.width>=1200 && player.height>=800 ? "large" : "medium"
            Layout.alignment: Qt.AlignHCenter
            spacing: transportGroup.spacing
            opacity: player.controlsShown?1:0
            enabled: opacity>0; Accessible.ignored: opacity===0
            Behavior on opacity {NumberAnimation {duration:Theme.normal;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.effectsCurve}}
            MButton { objectName: "immersiveShuffleButton"; symbol: "shuffle"; toggle: true; selected: app.shuffle; tip: app.shuffle?"Shuffle on":"Shuffle off"; Accessible.ignored: transport.opacity===0; onClicked: app.shuffle=!app.shuffle }
            MButtonGroup {
                id: transportGroup
                objectName: "immersiveTransportGroup"
                size: transport.buttonSize
                MButton { objectName: "immersivePreviousButton"; symbol: "previous"; tonal: true; size: transport.buttonSize; iconWidth: "narrow"; tip: "Previous"; enabled: app.queue.count>0; Accessible.ignored: transport.opacity===0; onClicked: app.previous() }
                MButton { objectName: "immersivePlayButton"; busy: app.buffering; morphPlayback:true; symbol: app.playing||app.resolving?"pause":"play"; filled: true; square: true; toggle: true; selected: app.playing||app.resolving; size: transport.buttonSize; iconWidth: "wide"; tip: app.playing||app.resolving?"Pause":"Play"; enabled: app.queue.count>0; Accessible.ignored: transport.opacity===0; onClicked: app.toggle() }
                MButton { objectName: "immersiveNextButton"; symbol: "next"; tonal: true; size: transport.buttonSize; iconWidth: "narrow"; tip: "Next"; enabled: app.queue.count>0; Accessible.ignored: transport.opacity===0; onClicked: app.next() }
            }
            MButton { objectName: "immersiveRepeatButton"; symbol: app.repeat===2?"repeat_one":"repeat"; toggle: true; selected: app.repeat>0; tip: app.repeat===0?"Repeat off":app.repeat===1?"Repeat queue":"Repeat song"; Accessible.ignored: transport.opacity===0; onClicked: app.repeat=(app.repeat+1)%3 }
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
            model:[{key:"artwork",label:"Artwork"},{key:"lyrics",label:"Lyrics"},{key:"split",label:"Split"},{key:"singalong",label:"Sing along"},{key:"visualizer",label:"Visualizer"}]
            MMenuItem {required property var modelData;objectName:"immersiveLayout_"+modelData.key;text:modelData.label;checkable:true
                // Offered only where the song can actually drive it.
                enabled:modelData.key!=="singalong" || player.hasTimedLyrics
                checked:player.preferredLayout===modelData.key;onTriggered:player.layoutRequested(modelData.key)}
        }
        MDivider {}
        // The two actions that used to be buttons of their own. The speed one
        // carried its value on its face, so the line says it instead.
        // Menu.kt:324-347 gives an item an optional leading icon. Material
        // Symbols' "speed" names this action; the history glyph it once wore
        // named a different one.
        MMenuItem {objectName:"immersiveSpeed";symbol:"speed";text:"Playback speed · "+Number(app.playbackRate.toFixed(2))+"×";onTriggered:player.speedRequested()}
        MMenuItem {objectName:"immersiveTiming";symbol:"settings";text:"Lyric timing";enabled:app.lyricLines.length>0;onTriggered:player.timingRequested()}
        MDivider {}
        MMenuItem {objectName:"immersiveCoverflowToggle";text:"Up next covers";checkable:true;checked:player.coverflow;onTriggered:player.coverflowRequested(!player.coverflow)}
        MMenuItem {objectName:"immersiveAutoHide";text:"Auto-hide controls";checkable:true;checked:player.autoHideControls;onTriggered:player.autoHideRequested(!player.autoHideControls)}
    }
}
