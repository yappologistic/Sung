import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Templates as T
// Built on the template rather than on the style's Dialog. The style declares a
// header label and a footer button box of its own, and a template defers only
// its background and content item, so every dialog built both and then set
// them aside for the ones below; a replaced header or footer is unparented,
// not destroyed. Qt's guidance for a control whose delegates are all replaced
// is to derive from the template:
// https://doc.qt.io/qt-6/qtquickcontrols-customize.html
// What the style gave that is still wanted is carried over as the style
// writes it: the implicit size and the two scrims.
T.Dialog {
    id: dialog
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding,
                            implicitHeaderWidth,
                            implicitFooterWidth)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding
                             + (implicitHeaderHeight > 0 ? implicitHeaderHeight + spacing : 0)
                             + (implicitFooterHeight > 0 ? implicitFooterHeight + spacing : 0))
    // A modal dialog dims the window with Material's scrim at 32%
    // (ScrimTokens), the same as the sheets and the drawer, rather than the
    // style's half-black shadow.
    T.Overlay.modal: Rectangle { color: Theme.scrimColor() }
    T.Overlay.modeless: Rectangle { color: Color.transparent(dialog.palette.shadow, 0.12) }
    // A compact window has no room to float a dialog inside it, so Material
    // gives the dialog the window: square corners, no inset, and the actions
    // pinned to the bottom edge rather than centred in the middle of nowhere.
    // A popup's parent is the overlay, which is the size of the window; the
    // Window attached property is not available from here.
    readonly property bool fullScreen: parent ? parent.width < 600 : false
    // What a dialog should measure at this window size. A compact window gives
    // it everything; otherwise it floats inside a 24dp inset up to what it asks
    // for. Dialogs that size themselves go through these rather than repeating
    // the arithmetic.
    function fitWidth(preferred) {
        if (!parent) return preferred
        return fullScreen ? parent.width : Math.min(parent.width-48, preferred)
    }
    function fitHeight(preferred) {
        if (!parent) return preferred
        return fullScreen ? parent.height : Math.min(parent.height-48, preferred)
    }
    property bool acceptEnabled: true
    property string acceptText: ""
    property Item initialFocus: null
    // M3 dialogs separate scrollable content from the actions with a divider,
    // so a clipped edge reads as "more below" rather than as a cut-off.
    property Flickable scrollSource: null
    readonly property bool moreBelow: !!scrollSource && scrollSource.contentHeight > scrollSource.height+1
                                      && scrollSource.contentY < scrollSource.contentHeight-scrollSource.height-1
    focus: true
    // A dialog's chrome is not built until the first time it opens. A dialog is
    // off the screen until something asks for it, so building a header, a
    // button bar and a background with the window spends memory and startup
    // time on a tree nobody is looking at. It stays built afterwards: a dialog
    // reopened is the common case, and rebuilding would cost a frame at exactly
    // the moment it is being watched. aboutToShow runs before the enter
    // transition, so the sizes below are in place for the first frame of it.
    property bool built: false
    onAboutToShow: {
        // A dialog that brings a footer of its own keeps it. This does not wait
        // on built: a dialog that fills its body before opening sets that
        // itself, and would otherwise open without its buttons.
        if (!footer) { footer = buttonBar.createObject(dialog); standardFooter = true }
        built = true
    }
    onOpened: if (initialFocus) initialFocus.forceActiveFocus(Qt.TabFocusReason)
    // Compose's alert dialog is padded 24dp all round, with 16dp under the
    // headline and 24dp between the body and the actions (AlertDialog.kt,
    // dialogPadding, TitlePadding, textPadding). The header below carries the
    // 16 and the standard button bar the 24, so a floating dialog's body
    // takes no padding of its own next to them; stacked, they made 48dp gaps.
    // A dialog that brings its own footer measured it against the padding
    // and keeps it.
    property bool standardFooter: false
    padding: 24
    topPadding: fullScreen ? 24 : 0
    bottomPadding: !fullScreen && standardFooter && footer && footer.visible ? 0 : 24
    anchors.centerIn: parent
    background: Rectangle {
        color: Theme.high; radius: dialog.fullScreen ? 0 : Theme.shapeExtraLarge
        // The surface itself is the dialog's background and stays; the shadow
        // it casts is a stack of rounded rectangles, and nothing casts one
        // until the dialog has been on the screen.
        // The shadow carries its own z to sit behind the surface it lifts, and
        // a holder in front of the surface would put it back in front, tinting
        // the face of the dialog. The holder takes the z instead.
        Loader {
            anchors.fill: parent
            z: -1
            active: dialog.built
            sourceComponent: MElevation { radius: dialog.background.radius; level: dialog.fullScreen ? 0 : 3 }
        }
    }
    // Material gives a basic dialog an optional icon, and centres the headline
    // under it when there is one. It is for a prompt that has to be read before
    // it is answered, which is usually one that cannot be undone.
    property string symbol: ""
    header: Loader {
      active: dialog.built
      sourceComponent: Item {
        // A full-screen dialog is headed by a 56dp bar carrying the close
        // affordance and the headline beside it, ruled off from the content.
        implicitHeight: dialog.fullScreen ? 56
                      : 24 + (dialogIcon.visible ? dialogIcon.height + 16 : 0) + titleLabel.implicitHeight + 16
        MButton {
            id: closeAffordance
            objectName: "dialogClose"
            visible: dialog.fullScreen
            symbol: "close"; tip: "Close"
            x: 8; anchors.verticalCenter: parent.verticalCenter
            onClicked: dialog.reject()
        }
        Icon {
            id: dialogIcon
            objectName: "dialogIcon"
            visible: !dialog.fullScreen && dialog.symbol.length > 0
            // DialogTokens.IconColor is secondary, not the accent.
            name: dialog.symbol; size: 24; ink: Theme.secondary
            anchors.horizontalCenter: parent.horizontalCenter
            y: 24
        }
        // DialogTokens.HeadlineFont is headline small as it stands, without
        // the emphasized weight.
        SungText {
            id: titleLabel; heading: true; objectName: "dialogTitle"
            x: dialog.fullScreen ? closeAffordance.x+closeAffordance.width+8 : 24
            width: parent.width-x-24
            y: dialogIcon.visible ? dialogIcon.y+dialogIcon.height+16
                                  : dialog.fullScreen ? (parent.height-implicitHeight)/2 : 24
            horizontalAlignment: dialogIcon.visible ? Text.AlignHCenter : Text.AlignLeft
            text: dialog.title; font.pixelSize: Theme.headlineSmall
            wrapMode: Text.Wrap; maximumLineCount: 2
        }
        Rectangle {
            objectName: "dialogHeaderDivider"
            visible: dialog.fullScreen
            anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
            height: 1; color: Theme.outlineVariant
        }
      }
    }
    // The button bar arrives with the rest of the chrome, on first open. Qt
    // wires accept and reject to whichever footer is a DialogButtonBox and
    // hands it the dialog's standardButtons as it is set, so a box built with
    // the window got its buttons from the dialog during startup and dropped
    // them again, and a box that is not there yet is handed them when it
    // becomes the footer.
    readonly property Component buttonBar: DialogButtonBox {
        visible: dialog.standardButtons !== Dialog.NoButton
        alignment: Qt.AlignRight
        buttonLayout: DialogButtonBox.AndroidLayout
        // Material's full-screen dialog puts its actions on a 56dp bar at the
        // bottom edge; a floating one keeps the 24dp inset it sits in.
        implicitHeight: dialog.fullScreen ? 56 : contentHeight+48
        padding: dialog.fullScreen ? 8 : 24; spacing: 8
        background: Item {
            Rectangle {
                objectName: "dialogScrollDivider"
                anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                height: 1; color: Theme.outlineVariant
                visible: dialog.moreBelow
                opacity: visible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
            }
        }
        delegate: MButton {
            objectName: dialog.objectName + "_button_" + DialogButtonBox.buttonRole
            readonly property bool confirming: DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole || DialogButtonBox.buttonRole === DialogButtonBox.YesRole
            ink: Theme.primary; enabled: !confirming || dialog.acceptEnabled
            Component.onCompleted: if (confirming && dialog.acceptText) text = Qt.binding(() => dialog.acceptText)
        }
    }
    enter: Transition { ParallelAnimation { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.enterDuration; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: app.motion?350:0; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastSpatialCurve } } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.exitDuration; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
}
