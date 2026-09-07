import QtQuick
import QtQuick.Layouts
RowLayout {
    id: bar
    required property var view
    property bool canRemove: false
    visible: view.selection.count>0
    implicitHeight: 48
    spacing: 4
    MButton { symbol: "close"; tip: "Clear selection · Esc"; implicitWidth: 36; implicitHeight: 40; onClicked: bar.view.selection.clear() }
    SungText { text: bar.view.selection.count+" selected"; Layout.fillWidth: true; font.pixelSize: 13 }
    MButton { symbol: "next"; tip: "Play selected next"; implicitWidth: 40; implicitHeight: 40; onClicked: app.enqueueItems(bar.view.selection.items(),true) }
    MButton { symbol: "queue"; tip: "Add selected to queue"; implicitWidth: 40; implicitHeight: 40; onClicked: app.enqueueItems(bar.view.selection.items()) }
    MButton { objectName: bar.view.queueMode?"bulkQueuePlaylist":"bulkCollectionPlaylist"; symbol: "plus"; tip: "Add selected to playlist"; implicitWidth: 40; implicitHeight: 40; onClicked: bar.view.addSelected() }
    MButton { symbol: "remove"; tip: "Remove selected · Delete"; visible: bar.canRemove; implicitWidth: 40; implicitHeight: 40; onClicked: bar.view.removeSelected() }
}
