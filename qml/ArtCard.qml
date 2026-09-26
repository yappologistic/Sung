import QtQuick
import QtQuick.Controls
Item {
    id: card
    readonly property string albumCardId:track.id || track.browseId || ""
    property var track: ({})
    property var openHandler: null
    // The corner the artwork takes. A card in a grid rounds at the large
    // increased step; one in a carousel takes the step past it.
    property real corner: Theme.shapeLargeIncreased
    // Material's carousel moves an item's visual at a different speed from its
    // container. -1 to 1, nought at rest. The shift is small and unclipped, so
    // the artwork keeps its rounded corners and its square.
    property real parallax: 0
    signal menuRequested(var item, var anchor)
    // A smart playlist's size depends on its rules and is only known once it
    // is opened, so the backend reports it as -1; a card says nothing rather
    // than printing that.
    readonly property string countLabel: track.count >= 0 ? track.count + (track.count === 1 ? " track" : " tracks") : ""
    readonly property bool playableCover: !!(track.videoId || track.localPath || track.serverSong) || ["album","playlist","local","local-album","local-artist"].indexOf(track.kind)>=0
    readonly property bool loadingCover: !!app.coverPlayId && app.coverPlayId===(track.browseId || track.id || "")
    width: 180; height: width + 68
    // Hidden while pooled: a culled delegate still takes Tab (TrackRow.qml).
    property bool pooled: false
    visible: !pooled
    GridView.onPooled: pooled=true
    GridView.onReused: pooled=false
    Item {
        id: art; width: parent.width; height: width
        transform: Translate { x: card.parallax*7 }
        property real radius: (card.track.kind === "artist" || card.track.kind === "local-artist") ? Theme.shapeFull(width) : card.corner
        // Artwork can be masked with a shape from Material's library; covers
        // keep their circle and their rounded square because a lobed edge only
        // reads as a portrait over photography, not over flat generated art.
        readonly property string shape: ""
        readonly property bool mosaic: !!card.track.artworks && card.track.artworks.length>0
        Artwork { anchors.fill: parent; url: card.track.art || ""; radius: art.radius; shape: art.shape; pixels: 400; visible: !art.mosaic }
        Loader { anchors.fill: parent; active: art.mosaic; sourceComponent: PlaylistCover { artworks: card.track.artworks; radius: art.radius } }
    }
    HoverHandler { id: hover }
    AbstractButton {
        id: openCard; objectName:"openCollectionCard";hoverEnabled: true; focusPolicy: Qt.StrongFocus
        anchors.fill: art
        Accessible.name: card.track.title || "Open collection"
        onClicked: {if(card.openHandler)card.openHandler(card.track,art);else app.open(card.track);}
        // A click leaves no ring: only keyboard focus draws one.
        background: Item { MFocusRing { objectName: "cardFocusRing"; targetRadius: art.radius; visible: openCard.visualFocus } }
    }
    MButton { id: cardAction; objectName: "cardAction"; anchors.right: art.right; anchors.bottom: art.bottom; anchors.margins: 12; busy: card.loadingCover; symbol: card.playableCover ? "play" : "chevron"; tip: (card.playableCover ? "Play " : "Open ") + (card.track.title || ""); filled: true; opacity: hover.hovered || openCard.visualFocus || cardAction.visualFocus || card.loadingCover ? 1 : 0; visible: opacity>0; Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } } onClicked: {if(card.playableCover)app.playCover(card.track);else app.open(card.track);} }
    MButton {
        objectName: "cardPinAction"
        anchors.right: art.right; anchors.top: art.top; anchors.margins: 12
        symbol: "pin"; tonal: true; toggle: true; selected: {app.pins;return app.isPinned(card.track);}
        tip: selected?"Unpin from Home":"Pin to Home"
        // Keep the contextual action in the tab order while focus crosses
        // from the cover to its other action, then hide it when focus leaves.
        visible: !String(card.track.kind).startsWith("local-") && !(card.track.videoId || card.track.localPath) && (hover.hovered || openCard.activeFocus || cardAction.activeFocus || activeFocus || selected)
        onClicked: app.togglePin(card.track)
    }
    MatchText { anchors.top: art.bottom; anchors.topMargin: 12; width: parent.width; revealFocused: openCard.activeFocus; sourceText: card.track.title || ""; font.pixelSize: Theme.bodyLarge; font.weight: Font.Medium }
    MatchText { objectName: "cardSubtitle"; anchors.top: art.bottom; anchors.topMargin: 36; width: parent.width; sourceText: card.countLabel ? ((card.track.artist?card.track.artist+" · ":"")+card.countLabel) : card.track.artist || (card.track.smart ? "Smart playlist" : card.track.kind === "album" ? "Album" : (card.track.kind === "artist" || card.track.kind === "local-artist") ? "Artist" : "Playlist"); font.pixelSize: Theme.bodySmall; color: Theme.muted }
}
