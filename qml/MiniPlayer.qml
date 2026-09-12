import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Window {
    id: mini
    objectName: "miniPlayerWindow"
    title: "Sung · Mini player"
    transientParent: null
    flags: Qt.Window | Qt.FramelessWindowHint
    readonly property bool hasTimedLyrics: app.lyricLines.length>0
    width: 520; height: hasTimedLyrics?216:188
    minimumWidth: 520; maximumWidth: 520
    minimumHeight: hasTimedLyrics?216:188; maximumHeight: minimumHeight
    color: Theme.background
    TrackPresentation { id: presentation }
    signal restoreRequested()
    Component.onCompleted: windowResources.manage(mini)
    onClosing: Qt.quit()
    Shortcut { sequence: "Ctrl+M"; onActivated: mini.restoreRequested() }
    Shortcut { sequence: "Escape"; enabled:!miniVolume.popupVisible;onActivated: mini.restoreRequested() }
    Shortcut { sequence: "Ctrl+Q"; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; enabled:!miniVolume.popupVisible;onActivated: app.toggle() }
    Shortcut { sequence: "Ctrl+Right"; onActivated: app.next() }
    Shortcut { sequence: "Ctrl+Left"; onActivated: app.previous() }
    Rectangle {
        anchors.fill: parent; anchors.margins: 1; radius: 24; color: Theme.container
        border.width: 1; border.color: Theme.outline
        MouseArea { anchors.fill: parent; onPressed: mini.startSystemMove() }
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 16; spacing: 4
            RowLayout {
                Layout.fillWidth: true; spacing: 12
                Artwork { url: app.current.art || ""; motionUrl: app.currentMotionArt; crossfade:true; Layout.preferredWidth: 54; Layout.preferredHeight: 54; radius: 12; pixels: 128; fit:app.currentArtworkFit }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 4
                    SungText { text: presentation.shown.title || "Nothing playing"; opacity: presentation.fade; transform: Translate { x: presentation.offset } Layout.fillWidth: true; font.pixelSize: 16; font.weight: Font.DemiBold }
                    SungText { text: presentation.shown.artist || ""; opacity: presentation.fade; transform: Translate { x: presentation.offset } Layout.fillWidth: true; font.pixelSize: 13; color: Theme.muted }
                }
                MButton { objectName: "miniRestoreButton"; symbol: "expand"; tip: "Full player · Ctrl+M"; onClicked: mini.restoreRequested() }
                MButton { symbol: "close"; tip: "Quit Sung"; onClicked: Qt.quit() }
            }
            Item {
                id: lyricLine; objectName:"miniLyricContainer"
                Layout.fillWidth:true;Layout.preferredHeight:24;visible:mini.hasTimedLyrics
                property string incoming: app.lyricIndex>=0 && app.lyricLines.length>app.lyricIndex ? app.lyricLines[app.lyricIndex].text : ""
                property string shown: ""
                property string previous: ""
                property real progress: 1
                readonly property bool animate: app.motion && mini.visible && mini.visibility!==Window.Minimized
                function settle(){lineFade.stop();shown=incoming;previous="";progress=1;}
                onIncomingChanged:{
                    lineFade.stop();
                    if(!animate || !shown || !incoming){settle();return;}
                    previous=shown;shown=incoming;progress=0;lineFade.start();
                }
                onAnimateChanged:if(!animate)settle()
                Component.onCompleted:settle()
                NumberAnimation {id:lineFade;target:lyricLine;property:"progress";to:1;duration:Theme.normal;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.effectsCurve;onFinished:lyricLine.previous=""}
                SungText {anchors.fill:parent;text:lyricLine.previous;opacity:1-lyricLine.progress;horizontalAlignment:Text.AlignHCenter;font.pixelSize:13;color:Theme.primary;Accessible.ignored:true}
                SungText {objectName:"miniLyricLine";anchors.fill:parent;text:lyricLine.shown;opacity:lyricLine.progress;horizontalAlignment:Text.AlignHCenter;font.pixelSize:13;color:Theme.primary;Accessible.name:text}
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter; spacing: 12
                MButton { symbol: "heart"; tip: app.liked?"Remove from liked songs":"Like"; selected: app.liked; enabled: app.currentIndex>=0; onClicked: app.toggleLike(app.current) }
                MButton { symbol: "previous"; tip: "Previous"; enabled: app.queue.count>0; onClicked: app.previous() }
                MButton { objectName: "miniPlayButton"; busy: app.buffering; morphPlayback:true; symbol: app.playing||app.resolving?"pause":"play"; tip: app.playing||app.resolving?"Pause":"Play"; filled: true; implicitWidth: 64; enabled: app.queue.count>0; onClicked: app.toggle() }
                MButton { symbol: "next"; tip: "Next"; enabled: app.queue.count>0; onClicked: app.next() }
                VolumeControl {id:miniVolume;showSlider:false;buttonName:"miniVolumeButton";sliderName:"miniVolumeSlider"}
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                SungText { font.features: {"tnum": 1}; text: app.formatTime(app.position); color: Theme.muted; font.pixelSize: 11; Layout.preferredWidth: 34 }
                SeekBar { Layout.fillWidth: true; implicitHeight: 28 }
                SungText { font.features: {"tnum": 1}; text: app.formatTime(app.duration); color: Theme.muted; font.pixelSize: 11; Layout.preferredWidth: 34; horizontalAlignment: Text.AlignRight }
            }
        }
    }
    Rectangle {
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 8
        height: 58; radius: 16; color: Theme.primaryContainer; visible: !!app.error
        SungText { anchors.left: parent.left; anchors.right: retry.left; anchors.verticalCenter: parent.verticalCenter; anchors.margins: 14; text: app.error; color: Theme.containerText; font.pixelSize: 12 }
        MButton { id: retry; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; text: app.canRetry?"Retry":"Open"; ink: Theme.containerText; onClicked: app.canRetry?app.retry():mini.restoreRequested() }
    }
}
