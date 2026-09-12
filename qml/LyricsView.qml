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
            placeholderText: "Find in lyrics"; selectByMouse: true; font.family: Theme.fontFamily; font.pixelSize: 14; color: Theme.text; placeholderTextColor: Theme.muted
            Accessible.name: "Find in lyrics"
            onTextChanged: searchDelay.restart()
            Keys.onDownPressed: {lyricResults.currentIndex=Math.min(lyricPane.matches.length-1,lyricResults.currentIndex+1);}
            Keys.onUpPressed: {lyricResults.currentIndex=lyricPane.matches.length?Math.max(0,lyricResults.currentIndex-1):-1;}
            Keys.onReturnPressed: {if(searchDelay.running){searchDelay.stop();lyricPane.refreshSearch();}else if(lyricResults.currentIndex<0)lyricPane.refreshSearch();lyricPane.jumpMatch(lyricResults.currentIndex);}
            Keys.onEscapePressed: lyricPane.closeSearch()
        }
        SungText { text: lyricPane.matches.length; visible: lyricSearch.text.length>0; color: Theme.muted; font.pixelSize: 12 }
    }
    Timer { id: searchDelay; interval: 90; onTriggered: lyricPane.refreshSearch() }
    ListView {
        id: lyricResults; objectName: "lyricSearchResults"; anchors.fill: parent; anchors.topMargin: searchControls.height+8; clip: true
        visible: lyricPane.searchOpen && lyricSearch.text.length>0; model: lyricPane.matches; reuseItems: true; spacing: 8
        currentIndex: -1; highlightMoveDuration: app.motion?Theme.fast:0
        ScrollBar.vertical: MScrollBar {}
        delegate: AbstractButton {
            required property var modelData; required property int index; objectName: "lyricSearchResult_"+index
            width: lyricResults.width; implicitHeight: matchText.implicitHeight+24; enabled: modelData.start>=0
            Accessible.name: modelData.text; Accessible.description: modelData.start>=0 ? "Seek to "+app.formatTime(Math.max(0,modelData.start-app.lyricOffset)) : "Untimed lyric"
            onClicked: lyricPane.jumpMatch(index)
            background: Rectangle { radius: 12; color: parent.ListView.isCurrentItem?Theme.high:"transparent" }
            SungText { font.features: {"tnum": 1}; anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter; visible: modelData.start>=0; text: app.formatTime(Math.max(0,modelData.start-app.lyricOffset)); color: Theme.muted; font.pixelSize: 12 }
            contentItem: MatchText { id: matchText; sourceText: modelData.text; query: lyricSearch.text; leftPadding: 12; rightPadding: modelData.start>=0?64:12; topPadding: 12; bottomPadding: 12; wrapMode: Text.Wrap; font.pixelSize: 18; color: Theme.text }
        }
        SungText { anchors.centerIn: parent; visible: lyricPane.matches.length===0; text: "No matches"; color: Theme.muted }
    }
    readonly property int gapSeconds: {app.position;app.lyricLines;app.lyricOffset;return visible && following && !searchOpen && !app.lyricsBusy ? app.lyricGapSeconds : 0;}
    SungText {id:gapCue;objectName:"lyricGapCue";anchors.horizontalCenter:parent.horizontalCenter;anchors.bottom:parent.bottom;height:visible?36:0;visible:lyricPane.gapSeconds>0;text:"Lyrics in "+lyricPane.gapSeconds+" s";color:Theme.muted;font.pixelSize:14;verticalAlignment:Text.AlignVCenter;Accessible.name:text}
    property bool expanded: false
    property bool following: true
    onFollowingChanged: { if(following)liveLyrics.centerCurrent(); }
    onExpandedChanged: liveLyrics.centerCurrent()
    property int textSize: app.lyricTextSize
    onTextSizeChanged: liveLyrics.centerCurrent()
    MBusyIndicator { objectName: "lyricsSpinner"; anchors.centerIn: parent; running: app.lyricsBusy; label: "Loading lyrics" }
    ListView {
        id: liveLyrics; objectName: "liveLyrics"
        anchors.fill: parent; anchors.topMargin: searchControls.height; anchors.bottomMargin:gapCue.visible?44:0; clip: true; spacing: 12
        visible: !app.lyricsBusy && app.lyricLines.length>0 && !(lyricPane.searchOpen && lyricSearch.text.length>0)
        model: app.lyricLines; reuseItems: true; cacheBuffer: 100
        function centerCurrent() {
            Qt.callLater(function(){if(lyricPane.following && liveLyrics.visible && app.lyricIndex>=0)liveLyrics.positionViewAtIndex(app.lyricIndex,ListView.Center);});
        }
        Component.onCompleted: centerCurrent()
        onCountChanged: centerCurrent()
        onVisibleChanged: {if(visible)centerCurrent();}
        onHeightChanged: centerCurrent()
        currentIndex: app.lyricIndex
        preferredHighlightBegin: height*0.35; preferredHighlightEnd: height*0.55
        highlightRangeMode: lyricPane.following ? ListView.ApplyRange : ListView.NoHighlightRange
        highlightMoveDuration: app.motion ? 350 : 0
        highlight: Item {}
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: MScrollBar {}
        onMovementStarted: { lyricPane.following=false; resumeFollow.restart(); }
        delegate: AbstractButton {
            id: lyricLine; objectName: "lyricLine"
            required property var modelData
            required property int index
            width: liveLyrics.width; implicitHeight: lyricLabel.implicitHeight+20
            property bool current: index===app.lyricIndex
            property bool completedHidden: !app.keepCompletedLyrics && (modelData.end>0 ? app.position+app.lyricOffset>=modelData.end : app.lyricIndex>index)
            opacity: completedHidden ? 0 : 1
            enabled: !completedHidden
            Accessible.ignored: completedHidden
            Behavior on opacity { enabled: app.motion; NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve } }
            hoverEnabled: true; focusPolicy: Qt.StrongFocus
            Accessible.name: modelData.text || "Instrumental"
            ToolTip.visible: hovered || visualFocus
            ToolTip.delay: 700
            ToolTip.text: app.formatTime(Math.max(0,modelData.start-app.lyricOffset))
            onClicked: { app.seekLyric(modelData.start);lyricPane.following=true;resumeFollow.stop(); }
            background: Rectangle { radius: 12; color: lyricLine.hovered ? Theme.high : "transparent"; border.width: lyricLine.visualFocus?2:0; border.color: Theme.primary }
            contentItem: SungText {
                id: lyricLabel; objectName: "lyricLabel"
                text: lyricLine.modelData.text || "…"
                leftPadding: 8; rightPadding: 8; topPadding: 10; bottomPadding: 10
                // Reserve the active size so emphasis never reflows adjacent lines.
                font.pixelSize: lyricPane.expanded ? Math.max(24,Math.min(app.lyricTextSize*1.68,width/14*app.lyricTextSize/25)) : app.lyricTextSize; font.weight: Font.Medium
                wrapMode: Text.Wrap; elide: Text.ElideNone; lineHeight: 1.25
                color: Theme.primary
                scale: lyricLine.current ? 1 : 0.86
                transformOrigin: Item.Left
                opacity: lyricLine.current || lyricLine.visualFocus ? 1 : lyricLine.hovered ? 0.8 : app.lyricIndex < 0 ? 0.65 : 0.34
                Behavior on scale { enabled: app.motion; NumberAnimation { duration: 350; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
                Behavior on opacity { enabled: app.motion; NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve } }
                Behavior on color { ColorAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve } }
            }
        }
    }
    Timer { id: resumeFollow; interval: 8000; onTriggered: lyricPane.following=true }
    MButton { anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; text: "Follow lyrics"; filled: true; visible: liveLyrics.visible && !lyricPane.following; onClicked: {lyricPane.following=true;resumeFollow.stop();} }
    ScrollView { id: lyricsScroll; anchors.fill: parent; anchors.topMargin: searchControls.height; visible: !app.lyricsBusy && app.lyricLines.length===0 && !(lyricPane.searchOpen && lyricSearch.text.length>0); contentWidth: availableWidth; clip: true; SungText { width: lyricsScroll.availableWidth; text: app.lyrics || "Lyrics unavailable"; wrapMode: Text.Wrap; elide: Text.ElideNone; font.pixelSize: lyricPane.expanded ? app.lyricTextSize*1.12 : app.lyricTextSize*0.84; lineHeight: 1.55; color: app.lyrics?Theme.text:Theme.muted } }
    MButton { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; text: "Try again"; tonal: true; visible: !app.lyrics && !app.lyricsBusy && !lyricPane.searchOpen && app.currentIndex>=0; onClicked: app.reloadLyrics() }
}
