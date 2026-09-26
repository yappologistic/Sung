import QtQuick
Item {
    id: indicator; objectName: "playingIndicator"
    implicitWidth: 25; implicitHeight: 20
    property color ink: Theme.primary
    readonly property bool animating: app.playing && app.motion && visible && Window.window && Window.window.visible && Window.window.visibility!==Window.Minimized
    Accessible.name: app.playing?"Playing":"Paused"
    Row {
        anchors.centerIn: parent; spacing: 2
        Repeater {
            model: 5
            Rectangle {
                required property int index
                objectName: "audioBar_"+index
                width: 3; height: 18; radius: Theme.shapeFull(3); color: indicator.ink
                transform: Scale {
                    origin.y: 18
                    yScale: indicator.animating ? 0.16+0.84*(app.audioLevels[index] || 0) : 0.16
                    // Each bar chases its band on FastEffects (1.0/3800,
                    // about 108ms): quick enough to keep up with the music,
                    // critically damped so a bar never reads past its level.
                    Behavior on yScale { enabled: indicator.animating; NumberAnimation { duration: Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastEffects } }
                }
            }
        }
    }
}
