import QtQuick
import QtQuick.Layouts

// Material 3 floating toolbar.
//
// Material Expressive replaced the bottom app bar with docked and floating
// toolbars. A floating toolbar holds the actions relevant to the current page,
// sits over the content rather than being anchored into it, and comes in a
// standard or a vibrant colour style, the vibrant one for greater emphasis.
//
// The specification also says a toolbar and a navigation bar should not be on
// screen together, which is why the immersive player uses one and the compact
// window uses the other.
Rectangle {
    id: toolbar
    objectName: "floatingToolbar"

    // Vibrant leans on the primary container, which is what Material means by
    // "greater emphasis"; standard stays on a neutral surface.
    property bool vibrant: false
    property alias content: row.data
    readonly property color ink: vibrant ? Theme.containerText : Theme.text

    // A floating toolbar rests above the content it acts on.
    MElevation { anchors.fill: parent; radius: parent.radius; level: 3 }

    // FloatingToolbarTokens: 8dp at each end and 4dp between the items.
    implicitWidth: row.implicitWidth + 16
    implicitHeight: 64
    radius: Theme.shapeFull(implicitHeight)
    color: vibrant ? Theme.primaryContainer : Theme.container
    Behavior on color { ColorAnimation { duration: Theme.springEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springEffects } }

    RowLayout {
        id: row
        objectName: "floatingToolbarRow"
        anchors.centerIn: parent
        spacing: 4
    }
}
