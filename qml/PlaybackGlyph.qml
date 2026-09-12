import QtQuick
import QtQuick.Shapes
Item {
    id:glyph;objectName:"playbackGlyph";width:24;height:24
    property bool paused:false
    property color ink:Theme.text
    property real progress:0
    property bool ready:false
    readonly property bool animate:ready && app.motion && visible && Window.window && Window.window.visible && Window.window.visibility!==Window.Minimized
    onPausedChanged:progress=paused?1:0
    onAnimateChanged:if(!animate){morph.stop();settle.restart();}
    Timer {id:settle;interval:0;onTriggered:if(!glyph.animate){morph.stop();glyph.progress=glyph.paused?1:0;}}
    Component.onCompleted:{progress=paused?1:0;ready=true;}
    Behavior on progress {enabled:glyph.animate;NumberAnimation {id:morph;duration:200;easing.type:Easing.BezierSpline;easing.bezierCurve:Theme.effectsCurve}}
    // Stable endpoints use the bundled Material Symbols. Only the short transition
    // interpolates the two pieces of the play triangle into the pause bars.
    Icon {anchors.centerIn:parent;name:glyph.paused?"pause":"play";ink:glyph.ink;visible:!morph.running}
    function outline(right){
        const a=right?[[11,8],[18,12],[18,12],[11,16]]:[[8,6],[11,8],[11,16],[8,18]];
        const b=right?[[14,5],[18,5],[18,19],[14,19]]:[[6,5],[10,5],[10,19],[6,19]];
        const points=a.map((p,i)=>[p[0]+(b[i][0]-p[0])*progress,p[1]+(b[i][1]-p[1])*progress]);
        return "M"+points.map(p=>p.join(",")).join(" L")+" Z";
    }
    Shape {anchors.fill:parent;visible:morph.running
        ShapePath {strokeWidth:0;fillColor:glyph.ink;PathSvg {path:glyph.outline(false)}}
        ShapePath {strokeWidth:0;fillColor:glyph.ink;PathSvg {path:glyph.outline(true)}}
    }
}
