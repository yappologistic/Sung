import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Item {
    id: player
    objectName: "immersivePlayer"
    TrackPresentation { id: presentation }
    property alias artwork: immersiveArt
    readonly property bool volumePopupVisible:immersiveVolume.popupVisible
    property bool coverHidden: false
    property real detailsOpacity: coverHidden?0:1
    Behavior on detailsOpacity { NumberAnimation { duration: app.motion?120:0; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve } }
    signal exitRequested()
    signal speedRequested()
    signal timingRequested()
    signal artworkRequested()
    readonly property real coverSize: Math.max(120,Math.min(420,width*0.34,height-360))
    NumberAnimation on opacity { from: 0; to: 1; duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: player.width<900?24:40; spacing: 20
        RowLayout {
            Layout.fillWidth: true
            MButton { objectName: "exitImmersiveButton"; symbol: "back"; tip: "Exit immersive · Esc"; onClicked: player.exitRequested() }
            Item { Layout.fillWidth: true }
            MButton { objectName: "immersiveLyricSearchButton"; symbol: "search"; tip: "Find in lyrics"; enabled: !!app.lyrics; onClicked: immersiveLyrics.openSearch() }
            MButton { text: Number(app.playbackRate.toFixed(2))+"×"; tip: "Playback speed"; onClicked: player.speedRequested() }
            MButton { symbol: "settings"; tip: "Lyric timing · saved for this song"; visible: app.lyricLines.length>0; onClicked: player.timingRequested() }
            MButton { symbol: "heart"; selected: app.liked; tip: app.liked?"Unlike":"Like"; enabled: app.currentIndex>=0; onClicked: app.toggleLike(app.current) }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0; spacing: Math.max(24,player.width*0.055)
            ColumnLayout {
                Layout.preferredWidth: player.coverSize; Layout.minimumWidth: player.coverSize; Layout.maximumWidth: player.coverSize; Layout.fillHeight: true; Layout.minimumHeight: 0; spacing: 18
                Item { Layout.fillHeight: true }
                Artwork { id: immersiveArt; objectName: "immersiveArtwork"; Layout.preferredWidth: player.coverSize; Layout.preferredHeight: player.coverSize; Layout.maximumHeight: player.coverSize; url: app.current.art || ""; motionUrl: app.currentMotionArt; crossfade:true; opacity: player.coverHidden?0:1; radius: 28; pixels: 850; highResolution: true; fit:app.currentArtworkFit
                    AbstractButton {anchors.fill:parent;Accessible.name:"View artwork";focusPolicy:Qt.StrongFocus;onClicked:player.artworkRequested();background:Rectangle {color:"transparent";radius:28;border.width:parent.visualFocus?2:0;border.color:Theme.primary}}
                }
                SungText { text: presentation.shown.title || "Nothing playing"; opacity: presentation.fade*player.detailsOpacity; transform: Translate { x: presentation.offset } Layout.fillWidth: true; font.pixelSize: player.width<900?22:30; font.weight: Font.DemiBold; wrapMode: Text.Wrap; maximumLineCount: 3 }
                SungText { text: presentation.shown.artist || ""; opacity: presentation.fade*player.detailsOpacity; transform: Translate { x: presentation.offset } Layout.fillWidth: true; font.pixelSize: 18; color: Theme.muted; wrapMode: Text.Wrap; maximumLineCount: 2 }
                Item { Layout.fillHeight: true }
            }
            LyricsView { id: immersiveLyrics; expanded: true; Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.fillHeight: true; Layout.minimumHeight: 0 }
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter; spacing: 16
            MButton { symbol: "shuffle"; selected: app.shuffle; tip: app.shuffle?"Shuffle on":"Shuffle off"; onClicked: app.shuffle=!app.shuffle }
            MButton { symbol: "previous"; tip: "Previous"; enabled: app.queue.count>0; onClicked: app.previous() }
            MButton { objectName: "immersivePlayButton"; busy: app.buffering; morphPlayback:true; symbol: app.playing||app.resolving?"pause":"play"; filled: true; implicitWidth: 80; implicitHeight: 56; tip: app.playing||app.resolving?"Pause":"Play"; enabled: app.queue.count>0; onClicked: app.toggle() }
            MButton { symbol: "next"; tip: "Next"; enabled: app.queue.count>0; onClicked: app.next() }
            MButton { symbol: app.repeat===2?"repeat_one":"repeat"; selected: app.repeat>0; tip: app.repeat===0?"Repeat off":app.repeat===1?"Repeat queue":"Repeat song"; onClicked: app.repeat=(app.repeat+1)%3 }
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: Math.min(800,player.width-80); spacing: 12
            SungText { font.features: {"tnum": 1}; text: app.formatTime(app.position); color: Theme.muted; font.pixelSize: 12; Layout.preferredWidth: 40 }
            SeekBar { Layout.fillWidth: true }
            SungText { font.features: {"tnum": 1}; text: app.formatTime(app.duration); color: Theme.muted; font.pixelSize: 12; Layout.preferredWidth: 40; horizontalAlignment: Text.AlignRight }
            VolumeControl {id:immersiveVolume;showSlider:false}
        }
    }
}
