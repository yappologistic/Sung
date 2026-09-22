import QtQuick
Item {
    id: icon
    objectName: "materialIcon"
    property string name: "play"
    property color ink: Theme.text
    property real size: 24
    // Material's fill axis. A navigation destination is drawn outlined while it
    // is inactive and filled once it is chosen, and the change is animated
    // rather than swapped, which is what the axis is for. Symbols with no
    // outlined form of their own simply stay as they are.
    property real fill: 1
    // A symbol set beside text sits on the text's baseline rather than on its
    // centre line, which Material puts at about 11.5% of the text size below
    // centre. A symbol on its own is centred as usual, so this is the size of
    // the text it stands next to, or zero when it stands alone.
    property real besideText: 0
    anchors.verticalCenterOffset: besideText > 0 ? Math.round(besideText*0.115) : 0
    readonly property bool hasOutline: ["home","library","heart","pin"].indexOf(name)>=0
    implicitWidth: size
    implicitHeight: size
    width: size
    height: size
    readonly property bool live: icon.name.length > 0 && icon.visible && (!icon.Window.window || icon.Window.window.visible)
    // The outlined form sits underneath and the filled one fades in over it, so
    // the axis reads as a fill arriving rather than one icon replacing another.
    // Only the four symbols named above have an outlined form to fade from, and
    // an icon is built several hundred times over in a window, so the layer is
    // not built for the ones that would leave it empty. Each one carried an
    // Image and a Screen attachment of its own.
    Loader {
        anchors.fill: parent
        active: icon.hasOutline
        sourceComponent: Image {
            objectName: "iconOutline"
            anchors.fill: parent
            visible: icon.fill < 1
            source: icon.live ? "image://symbols/" + icon.name + "_outline/" + Math.round(icon.size) + "/" + icon.ink.toString().substring(1) : ""
            cache: false
            sourceSize: Qt.size(icon.size * Screen.devicePixelRatio, icon.size * Screen.devicePixelRatio)
            fillMode: Image.PreserveAspectFit
            smooth: true
        }
    }
    Image {
        objectName: "iconFill"
        anchors.fill: parent
        opacity: icon.hasOutline ? icon.fill : 1
        source: icon.live ? "image://symbols/" + icon.name + "/" + Math.round(icon.size) + "/" + icon.ink.toString().substring(1) : ""
        // Fresh textures on window re-entry also support Qt's software renderer.
        cache: false
        sourceSize: Qt.size(icon.size * Screen.devicePixelRatio, icon.size * Screen.devicePixelRatio)
        fillMode: Image.PreserveAspectFit
        smooth: true
        Behavior on opacity { enabled: app.motion; NumberAnimation { duration: Theme.springEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springEffects } }
    }
}
