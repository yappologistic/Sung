import QtQuick
import QtQuick.Controls

// Material 3 carousel, in its multi-browse layout.
//
// Items are laid out at full size and masked at the edge, as Carousel.kt:70-75
// describes. The artwork still travels at a different speed from its cell.
//
// Material's own research found that a squashed preview item is what people
// read as "there is more here", and that they expect around ten items in a
// carousel that scrolls several at a time.
ListView {
    id: carousel
    objectName: "carousel"

    property real cellWidth: 180
    property var openHandler: null

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
    ScrollBar.horizontal: MScrollBar {}

    delegate: Item {
        id: cell
        required property var modelData
        required property int index
        objectName: "carouselCell_" + index
        // Hidden while pooled: a culled delegate still takes Tab (TrackRow.qml).
        property bool pooled: false
        visible: !pooled
        ListView.onPooled: pooled=true
        ListView.onReused: pooled=false
        width: carousel.cellWidth
        height: carousel.height

        // How much of this cell has left the viewport, at either end.
        readonly property real offset: x - carousel.contentX
        readonly property real outside: Math.max(0, Math.max(-offset, offset + width - carousel.width))
        readonly property real squeeze: Math.max(0, Math.min(1, outside / width))
        readonly property bool leading: offset < 0
        // Carousel.kt:70-75 and :535-542 keep the item's contents at full
        // size and mask its bounds. :816-820 gives a 40-56dp small item; 48dp
        // is the midpoint and keeps the edge preview readable.
        readonly property real maskWidth: Math.max(48, width - squeeze*(width-48))
        Item {
            objectName: "carouselMask"
            x: cell.leading ? cell.width-width : 0
            width: cell.maskWidth; height: cell.height
            clip: true
            ArtCard {
                objectName: "carouselCard"
                width: carousel.cellWidth
                x: cell.leading ? parent.width-width : 0
                anchors.top: parent.top
                track: cell.modelData
                openHandler: carousel.openHandler
                // The full-size item retains its extra large cover corner.
                corner: Theme.shapeExtraLarge
                parallax: cell.squeeze * (cell.leading ? 1 : -1)
            }
        }
    }
}
