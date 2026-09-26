import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// What has been listened to. The numbers come from a log of plays kept on this
// device; nothing here is sent anywhere.
//
// Material's layout: the headline figures sit on their own tonal surfaces at
// the top, the shape of the period follows as a bar row, and the rankings below
// are ordinary list rows so they can grow without the dialog changing shape.
MDialog {
    id: dialog
    objectName: "listeningStatsDialog"
    title: "Listening"
    modal: true
    width: fitWidth(640)
    height: Math.min(700, parent ? parent.height-48 : 700)
    standardButtons: Dialog.Close

    property int days: 7
    property var stats: ({})
    readonly property var periods: [{key:7,label:"Week"},{key:30,label:"Month"},{key:365,label:"Year"},{key:0,label:"All time"}]
    property string ranking: "artists"

    function refresh() { stats = app.listeningStats(days) }
    onAboutToShow: refresh()
    Connections { target: app; function onLibraryChanged() { if (dialog.visible) dialog.refresh() } }

    // Hours and minutes, because a listening total in seconds means nothing.
    function spell(seconds) {
        if (!seconds) return "0 min"
        const hours = Math.floor(seconds/3600), minutes = Math.round(seconds%3600/60)
        if (hours >= 100) return hours+" hr"
        return hours > 0 ? hours+" hr "+minutes+" min" : Math.max(1,minutes)+" min"
    }

    // Laid out as a child filling the dialog, the way the other dialogs are, so
    // the ranking list is given the height that is left over.
    ColumnLayout {
        objectName: "statsBody"
        anchors.fill: parent
        spacing: 12

        MSegmentedControl {
            objectName: "statsPeriod"
            Layout.fillWidth: true
            accessibleName: "Period"
            options: dialog.periods.map(p => ({key:p.key, label:p.label, name:"statsPeriod_"+p.key}))
            value: dialog.days
            onChosen: key => { dialog.days = key; dialog.refresh() }
        }

        // The headline figures.
        GridLayout {
            objectName: "statsFigures"
            Layout.fillWidth: true
            // Four across where there is room, two where there is not.
            columns: width < 520 ? 2 : 4
            columnSpacing: 12
            rowSpacing: 12
            Repeater {
                model: [{name:"statsTime", label:"Listened", value:dialog.spell(dialog.stats.seconds || 0)},
                        {name:"statsPlays", label:"Plays", value:String(dialog.stats.plays || 0)},
                        {name:"statsSongs", label:"Songs", value:String(dialog.stats.songs || 0)},
                        {name:"statsArtists", label:"Artists", value:String(dialog.stats.artists || 0)}]
                MCard {
                    id: figure
                    required property var modelData
                    objectName: modelData.name
                    variant: "filled"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 84
                    Layout.minimumHeight: 72
                    radius: Theme.shapeLargeIncreased
                    // The figures are drawn in Material's fixed accent, which
                    // holds one tone in both themes: the summary of a period
                    // reads the same however the rest of the window is lit.
                    color: Theme.primaryFixed
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 2
                        SungText { text: figure.modelData.label; color: Theme.primaryFixedVariantText; font.pixelSize: Theme.labelMedium }
                        SungText {
                            objectName: figure.modelData.name+"Value"
                            text: figure.modelData.value
                            color: Theme.primaryFixedText
                            font.pixelSize: Theme.headlineSmall
                            emphasized: true
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        // The shape of the period, one bar per day.
        ColumnLayout {
            objectName: "statsDaily"
            Layout.fillWidth: true
            visible: (dialog.stats.daily || []).length > 1
            spacing: 6
            SungText { text: "By day"; color: Theme.muted; font.pixelSize: Theme.labelMedium }
            RowLayout {
                objectName: "statsDailyBars"
                Layout.fillWidth: true
                Layout.preferredHeight: 72
                // The bars carry the whole point of the row, so they keep a
                // floor rather than collapsing into a rule.
                Layout.minimumHeight: 56
                spacing: 4
                Repeater {
                    model: (dialog.stats.daily || []).slice(-14)
                    ColumnLayout {
                        required property var modelData
                        readonly property real peak: Math.max(1, ...(dialog.stats.daily || [{seconds:1}]).slice(-14).map(d => d.seconds))
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 4
                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Rectangle {
                                objectName: "statsBar"
                                anchors.bottom: parent.bottom
                                width: parent.width
                                // A day with nothing in it still shows a floor,
                                // so the row reads as a scale rather than a gap.
                                height: Math.max(3, parent.height*modelData.seconds/parent.parent.peak)
                                radius: Theme.shapeSmall
                                color: modelData.seconds > 0 ? Theme.primary : Theme.outlineVariant
                                // MotionSchemeKeyTokens.kt:25 FastSpatial moves this
                                // short bar by one spring pair as its height changes.
                                Behavior on height { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
                            }
                        }
                        SungText {
                            text: modelData.day
                            color: Theme.muted
                            font.pixelSize: Theme.labelSmall
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }
                }
            }
        }

        MSegmentedControl {
            objectName: "statsRanking"
            Layout.fillWidth: true
            accessibleName: "Ranking"
            options: [{key:"artists",label:"Artists",name:"statsRanking_artists"},
                      {key:"albums",label:"Albums",name:"statsRanking_albums"},
                      {key:"songs",label:"Songs",name:"statsRanking_songs"}]
            value: dialog.ranking
            onChosen: key => dialog.ranking = key
        }

        ListView {
            id: rankings
            objectName: "statsRankingList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            // A ranking is the point of the dialog, so it keeps a floor of its
            // own rather than being squeezed out by the figures above it.
            Layout.minimumHeight: 140
            clip: true
            spacing: 4
            reuseItems: true
            model: dialog.ranking === "artists" ? (dialog.stats.topArtists || [])
                 : dialog.ranking === "albums" ? (dialog.stats.topAlbums || [])
                 : (dialog.stats.topSongs || [])
            ScrollBar.vertical: MScrollBar {}
            delegate: Rectangle {
                required property var modelData
                required property int index
                objectName: "statsRow_"+index
                width: rankings.width
                height: 56
                radius: Theme.shapeMedium
                color: index % 2 === 0 ? Theme.container : "transparent"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 12
                    SungText {
                        text: String(parent.parent.index+1)
                        color: Theme.muted
                        font.pixelSize: Theme.labelMedium
                        font.features: {"tnum": 1}
                        Layout.preferredWidth: 20
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        SungText { text: parent.parent.parent.modelData.name; Layout.fillWidth: true; elide: Text.ElideRight; emphasized: true }
                        SungText {
                            text: parent.parent.parent.modelData.subtitle || ""
                            visible: !!text
                            color: Theme.muted
                            font.pixelSize: Theme.labelMedium
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }
                    SungText {
                        text: dialog.spell(parent.parent.modelData.seconds)
                        color: Theme.muted
                        font.pixelSize: Theme.labelMedium
                        font.features: {"tnum": 1}
                    }
                }
            }
            SungText {
                objectName: "statsEmpty"
                anchors.centerIn: parent
                visible: rankings.count === 0
                text: dialog.days > 0 ? "Nothing played in this period" : "Nothing played yet"
                color: Theme.muted
            }
        }
    }
}
