import QtQuick
Text {
    id: label
    objectName: "sungText"
    // Material's emphasized type styles carry hierarchy on the variable
    // font's weight axis. Google Sans Flex is variable, so this is the real
    // axis rather than a synthesised bold. The step Material takes
    // depends on the role: a label goes from medium to bold, and every other
    // role from regular to medium, so the style has to say which it is.
    property bool emphasized: false
    property bool labelRole: false
    // The role this style is, for the sizes that more than one role shares.
    property string typeRole: ""
    // What assistive technology navigates a screen by is its headings, so a
    // style that names a section says that it is one rather than only looking
    // like one. Material asks for a heading hierarchy, not for headings that
    // are merely larger.
    property bool heading: false
    Accessible.role: heading ? Accessible.Heading : Accessible.StaticText
    // A style whose size is set by the window or by the reader, rather than
    // chosen from Material's scale, says so. Everything else is held to the
    // scale, because a size that is not a role carries no line height or
    // letter spacing of its own either.
    property bool scaled: false
    font.family: Theme.fontFamily
    font.variableAxes: ({"wdth": Theme.regularWidth})
    color: Theme.text
    font.pixelSize: Theme.bodyMedium
    font.weight: Theme.weightFor(emphasized, labelRole, metricSize, typeRole)
    // A role carries its line height and letter spacing, not only its size.
    // Letter spacing cannot read the size from a binding: font.letterSpacing
    // and font.pixelSize live in one grouped property, so a binding that sets
    // the first while reading the second re-enters itself. The size is
    // assigned across instead, which settles where a binding would loop.
    property real metricSize: Theme.bodyMedium
    onFontChanged: if (metricSize !== label.font.pixelSize) metricSize = label.font.pixelSize
    Component.onCompleted: metricSize = font.pixelSize
    font.letterSpacing: Theme.trackingFor(metricSize, labelRole, typeRole, emphasized)
    // A style that wants its own line height states it as a multiple of the
    // size, the way Text takes one, and it is turned into the pixels the
    // fixed mode reads. Left alone, the role's own line height applies.
    property real lineSpacing: 0
    lineHeight: lineSpacing > 0 ? Math.round(font.pixelSize*lineSpacing)
                                : Theme.lineFor(font.pixelSize, labelRole, typeRole)
    lineHeightMode: Text.FixedHeight
    textFormat: Text.PlainText
    elide: Text.ElideRight
    verticalAlignment: Text.AlignVCenter
}
