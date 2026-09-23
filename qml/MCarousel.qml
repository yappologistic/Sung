import QtQuick
import QtQuick.Controls

// Material 3 carousel, in its multi-browse layout.
//
// What makes a carousel a carousel rather than a row that scrolls: items change
// size as they move through it, so the one at the edge is visibly squashed and
// tells you there is more to come; the visuals travel at a different speed from
// their containers, which is the parallax; and items snap into place instead of
// stopping wherever the flick ran out.
//
// Material's own research found that a squashed preview item is what people
// read as "there is more here", and that they expect around ten items in a
// carousel that scrolls several at a time.
ListView {
    id: carousel
    objectName: "carousel"

    property real cellWidth: 180
    property var openHandler: null
    // How far a cell at the edge is allowed to shrink. Material keeps a
    // readable sliver rather than tapering to nothing.
    readonly property real squashed: 0.38

    orientation: ListView.Horizontal
    // Carousel.kt:822-826 defaults its content padding to zero. The first
    // cover shares the heading's edge; the shelf keeps its 8dp item rhythm.
    spacing: 8
    leftMargin: 0
    rightMargin: 0
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    // Items snap into place to keep the layout, rather than resting part-way.
    snapMode: ListView.SnapToItem
    flickDeceleration: 2400
    reuseItems: true
    cacheBuffer: Math.round(cellWidth*2)
    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

    delegate: Item {
        id: cell
        required property var modelData
        required property int index
        objectName: "carouselCell_" + index
        width: carousel.cellWidth
        height: carousel.height

        // How much of this cell has left the viewport, at either end.
        readonly property real offset: x - carousel.contentX
        readonly property real outside: Math.max(0, Math.max(-offset, offset + width - carousel.width))
        readonly property real squeeze: Math.max(0, Math.min(1, outside / width))
        readonly property bool leading: offset < 0

        ArtCard {
            objectName: "carouselCard"
            width: carousel.cellWidth
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            track: cell.modelData
            openHandler: carousel.openHandler
            // A carousel item rounds at the extra large step, which is a step
            // past what the same card takes in a grid.
            corner: Theme.shapeExtraLarge
            // Items change size as they move through the carousel, growing back
            // as they come fully into view.
            scale: 1 - cell.squeeze*carousel.squashed
            transformOrigin: cell.leading ? Item.Right : Item.Left
            // The visual lags its container, which is the parallax.
            parallax: cell.squeeze * (cell.leading ? 1 : -1)
        }
    }
}
