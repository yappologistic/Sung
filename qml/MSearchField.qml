import QtQuick
import QtQuick.Controls

TextField {
    id: field
    readonly property bool handlesTextInput: true
    property bool clearEnabled: true
    property string clearTip: "Clear search"
    signal cleared()
    implicitHeight: 56
    leftPadding: 52; rightPadding: clearEnabled ? 52 : 16
    topPadding: 8; bottomPadding: 8
    selectByMouse: true
    verticalAlignment: TextInput.AlignVCenter
    font.family: Theme.fontFamily; font.pixelSize: Theme.bodyLarge
    color: Theme.text; placeholderTextColor: Theme.muted
    selectionColor: Theme.primary; selectedTextColor: Theme.primaryText
    Accessible.name: placeholderText
    background: Rectangle {
        radius: height / 2; color: Theme.high
        border.width: field.activeFocus ? 2 : 0; border.color: Theme.primary
    }
    Icon { name: "search"; size: 24; x: 16; anchors.verticalCenter: parent.verticalCenter; ink: Theme.muted; Accessible.ignored: true }
    MButton {
        objectName: "clearSearchButton"
        anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter
        width: 48; height: 48; symbol: "close"; tip: field.clearTip
        visible: field.clearEnabled && field.length > 0
        onClicked: { field.clear(); field.textEdited(); field.cleared(); field.forceActiveFocus(); }
    }
}
