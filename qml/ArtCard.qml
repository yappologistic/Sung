import QtQuick
import QtQuick.Controls
Item {
    id: card
    property var track: ({})
    signal menuRequested(var item, var anchor)
    readonly property bool playableCover: !!(track.videoId || track.localPath || track.serverSong) || ["album","playlist","local","local-album","local-artist"].indexOf(track.kind)>=0
    readonly property bool loadingCover: !!app.coverPlayId && app.coverPlayId===(track.browseId || track.id || "")
    width: 180; height: width + 68
    Item {
        id: art; width: parent.width; height: width
        property real radius: (card.track.kind === "artist" || card.track.kind === "local-artist") ? width/2 : 20
        readonly property bool mosaic: !!card.track.artworks && card.track.artworks.length>0
        Artwork { anchors.fill: parent; url: card.track.art || ""; radius: art.radius; pixels: 400; visible: !art.mosaic }
        Loader { anchors.fill: parent; active: art.mosaic; sourceComponent: PlaylistCover { artworks: card.track.artworks; radius: art.radius } }
    }
    HoverHandler { id: hover }
    AbstractButton {
        id: openCard; hoverEnabled: true; focusPolicy: Qt.StrongFocus
        anchors.fill: art
        Accessible.name: card.track.title || "Open collection"
        onClicked: app.open(card.track)
        background: Rectangle { radius: art.radius; border.width: openCard.activeFocus?3:0; border.color: Theme.primary; color: "transparent"; opacity: 1; Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } } }
    }
    MButton { id: cardAction; objectName: "cardAction"; anchors.right: art.right; anchors.bottom: art.bottom; anchors.margins: 10; busy: card.loadingCover; symbol: card.playableCover ? "play" : "chevron"; tip: (card.playableCover ? "Play " : "Open ") + (card.track.title || ""); filled: true; opacity: hover.hovered || openCard.activeFocus || cardAction.activeFocus || card.loadingCover ? 1 : 0; visible: opacity>0; Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } } onClicked: {if(card.playableCover)app.playCover(card.track);else app.open(card.track);} }
    MButton {
        anchors.right: art.right; anchors.top: art.top; anchors.margins: 10
        symbol: "pin"; tonal: true; selected: {app.pins;return app.isPinned(card.track);}
        tip: selected?"Unpin from Home":"Pin to Home"
        visible: !String(card.track.kind).startsWith("local-") && !(card.track.videoId || card.track.localPath) && (hover.hovered || openCard.activeFocus || activeFocus || selected)
        onClicked: app.togglePin(card.track)
    }
    MatchText { anchors.top: art.bottom; anchors.topMargin: 12; width: parent.width; revealFocused: openCard.activeFocus; sourceText: card.track.title || ""; font.pixelSize: Theme.bodyLarge; font.weight: Font.Medium }
    MatchText { anchors.top: art.bottom; anchors.topMargin: 36; width: parent.width; sourceText: card.track.count!==undefined ? ((card.track.artist?card.track.artist+" · ":"")+card.track.count+" tracks") : card.track.artist || (card.track.kind === "album" ? "Album" : (card.track.kind === "artist" || card.track.kind === "local-artist") ? "Artist" : "Playlist"); font.pixelSize: 12; color: Theme.muted }
}
