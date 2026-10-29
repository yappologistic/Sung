import QtQuick
import QtQuick.Layouts

// What to do with the songs that are selected.
//
// Material calls this a docked toolbar: the actions that apply to what is on
// screen, on a container of their own and docked across the surface rather
// than floating loose above the list.
MDockedToolbar {
    id: bar
    required property var view
    property bool canRemove: false
    visible: view.selection.count>0
    objectName: "selectionToolbar"

    MButton { symbol: "close"; tip: qsTranslate("Main", "Clear selection · Esc"); onClicked: bar.view.selection.clear() }
    SungText { text: qsTranslate("Main", "Selected: %1").arg(bar.view.selection.count); Layout.fillWidth: true; font.pixelSize: Theme.labelLarge; labelRole: true }
    MButton { symbol: "next"; tip: qsTranslate("Main", "Play selected next"); onClicked: {const before=app.queue.count;app.enqueueItems(bar.view.selection.items(),true);if(app.queue.count>before)confirm();} }
    MButton { symbol: "queue"; tip: qsTranslate("Main", "Add selected to queue"); onClicked: {const before=app.queue.count;app.enqueueItems(bar.view.selection.items());if(app.queue.count>before)confirm();} }
    MButton { objectName: bar.view.queueMode?"bulkQueuePlaylist":"bulkCollectionPlaylist"; symbol: "plus"; tip: qsTranslate("Main", "Add selected to playlist"); onClicked: bar.view.addSelected() }
    MButton { symbol: "remove"; tip: qsTranslate("Main", "Remove selected · Delete"); visible: bar.canRemove; onClicked: bar.view.removeSelected() }
}
