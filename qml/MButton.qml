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
    // IconButton.kt:245 sizes from smallContainerSize without the pointer
    // branch that Button.kt:1059 uses for labelled buttons.
    readonly property real sizedHeight: text.length ? (Theme.buttonHeights[size] || 40)
                                                   : (Theme.iconButtonHeights[size] || 40)
    readonly property real sizedIcon: text.length ? (Theme.buttonIcon[size] || 20)
                                                 : (Theme.iconButtonIcon[size] || 24)
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
    // Material's two icon button shapes. A round one is fully rounded; a
    // square one takes the size's square corner (MediumIconButtonTokens.
    // ContainerShapeSquare is CornerLarge, the large size CornerExtraLarge).
    // A toggle that comes on swaps to the other one, so a square button turns
    // round: "a selected button should change shape from round to square, or
    // square to round" (button group guidelines, Behavior).
    property bool square: false
    // What a standard button group adds to or takes from this button's width
    // while a press is under way. MButtonGroup owns both; a button outside a
    // group keeps them at zero.
    property real groupPress: 0
    property real groupExpansion: 0
    // In a group the press is shown by width, so the shrink every other
    // button makes under the finger would pull against it. Compose scales
    // neither (IconButton.kt, ButtonGroup.kt).
    property bool grouped: false
    // A container cut from Material's shape library instead of a rounded
    // rectangle: Compose's IconButton takes any Shape, and MaterialShapes
    // gives the 35 of them (MaterialShapes.kt). A toggle that comes on morphs
    // to its selected shape, and the pulse deepens the outline (MShape). The
    // shape guidelines ask for these sparingly, so this is off unless a caller
    // names one.
    property string materialShape: ""
    property string selectedMaterialShape: ""
    property real shapePulse: 0
    readonly property bool shaped: materialShape.length > 0
    property bool morphPlayback:false
    property bool busy: false
    property bool confirmed: false
    function confirm() {confirmed=true;confirmation.restart();}
    Timer { id: confirmation; interval: 1100; onTriggered: control.confirmed=false }
    onVisibleChanged: if(!visible){confirmation.stop();confirmed=false;}
    readonly property bool needsTooltip: tip.length > 0 && (!text.length || tip !== text || buttonLabel.truncated)
    // BasicTooltip.kt:188-199 dismisses the popup when its anchor is pressed,
    // and :262-280 waits for another pointer Enter before showing it again.
    // A keyboard press re-arms on focus exit instead of pointer exit.
    property int tooltipRearm: 0 // 0 armed, 1 wait for hover exit, 2 wait for focus exit
    onDownChanged: if (down && needsTooltip) tooltipRearm = visualFocus ? 2 : 1
    onHoveredChanged: if (!hovered && tooltipRearm === 1) tooltipRearm = 0
    onVisualFocusChanged: if (!visualFocus && tooltipRearm === 2) tooltipRearm = 0
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
    // Disabled, a labelled button dims the variant ink and an icon button
    // the surface ink (FilledButtonTokens.DisabledLabelTextColor,
    // IconButtonTokens.DisabledColor), both at 38% below. The tonal button is
    // the labelled exception: Compose reads FilledTonalButtonTokens for it,
    // which dim onSurface over a 12% container (Button.kt).
    property color ink: dimmed ? (text.length && !tonal ? Theme.muted : Theme.text)
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
    // hit comfortably. The target is the footprint the layout sees. Across,
    // an icon button is as wide as its container or 48dp, whichever is more:
    // a narrow large button is 64dp wide, not the 96dp of its height, or the
    // gaps a button group sets between containers come out 16dp too wide.
    readonly property real touchTarget: Math.max(Theme.minimumTarget, sizedHeight)
    // A left aligned button is a list row: its label starts after the leading
    // inset and keeps room at the far end for a trailing symbol, or for air.
    // Its natural width has to budget the same room its label is laid out in,
    // or a row sized to its own label elides it ("Reset layo…").
    readonly property real alignedLeadRoom: (symbol.length || busy ? sizedIcon + sizedGap + 8 : 0) + contentInset
    readonly property real alignedTrailRoom: trailingSymbol.length ? 36 : 18
    implicitWidth: (text.length ? buttonLabel.implicitWidth + (leftAligned ? alignedLeadRoom + alignedTrailRoom
                                                                          : (symbol.length || busy ? control.sizedIcon+control.sizedGap : 0) + control.contentInset*2)
                                : Math.max(Theme.minimumTarget, Math.round(control.sizedSquareWidth)))
                   + groupExpansion
    implicitHeight: control.touchTarget
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    // Material does not fade a disabled control as a whole; the container and
    // the content take their own treatment below.
    Accessible.name: tip
    Accessible.description: busy ? "Loading" : confirmed ? "Added to queue" : ""
    Loader {
        id: tooltipLoader
        readonly property bool wanted: control.tooltipRearm === 0 && (control.hovered || control.visualFocus) && control.needsTooltip
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
        visible: !control.shaped
        // The container sits inside the touch target rather than filling it.
        width: control.text.length ? control.width : Math.min(control.width, Math.round(control.sizedSquareWidth)+control.groupExpansion)
        height: Math.min(control.height, control.sizedHeight)
        x: (control.width-width)/2
        y: (control.height-height)/2
        // Material maps buttons to the full shape style, which is half of the
        // shorter side rather than half the height: a narrow button is still a
        // stadium, not an over-rounded lozenge. Pressing morphs it towards a
        // squarer step, which is the shape morph the specification asks for on
        // interaction states. Every button presses to the size's pressed step
        // (ButtonSmallTokens.PressedContainerShape is the small corner); the
        // square step is for a square button at rest and a round toggle that
        // is on, and a square toggle that comes on turns round.
        radius: control.down ? control.sizedPressed
                             : control.square !== (control.toggle && control.selected) ? control.sizedSquare
                             : Theme.shapeFull(Math.min(width, height))
        color: control.dimmed
                 ? (control.hasContainer ? Qt.rgba(Theme.text.r,Theme.text.g,Theme.text.b,
                                                   control.tonal && control.text.length ? Theme.disabledSurfaceOpacity : Theme.disabledContainerOpacity)
                                         : "transparent")
             : control.filled ? Theme.primary
             : control.elevated ? Theme.surfaceLow
             : control.tonal ? (control.toggle && control.selected ? Theme.secondary : Theme.secondaryContainer)
             : control.selected && !control.toggle ? Theme.high : "transparent"
        border.color: control.outlined && !control.dimmed ? Theme.outlineVariant
                    : control.outlined ? Qt.rgba(Theme.text.r,Theme.text.g,Theme.text.b,Theme.disabledContainerOpacity)
                    : "transparent"
        border.width: control.outlined ? control.sizedOutline : 2
        // An elevated button is the one variant Material lifts off the page,
        // one level at rest and two under the pointer
        // (ElevatedButtonTokens.ContainerElevation, HoveredContainerElevation).
        MElevation { anchors.fill: parent; radius: parent.radius; level: control.elevated && !control.dimmed ? (control.hovered && !control.down ? 2 : 1) : 0 }
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
    // The ring sits outside the button, so optical roundness adds the gap
    // between them rather than repeating the button's own radius.
    MFocusRing {
        objectName: "buttonFocusRing"
        target: control.background; targetRadius: control.background.radius
        visible: control.visualFocus && !control.shaped
    }
    // A shaped container takes the rectangle's colour and state layer and
    // draws them as the shape. It sits between the hidden rectangle and the
    // content (a Control keeps its background at z -1).
    Loader {
        z: -0.5
        active: control.shaped
        x: control.background.x; y: control.background.y
        width: control.background.width; height: control.background.height
        sourceComponent: Item {
            // IconButton.kt:1561-1585 gives IconToggleButton's shape change
            // DefaultEffects, so the morph cannot bounce past either shape.
            property real morph: control.toggle && control.selected ? 1 : 0
            Behavior on morph { enabled: app.motion; NumberAnimation { objectName: "buttonShapeMorph"; duration: Theme.springEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springEffects } }
            property real stateLayer: control.down || control.visualFocus ? Theme.pressedOpacity : control.hovered ? Theme.hoverOpacity : 0
            Behavior on stateLayer { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
            MShape {
                objectName: "buttonShape"
                anchors.fill: parent
                shape: control.materialShape
                toShape: control.selectedMaterialShape || control.materialShape
                progress: parent.morph
                pulse: control.shapePulse*parent.morph
                color: Qt.tint(control.background.color, Qt.rgba(control.ink.r,control.ink.g,control.ink.b,parent.stateLayer))
            }
            // The ring follows the outline 3dp out, as the rectangle's does.
            // It exists only while focused: an outline that pulses with the
            // music is rebuilt every frame, and a hidden one would be too.
            Loader {
                anchors.fill: parent; anchors.margins: -Theme.focusRingOutset
                active: control.visualFocus
                sourceComponent: MShape {
                    objectName: "buttonShapeFocusRing"
                    shape: control.materialShape
                    toShape: control.selectedMaterialShape || control.materialShape
                    progress: parent.parent.morph
                    pulse: control.shapePulse*parent.parent.morph
                    color: "transparent"; strokeColor: Theme.focusRing; strokeWidth: Theme.focusRingWidth
                }
            }
        }
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
                Loader {id:playbackGlyph;anchors.centerIn:parent;active:control.morphPlayback && !control.busy && !control.confirmed && (control.symbol==="play" || control.symbol==="pause");sourceComponent:PlaybackGlyph {paused:control.symbol==="pause";ink:control.ink;size:control.sizedIcon}}
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
    scale: down && !grouped ? 0.96 : 1
    Behavior on scale { enabled: app.motion; NumberAnimation { duration: Theme.springEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springEffects } }
}
