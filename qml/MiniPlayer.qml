import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Window {
    id: mini
    objectName: "miniPlayerWindow"
    title: "Sung · Mini player"
    transientParent: null
    flags: Qt.Window | Qt.FramelessWindowHint
    width: 520; height: 216
    minimumWidth: 520; maximumWidth: 520
    minimumHeight: 216; maximumHeight: 216
    color: Theme.background
    TrackPresentation { id: presentation }
    signal restoreRequested()
    Component.onCompleted: windowResources.manage(mini)
    onClosing: Qt.quit()
    Shortcut { sequence: "Ctrl+M"; onActivated: mini.restoreRequested() }
    Shortcut { sequence: "Escape"; onActivated: mini.restoreRequested() }
    Shortcut { sequence: "Ctrl+Q"; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; onActivated: app.toggle() }
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
                Artwork { url: presentation.shown.art || ""; motionUrl: presentation.shown.motionArt || ""; opacity: presentation.fade; Layout.preferredWidth: 54; Layout.preferredHeight: 54; radius: 12; pixels: 128 }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 4
                    SungText { text: presentation.shown.title || "Nothing playing"; opacity: presentation.fade; transform: Translate { y: presentation.offset } Layout.fillWidth: true; font.pixelSize: 16; font.weight: Font.DemiBold }
                    SungText { text: presentation.shown.artist || ""; opacity: presentation.fade; transform: Translate { y: presentation.offset } Layout.fillWidth: true; font.pixelSize: 13; color: Theme.muted }
                }
                MButton { objectName: "miniRestoreButton"; symbol: "expand"; tip: "Full player · Ctrl+M"; onClicked: mini.restoreRequested() }
                MButton { symbol: "close"; tip: "Quit Sung"; onClicked: Qt.quit() }
            }
            SungText {
                objectName: "miniLyricLine"; Layout.fillWidth: true; Layout.preferredHeight: 24
                text: app.lyricIndex>=0 && app.lyricLines.length>app.lyricIndex ? app.lyricLines[app.lyricIndex].text : ""
                horizontalAlignment: Text.AlignHCenter; font.pixelSize: 13; color: Theme.primary
                Accessible.name: text
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter; spacing: 12
                MButton { symbol: "heart"; tip: app.liked?"Remove from liked songs":"Like"; selected: app.liked; enabled: app.currentIndex>=0; onClicked: app.toggleLike(app.current) }
                MButton { symbol: "previous"; tip: "Previous"; enabled: app.queue.count>0; onClicked: app.previous() }
                MButton { objectName: "miniPlayButton"; busy: app.buffering; symbol: app.playing||app.resolving?"pause":"play"; tip: app.playing||app.resolving?"Pause":"Play"; filled: true; implicitWidth: 64; enabled: app.queue.count>0; onClicked: app.toggle() }
                MButton { symbol: "next"; tip: "Next"; enabled: app.queue.count>0; onClicked: app.next() }
                MButton { objectName: "miniVolumeButton"; symbol: "volume"; tip: "Volume"; onClicked: volumeMenu.open() }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                SungText { font.features: {"tnum": 1}; text: app.formatTime(app.position); color: Theme.muted; font.pixelSize: 11; Layout.preferredWidth: 34 }
                SeekBar { Layout.fillWidth: true; implicitHeight: 28 }
                SungText { font.features: {"tnum": 1}; text: app.formatTime(app.duration); color: Theme.muted; font.pixelSize: 11; Layout.preferredWidth: 34; horizontalAlignment: Text.AlignRight }
            }
        }
    }
    Popup {
        id: volumeMenu; x: mini.width-190; y: 70; width: 170; height: 62; padding: 12
        background: Rectangle { radius: 18; color: Theme.high; border.color: Theme.outline }
        contentItem: SeekBar { objectName: "miniVolumeSlider"; volumeMode: true }
    }
    Rectangle {
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 8
        height: 58; radius: 16; color: Theme.primaryContainer; visible: !!app.error
        SungText { anchors.left: parent.left; anchors.right: retry.left; anchors.verticalCenter: parent.verticalCenter; anchors.margins: 14; text: app.error; color: Theme.containerText; font.pixelSize: 12 }
        MButton { id: retry; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; text: app.canRetry?"Retry":"Open"; ink: Theme.containerText; onClicked: app.canRetry?app.retry():mini.restoreRequested() }
    }
}
