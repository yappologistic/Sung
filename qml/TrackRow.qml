import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ItemDelegate {
    id: row
    property var track: ({})
    property int rowIndex: -1
    property var selection: null
    property int selectionIndex: -1
    property var listOwner: null
    property var dragHub: null
    property bool selected: selection ? (selection.revision,selection.contains(selectionIndex)) : false
    property bool selectable: selection && !!(track.videoId || track.localPath || track.serverSong) && track.available!==false
    property bool keyboardCurrent: activeFocus || (listOwner && listOwner.activeFocus && listOwner.currentIndex===selectionIndex)
    property bool selectionVisible: selectable && (hovered || pointer.containsMouse || keyboardCurrent || selection.count>0)
    property bool motionRaised: false
    z: motionRaised && app.motion ? 2 : 0
    property string matchQuery: ""
    readonly property bool titleRevealAllowed: !dragging && (!dragHub || !dragHub.owner) && (!listOwner || (!listOwner.moving && y>=listOwner.contentY && y+height<=listOwner.contentY+listOwner.height))
    property bool dragging: false
    property point pressPoint
    property int pressModifiers: 0
    property bool queueMode: false
    property bool active: queueMode ? rowIndex===app.currentIndex : app.current.id !== undefined && app.current.id === track.id
    signal menuRequested(var item, int index, var anchor)
    ListView.onReused: {motionRaised=false;opacity=Qt.binding(()=>enabled?1:0.45);}
    Behavior on implicitHeight {enabled:app.motion && visible && !dragging;NumberAnimation {id:rowResize;duration:220;easing.type:Easing.InOutCubic}}
    Connections {target:app;function onSettingsChanged(){if(!app.motion)rowResize.complete();}}
    implicitHeight: queueMode ? (app.compactDensity?56:72) : Theme.rowHeight
    width: ListView.view ? ListView.view.width : 500
    hoverEnabled: true
    enabled: track.available !== false
    opacity: enabled ? 1 : 0.45
    Accessible.description: selectable ? "Ctrl-click to toggle selection, Shift-click for a range. Drag selected songs to move them." : ""
    Accessible.name: (track.title || "") + ", " + (track.artist || "")
    Accessible.selected: selected
    background: Rectangle {
        radius: 16; color: row.selected ? Theme.primaryContainer : row.motionRaised ? Theme.container : row.active ? Theme.high : row.hovered ? Theme.container : "transparent"
        border.width: row.keyboardCurrent ? 2 : 0; border.color: Theme.primary
        Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
    }
    MouseArea {
        id: pointer; anchors.fill: parent; anchors.rightMargin: 60
        enabled: !!row.selection; acceptedButtons: Qt.LeftButton | Qt.RightButton; hoverEnabled: true; preventStealing: true
        onPressed: mouse=> {if(mouse.button===Qt.RightButton){if(row.track.kind!=="smart")row.menuRequested(row.track,row.rowIndex,row);return;}row.pressPoint=Qt.point(mouse.x,mouse.y);row.pressModifiers=mouse.modifiers;row.dragging=false;row.forceActiveFocus();row.listOwner.currentIndex=row.selectionIndex;}
        onPositionChanged: mouse=> {
            if(!(pressedButtons&Qt.LeftButton) || !row.selectable)return;
            const p=mapToItem(null,mouse.x,mouse.y);
            if(!row.dragging && Math.hypot(mouse.x-row.pressPoint.x,mouse.y-row.pressPoint.y)>10){row.dragging=true;row.listOwner.beginDrag(row.selectionIndex,p);}
            if(row.dragging)row.dragHub.move(p);
        }
        onReleased: mouse=> {
            if(mouse.button===Qt.RightButton)return;
            if(row.dragging){row.dragHub.finish();row.dragging=false;return;}
            if(row.selectable && (mouse.x<64 || row.pressModifiers&(Qt.ControlModifier|Qt.ShiftModifier)))row.selection.select(row.selectionIndex,mouse.x<64?Qt.ControlModifier:row.pressModifiers);
            else if(row.selection.count && row.selectable)row.selection.select(row.selectionIndex,0);
            else row.clicked();
        }
        onCanceled: {if(row.dragging)row.dragHub.cancel();row.dragging=false;}
        onDoubleClicked: mouse=> {if(!(mouse.modifiers&(Qt.ControlModifier|Qt.ShiftModifier)))row.clicked();}
    }
    contentItem: RowLayout {
        spacing: 14
        Item {
            Layout.preferredWidth: row.queueMode?(app.compactDensity?36:48):Theme.rowArtwork; Layout.preferredHeight: Layout.preferredWidth
            Icon { anchors.centerIn: parent; name: "shuffle"; size: 24; ink: Theme.primary; visible: row.track.kind==="smart" }
            Artwork { visible: row.track.kind!=="smart"; anchors.fill: parent; url: row.track.art || ""; radius: 8; pixels: 112 }
            Rectangle { anchors.centerIn: parent; width: 28; height: 28; radius: 8; visible: opacity>0; opacity: row.selectionVisible?1:0; color: row.selected?Theme.primary:Theme.container; border.width: row.selected?0:2; border.color: Theme.muted
                Behavior on opacity { NumberAnimation { duration: app.motion?Theme.fast:0; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
                Icon { anchors.centerIn: parent; name: "check"; size: 20; ink: Theme.primaryText; visible: row.selected }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true; spacing: 3
            MatchText { objectName: "trackTitle"; query: row.matchQuery; tooltipEnabled: row.titleRevealAllowed; revealFocused: row.keyboardCurrent; sourceText: row.track.title || ""; Layout.fillWidth: true; font.pixelSize: Theme.bodyLarge; font.weight: row.active ? Font.DemiBold : Font.Medium; color: row.selected ? Theme.containerText : row.active ? Theme.primary : Theme.text }
            MatchText { visible: row.track.kind!=="smart"; query: row.matchQuery; tooltipEnabled: row.titleRevealAllowed; revealFocused: row.keyboardCurrent; sourceText: row.track.artist || (row.track.kind === "artist" ? "Artist" : row.track.kind === "album" ? "Album" : row.track.kind === "playlist" ? "Playlist" : ""); Layout.fillWidth: true; color: row.selected ? Theme.containerText : Theme.muted; font.pixelSize: Theme.bodyMedium }
        }
        PlayingIndicator { ink: row.selected ? Theme.containerText : Theme.primary; visible: row.active }
        SungText { font.features: {"tnum": 1}; visible: !row.queueMode || row.width>350; text: row.track.duration || (row.track.seconds ? app.formatTime(row.track.seconds*1000) : ""); font.pixelSize: 12; color: row.selected ? Theme.containerText : Theme.muted; Layout.rightMargin: 2 }
        MButton { visible: row.track.kind!=="smart"; symbol: "more"; tip: "Track actions"; Accessible.name: "Actions for "+(row.track.title||"track"); ink: row.selected ? Theme.containerText : Theme.text; onClicked: row.menuRequested(row.track,row.rowIndex,this) }
    }
}
