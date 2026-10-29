import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
MDialog {
    id:dialog;objectName:"viewLayoutDialog";title:qsTranslate("Main", "View layout");modal:true;standardButtons:Dialog.Close
    width:Math.min(500,parent.width-32);height:Math.min(parent.height-32,implicitHeight)
    // The body is built the first time the dialog opens. MDialog sets
    // `built` on aboutToShow, which runs before the enter transition, so
    // the first frame of that transition already has the content in it.
    // Popup places its default contentItem below the header and inside padding.
    // Anchoring a replacement contentItem to the Popup bypasses that layout.
    Loader {
        anchors.fill: parent
        active: dialog.built
        sourceComponent: Component {
            ColumnLayout {spacing:12
                SungText {text:qsTranslate("Main", "Density");font.pixelSize:Theme.titleMedium;typeRole:"titleMedium"}
                MSegmentedControl {
                    accessibleName:qsTranslate("Main", "View density")
                    options:[{key:-1,label:qsTranslate("Main", "Default"),name:"viewDensityDefault"},{key:0,label:qsTranslate("Main", "Comfortable"),name:"viewDensityComfortable"},{key:1,label:qsTranslate("Main", "Compact"),name:"viewDensityCompact"}]
                    value:app.viewDensity;onChosen:value=>app.viewDensity=value
                }
                SungText {visible:app.viewSupportsGrid;text:qsTranslate("Main", "Layout");font.pixelSize:Theme.titleMedium;typeRole:"titleMedium"}
                MSegmentedControl {
                    visible:app.viewSupportsGrid;accessibleName:qsTranslate("Main", "Layout")
                    options:[{key:"grid",label:qsTranslate("Main", "Grid"),name:"viewModeGrid"},{key:"list",label:qsTranslate("Main", "List"),name:"viewModeList"}]
                    value:app.viewMode;onChosen:value=>app.viewMode=value
                }
            }
        }
    }
}
