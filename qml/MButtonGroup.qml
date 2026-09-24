import QtQuick

// Material 3 standard button group.
//
// A standard group adds interaction between adjacent buttons: "the selected
// button changes shape and width" and "adjacent buttons move and temporarily
// change width" (button group guidelines, Usage). The numbers are Compose's
// ButtonGroup.kt:
//
// - A press drives a progress from 0 to 1 on the fast spatial spring
//   (defaultAnimationSpec, :138), so the width rings the way the rest of the
//   expressive motion scheme does.
// - The pressed button grows by ButtonGroupDefaults.ExpandedRatio, 15% of its
//   width (:186). A middle button takes half from each neighbour, a first or
//   last one takes all of it from the one it has (:474-514), and no neighbour
//   gives up more than its compression limit, ButtonDefaults.ContentPadding's
//   end (BaselineButtonTokens.TrailingSpace, 24dp).
// - Releasing waits until the progress has passed 0.75 before shrinking back
//   (EnlargeOnPressNode, :1040), so a quick tap still shows.
//
// The group moves nothing around it: what the pressed button takes, its
// neighbours give, so its width holds and its centre stays where it is.
Row {
    id: group
    objectName: "buttonGroup"
    // The space between buttons depends on their size so every target stays
    // 48dp: "Standard button group inner padding: XS 18dp, S 12dp, M 8dp,
    // L 8dp, XL 8dp" (button groups, Specs). Compose ships only the small
    // one (ButtonGroupSmallTokens.BetweenSpace, 12dp) and uses it for every
    // size, which spaced large buttons half as wide again as the spec.
    property string size: "small"
    spacing: ({xsmall: 18, small: 12, medium: 8, large: 8, xlarge: 8})[size] || 12
    property real expandedRatio: 0.15
    readonly property real compressionLimit: 24

    readonly property var buttons: {
        const found=[];
        for(let i=0;i<children.length;++i){
            const child=children[i];
            if(child.visible && child.groupPress!==undefined)found.push(child);
        }
        return found;
    }
    onButtonsChanged: layoutWidths()

    function layoutWidths() {
        const list=buttons;
        const growth=list.map(()=>0);
        for(let i=0;i<list.length;++i){
            const press=list[i].groupPress;
            if(press===0 || list.length<2)continue;
            const width=Math.round(list[i].sizedSquareWidth);
            if(i>0 && i<list.length-1){
                const each=press*Math.min(group.expandedRatio*width/2,group.compressionLimit);
                growth[i-1]-=each;growth[i+1]-=each;growth[i]+=2*each;
            } else {
                const all=press*Math.min(group.expandedRatio*width,group.compressionLimit);
                growth[i===0?1:i-1]-=all;growth[i]+=all;
            }
        }
        for(let i=0;i<list.length;++i)list[i].groupExpansion=growth[i];
    }

    Component {
        id: pressMotion
        NumberAnimation {
            objectName: "buttonGroupPressMotion"
            property: "groupPress"
            duration: Theme.springFastSpatialMs
            easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial
        }
    }
    Component {
        id: pressTracker
        QtObject {
            required property Item button
            property bool releasing: false
            readonly property NumberAnimation motion: pressMotion.createObject(this,{target:button})
            function run(to) {
                releasing=false;motion.stop();
                if(Theme.springFastSpatialMs<=0){button.groupPress=to;return;}
                motion.from=button.groupPress;motion.to=to;motion.start();
            }
            readonly property Connections watch: Connections {
                target: button
                function onDownChanged() {
                    if(button.down)run(1);
                    else if(button.groupPress>=0.75 || Theme.springFastSpatialMs<=0)run(0);
                    else releasing=true;
                }
                function onGroupPressChanged() {
                    if(releasing && button.groupPress>=0.75)run(0);
                    group.layoutWidths();
                }
            }
        }
    }
    Component.onCompleted: {
        for(let i=0;i<children.length;++i)
            if(children[i].groupPress!==undefined){
                children[i].grouped=true;
                pressTracker.createObject(group,{button:children[i]});
            }
    }
}
