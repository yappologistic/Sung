import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
MDialog {
    id:dialog;objectName:"viewLayoutDialog";title:"View layout";modal:true;standardButtons:Dialog.Close
    width:Math.min(500,parent.width-32);height:Math.min(parent.height-32,implicitHeight)
    // The body is built the first time the dialog opens. MDialog sets
    // `built` on aboutToShow, which runs before the enter transition, so
    // the first frame of that transition already has the content in it.
    contentItem: Loader {
        anchors.fill: parent
        active: dialog.built
        sourceComponent: Component {
            ColumnLayout {spacing:12
                SungText {text:"Density";font.pixelSize:Theme.titleMedium;typeRole:"titleMedium"}
                MSegmentedControl {
                    accessibleName:"View density"
                    options:[{key:-1,label:"Default",name:"viewDensityDefault"},{key:0,label:"Comfortable",name:"viewDensityComfortable"},{key:1,label:"Compact",name:"viewDensityCompact"}]
                    value:app.viewDensity;onChosen:value=>app.viewDensity=value
                }
                SungText {visible:app.viewSupportsGrid;text:"Layout";font.pixelSize:Theme.titleMedium;typeRole:"titleMedium"}
                MSegmentedControl {
                    visible:app.viewSupportsGrid;accessibleName:"Layout"
                    options:[{key:"grid",label:"Grid",name:"viewModeGrid"},{key:"list",label:"List",name:"viewModeList"}]
                    value:app.viewMode;onChosen:value=>app.viewMode=value
                }
            }
        }
    }
}
