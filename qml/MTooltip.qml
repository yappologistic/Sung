import QtQuick
import QtQuick.Controls

// Material 3 plain tooltip.
//
// A short label about the control it belongs to, drawn against the theme
// rather than in it: the inverse surface, the ink that goes on it, body small,
// and the smallest corner on the scale. Material does not raise it, so it
// carries no shadow; the rich tooltip is the one that is a surface of its own.
//
// Every plain tooltip in the app is this, so the treatment lives in one place
// rather than being written out again at each control that wants one.
ToolTip {
    id: tip

    // Compose pads the plain tooltip 8dp across and 4dp down, holds it to at
    // least 40x24 and wraps it at 200 (Tooltip.kt, PlainTooltipContentPadding,
    // TooltipMinWidth/Height, plainTooltipMaxWidth). There are no tokens for
    // these, which is why they live in the source.
    leftPadding: 8; rightPadding: 8; topPadding: 4; bottomPadding: 4
    contentItem: SungText {
        text: tip.text
        font.pixelSize: Theme.bodySmall
        color: Theme.inverseSurfaceText
        wrapMode: Text.Wrap
    }
    implicitWidth: Math.max(40, Math.min(200, contentItem.implicitWidth + leftPadding + rightPadding))
    implicitHeight: Math.max(24, contentItem.implicitHeight + topPadding + bottomPadding)
    background: Rectangle {
        objectName: "tooltipContainer"
        color: Theme.inverseSurface
        radius: Theme.shapeExtraSmall
    }
}
