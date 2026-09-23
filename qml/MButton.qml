import QtQuick
import QtQuick.Controls
AbstractButton {
    id: control
    property string symbol: ""
    property string tip: text
    // Material's button sizes. A size carries its own height, its own squarer
    // corner for the pressed and selected states, its own icon size, its own
    // side padding and its own label style, so asking for one is enough.
    property string size: "small"
    // Material's icon button widths. A uniform one is square; narrow and wide
    // keep the height and change how much room the glyph is given.
    // Material's three icon button widths. "uniform" is its default width.
    property string iconWidth: "uniform"
    readonly property var iconSpaces: Theme.iconButtonSpace[size] || [4,8,14]
    readonly property real iconSpace: iconSpaces[iconWidth === "narrow" ? 0 : iconWidth === "wide" ? 2 : 1]
    // Material's default icon button is as wide as it is tall, and the other
    // two widths are that container opened or closed by the difference between
    // their space and the default one. Taking it as a difference rather than
    // as the glyph plus its space keeps the default square whatever size the
    // glyph itself is drawn at.
    readonly property real sizedSquareWidth: sizedHeight + 2*(iconSpace - iconSpaces[1])
    readonly property real sizedHeight: Theme.buttonHeights[size] || 40
    readonly property real sizedIcon: Theme.buttonIcon[size] || 20
    readonly property real sizedGap: Theme.buttonGap[size] || 8
    readonly property real sizedSquare: Theme.buttonSquare[size] || Theme.shapeMedium
    readonly property real sizedPressed: Theme.buttonPressed[size] || Theme.shapeSmall
    readonly property real sizedOutline: Theme.buttonOutline[size] || 1
    property real contentInset: Theme.buttonInset[size] || 16
    // M3 button labels are label-large; list rows built from a button use body-large.
    property real labelSize: Theme.buttonLabel[size] || Theme.labelLarge
    property string labelTypeRole: Theme.buttonLabelRole[size] || "labelLarge"
    // M3: a trailing icon communicates an action, such as opening something.
    property string trailingSymbol: ""
    property bool leftAligned: false
    property bool filled: false
    property bool tonal: false
    // Material's other two button variants. An elevated button sits on the low
    // surface and casts a shadow so it holds against busy content; an outlined
    // one draws a boundary instead of a container, for an action beside a more
    // important one.
    property bool elevated: false
    property bool outlined: false
    property bool selected: false
    // A toggle button reports a state rather than firing an action, so Material
    // has it morph as well as recolour: a round icon button is full cornered
    // while it is off, medium once it is on, and small under the finger.
    property bool toggle: false
    property bool morphPlayback:false
    property bool busy: false
    property bool confirmed: false
    function confirm() {confirmed=true;confirmation.restart();}
    Timer { id: confirmation; interval: 1100; onTriggered: control.confirmed=false }
    onVisibleChanged: if(!visible){confirmation.stop();confirmed=false;}
    readonly property bool needsTooltip: tip.length > 0 && (!text.length || tip !== text || buttonLabel.truncated)
    // The background's corners, which optical centering reads.
    property real startRadius: 0
    property real endRadius: 0
    readonly property real opticalShift: Theme.opticalShift(startRadius, endRadius)
    readonly property bool dimmed: !enabled && !busy
    readonly property bool hasContainer: filled || tonal || elevated || (selected && !toggle)
    // Material has four icon buttons and a toggle stays inside the one it is,
    // changing role rather than changing variant when it comes on. A standard
    // one carries no container at all: on is a filled glyph in the accent. A
    // tonal one sits on the secondary container and inverts onto the secondary
    // role itself. The unselected ink stays the surface ink rather than the
    // variant Material names, so a toggle in a row of plain icon buttons reads
    // the same as the ones beside it.
    // The ink of whatever this button sits on. An icon button takes the
    // ambient content colour rather than forcing one of its own, so a
    // container that sets a different one, such as an app bar's trailing
    // side, says so here.
    property color ambientInk: Theme.text
    property color ink: dimmed ? Theme.muted
                      : filled ? Theme.primaryText
                      : elevated ? Theme.primary
                      : outlined ? Theme.muted
                      : tonal ? (toggle && selected ? Theme.secondaryText : Theme.secondaryContainerText)
                      : toggle ? (selected ? Theme.primary : ambientInk)
                      : selected ? Theme.primary
                      // Material's text button labels itself in the accent. The
                      // generated token says the variant ink, but Compose sets
                      // Primary and notes the token is uncorrected, and the
                      // dialog action token agrees. An icon button has no label
                      // to colour and takes the ink of what it sits on, and a
                      // left aligned one is how this app builds a list row,
                      // where the label is the row's own ink rather than an
                      // offer to act.
                      : text.length && !leftAligned ? Theme.primary : ambientInk
    // Material draws the container at the size's own height and keeps a 48dp
    // touch target around it, so a small button is a 40dp shape you can still
    // hit comfortably. The target is the footprint the layout sees.
    readonly property real touchTarget: Math.max(Theme.minimumTarget, sizedHeight)
    // A left aligned button is a list row: its label starts after the leading
    // inset and keeps room at the far end for a trailing symbol, or for air.
    // Its natural width has to budget the same room its label is laid out in,
    // or a row sized to its own label elides it ("Reset layo…").
    readonly property real alignedLeadRoom: (symbol.length || busy ? sizedIcon + sizedGap + 8 : 0) + contentInset
    readonly property real alignedTrailRoom: trailingSymbol.length ? 36 : 18
    implicitWidth: text.length ? buttonLabel.implicitWidth + (leftAligned ? alignedLeadRoom + alignedTrailRoom
                                                                         : (symbol.length || busy ? control.sizedIcon+control.sizedGap : 0) + control.contentInset*2)
                               : Math.max(control.touchTarget, Math.round(control.sizedSquareWidth))
    implicitHeight: control.touchTarget
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    // Material does not fade a disabled control as a whole; the container and
    // the content take their own treatment below.
    Accessible.name: tip
    Accessible.description: busy ? "Loading" : confirmed ? "Added to queue" : ""
    Loader {
        id: tooltipLoader
        readonly property bool wanted: (control.hovered || control.visualFocus) && control.needsTooltip
        // Keep the popup alive until its exit transition has finished.
        active: false
        function releaseIfIdle() { if (!wanted && (!item || !item.visible)) active=false; }
        onWantedChanged: { if (wanted) active=true; else releaseIfIdle(); }
        Component.onCompleted: if (wanted) active=true
        sourceComponent: MTooltip {
            objectName: "buttonTip"
            parent: control
            visible: tooltipLoader.wanted
            onClosed: Qt.callLater(tooltipLoader.releaseIfIdle)
            delay: 650; text: control.tip
        }
    }
    background: Rectangle {
        // The container sits inside the touch target rather than filling it.
        width: control.text.length ? control.width : Math.min(control.width, Math.round(control.sizedSquareWidth))
        height: Math.min(control.height, control.sizedHeight)
        x: (control.width-width)/2
        y: (control.height-height)/2
        // Material maps buttons to the full shape style, which is half of the
        // shorter side rather than half the height: a narrow button is still a
        // stadium, not an over-rounded lozenge. Pressing morphs it towards a
        // squarer step, which is the shape morph the specification asks for on
        // interaction states.
        radius: control.down ? (control.toggle ? control.sizedPressed : control.sizedSquare)
                             : control.toggle && control.selected ? control.sizedSquare
                             : Theme.shapeFull(Math.min(width, height))
        color: control.dimmed
                 ? (control.hasContainer ? Qt.rgba(Theme.text.r,Theme.text.g,Theme.text.b,Theme.disabledContainerOpacity) : "transparent")
             : control.filled ? Theme.primary
             : control.elevated ? Theme.surfaceLow
             : control.tonal ? (control.toggle && control.selected ? Theme.secondary : Theme.secondaryContainer)
             : control.selected && !control.toggle ? Theme.high : "transparent"
        border.color: control.outlined && !control.dimmed ? Theme.outlineVariant
                    : control.outlined ? Qt.rgba(Theme.text.r,Theme.text.g,Theme.text.b,Theme.disabledContainerOpacity)
                    : "transparent"
        border.width: control.outlined ? control.sizedOutline : 2
        // An elevated button is the one variant Material lifts off the page.
        MElevation { anchors.fill: parent; radius: parent.radius; level: control.elevated && !control.dimmed ? 1 : 0 }
        Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        // Material morphs the container squarer while it is held, and says
        // outright that this one takes the effects spring "to prevent any
        // bounce in this component": a control answering a finger must not
        // wobble under it. Spatial springs ring; this is the one that does not.
        Behavior on radius { enabled: app.motion; NumberAnimation { duration: Theme.springEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springEffects } }
        Rectangle {
            anchors.fill: parent; radius: parent.radius
            color: control.ink
            opacity: control.down || control.visualFocus ? Theme.pressedOpacity : control.hovered ? Theme.hoverOpacity : 0
            Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        }
    }
    Rectangle {
        objectName: "buttonFocusRing"
        anchors.fill: control.background; anchors.margins: -3
        // The ring sits outside the button, so optical roundness adds the gap
        // between them rather than repeating the button's own radius.
        radius: Theme.shapeInside(Theme.shapeFull(Math.min(width, height)), -3); color: "transparent"
        border.width: 2; border.color: Theme.focusRing
        visible: control.visualFocus
    }
    contentItem: Item {
        opacity: control.dimmed ? Theme.disabledContentOpacity : 1
        Row {
            id: contentRow; anchors.verticalCenter: parent.verticalCenter
            // Material nudges content inside an asymmetric shape so it looks
            // centred; a symmetric one is left where it measures.
            x: (control.leftAligned?control.contentInset:(parent.width-width)/2)+control.opticalShift
            spacing: control.sizedGap
            Item {
                width: control.sizedIcon; height: control.sizedIcon; visible: control.symbol.length > 0 || control.busy
                anchors.verticalCenter: parent.verticalCenter
                Icon { anchors.centerIn: parent; size: control.sizedIcon; besideText: control.text.length ? control.labelSize : 0; visible: !control.busy && !playbackGlyph.active; name: control.confirmed?"check":control.symbol; ink: control.ink
                    // Material's fill axis carries a state: a symbol that has
                    // an outlined form is drawn outlined until the control
                    // reporting it is on.
                    fill: control.selected ? 1 : 0 }
                Loader {id:playbackGlyph;anchors.centerIn:parent;active:control.morphPlayback && !control.busy && !control.confirmed && (control.symbol==="play" || control.symbol==="pause");sourceComponent:PlaybackGlyph {paused:control.symbol==="pause";ink:control.ink}}
                Loader {
                    anchors.fill: parent; active: control.busy
                    sourceComponent: MLoadingIndicator { objectName: "buttonSpinner"; running: control.busy; ink: control.ink; trackColor: "transparent"; label: "Loading"; Accessible.ignored: true }
                }
            }
            SungText { id: buttonLabel; visible: control.text.length > 0; text: control.text; color: control.ink; font.pixelSize: control.labelSize; labelRole: true; typeRole: control.labelTypeRole; width: control.leftAligned ? Math.max(0,control.width-control.alignedLeadRoom-control.alignedTrailRoom) : implicitWidth; elide: Text.ElideRight; anchors.verticalCenter: parent.verticalCenter }
        }
        // Most buttons carry no trailing symbol, and a button is built more
        // often than anything else in the window, so the glyph waits until one
        // is asked for rather than sitting in every button empty.
        Loader {
            active: control.trailingSymbol.length > 0
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            sourceComponent: Icon {
                objectName: "buttonTrailingIcon"
                besideText: control.text.length ? control.labelSize : 0
                name: control.trailingSymbol; ink: control.ink; Accessible.ignored: true
            }
        }
    }
    scale: down ? 0.96 : 1
    Behavior on scale { enabled: app.motion; NumberAnimation { duration: Theme.springEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springEffects } }
}
