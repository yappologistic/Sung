import QtQuick
Item {
    id: cover
    property var artworks: []
    property real radius: 12
    readonly property int tiles: Math.min(4,artworks.length)
    Rectangle { anchors.fill: parent; radius: cover.radius; color: Theme.high }
    Icon { anchors.centerIn: parent; name: "queue"; ink: Theme.primary; visible: cover.tiles===0 }
    Repeater {
        model: cover.tiles
        Artwork {
            required property int index
            width: cover.tiles===1?cover.width:(cover.width-2)/2
            height: cover.tiles<=2 || (cover.tiles===3 && index===0)?cover.height:(cover.height-2)/2
            x: cover.tiles===1?0:cover.tiles===3?(index===0?0:width+2):(index%2)*(width+2)
            y: cover.tiles<=2?0:cover.tiles===3?(index===2?height+2:0):Math.floor(index/2)*(height+2)
            url: cover.artworks[index]; pixels: Math.ceil(Math.max(width,height)*2); radius: cover.tiles===1?cover.radius:4
        }
    }
}
