import QtQuick
import QtQuick.Controls
Item {
    id: card
    property var track: ({})
    signal menuRequested(var item, var anchor)
    width: 180; height: width + 68
    Artwork { id: art; width: parent.width; height: width; url: card.track.art || ""; radius: card.track.kind === "artist" ? width/2 : 20; pixels: 400 }
    HoverHandler { id: hover }
    AbstractButton {
        id: openCard; hoverEnabled: true; focusPolicy: Qt.StrongFocus
        anchors.fill: art
        Accessible.name: card.track.title || "Open collection"
        onClicked: app.open(card.track)
        background: Rectangle { radius: 20; border.width: openCard.activeFocus?3:0; border.color: Theme.primary; color: "transparent"; opacity: 1; Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } } }
    }
    MButton { anchors.right: art.right; anchors.bottom: art.bottom; anchors.margins: 10; symbol: (card.track.videoId || card.track.localPath) ? "play" : "chevron"; tip: "Open " + (card.track.title || ""); filled: true; opacity: hover.hovered || openCard.activeFocus ? 1 : 0; visible: opacity>0; Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } } onClicked: app.open(card.track) }
    MButton {
        anchors.right: art.right; anchors.top: art.top; anchors.margins: 10
        symbol: "pin"; tonal: true; selected: {app.pins;return app.isPinned(card.track);}
        tip: selected?"Unpin from Home":"Pin to Home"
        visible: !(card.track.videoId || card.track.localPath) && (hover.hovered || openCard.activeFocus || activeFocus || selected)
        onClicked: app.togglePin(card.track)
    }
    SungText { anchors.top: art.bottom; anchors.topMargin: 12; width: parent.width; text: card.track.title || ""; font.pixelSize: 15; font.weight: Font.Medium }
    SungText { anchors.top: art.bottom; anchors.topMargin: 36; width: parent.width; text: card.track.artist || (card.track.kind === "album" ? "Album" : card.track.kind === "artist" ? "Artist" : "Playlist"); font.pixelSize: 12; color: Theme.muted }
}
