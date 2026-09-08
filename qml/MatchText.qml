import QtQuick
import QtQuick.Controls
SungText {
    id: label
    property string sourceText: ""
    property string query: ""
    property bool revealFocused: false
    property bool tooltipEnabled: true
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
    ToolTip {
        objectName: "fullTitleTip"
        visible: label.tooltipEnabled && label.truncated && label.visible && (hover.hovered || label.revealFocused)
        delay: 650; timeout: -1; width: Math.min(360,label.Window.window?label.Window.window.width-32:360); padding: 12
        contentItem: SungText { text: label.sourceText; wrapMode: Text.Wrap; maximumLineCount: 8; font.pixelSize: 13; color: Theme.text }
        background: Rectangle { color: Theme.high; radius: 12; border.color: Theme.outline }
    }
}
