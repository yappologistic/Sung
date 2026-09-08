import QtQuick
import Sung.Native 1.0
Item {
    id: root
    property string url: ""
    property string motionUrl: ""
    property real radius: 16
    property int pixels: 360
    implicitWidth: 56; implicitHeight: 56
    Connections {
        target: app.server
        function onAccountChanged() { if(root.url.startsWith("sungcover:"))art.source="" }
        function onChanged() { if(root.url.startsWith("sungcover:") && app.server.connected && !art.source.toString())art.source=root.url }
    }
    Rectangle { anchors.fill: parent; radius: root.radius; color: Theme.high }
    Icon { anchors.centerIn: parent; name: "disc"; size: Math.min(48,parent.width*0.4); ink: Theme.muted; visible: !art.ready }
    RoundedArt {
        id: art; animation: root.visible && root.motionUrl && root.motionUrl===motionArtwork.source.toString() ? motionArtwork : null
        anchors.fill: parent; source: root.url; radius: root.radius; pixels: root.pixels
        opacity: ready ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve } }
    }
}
