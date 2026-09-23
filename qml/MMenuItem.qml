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
    // MenuDefaults.leadingItemShape, middleItemShape and trailingItemShape
    // shape one group; the menu marks the first and last items.
    property bool firstInRun: false
    property bool lastInRun: false
    // MenuTokens.ListItemSelectedContainerColor and ListItemSelectedLabelTextColor
    // mark a checked item with secondaryContainer and onSecondaryContainer.
    // StandardMenuTokens.ItemSelected* ask for the tertiary pair, but tertiary
    // is the source hue turned around the wheel: over a warm cover it lands on
    // a green no other surface in the window uses. The secondary pair is the
    // one every other selection in Sung wears.
    // The unselected item uses ItemContainerColor (surfaceContainerLow) and
    // ItemLabelTextColor (onSurface); its leading icon uses onSurfaceVariant.
    //
    // A disabled item is onSurface at 38%, label and icon alike
    // (StandardMenuTokens.ItemDisabledLabelTextColor and its opacity).
    readonly property color disabledInk: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, Theme.disabledContentOpacity)
    readonly property color ink: !control.enabled ? disabledInk
                               : control.checked ? Theme.secondaryContainerText
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

    // Menu.kt:2374-2375 gives a menu item a 48dp container height.
    implicitHeight: 48
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
            // SegmentedMenuTokens.ItemLabelTextFont is BodyLarge; Menu.kt:2053
            // still uses labelLarge under a TODO, so the token sets the style.
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
      // Menu.kt:2038-2046 pads the selectable item's Surface 4dp on each
      // side. Inset the whole background so selection and state layers agree.
      x: 4; width: control.width-8; height: control.height
      active: control.built
      sourceComponent: Item {
        // The group's item container. Material rounds the ends to the medium
        // step and leaves the corners inside it at extra small, and
        // rounds the chosen item to medium all round (MenuDefaults.kt,
        // leadingItemShape, middleItemShape, trailingItemShape,
        // selectedItemShape). The shape follows the choice alone, not the
        // pointer (Menu.kt, shapeByInteraction).
        Rectangle {
            objectName: "menuItemContainer"
            height: parent.height
            width: parent.width
            topLeftRadius: control.checked || control.firstInRun ? Theme.shapeMedium : Theme.shapeExtraSmall
            topRightRadius: topLeftRadius
            bottomLeftRadius: control.checked || control.lastInRun ? Theme.shapeMedium : Theme.shapeExtraSmall
            bottomRightRadius: bottomLeftRadius
            // MenuTokens.ListItemSelectedContainerColor is secondaryContainer;
            // ItemContainerColor keeps the unchecked item on surfaceContainerLow.
            color: control.checked ? Theme.secondaryContainer : Theme.surfaceLow
            Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        }
        Rectangle {
            objectName: "menuItemStateLayer"
            anchors.fill: parent; radius: Theme.shapeMedium; color: control.ink
            opacity: control.down || control.visualFocus ? Theme.pressedOpacity : control.highlighted ? Theme.hoverOpacity : 0
            Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        }
        Rectangle { anchors.fill: parent; anchors.margins: 2; radius: Theme.shapeSmall; color: "transparent"; border.color: Theme.focusRing; border.width: 2; visible: control.visualFocus }
      }
    }
}
