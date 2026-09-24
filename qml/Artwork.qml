import QtQuick
import Sung.Native 1.0
Item {
    id: root
    property string url: ""
    property string motionUrl: ""
    property real radius: Theme.shapeLarge
    // A shape from Material's library to mask with, instead of the corner
    // radius. Material uses these for avatars and hero imagery.
    property string shape: ""
    // A second shape the mask is morphing to, and how far it has got.
    property string toShape: ""
    property real morph: 0
    property int pixels: 360
    property bool crossfade: false
    readonly property bool transitioning: art.transitioning
    property bool fit: false
    property bool highResolution: false
    property int requestedPixels: pixels
    readonly property int targetPixels: highResolution ? Math.min(1600,Math.max(48,Math.ceil(Math.max(width,height)*Screen.devicePixelRatio/128)*128)) : pixels
    onTargetPixelsChanged: resizeDelay.restart()
    Component.onCompleted: requestedPixels=targetPixels
    Timer {id:resizeDelay;interval:160;onTriggered:root.requestedPixels=root.targetPixels}
    implicitWidth: 56; implicitHeight: 56
    Connections {
        target: app.server
        function onAccountChanged() { if(root.url.startsWith("sungcover:"))art.source="" }
        function onChanged() { if(root.url.startsWith("sungcover:") && app.server.connected && !art.source.toString())art.source=root.url }
    }
    Rectangle { anchors.fill: parent; visible: !root.shape; radius: root.radius; color: Theme.high }
    MShape { anchors.fill: parent; visible: !!root.shape; shape: root.shape; toShape: root.toShape; progress: root.morph; color: Theme.high }
    Icon { anchors.centerIn: parent; name: "disc"; size: Math.min(48,parent.width*0.4); ink: Theme.muted; visible: !art.ready }
    RoundedArt {
        id: art; objectName: "artworkPicture"; crossfade:root.crossfade && root.visible && app.motion && Window.window && Window.window.visible && Window.window.visibility!==Window.Minimized; animation: root.visible && root.motionUrl && root.motionUrl===motionArtwork.source.toString() ? motionArtwork : null
        anchors.fill: parent; source: root.url; radius: root.radius; shape: root.shape; toShape: root.toShape; morph: root.morph; pixels: root.requestedPixels; fit: root.fit
        opacity: ready ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve } }
    }
}
