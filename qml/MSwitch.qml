import QtQuick
import QtQuick.Controls
Switch {
    id: control
    // What the setting actually does. Material puts an explanation this long in
    // a rich tooltip rather than a plain one, so it can be read rather than
    // glanced at.
    property string hint: ""
    implicitHeight: Math.max(48, label.implicitHeight+16)
    // A settings row anchors its label and its control to the same edges as
    // the rows around it, so a column of switches and value controls lines up.
    leftPadding: 0
    rightPadding: 0
    hoverEnabled: true
    readonly property bool dimmed: !enabled
    indicator: Rectangle {
        objectName: "switchTrack"
        implicitWidth: 52; implicitHeight: 32
        x: control.width-control.rightPadding-width; y: (control.height-height)/2
        radius: Theme.shapeLarge
        // SwitchTokens.Disabled*: a disabled track keeps the role it had,
        // onSurface when on and surfaceContainerHighest when off, at 12%, and
        // the off track's edge drops to onSurface at 12% with it.
        color: control.dimmed ? (control.checked ? Qt.rgba(Theme.text.r,Theme.text.g,Theme.text.b,Theme.disabledSurfaceOpacity)
                                                 : Qt.rgba(Theme.highest.r,Theme.highest.g,Theme.highest.b,Theme.disabledSurfaceOpacity))
             : control.checked ? Theme.primary : Theme.highest
        border.width: control.checked ? 0 : 2
        border.color: control.dimmed ? Qt.rgba(Theme.text.r,Theme.text.g,Theme.text.b,Theme.disabledSurfaceOpacity) : Theme.outline
        Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        Rectangle {
            anchors.fill: parent; anchors.margins: -4; radius: Theme.shapeLargeIncreased
            color: "transparent"; border.width: 2; border.color: Theme.focusRing
            visible: control.visualFocus
        }
        Rectangle {
            x: (control.checked ? 36 : 16) - width/2
            anchors.verticalCenter: parent.verticalCenter
            width: 40; height: 40; radius: Theme.shapeLargeIncreased
            color: control.checked ? Theme.primary : Theme.text
            opacity: control.down || control.visualFocus ? Theme.pressedOpacity : control.hovered ? Theme.hoverOpacity : 0
            Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        }
        Rectangle {
            objectName: "switchHandle"
            x: (control.checked ? 36 : 16) - width/2
            anchors.verticalCenter: parent.verticalCenter
            width: control.down ? 28 : control.checked ? 24 : 16; height: width; radius: width/2
            // Material's off switch is drawn in the outline role, not in the
            // variant ink: the handle and the track edge are the boundary of a
            // control, and they read as one piece because of it. Disabled, the
            // on handle turns to the plain surface and the off one to
            // onSurface at 38% (SwitchTokens.Disabled*HandleColor).
            color: control.dimmed ? (control.checked ? Theme.surface : Qt.rgba(Theme.text.r,Theme.text.g,Theme.text.b,Theme.disabledContentOpacity))
                 : control.checked ? Theme.primaryText : Theme.outline
            // The thumb travelling across the track is movement, and
            // Material gives the switch the fast spatial spring for it, so it
            // carries a little of the overshoot a spatial spring has.
            Behavior on x { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
            // Switch.kt:180-189 gives thumb resizing the same FastSpatial spring.
            Behavior on width { NumberAnimation { id: switchThumbSpring; objectName: "switchThumbSpring"; duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
        }
    }
    contentItem: SungText { id:label;opacity: control.dimmed ? Theme.disabledContentOpacity : 1;text: control.text;rightPadding:68;verticalAlignment:Text.AlignVCenter;wrapMode:Text.Wrap;font.pixelSize:Theme.bodyLarge }
    Loader {
        active: control.hint.length > 0
        sourceComponent: MRichTooltip {
            objectName: "settingHint"
            parent: control
            visible: control.hovered || control.activeFocus
            x: 0; y: control.height
            subhead: control.text
            supporting: control.hint
        }
    }
}
