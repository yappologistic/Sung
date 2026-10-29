import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Material 3 app bar row.
//
// Material measures the actions an app bar is asked to carry and moves whatever
// will not fit into an overflow menu at the end of the row, rather than letting
// the row drop them. Every action therefore describes itself twice: once as a
// button in the row, and once as a line in the menu. When the row does overflow
// it keeps one slot for the overflow button itself, so the last thing shown is
// always the way to reach the rest.
Item {
    id: bar

    // Each action is {key, symbol, label, text, tip, name, enabled, checked,
    // toggle, trigger}. An action with `text` draws as a labelled button in the
    // row and still as a plain line in the menu; `name` keeps a button findable
    // under the name it had before it joined the row.
    property var actions: []
    property real spacing: 4
    property real itemWidth: 48

    FontMetrics { id: metrics; font.family: Theme.fontFamily; font.pixelSize: Theme.labelLarge; font.weight: Font.Medium }
    function slotWidth(action) {
        return action.text ? metrics.advanceWidth(action.sizingText || action.text) + 36 + (action.symbol ? 32 : 0) : itemWidth
    }

    readonly property var live: actions.filter(a => a.visible !== false)
    readonly property real fullWidth: live.length ? live.reduce((sum, a) => sum + slotWidth(a), 0) + (live.length-1)*spacing : 0
    // How many of them the width in hand can hold, measured one at a time so a
    // labelled action is not mistaken for an icon.
    readonly property int roomFor: {
        let used = 0, held = 0
        for (let i = 0; i < live.length; ++i) {
            used += (held ? spacing : 0) + slotWidth(live[i])
            if (used > width) break
            ++held
        }
        return held
    }
    readonly property bool overflowing: roomFor < live.length
    // Overflowing costs one slot, and the overflow button may itself displace
    // the last action that would otherwise have fitted.
    readonly property int shownCount: {
        if (!overflowing) return live.length
        let used = itemWidth, held = 0
        for (let i = 0; i < live.length; ++i) {
            used += spacing + slotWidth(live[i])
            if (used > width) break
            ++held
        }
        return held
    }
    readonly property var hidden: live.slice(shownCount)

    implicitWidth: fullWidth
    implicitHeight: itemWidth
    Layout.maximumWidth: fullWidth
    Layout.minimumWidth: live.length ? itemWidth : 0

    Row {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: bar.spacing
        Repeater {
            model: bar.live.slice(0, bar.shownCount)
            delegate: MButton {
                required property var modelData
                objectName: modelData.name || ("appBarAction_" + modelData.key)
                symbol: modelData.symbol || ""
                text: modelData.text || ""
                tip: modelData.tip || modelData.label
                enabled: modelData.enabled !== false
                // Material draws an app bar's trailing actions in the variant
                // ink and keeps the surface ink for the leading one.
                ambientInk: Theme.muted
                toggle: modelData.toggle === true
                selected: modelData.checked === true
                onClicked: modelData.trigger()
            }
        }
        MButton {
            id: overflowButton
            objectName: "appBarOverflow"
            visible: bar.overflowing
            symbol: "more"
            ambientInk: Theme.muted
            tip: "More actions"
            selected: overflowMenu.opened
            onClicked: overflowMenu.popup(overflowButton, 0, overflowButton.height)
        }
    }

    MMenu {
        id: overflowMenu
        objectName: "appBarOverflowMenu"
        Repeater {
            model: bar.hidden
            delegate: MMenuItem {
                required property var modelData
                objectName: "appBarMenuAction_" + modelData.key
                text: modelData.label || ""
                symbol: modelData.symbol || ""
                enabled: modelData.enabled !== false
                checkable: modelData.toggle === true
                checked: modelData.checked === true
                onTriggered: modelData.trigger()
            }
        }
    }
}
