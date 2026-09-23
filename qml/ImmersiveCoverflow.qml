import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// M3 center-aligned hero carousel over the queue: one large item in the middle
// with small items peeking either side, snap-scrolled. The queue sheet is the
// "show all" companion the carousel guidance asks for on scrolling surfaces.
ColumnLayout {
    id: flow
    objectName: "immersiveCoverflow"
    property bool interactive: true
    // M3 sizes the small carousel item 40-56dp; the large item shrinks with the
    // window so the playing cover above it always stays the dominant one.
    readonly property int small: 48
    readonly property int large: Math.round(Math.max(72,Math.min(104,(Window.window?Window.window.height:800)*0.11)))
    readonly property int cell: large+16
    readonly property int reserved: large+58
    readonly property int centeredIndex: covers.currentIndex
    signal showAllRequested()
    spacing: 6
    ListView {
        id: covers
        objectName: "coverflowView"
        Layout.fillWidth: true
        Layout.preferredHeight: flow.large+16
        orientation: ListView.Horizontal
        model: app.queue
        clip: false
        reuseItems: true
        cacheBuffer: flow.cell*4
        boundsBehavior: Flickable.StopAtBounds
        interactive: flow.interactive
        snapMode: ListView.SnapToItem
        highlightRangeMode: ListView.StrictlyEnforceRange
        preferredHighlightBegin: (width-flow.cell)/2
        preferredHighlightEnd: (width+flow.cell)/2
        // Qt's custom highlight carries a playback change across the strict
        // centre range. DefaultSpatial moves it with its matching curve and
        // duration; the cell width lets ListView track the cover's full bounds.
        highlightFollowsCurrentItem: false
        highlight: Item {
            width: flow.cell; height: covers.height
            x: covers.currentItem ? covers.currentItem.x : 0
            Behavior on x { enabled: app.motion && !covers.recentering; NumberAnimation { objectName: "coverflowHighlightMotion"; duration: Theme.springSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial } }
        }
        property bool recentering: false
        Accessible.role: Accessible.List
        Accessible.name: "Up next"
        // A playback change re-centers the carousel. Because centering always
        // lands on the playing track, it can never look like a user scroll.
        function center(animate) {
            if(app.currentIndex<0 || app.currentIndex>=count)return;
            if(animate){currentIndex=app.currentIndex;return;}
            recentering=true;
            currentIndex=app.currentIndex;
            positionViewAtIndex(app.currentIndex,ListView.Center);
            recentering=false;
        }
        Connections { target: app; function onTrackChanged(){covers.center(true);} }
        Component.onCompleted: center(false)
        onCountChanged: if(!moving && !flicking)Qt.callLater(()=>covers.center(false))
        // Coming to rest on a different cover plays it: the carousel moves
        // through the queue rather than only previewing it.
        Timer {
            id: settle; objectName: "coverflowSettle"; interval: 200
            onTriggered: if(!covers.moving && !covers.dragging && covers.currentIndex>=0 && covers.currentIndex!==app.currentIndex)app.playAt(covers.currentIndex)
        }
        onMovementEnded: settle.restart()
        Keys.onReturnPressed: event=>{if(currentIndex>=0){app.playAt(currentIndex);event.accepted=true;}}
        Keys.onEnterPressed: event=>{if(currentIndex>=0){app.playAt(currentIndex);event.accepted=true;}}
        delegate: Item {
            id: cellItem
            required property var entry
            required property int index
            objectName: "coverflowItem_"+index
            width: flow.cell; height: covers.height
            readonly property real distance: Math.abs((x+width/2)-(covers.contentX+covers.width/2))/flow.cell
            readonly property real emphasis: Math.max(0,1-Math.min(1,distance))
            readonly property real coverSize: flow.small+(flow.large-flow.small)*emphasis
            readonly property bool playing: index===app.currentIndex
            AbstractButton {
                anchors.centerIn: parent
                width: cellItem.coverSize; height: cellItem.coverSize
                focusPolicy: Qt.StrongFocus
                hoverEnabled: true
                Accessible.name: (cellItem.entry.title || "Track")+" · "+(cellItem.entry.artist || "")
                onClicked: app.playAt(cellItem.index)
                background: null
                contentItem: Artwork {
                    objectName: "coverflowArt_"+cellItem.index
                    url: cellItem.entry.art || ""
                    radius: Math.max(8,cellItem.coverSize*0.12)
                    pixels: 260
                    opacity: cellItem.playing ? 1 : 0.55+0.45*cellItem.emphasis
                }
                Rectangle {
                    anchors.fill: parent; anchors.margins: -4
                    radius: Math.max(10,cellItem.coverSize*0.12)+4
                    color: "transparent"
                    border.width: parent.visualFocus ? 2 : cellItem.playing ? 2 : 0
                    border.color: Theme.focusRing
                }
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true; spacing: 8
        Item { Layout.preferredWidth: showAll.width }
        SungText {
            objectName: "coverflowLabel"
            Layout.fillWidth: true; Layout.minimumWidth: 0
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            color: Theme.muted; font.pixelSize: Theme.bodySmall
            text: {
                const item=app.queue.get(covers.currentIndex);
                return covers.currentIndex<0 ? "" : item.title || "";
            }
        }
        MButton { id: showAll; objectName: "coverflowShowAll"; text: "Show all"; tip: "Open the full queue"; onClicked: flow.showAllRequested() }
    }
}
