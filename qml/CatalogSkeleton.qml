import QtQuick
Item {
    id: skeleton; objectName: "catalogSkeleton"
    property bool loading: false
    property bool cards: false
    property bool revealed: false
    readonly property bool animating: visible && app.motion && Window.window && Window.window.visible && Window.window.visibility!==Window.Minimized
    visible: loading && revealed; clip: true
    Accessible.role: Accessible.Indicator; Accessible.name: "Loading music"
    onLoadingChanged: {delay.stop();revealed=false;if(loading)delay.start();}
    Timer { id: delay; interval: 150; onTriggered: skeleton.revealed=skeleton.loading }
    Component.onCompleted: if(loading)delay.start()
    // This continuous loading shimmer keeps its 1500ms period, which is not a
    // control state spring. animating stops it when hidden or motion is off.
    // Material's skeleton pulses, and the pulse travels: it starts at the top
    // left and moves down to the bottom right rather than brightening the whole
    // surface at once, which is what tells the eye the screen is filling rather
    // than flashing. Each block reads the wave at its own place in the layout.
    property real wave: 0
    NumberAnimation on wave {
        objectName: "catalogShimmerAnimation"
        running: skeleton.animating; loops: Animation.Infinite
        from: 0; to: 2*Math.PI; duration: 1500
    }
    readonly property real diagonal: Math.max(1, skeleton.width + skeleton.height)
    function pulseAt(x,y) {
        return 0.55 + 0.35*(0.5 + 0.5*Math.sin(skeleton.wave - 2*Math.PI*(x+y)/skeleton.diagonal))
    }

    // A placeholder row is laid out as TrackRow lays out a real one: the
    // ListTokens 16dp leading space, the row's cover at its size and corner,
    // 12dp to the text, and the list's 4dp between rows. When the songs
    // arrive they land where their placeholders were instead of shifting.
    readonly property int rowStride: Theme.rowHeight + 4
    readonly property int textStart: 16 + Theme.rowArtwork + 12
    Item {
        id: shapes; anchors.fill: parent
        Repeater {
            model: skeleton.cards?Math.min(8,Math.max(1,Math.floor(skeleton.width/180))*2):Math.min(8,Math.ceil(skeleton.height/skeleton.rowStride))
            Item {
                required property int index
                readonly property int columns: Math.max(1,Math.floor(skeleton.width/180))
                x: skeleton.cards?(index%columns)*(skeleton.width/columns):0
                y: skeleton.cards?Math.floor(index/columns)*230:index*skeleton.rowStride
                width: skeleton.cards?skeleton.width/columns-20:skeleton.width; height: skeleton.cards?210:Theme.rowHeight
                opacity: skeleton.animating ? skeleton.pulseAt(x,y) : 0.7
                Rectangle {
                    x: skeleton.cards?0:16; y: skeleton.cards?0:(parent.height-height)/2
                    width: skeleton.cards?Math.min(parent.width,180):Theme.rowArtwork; height: width
                    radius: skeleton.cards?Theme.shapeLargeIncreased:Theme.shapeSmall; color: Theme.high
                }
                Rectangle { x: skeleton.cards?0:skeleton.textStart; y: skeleton.cards?Math.min(parent.width,180)+14:parent.height/2-14; width: skeleton.cards?parent.width*0.72:parent.width*0.38; height: 12; radius: Theme.shapeSmall; color: Theme.high }
                Rectangle { x: skeleton.cards?0:skeleton.textStart; y: skeleton.cards?Math.min(parent.width,180)+36:parent.height/2+6; width: skeleton.cards?parent.width*0.45:parent.width*0.23; height: 9; radius: Theme.shapeExtraSmall; color: Theme.high }
            }
        }
    }
}
