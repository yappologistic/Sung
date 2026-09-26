import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Earlier versions of a playlist. Undo takes back the last edit; this takes
// back one from last week. Each row is the shape the playlist had before a
// change, newest first, and restoring one is itself an edit that can be taken
// back in turn.
MDialog {
    id: dialog
    objectName: "playlistVersionsDialog"
    title: "Version history"
    modal: true
    width: fitWidth(520)
    height: Math.min(560, parent ? parent.height-48 : 560)
    standardButtons: Dialog.Close

    property string playlistId: ""
    property string playlistName: ""
    property var versions: []

    function inspect(id, name) {
        playlistId = id
        playlistName = name || ""
        refresh()
        open()
    }
    function refresh() { versions = playlistId ? app.playlistVersions(playlistId) : [] }
    Connections { target: app; function onLibraryChanged() { if (dialog.visible) dialog.refresh() } }

    // A version is worth dating loosely: the day it was replaced, and the time.
    function spell(at) {
        const when = new Date(at*1000), now = new Date()
        const sameDay = when.toDateString() === now.toDateString()
        const yesterday = new Date(now.getTime()-86400000).toDateString() === when.toDateString()
        const clock = Qt.formatTime(when, Qt.locale(), Locale.ShortFormat)
        if (sameDay) return "Today · "+clock
        if (yesterday) return "Yesterday · "+clock
        return Qt.formatDate(when, Qt.locale(), Locale.ShortFormat)+" · "+clock
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        SungText {
            objectName: "playlistVersionsSubtitle"
            Layout.fillWidth: true
            text: dialog.playlistName ? "Earlier versions of "+dialog.playlistName : "Earlier versions"
            color: Theme.muted
            font.pixelSize: Theme.bodyMedium
            wrapMode: Text.Wrap
        }

        ListView {
            id: list
            objectName: "playlistVersionsList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 160
            clip: true
            spacing: 8
            reuseItems: true
            model: dialog.versions
            ScrollBar.vertical: MScrollBar {}
            delegate: Rectangle {
                id: version
                required property var modelData
                required property int index
                objectName: "playlistVersion_"+index
                // Hidden while pooled: a culled delegate still takes Tab (TrackRow.qml).
                property bool pooled: false
                visible: !pooled
                ListView.onPooled: pooled=true
                ListView.onReused: pooled=false
                width: list.width
                height: 72
                radius: Theme.shapeLarge
                color: Theme.high
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 12
                    spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        SungText {
                            objectName: "playlistVersionWhen_"+version.index
                            text: dialog.spell(version.modelData.at)
                            emphasized: true
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        SungText {
                            text: version.modelData.summary
                            color: Theme.muted
                            font.pixelSize: Theme.labelMedium
                        }
                    }
                    MButton {
                        objectName: "restoreVersion_"+version.index
                        text: "Restore"
                        tonal: true
                        onClicked: app.restorePlaylistVersion(dialog.playlistId, version.index)
                    }
                }
            }
            Column {
                anchors.centerIn: parent
                spacing: 12
                visible: list.count === 0
                SungText {
                    objectName: "playlistVersionsEmpty"
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "No earlier versions yet"
                    color: Theme.muted
                }
                SungText {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(320, list.width-32)
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: "A version is kept each time this playlist changes."
                    color: Theme.muted
                    font.pixelSize: Theme.labelMedium
                }
            }
        }

        MButton {
            objectName: "clearPlaylistVersions"
            Layout.alignment: Qt.AlignLeft
            text: "Forget versions"
            enabled: dialog.versions.length > 0
            onClicked: { app.clearPlaylistVersions(dialog.playlistId); dialog.refresh() }
        }
    }
}
