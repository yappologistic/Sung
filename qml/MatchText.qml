import QtQuick
import QtQuick.Controls
SungText {
    id: label
    property string sourceText: ""
    property string query: ""
    property bool revealFocused: false
    property bool tooltipEnabled: true
    // A row's MouseArea receives pointer events before this label does. Its
    // position is passed in where needed; other MatchText users keep HoverHandler.
    property bool pointerHovered: false
    readonly property bool hoveredForTooltip: hover.hovered || pointerHovered
    // BasicTooltip.kt:188-199 dismisses a tooltip on its anchor's press;
    // :262-280 starts mouse display again on Enter. A row can keep focus after
    // a click, so pointer dismissal waits for Exit and then the next Enter.
    property int tooltipRearm: 0 // 0 armed, 1 wait for Exit, 2 wait for Enter, 3 wait for focus exit
    function dismissTooltip() { tooltipRearm = hoveredForTooltip ? 1 : 3 }
    onRevealFocusedChanged: if (!revealFocused && tooltipRearm === 3) tooltipRearm = 0
    onHoveredForTooltipChanged: {
        if (!hoveredForTooltip && tooltipRearm === 1) tooltipRearm = 2
        else if (hoveredForTooltip && tooltipRearm === 2) tooltipRearm = 0
    }
    function escapeText(value) {return value.replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;").replace(/"/g,"&quot;");}
    function marked(value,search) {
        const words=search.toLowerCase().trim().split(/\s+/).filter(w=>w.length>0);
        if(!words.length)return escapeText(value);
        const lower=value.toLowerCase();let result="",i=0;
        while(i<value.length){let length=0;for(const word of words)if(lower.slice(i,i+word.length)===word)length=Math.max(length,word.length);
            if(length){result+="<b>"+escapeText(value.slice(i,i+length))+"</b>";i+=length;}
            else {result+=escapeText(value[i]);++i;}}
        return result;
    }
    text: query.trim()?marked(sourceText,query):sourceText
    textFormat: query.trim()?Text.StyledText:Text.PlainText
    Accessible.name: sourceText
    HoverHandler { id: hover }
    Loader {
        id: tooltipLoader
        readonly property bool wanted: label.tooltipRearm === 0 && label.tooltipEnabled && label.truncated && label.visible && (label.hoveredForTooltip || label.revealFocused)
        // Keep the popup alive until its exit transition has finished.
        active: false
        function releaseIfIdle() { if (!wanted && (!item || !item.visible)) active=false; }
        onWantedChanged: { if (wanted) active=true; else releaseIfIdle(); }
        Component.onCompleted: if (wanted) active=true
        // An elided label says the rest of itself in a plain tooltip, which is
        // what Material calls this: a short label about the thing under the
        // pointer. It was drawn as a small surface of its own, which is the
        // rich tooltip's treatment and says more than it should.
        sourceComponent: MTooltip {
            objectName: "fullTitleTip"
            parent: label
            visible: tooltipLoader.wanted
            onClosed: Qt.callLater(tooltipLoader.releaseIfIdle)
            delay: 650; timeout: -1
            width: Math.min(360,label.Window.window?label.Window.window.width-32:360)
            text: label.sourceText
            contentItem: SungText {
                text: label.sourceText; wrapMode: Text.Wrap; maximumLineCount: 8
                font.pixelSize: Theme.bodySmall; color: Theme.inverseSurfaceText
            }
        }
    }
}
