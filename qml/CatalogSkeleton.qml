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
    Item {
        id: shapes; anchors.fill: parent; opacity: 0.7
        SequentialAnimation { running: skeleton.animating; loops: Animation.Infinite
            NumberAnimation { target: shapes; property: "opacity"; from: 0.55; to: 0.9; duration: 750; easing.type: Easing.InOutSine }
            NumberAnimation { target: shapes; property: "opacity"; from: 0.9; to: 0.55; duration: 750; easing.type: Easing.InOutSine }
        }
        Repeater {
            model: skeleton.cards?Math.min(8,Math.max(1,Math.floor(skeleton.width/180))*2):Math.min(8,Math.ceil(skeleton.height/80))
            Item {
                required property int index
                readonly property int columns: Math.max(1,Math.floor(skeleton.width/180))
                x: skeleton.cards?(index%columns)*(skeleton.width/columns):0
                y: skeleton.cards?Math.floor(index/columns)*230:index*80
                width: skeleton.cards?skeleton.width/columns-20:skeleton.width; height: skeleton.cards?210:72
                Rectangle { width: skeleton.cards?Math.min(parent.width,180):52; height: width; radius: skeleton.cards?20:12; color: Theme.high }
                Rectangle { x: skeleton.cards?0:68; y: skeleton.cards?Math.min(parent.width,180)+14:14; width: skeleton.cards?parent.width*0.72:parent.width*0.38; height: 12; radius: 6; color: Theme.high }
                Rectangle { x: skeleton.cards?0:68; y: skeleton.cards?Math.min(parent.width,180)+36:38; width: skeleton.cards?parent.width*0.45:parent.width*0.23; height: 9; radius: 5; color: Theme.high }
            }
        }
    }
}
