import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// An artist's own header. Material's large top app bar gives a page its
// identity before its content; an artist has a face, so the cover sits behind
// the name as a hero rather than beside it as a thumbnail. Scrolling collapses
// it back to a title row, which is the behaviour the bar is specified to have.
Item {
    id: hero
    objectName: "artistHero"

    // 0 while the list is at rest, 1 once it has been scrolled past the band.
    property real collapse: 0
    readonly property real expandedHeight: Math.min(216,window.height*0.26)
    readonly property real collapsedHeight: 72
    readonly property real portrait: (expandedHeight-96)*(1-collapse)+48*collapse

    implicitHeight: expandedHeight*(1-collapse)+collapsedHeight*collapse
    // PaneMotion.kt:150-177 uses DefaultSpatial for bounds changes.
    Behavior on implicitHeight { enabled: app.motion && !tracks.moving; NumberAnimation { objectName: "artistHeroHeightMotion"; duration: Theme.springSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial } }
    clip: true

    AmbientBackdrop {
        objectName: "artistHeroBackdrop"
        anchors.fill: parent
        url: app.cover || ""
        // The band sits on the surface it belongs to, so the cover only ever
        // tints it. Material keeps hero imagery behind text, never competing.
        scrim: Theme.surfaceLow
        dim: 0.82
        corner: 28
        opacity: 1-hero.collapse
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 4
        anchors.rightMargin: 4
        spacing: 20

        Artwork {
            objectName: "artistHeroPortrait"
            Layout.preferredWidth: hero.portrait
            Layout.preferredHeight: hero.portrait
            Layout.alignment: Qt.AlignVCenter
            visible: !!app.cover
            url: app.cover
            radius: width/2
            pixels: 384
            AbstractButton {
                anchors.fill: parent
                objectName: "inspectArtistPortrait"
                Accessible.name: "View artist picture"
                focusPolicy: Qt.StrongFocus
                onClicked: artworkViewer.inspect(app.cover)
                background: Rectangle { color: "transparent"; radius: width/2; border.width: parent.visualFocus?2:0; border.color: Theme.focusRing }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 8
            SungText {
                objectName: "artistHeroName"
                heading: true
                // It travels between two roles as the band collapses.
                scaled: true
                Layout.fillWidth: true
                text: app.title
                // Display small while the band is open, title large once it has
                // collapsed into an ordinary header row.
                font.pixelSize: Theme.displaySmall-(Theme.displaySmall-Theme.titleLarge)*hero.collapse
                // DefaultSpatial changes the visible size as the hero collapses.
                Behavior on font.pixelSize { id: typeSizeBehavior; enabled: app.motion; NumberAnimation { objectName: "artistHeroTypeMotion"; duration: Theme.springSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial } }
                emphasized: true
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            SungText {
                objectName: "artistHeroSummary"
                Layout.fillWidth: true
                visible: !!app.artistInfo.summary
                opacity: 1-hero.collapse
                Layout.maximumHeight: implicitHeight*(1-hero.collapse)
                clip: true
                text: app.artistInfo.summary || ""
                color: Theme.muted
                font.pixelSize: Theme.bodyMedium
                elide: Text.ElideRight
            }
            RowLayout {
                objectName: "artistHeroActions"
                spacing: 12
                opacity: 1-hero.collapse
                Layout.maximumHeight: implicitHeight*(1-hero.collapse)
                clip: true
                MButton {
                    objectName: "artistHeroPlay"
                    text: "Play"; symbol: "play"; filled: true
                    enabled: app.collection.count>0
                    onClicked: app.playCollection(0)
                }
                MButton {
                    objectName: "artistHeroShuffle"
                    text: "Shuffle"; symbol: "shuffle"; elevated: true
                    enabled: app.collection.count>1
                    onClicked: {app.shuffle=true;app.playCollection(Math.floor(Math.random()*app.collection.count));}
                }
            }
        }

        MButton {
            objectName: "artistHeroPin"
            symbol: "pin"
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 4
            visible: !!app.collectionItem.id
            selected: {app.pins;return app.isPinned(app.collectionItem);}
            tip: selected?"Unpin from Home":"Pin to Home"
            onClicked: app.togglePin(app.collectionItem)
        }
        MButton {
            objectName: "artistHeroRefresh"
            symbol: "refresh"
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 4
            busy: app.busy && app.results.count>0
            tip: "Refresh"
            enabled: !app.busy
            onClicked: app.refresh()
        }
    }
}
