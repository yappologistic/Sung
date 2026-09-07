import QtQuick
Item {
    id: icon
    objectName: "materialIcon"
    property string name: "play"
    property color ink: Theme.text
    property real size: 24
    implicitWidth: size
    implicitHeight: size
    width: size
    height: size
    Image {
        anchors.fill: parent
        source: icon.visible && (!icon.Window.window || icon.Window.window.visible) ? "image://symbols/" + icon.name + "/" + icon.ink.toString().substring(1) : ""
        // Fresh textures on window re-entry also support Qt's software renderer.
        cache: false
        sourceSize: Qt.size(icon.size * Screen.devicePixelRatio, icon.size * Screen.devicePixelRatio)
        fillMode: Image.PreserveAspectFit
        smooth: true
    }
}
