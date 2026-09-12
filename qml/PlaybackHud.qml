import QtQuick
import QtQuick.Layouts
Rectangle {
    id: hud
    objectName: "playbackHud"
    property string symbol: "volume"
    property string label: ""
    property bool shown: false
    function show(icon, text) {symbol=icon;label=text;shown=true;dismiss.restart();}
    visible: opacity>0
    opacity: shown?1:0
    width: Math.max(120,contents.implicitWidth+32);height:48;radius:24
    color: Theme.high
    Accessible.role: Accessible.StaticText
    Accessible.name: label
    Behavior on opacity {NumberAnimation {duration:Theme.fast;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.fastEffectsCurve}}
    Timer {id:dismiss;interval:1100;onTriggered:hud.shown=false}
    RowLayout {id:contents;anchors.centerIn:parent;spacing:12
        Icon {name:hud.symbol;size:24;ink:Theme.primary}
        SungText {text:hud.label;color:Theme.text;font.pixelSize:16;font.features:{"tnum":1}}
    }
}
