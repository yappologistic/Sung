import QtQuick
import QtQuick.Layouts

// A settings list row. M3 asks that primary text sit in the same position in
// every list item, so the label is anchored to the leading edge and sized like
// the switch labels it sits beside, not like a button.
MButton {
    // A row that opens something says so with a trailing icon; a row that acts
    // immediately does not, so the two read differently at a glance.
    property bool opens: false
    trailingSymbol: opens ? "chevron" : ""
    leftAligned: true
    contentInset: 0
    // A list item's headline is body large (ListTokens.ItemLabelTextFont),
    // with body large's tracking and regular weight, not a button label's.
    labelSize: Theme.bodyLarge
    labelTypeRole: "bodyLarge"
    Layout.fillWidth: true
    Layout.minimumWidth: 0
}
