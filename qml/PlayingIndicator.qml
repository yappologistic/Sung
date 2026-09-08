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
                width: 3; height: 18; radius: 1.5; color: indicator.ink
                transform: Scale {
                    origin.y: 18
                    yScale: indicator.animating ? 0.16+0.84*(app.audioLevels[index] || 0) : 0.16
                    Behavior on yScale { enabled: indicator.animating; NumberAnimation { duration: 65; easing.type: Easing.OutCubic } }
                }
            }
        }
    }
}
