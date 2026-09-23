import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
MDialog {
    id:dialog;objectName:"sessionsDialog";title:"Listening sessions";modal:true
    // AlertDialog.kt:147 uses sizeIn(maxWidth = 560.dp), then
    // takes its height from content under the window's height constraint.
    // Keep a short session list short and let a long one scroll at that cap.
    width:fitWidth(560)
    height:fitHeight(implicitHeaderHeight+implicitFooterHeight+topPadding+bottomPadding
                     +(body.item?body.item.implicitHeight:88))
    standardButtons:Dialog.Close
    scrollSource:body.item?body.item.scrolling:null
    property var selectedSession: ({})
    property string action: ""
    // The body is built the first time the dialog opens. MDialog sets
    // `built` on aboutToShow, which runs before the enter transition, so
    // the first frame of that transition already has the content in it.
    Loader {
        id:body
        anchors.fill: parent
        active: dialog.built
        sourceComponent: Component {
            Item {
                implicitHeight:contentColumn.implicitHeight
                readonly property Flickable scrolling:list
                ColumnLayout {
                    id:contentColumn
                    // Loader fills the available body; the column keeps its
                    // Qt Quick Layouts implicit height until a long list needs
                    // that space for scrolling.
                    width:parent.width;height:Math.min(implicitHeight,parent.height);spacing:12
                    RowLayout {Layout.fillWidth:true
                        MTextField {id:nameField;objectName:"sessionName";Layout.fillWidth:true;placeholderText:"Session name";maximumLength:80;Accessible.name:"New session name";onAccepted:saveButton.clicked()}
                        MButton {id:saveButton;objectName:"saveSessionButton";text:"Save queue";filled:true;enabled:app.queue.count>0 && !!nameField.text.trim();onClicked:{if(enabled&&app.saveSession(nameField.text))nameField.clear();}}
                    }
                    ListView {
                        id:list;objectName:"sessionsList";Layout.fillWidth:true;Layout.fillHeight:count>0
                        Layout.preferredHeight:count>0?count*96-8:emptyMessage.implicitHeight
                        clip:true;spacing:8;reuseItems:true;model:dialog.visible?app.sessions:[]
                        ScrollBar.vertical:MScrollBar {}
                        delegate:Rectangle {
                            // Hidden while pooled: a culled delegate still takes Tab (TrackRow.qml).
                            property bool pooled: false
                            visible: !pooled
                            ListView.onPooled: pooled=true
                            ListView.onReused: pooled=false
                            required property var modelData;width:list.width;height:88;radius:Theme.shapeLarge;color:Theme.high
                            RowLayout {anchors.fill:parent;anchors.margins:12;spacing:8
                                ColumnLayout {Layout.fillWidth:true;spacing: 4
                                    SungText {text:modelData.title;Layout.fillWidth:true;font.weight:Font.Medium}
                                    SungText {text:modelData.song || "";Layout.fillWidth:true;color:Theme.muted;font.pixelSize:Theme.bodySmall}
                                    SungText {text:modelData.count+(modelData.count===1?" track · ":" tracks · ")+Math.floor(modelData.position/60000)+":"+String(Math.floor(modelData.position/1000)%60).padStart(2,"0");color:Theme.muted;font.pixelSize:Theme.bodySmall}
                                }
                                MButton {objectName:"resumeSessionButton";symbol:"play";tip:"Resume session";onClicked:{dialog.selectedSession=modelData;dialog.action="resume";sessionConfirmDialog.open();}}
                                MButton {symbol:"more";tip:"Session actions";onClicked:{dialog.selectedSession=modelData;actions.popup(this);}}
                            }
                        }
                        // Keep the list reachable as the dialog's scroll
                        // source while the empty message sits at its top.
                        SungText {id:emptyMessage;objectName:"sessionsEmpty";width:list.width;visible:list.count===0;text:"Save a queue to return to it later";color:Theme.muted}
                    }
                }
            }
        }
    }
    MMenu {id:actions
        MMenuItem {text:"Update from current queue";enabled:app.queue.count>0;onTriggered:{dialog.action="update";sessionConfirmDialog.open();}}
        MMenuItem {text:"Rename";onTriggered:{renameField.text=dialog.selectedSession.title;renameDialog.open();}}
        MMenuItem {text:"Delete";onTriggered:{dialog.action="delete";sessionConfirmDialog.open();}}
    }
    MDialog {id:sessionConfirmDialog;objectName:"sessionConfirm";anchors.centerIn:parent;modal:true;width:Math.min(440,dialog.width)
        title:dialog.action==="resume"?"Replace the current queue?":dialog.action==="update"?"Replace this saved session?":"Delete this session?"
        standardButtons:Dialog.Ok|Dialog.Cancel
        onAccepted:{if(dialog.action==="resume"){if(app.restoreSession(dialog.selectedSession.id))dialog.close();}else if(dialog.action==="update")app.saveSession(dialog.selectedSession.title,dialog.selectedSession.id);else app.deleteSession(dialog.selectedSession.id);}
    }
    MDialog {id:renameDialog;anchors.centerIn:parent;modal:true;width:Math.min(440,dialog.width);title:"Rename session";standardButtons:Dialog.Ok|Dialog.Cancel;acceptEnabled:!!renameField.text.trim();initialFocus:renameField
        MTextField {id:renameField;width:parent.width;maximumLength:80;Accessible.name:"Session name"}
        onAccepted:app.renameSession(dialog.selectedSession.id,renameField.text)
    }
}
