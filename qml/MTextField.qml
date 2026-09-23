import QtQuick
import QtQuick.Controls
// Material 3 text field.
//
// Two containers to choose between. An outlined field draws a boundary and
// nothing else, which keeps a form of them quiet; a filled one takes the
// highest surface container and rules itself off underneath with an active
// indicator that thickens and takes the accent when it has focus. Material
// reaches for filled where a field is the thing on the surface rather than one
// of several, so a single field in a dialog is a filled one.
TextField {
    id: field
    // "outlined" or "filled".
    property string variant: "outlined"
    readonly property bool filled: variant === "filled"
    property string label: ""
    readonly property bool labelInAccessibleTree: label.length > 0 && echoMode !== TextInput.Normal
    // What a floating label is painted over where it cuts the outline: the
    // surface the field sits on. Every labelled outlined field is in a dialog,
    // which is surfaceContainerHigh; the container role showed as a darker
    // patch behind the label there.
    property color labelSurface: Theme.high
    // Material's supporting text sits under the field and explains it; when the
    // field is in error the same line carries the reason and everything the
    // field is drawn with moves to the error role.
    property string supporting: ""
    property string errorText: ""
    readonly property bool errored: errorText.length > 0
    // Disabled, input and label drop to onSurface at 38% and the outline to
    // 12%; a filled container to 4% with its indicator at 38%
    // (OutlinedTextFieldTokens.Disabled*, FilledTextFieldTokens.Disabled*).
    readonly property bool dimmed: !enabled
    function onSurface(amount) { return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, amount) }
    readonly property color accent: dimmed ? onSurface(Theme.disabledSurfaceOpacity)
                                  : errored ? Theme.error : activeFocus ? Theme.primary : Theme.outline
    readonly property bool floatingLabel: activeFocus || length > 0 || preeditText.length > 0
    readonly property bool handlesTextInput: true
    implicitHeight: 56
    leftPadding: 16; rightPadding: 16
    // A filled field floats its label inside the container, so the text it
    // labels sits below it rather than in the middle.
    topPadding: field.filled && field.label.length ? 24 : 16
    bottomPadding: field.filled && field.label.length ? 8 : 16
    selectByMouse: true
    verticalAlignment: TextInput.AlignVCenter
    font.family: Theme.fontFamily; font.pixelSize: Theme.bodyLarge
    color: dimmed ? onSurface(Theme.disabledContentOpacity) : Theme.text
    placeholderTextColor: dimmed ? onSurface(Theme.disabledContentOpacity) : Theme.muted
    selectionColor: Theme.primary; selectedTextColor: Theme.primaryText
    // Material reserves the supporting line so a field does not jump when an
    // error arrives.
    bottomInset: supportLine.visible ? -supportLine.height-4 : 0
    // Qt 6.11 hides Accessible.name for a password edit, while keeping its
    // passwordEdit state. QQuickAccessibleAttached's labelledBy relation
    // (qquickaccessibleattached_p.h:61, 90-95, 146-163) gives assistive
    // technology the visible label without changing that password state.
    // The setter rejects null, so only labelled fields set the relation.
    Accessible.name: field.label.length ? field.label : field.placeholderText
    function linkVisibleLabel() {
        if (field.labelInAccessibleTree && !Accessible.labelledBy)
            Accessible.labelledBy = fieldLabel
    }
    // A caller can assign label after this component completes; defer the
    // relation until both the text and its visual label are ready.
    onLabelInAccessibleTreeChanged: Qt.callLater(linkVisibleLabel)
    Component.onCompleted: Qt.callLater(linkVisibleLabel)
    Accessible.description: errored ? errorText : supporting
    background: Rectangle {
        objectName: "fieldContainer"
        // A filled field rounds only the corners away from its indicator.
        radius: field.filled ? 0 : Theme.shapeExtraSmall
        topLeftRadius: field.filled ? Theme.shapeExtraSmall : radius
        topRightRadius: topLeftRadius
        color: field.filled ? (field.dimmed ? field.onSurface(0.04) : Theme.highest) : "transparent"
        border.width: field.filled ? 0 : field.activeFocus || field.errored ? 2 : 1
        border.color: field.accent
        Behavior on border.color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        Rectangle {
            objectName: "fieldIndicator"
            visible: field.filled
            anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
            height: field.activeFocus || field.errored ? 2 : 1
            color: field.dimmed ? field.onSurface(Theme.disabledContentOpacity)
                 : field.errored ? Theme.error : field.activeFocus ? Theme.primary : Theme.muted
            Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        }
    }
    SungText {
        id: fieldLabel; objectName: "fieldLabel"
        x: field.leftPadding
        y: field.floatingLabel ? (field.filled ? 8 : -height/2) : (field.height-height)/2
        text: field.label; visible: text.length > 0
        font.pixelSize: field.floatingLabel ? Theme.labelMedium : Theme.bodyLarge
        color: field.dimmed ? field.onSurface(Theme.disabledContentOpacity)
             : field.errored ? Theme.error : field.activeFocus ? Theme.primary : Theme.muted
        // A password label must remain in Qt's accessible tree so its
        // labelledBy relation can resolve to named static text. Ordinary
        // field labels stay out of traversal (qquickaccessibleattached_p.h:146-163).
        Accessible.name: field.label
        Accessible.ignored: !field.labelInAccessibleTree
        Rectangle { objectName: "fieldLabelMask"; anchors.fill: parent; anchors.leftMargin: -4; anchors.rightMargin: -4; color: field.labelSurface; visible: field.floatingLabel && !field.filled; z: -1 }
        Behavior on y { NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
        Behavior on font.pixelSize { NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
    }
    SungText {
        id: supportLine
        objectName: "fieldSupport"
        visible: text.length > 0
        x: field.leftPadding; y: field.height + 4
        width: field.width - field.leftPadding - field.rightPadding
        text: field.errored ? field.errorText : field.supporting
        color: field.errored ? Theme.error : Theme.muted
        font.pixelSize: Theme.labelMedium
        wrapMode: Text.Wrap
        Accessible.ignored: true
    }
}
