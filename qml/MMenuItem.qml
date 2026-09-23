import QtQuick
import QtQuick.Controls

// Material 3 menu item: a leading icon, the label, and trailing text for the
// keyboard shortcut that does the same thing. Material reserves the leading
// slot across the whole menu, so labels line up whether or not an individual
// item has an icon to put there.
MenuItem {
    id: control

    // The leading symbol. A checkable item shows its tick there instead.
    property string symbol: ""
    // The keyboard shortcut that reaches this item without the menu.
    property string shortcut: ""
    // Material's segmented menu draws its items as one run: each one carries a
    // container of its own, nearly square inside the run and round at its
    // ends, set apart rather than divided by a rule. The menu sets these.
    property bool segmented: false
    property bool firstInRun: false
    property bool lastInRun: false
    // MenuTokens marks a chosen item with the secondary pair, the same one
    // that marks a chosen anything else: ListItemSelectedContainerColor is the
    // secondary container and ListItemSelectedLabelTextColor the ink on it.
    // This asked for the tertiary pair, which is the source hue rotated and so
    // belongs to no other surface in the window.
    //
    // Only a segmented menu draws the item a container of its own, so only
    // there is there a secondary container for that ink to sit on. Elsewhere
    // the row keeps the menu's own ink and the tick alone marks the choice,
    // which is how Material's list marks a selection without a container.
    //
    // A disabled item is onSurface at 38%, label and icon alike
    // (StandardMenuTokens.ItemDisabledLabelTextColor and its opacity).
    readonly property color disabledInk: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, Theme.disabledContentOpacity)
    readonly property color ink: !control.enabled ? disabledInk
                               : control.checked && control.segmented ? Theme.secondaryContainerText
                               : Theme.text
    // The leading icon is the variant ink until the item is chosen, when it
    // takes the ink of the container it has been given.
    readonly property color leadingInk: !control.enabled ? disabledInk
                                      : control.checked ? ink : Theme.muted

    // A menu item's insides are built the first time the menu it sits in is
    // actually on the screen. An item is only effectively visible once its
    // menu is open, so that is the gate. Menus are built with the window and
    // then wait, sometimes forever, and each item carried a glyph, two labels
    // and three containers while it waited. Built stays built, because a menu
    // reopened is the common case.
    property bool built: false
    onVisibleChanged: if (visible) built = true
    Component.onCompleted: if (visible) built = true

    readonly property bool showsTick: checkable && checked
    readonly property bool hasLeading: showsTick || symbol.length > 0
    readonly property real leadingSpace: 32

    // No menu here opens a submenu, and the style's arrow for one is built for
    // every item unless the item says it has none.
    arrow: null

    implicitHeight: segmented ? 44 : 48
    height: visible ? implicitHeight : 0
    leftPadding: 14; rightPadding: 14
    palette.windowText: control.ink
    // The leading glyph stays a plain Icon rather than going behind a holder:
    // the menu reads the indicator's own ink and size, and a holder in front of
    // it would answer for neither.
    indicator: Icon {
        objectName: "menuItemLeading"
        name: control.showsTick ? "check" : control.symbol
        size: 20; ink: control.leadingInk
        visible: control.hasLeading
        x: control.mirrored ? control.width-width-control.rightPadding : control.leftPadding
        // Set on the label's baseline rather than the row's centre line.
        y: (control.height-height)/2 + Math.round(Theme.labelLarge*0.115)
    }
    contentItem: Loader {
      active: control.built
      sourceComponent: Item {
        SungText {
            objectName: "menuItemLabel"
            anchors.verticalCenter: parent.verticalCenter
            x: control.mirrored ? 0 : control.leadingSpace
            width: parent.width-control.leadingSpace-(shortcutLabel.visible ? shortcutLabel.width+12 : 0)
            text: control.text; color: control.ink; font.pixelSize: Theme.bodyLarge
            elide: Text.ElideRight
        }
        SungText {
            id: shortcutLabel
            objectName: "menuItemShortcut"
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            visible: control.shortcut.length > 0
            text: control.shortcut
            color: Theme.muted
            font.pixelSize: Theme.labelMedium
            Accessible.ignored: true
        }
      }
    }
    Accessible.name: control.text + (control.shortcut ? ", " + control.shortcut : "")
    background: Loader {
      active: control.built
      sourceComponent: Item {
        // The run's own container. Material rounds the ends of the run to the
        // medium step and leaves the corners inside it at extra small, and
        // rounds the chosen item to medium all round (MenuDefaults.kt,
        // leadingItemShape, middleItemShape, trailingItemShape,
        // selectedItemShape). The shape follows the choice alone, not the
        // pointer (Menu.kt, shapeByInteraction).
        Rectangle {
            objectName: "menuItemContainer"
            visible: control.segmented
            y: 1; height: parent.height-2
            width: parent.width
            topLeftRadius: control.checked || control.firstInRun ? Theme.shapeMedium : Theme.shapeExtraSmall
            topRightRadius: topLeftRadius
            bottomLeftRadius: control.checked || control.lastInRun ? Theme.shapeMedium : Theme.shapeExtraSmall
            bottomRightRadius: bottomLeftRadius
            // The run sits on the menu's own container, so an item in it takes
            // the step above to read as a container rather than a hole.
            color: control.checked ? Theme.secondaryContainer : Theme.high
            Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        }
        Rectangle {
            anchors.fill: parent; radius: Theme.shapeMedium; color: control.ink
            opacity: control.down || control.visualFocus ? Theme.pressedOpacity : control.highlighted ? Theme.hoverOpacity : 0
            Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        }
        Rectangle { anchors.fill: parent; anchors.margins: 2; radius: Theme.shapeSmall; color: "transparent"; border.color: Theme.focusRing; border.width: 2; visible: control.visualFocus }
      }
    }
}
