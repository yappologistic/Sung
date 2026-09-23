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

    Item {
        id: shapes; anchors.fill: parent
        Repeater {
            model: skeleton.cards?Math.min(8,Math.max(1,Math.floor(skeleton.width/180))*2):Math.min(8,Math.ceil(skeleton.height/80))
            Item {
                required property int index
                readonly property int columns: Math.max(1,Math.floor(skeleton.width/180))
                x: skeleton.cards?(index%columns)*(skeleton.width/columns):0
                y: skeleton.cards?Math.floor(index/columns)*230:index*80
                width: skeleton.cards?skeleton.width/columns-20:skeleton.width; height: skeleton.cards?210:72
                opacity: skeleton.animating ? skeleton.pulseAt(x,y) : 0.7
                Rectangle { width: skeleton.cards?Math.min(parent.width,180):52; height: width; radius: skeleton.cards?20:12; color: Theme.high }
                Rectangle { x: skeleton.cards?0:68; y: skeleton.cards?Math.min(parent.width,180)+14:14; width: skeleton.cards?parent.width*0.72:parent.width*0.38; height: 12; radius: Theme.shapeSmall; color: Theme.high }
                Rectangle { x: skeleton.cards?0:68; y: skeleton.cards?Math.min(parent.width,180)+36:38; width: skeleton.cards?parent.width*0.45:parent.width*0.23; height: 9; radius: Theme.shapeExtraSmall; color: Theme.high }
            }
        }
    }
}
