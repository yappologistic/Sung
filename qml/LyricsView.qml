import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Item {
    id: lyricPane
    objectName: "lyricsView"
    property bool searchOpen: false
    property var matches: []
    property string songId: app.current.id || ""
    onSongIdChanged: { lyricSearch.clear(); matches=[]; searchOpen=false; }
    function openSearch() {searchOpen=true;refreshSearch();Qt.callLater(function(){if(lyricPane.searchOpen){lyricSearch.forceActiveFocus();lyricSearch.selectAll();}});}
    function closeSearch() {searchOpen=false;following=true;resumeFollow.stop();lyricPane.forceActiveFocus();liveLyrics.centerCurrent();}
    function refreshSearch() {matches=app.searchLyrics(lyricSearch.text);lyricResults.currentIndex=matches.length?0:-1;}
    function jumpMatch(index) {
        if(index<0 || index>=matches.length || matches[index].start<0)return;
        app.seekLyric(matches[index].start);closeSearch();
    }
    Connections { target: app; function onLyricsChanged(){if(lyricPane.searchOpen)lyricPane.refreshSearch();} }
    RowLayout {
        id: searchControls; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: visible?48:0; visible: lyricPane.searchOpen; spacing: 6
        MButton { objectName: "closeLyricSearch"; symbol: "back"; tip: "Back to lyrics"; onClicked: lyricPane.closeSearch() }
        MSearchField {
            id: lyricSearch; objectName: "lyricSearchField"; Layout.fillWidth: true; Layout.minimumWidth: 0; implicitHeight: 48
            placeholderText: "Find in lyrics"; selectByMouse: true; font.family: Theme.fontFamily; font.pixelSize: Theme.bodyMedium; color: Theme.text; placeholderTextColor: Theme.muted
            Accessible.name: "Find in lyrics"
            onTextChanged: searchDelay.restart()
            Keys.onDownPressed: {lyricResults.currentIndex=Math.min(lyricPane.matches.length-1,lyricResults.currentIndex+1);}
            Keys.onUpPressed: {lyricResults.currentIndex=lyricPane.matches.length?Math.max(0,lyricResults.currentIndex-1):-1;}
            Keys.onReturnPressed: {if(searchDelay.running){searchDelay.stop();lyricPane.refreshSearch();}else if(lyricResults.currentIndex<0)lyricPane.refreshSearch();lyricPane.jumpMatch(lyricResults.currentIndex);}
            Keys.onEscapePressed: lyricPane.closeSearch()
        }
        SungText { text: lyricPane.matches.length; visible: lyricSearch.text.length>0; color: Theme.muted; font.pixelSize: Theme.labelMedium; labelRole: true }
    }
    Timer { id: searchDelay; interval: 90; onTriggered: lyricPane.refreshSearch() }
    ListView {
        id: lyricResults; objectName: "lyricSearchResults"; anchors.fill: parent; anchors.topMargin: searchControls.height+8; clip: true
        visible: lyricPane.searchOpen && lyricSearch.text.length>0; model: lyricPane.matches; reuseItems: true; spacing: 8
        currentIndex: -1
        // Qt ListView's duration-only move cannot use Material's curve. Its
        // documented custom-highlight path lets FastSpatial move selection.
        highlightFollowsCurrentItem: false
        highlight: Item {
            y: lyricResults.currentItem ? lyricResults.currentItem.y : 0
            Behavior on y { enabled: app.motion; NumberAnimation { objectName: "lyricSearchHighlightMotion"; duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
        }
        ScrollBar.vertical: MScrollBar {}
        delegate: AbstractButton {
            required property var modelData; required property int index; objectName: "lyricSearchResult_"+index
            // Hidden while pooled: a culled delegate still takes Tab (TrackRow.qml).
            property bool pooled: false
            visible: !pooled
            ListView.onPooled: pooled=true
            ListView.onReused: pooled=false
            width: lyricResults.width; implicitHeight: matchText.implicitHeight+24; enabled: modelData.start>=0
            Accessible.name: modelData.text; Accessible.description: modelData.start>=0 ? "Seek to "+app.formatTime(Math.max(0,modelData.start-app.lyricOffset)) : "Untimed lyric"
            onClicked: lyricPane.jumpMatch(index)
            background: Rectangle { radius: Theme.shapeMedium; color: parent.ListView.isCurrentItem?Theme.high:"transparent" }
            SungText { font.features: {"tnum": 1}; anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter; visible: modelData.start>=0; text: app.formatTime(Math.max(0,modelData.start-app.lyricOffset)); color: Theme.muted; font.pixelSize: Theme.labelMedium; labelRole: true }
            contentItem: MatchText { id: matchText; sourceText: modelData.text; query: lyricSearch.text; leftPadding: 12; rightPadding: modelData.start>=0?64:12; topPadding: 12; bottomPadding: 12; wrapMode: Text.Wrap; font.pixelSize: Theme.bodyLarge; color: Theme.text }
        }
        SungText { anchors.centerIn: parent; visible: lyricPane.matches.length===0; text: "No matches"; color: Theme.muted }
    }
    readonly property int gapSeconds: {app.position;app.lyricLines;app.lyricOffset;return visible && following && !searchOpen && !app.lyricsBusy ? app.lyricGapSeconds : 0;}
    SungText {id:gapCue;objectName:"lyricGapCue";anchors.horizontalCenter:parent.horizontalCenter;anchors.bottom:parent.bottom;height:visible?36:0;visible:lyricPane.gapSeconds>0;text:"Lyrics in "+lyricPane.gapSeconds+" s";color:Theme.muted;font.pixelSize:Theme.labelLarge;labelRole:true;verticalAlignment:Text.AlignVCenter;Accessible.name:text}
    property bool expanded: false
    property bool following: true
    onFollowingChanged: { if(following)liveLyrics.centerCurrent(); }
    onExpandedChanged: liveLyrics.centerCurrent()
    property int textSize: app.lyricTextSize
    onTextSizeChanged: liveLyrics.centerCurrent()
    MLoadingIndicator { objectName: "lyricsSpinner"; anchors.centerIn: parent; running: app.lyricsBusy; label: "Loading lyrics" }
    ListView {
        id: liveLyrics; objectName: "liveLyrics"
        anchors.fill: parent; anchors.topMargin: searchControls.height; anchors.bottomMargin:gapCue.visible?44:0; clip: true; spacing: 12
        visible: !app.lyricsBusy && app.lyricLines.length>0 && !(lyricPane.searchOpen && lyricSearch.text.length>0)
        model: app.lyricLines; reuseItems: true; cacheBuffer: 100
        // Half a viewport of scroll room lets the first and last timed lines
        // reach the same reading position as lines in the middle.
        topMargin: height/2; bottomMargin: height/2
        function centerCurrent() {
            Qt.callLater(function(){if(lyricPane.following && liveLyrics.visible && app.lyricIndex>=0)liveLyrics.positionViewAtIndex(app.lyricIndex,ListView.Center);});
        }
        Component.onCompleted: centerCurrent()
        onCountChanged: centerCurrent()
        onVisibleChanged: {if(visible)centerCurrent();}
        onHeightChanged: centerCurrent()
        currentIndex: app.lyricIndex
        property int keyboardIndex: -1
        // Qt ListView is a focus scope; one Tab stop owns keyboard selection.
        // ListView.positionViewAtIndex(..., Center) keeps the chosen lyric in
        // the reading band without guessing contentY for variable text heights.
        activeFocusOnTab: true
        // The playback index owns currentIndex. Keyboard choice is separate,
        // so ListView's own arrow handling must not rewrite that binding.
        keyNavigationEnabled: false
        Accessible.role: Accessible.List
        Accessible.name: {
            const index=keyboardIndex>=0?keyboardIndex:currentIndex;
            const line=index>=0?app.lyricLines[index]:null;
            return "Lyrics"+(line ? ", "+(line.text || "Instrumental")+", "+(index+1)+" of "+count : "");
        }
        onActiveFocusChanged: if(activeFocus)keyboardIndex=currentIndex>=0?currentIndex:0
        function focusLine(index) {
            if(count<1)return;
            keyboardIndex=Math.max(0,Math.min(count-1,index));
            following=false;resumeFollow.restart();
            positionViewAtIndex(keyboardIndex,ListView.Center);
        }
        function seekKeyboardLine() {
            const line=keyboardIndex>=0?app.lyricLines[keyboardIndex]:null;
            if(!line || line.start<0)return;
            app.seekLyric(line.start);
            following=true;resumeFollow.stop();
        }
        // Qt Keys has no Home or End-specific signal. Accept these here so
        // ListView does not also move its playback-bound currentIndex.
        Keys.onPressed: event=>{
            if(event.key===Qt.Key_Up){focusLine(keyboardIndex-1);event.accepted=true;}
            else if(event.key===Qt.Key_Down){focusLine(keyboardIndex+1);event.accepted=true;}
            else if(event.key===Qt.Key_Home){focusLine(0);event.accepted=true;}
            else if(event.key===Qt.Key_End){focusLine(count-1);event.accepted=true;}
            else if(event.key===Qt.Key_Return || event.key===Qt.Key_Enter || event.key===Qt.Key_Space){seekKeyboardLine();event.accepted=true;}
        }
        preferredHighlightBegin: height*0.35; preferredHighlightEnd: height*0.55
        highlightRangeMode: lyricPane.following ? ListView.ApplyRange : ListView.NoHighlightRange
        // Qt ListView's custom highlight moves with DefaultSpatial, which
        // preserves the centred lyric scroll without a duration-only curve.
        highlightFollowsCurrentItem: false
        highlight: Item {
            y: liveLyrics.currentItem ? liveLyrics.currentItem.y : 0
            Behavior on y { enabled: app.motion; NumberAnimation { objectName: "lyricHighlightMotion"; duration: Theme.springSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial } }
        }
        boundsBehavior: Flickable.StopAtBounds
        // The reading column reveals its scroll rail on hover; the side pane
        // keeps the persistent cue used elsewhere in the library.
        ScrollBar.vertical: MScrollBar { opacity: lyricPane.expanded && !hovered && !pressed ? 0 : 1 }
        onMovementStarted: { lyricPane.following=false; resumeFollow.restart(); }
        delegate: Item {
            id: lyricLine; objectName: "lyricLine"
            required property var modelData
            required property int index
            width: liveLyrics.width; implicitHeight: lyricLabel.implicitHeight+20
            property bool current: index===app.lyricIndex
            property bool completedHidden: !app.keepCompletedLyrics && (modelData.end>0 ? app.position+app.lyricOffset>=modelData.end : app.lyricIndex>index)
            readonly property bool hovered: lineHover.hovered
            // A line crossing the clipped viewport should vanish before a
            // glyph is cut. One measured lyric line height is the fade band,
            // so the distance follows the reader's text size in both panes.
            readonly property real edgeTop: y-liveLyrics.contentY
            readonly property real edgeBand: Math.max(1,lyricLabel.lineHeight)
            readonly property real edgeOpacity: current ? 1 : Math.max(0,Math.min(1,
                Math.min(edgeTop,liveLyrics.height-edgeTop-height)/edgeBand))
            opacity: completedHidden ? 0 : edgeOpacity
            enabled: !completedHidden
            activeFocusOnTab: false
            Accessible.role: Accessible.ListItem
            Accessible.ignored: completedHidden
            // Edge opacity follows scrolling directly. Completed-line fades
            // still use DefaultEffects when the line is within the viewport.
            Behavior on opacity { enabled: app.motion && lyricLine.edgeOpacity>=1; NumberAnimation { duration: Theme.springEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springEffects } }
            Accessible.name: modelData.text || "Instrumental"
            ToolTip.visible: hovered || (liveLyrics.activeFocus && liveLyrics.keyboardIndex===index)
            ToolTip.delay: 700
            ToolTip.text: app.formatTime(Math.max(0,modelData.start-app.lyricOffset))
            HoverHandler { id: lineHover }
            TapHandler { onTapped: {app.seekLyric(lyricLine.modelData.start);lyricPane.following=true;resumeFollow.stop();} }
            Rectangle { anchors.fill: parent; radius: Theme.shapeMedium; color: lyricLine.hovered ? Theme.high : "transparent"; border.width: liveLyrics.activeFocus && liveLyrics.keyboardIndex===index ? 2 : 0; border.color: Theme.focusRing }
            SungText {
                id: lyricLabel; objectName: "lyricLabel"
                anchors.fill: parent
                text: lyricLine.modelData.text || "…"
                leftPadding: 8; rightPadding: 8; topPadding: 10; bottomPadding: 10
                // Reserve the active size so emphasis never reflows adjacent lines.
                scaled: true
                font.pixelSize: lyricPane.expanded ? Math.max(24,Math.min(app.lyricTextSize*1.68,width/14*app.lyricTextSize/25)) : app.lyricTextSize; font.weight: Font.Medium
                wrapMode: Text.Wrap; elide: Text.ElideNone; lineSpacing: 1.25
                // Material onSurfaceVariant carries lower-emphasis reading
                // text. WCAG 1.4.3 requires 4.5:1 here at body size; the
                // immersive size requires 3:1. The active line keeps primary.
                color: lyricLine.current || (liveLyrics.activeFocus && liveLyrics.keyboardIndex===index) ? Theme.primary : Theme.muted
                scale: lyricLine.current ? 1 : 0.86
                transformOrigin: Item.Left
                // DefaultSpatial changes the size of the focused lyric line.
                Behavior on scale { id: lyricScaleBehavior; enabled: app.motion; NumberAnimation { objectName: "lyricScaleMotion"; duration: Theme.springSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial } }
                Behavior on color { ColorAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve } }
            }
        }
    }
    Timer { id: resumeFollow; interval: 8000; onTriggered: lyricPane.following=true }
    MButton { anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; text: "Follow lyrics"; filled: true; visible: liveLyrics.visible && !lyricPane.following; onClicked: {lyricPane.following=true;resumeFollow.stop();} }
    ScrollView { id: lyricsScroll; anchors.fill: parent; anchors.topMargin: searchControls.height; visible: !app.lyricsBusy && app.lyricLines.length===0 && !(lyricPane.searchOpen && lyricSearch.text.length>0); contentWidth: availableWidth; clip: true; SungText { width: lyricsScroll.availableWidth; text: app.lyrics || "Lyrics unavailable"; scaled: true; wrapMode: Text.Wrap; elide: Text.ElideNone; font.pixelSize: lyricPane.expanded ? app.lyricTextSize*1.12 : app.lyricTextSize*0.84; lineSpacing: 1.55; color: app.lyrics?Theme.text:Theme.muted } }
    MButton { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; text: "Try again"; tonal: true; visible: !app.lyrics && !app.lyricsBusy && !lyricPane.searchOpen && app.currentIndex>=0; onClicked: app.reloadLyrics() }
}
