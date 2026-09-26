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
    width: fitWidth(560)
    // Tall enough for the figures, the listening graph and a ranking you can
    // read on an ordinary window; a shorter one scrolls (statsScroll).
    height: Math.min(780, parent ? parent.height-48 : 780)
    standardButtons: Dialog.Close

    property int days: 7
    // A day picked on the listening graph, which the figures and rankings
    // then show in place of the period.
    property string day: ""
    property var stats: ({})
    readonly property var periods: [{key:7,label:"Week"},{key:30,label:"Month"},{key:365,label:"Year"},{key:0,label:"All time"}]
    property string ranking: "artists"

    function refresh() {
        stats = day ? app.listeningStatsOn(day) : app.listeningStats(days)
        calendar.refresh()
    }
    onAboutToShow: { day = ""; refresh() }
    Connections { target: app; function onLibraryChanged() { if (dialog.visible) dialog.refresh() } }

    // Hours and minutes, because a listening total in seconds means nothing.
    function spell(seconds) {
        if (!seconds) return "0 min"
        const hours = Math.floor(seconds/3600), minutes = Math.round(seconds%3600/60)
        if (hours >= 100) return hours+" hr"
        return hours > 0 ? hours+" hr "+minutes+" min" : Math.max(1,minutes)+" min"
    }

    // Laid out as a child filling the dialog, the way the other dialogs are, so
    // the ranking list is given the height that is left over. A window too
    // short for the figures, the graph and a readable ranking scrolls the
    // whole body instead of letting the list run under the buttons.
    Flickable {
        id: bodyScroll
        objectName: "statsScroll"
        anchors.fill: parent
        contentHeight: body.height
        interactive: contentHeight > height
        clip: interactive
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: MScrollBar { visible: bodyScroll.interactive }
    ColumnLayout {
        id: body
        objectName: "statsBody"
        // Clear of the scroll bar when there is one.
        width: bodyScroll.width - (bodyScroll.interactive ? bodyScroll.ScrollBar.vertical.width + Theme.spaceSmall : 0)
        height: Math.max(bodyScroll.height, implicitHeight)
        spacing: 12

        MSegmentedControl {
            objectName: "statsPeriod"
            Layout.fillWidth: true
            accessibleName: "Period"
            options: dialog.periods.map(p => ({key:p.key, label:p.label, name:"statsPeriod_"+p.key}))
            // No period is chosen while a single day is.
            value: dialog.day ? undefined : dialog.days
            onChosen: key => { dialog.day = ""; dialog.days = key; dialog.refresh() }
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

        ListeningCalendar {
            id: calendar
            Layout.fillWidth: true
            selected: dialog.day
            spell: dialog.spell
            onChosen: date => { dialog.day = date; dialog.refresh() }
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
                text: dialog.stats.totalOnly ? "Only the total is kept for this day"
                    : dialog.day ? "Nothing played on this day"
                    : dialog.days > 0 ? "Nothing played in this period" : "Nothing played yet"
                color: Theme.muted
            }
        }
    }
    }
}
