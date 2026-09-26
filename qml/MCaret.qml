import QtQuick

// The text cursor of a Material text field. FilledTextFieldTokens.CaretColor
// and OutlinedTextFieldTokens.CaretColor are primary, and Compose draws it
// 2dp wide; Qt's Basic caret takes the text colour instead.
//
// A custom cursor delegate owns its own blink. It follows the platform's
// cursor flash time, only runs while the field shows a cursor, and holds
// steady for a moment after each move, as the stock caret does.
Rectangle {
    id: caret
    readonly property Item field: parent
    property bool lit: true
    width: 2
    color: Theme.primary
    visible: !!field && field.cursorVisible && lit
    Timer {
        id: blink
        interval: Math.max(1, Qt.styleHints.cursorFlashTime/2)
        running: !!caret.field && caret.field.cursorVisible && Qt.styleHints.cursorFlashTime > 0
        repeat: true
        onTriggered: caret.lit = !caret.lit
        onRunningChanged: caret.lit = true
    }
    Connections {
        target: caret.field
        function onCursorPositionChanged() { caret.lit = true; if (blink.running) blink.restart() }
    }
}
