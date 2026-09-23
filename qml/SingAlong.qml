import QtQuick
import QtQuick.Controls

// Sing along. The lyrics view is for reading; this one is for singing, so it
// answers a different question: not "what are the words" but "where are we in
// them right now". The line being sung fills from its first syllable to its
// last, and the lines around it stand back far enough to be read ahead without
// competing.
//
// Two things keep it smooth. The fill is animated across each line at the pace
// the line is sung, rather than being redrawn wherever playback happens to
// report itself, so it sweeps instead of stepping four times a second. And
// every line is set at one size, with emphasis carried by scale, so changing
// line never re-shapes text.
Item {
    id: root
    objectName: "singAlong"

    readonly property int activeIndex: app.lyricIndex
    readonly property bool ready: app.lyricLines.length>0 && !app.lyricsBusy
    // Sized from the window so a line lands somewhere near a comfortable
    // measure at any width, then capped so it never outgrows the display scale.
    readonly property int lineSize: Math.max(28,Math.min(72,Math.round(width/22)))
    readonly property real restingScale: 0.62

    // 0 to 1 across the line being sung. Playback reports itself four times a
    // second; this carries on between those reports and re-anchors on each one.
    property real fill: 0
    NumberAnimation {
        id: sweep
        target: root
        property: "fill"
        to: 1
        easing.type: Easing.Linear
    }
    function resync() {
        sweep.stop()
        const measured = app.lyricProgress
        if (measured < 0) { fill = 0; return }
        fill = measured
        if (!app.playing || !app.motion) return
        const remaining = (1-measured)*app.lyricSpan/Math.max(0.1,app.playbackRate)
        if (remaining <= 16) { fill = 1; return }
        sweep.duration = remaining
        sweep.start()
    }
    Component.onCompleted: resync()
    onVisibleChanged: resync()
    Connections {
        target: app
        function onPositionChanged() { root.resync() }
        function onLyricIndexChanged() { root.resync() }
        function onPlaybackChanged() { root.resync() }
        function onSeeked() { root.resync() }
        function onLyricsChanged() { root.resync() }
    }

    ListView {
        id: lines
        objectName: "singAlongLines"
        anchors.fill: parent
        anchors.leftMargin: 48
        anchors.rightMargin: 48
        // The words keep clear of the countdown rather than running under it.
        anchors.bottomMargin: waiting.visible ? waiting.height+16 : 0
        visible: root.ready
        clip: true
        // Every line reserves the sung line's height, so the run of words never
        // shifts as emphasis moves through it.
        // Scale already opens a gap around every line, so the list adds little.
        spacing: Math.round(root.lineSize*0.10)
        model: app.lyricLines
        reuseItems: true
        cacheBuffer: 200
        boundsBehavior: Flickable.StopAtBounds
        interactive: false
        // Padding above and below the words, so even a short lyric can bring
        // its first and last lines to the place the eye is already looking.
        topMargin: height*0.34
        bottomMargin: height*0.5
        currentIndex: root.activeIndex
        // The sung line sits just above centre, which leaves more of what is
        // coming visible than what has gone.
        preferredHighlightBegin: height*0.38
        preferredHighlightEnd: height*0.46
        highlightRangeMode: ListView.ApplyRange
        // Qt ListView's custom-highlight path allows DefaultSpatial to move
        // the reading position with its published duration and curve.
        highlightFollowsCurrentItem: false
        highlight: Item {
            y: lines.currentItem ? lines.currentItem.y : 0
            Behavior on y { enabled: app.motion; NumberAnimation { objectName: "singAlongHighlightMotion"; duration: Theme.springSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial } }
        }

        function centre() {
            Qt.callLater(function(){ if(lines.visible && root.activeIndex>=0) lines.positionViewAtIndex(root.activeIndex,ListView.Center); });
        }
        Component.onCompleted: centre()
        onCountChanged: centre()
        onVisibleChanged: if(visible)centre()

        delegate: Item {
            id: line
            required property var modelData
            required property int index
            readonly property bool current: index===root.activeIndex
            readonly property bool past: root.activeIndex>=0 && index<root.activeIndex
            width: lines.width
            implicitHeight: body.implicitHeight

            // Everything but the line being sung stands back; what has already
            // been sung stands back furthest.
            opacity: current ? 1 : past ? 0.26 : 0.42
            Behavior on opacity { enabled: app.motion; NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve } }
            // Scale, not size: changing a font size would re-shape the text on
            // every frame of the transition, which is what makes it stutter.
            scale: current ? 1 : root.restingScale
            transformOrigin: Item.Center
            // DefaultSpatial changes the size of the active lyric line.
            Behavior on scale { enabled: app.motion; NumberAnimation { objectName: "singAlongScaleMotion"; duration: Theme.springSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial } }

            Text {
                id: body
                objectName: line.current ? "singAlongCurrent" : "singAlongLine"
                width: parent.width
                text: line.modelData.text || "…"
                font.family: Theme.fontFamily
                font.pixelSize: root.lineSize
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                lineHeight: 1.2
                // Unsung text is the same colour held at reading contrast, so
                // the fill below reads as the line being consumed.
                color: Theme.text
            }

            // The fill: the same line, in the accent colour, revealed from the
            // left exactly as far as playback has travelled through it.
            Item {
                objectName: "singAlongFill"
                anchors.left: body.left
                anchors.top: body.top
                height: body.height
                width: line.current ? body.width*Math.max(0,Math.min(1,root.fill)) : line.past ? body.width : 0
                visible: line.current || line.past
                clip: true
                Text {
                    width: body.width
                    text: body.text
                    font: body.font
                    horizontalAlignment: body.horizontalAlignment
                    wrapMode: body.wrapMode
                    lineHeight: body.lineHeight
                    color: Theme.primary
                }
            }
        }
    }

    // A countdown through instrumental stretches, so a long gap does not read as
    // the words having stopped working. It sits under them rather than across
    // them: the words stay on screen through a gap, and a cue laid over them
    // would leave neither readable.
    Column {
        id: waiting
        objectName: "singAlongWaiting"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        spacing: 12
        visible: root.ready && root.activeIndex<0
        SungText {
            objectName: "singAlongCue"
            scaled: true
            anchors.horizontalCenter: parent.horizontalCenter
            text: app.lyricGapSeconds>0 ? "Lyrics in "+app.lyricGapSeconds+" s" : "♪"
            color: Theme.muted
            font.pixelSize: Math.max(14,Math.round(root.lineSize*0.30))
        }
    }

    MLoadingIndicator { anchors.centerIn: parent; running: app.lyricsBusy; label: "Loading lyrics" }
    SungText {
        objectName: "singAlongUnavailable"
        anchors.centerIn: parent
        visible: !root.ready && !app.lyricsBusy
        text: "No timed lyrics for this song"
        color: Theme.muted
        font.pixelSize: Theme.bodyLarge
    }
}
