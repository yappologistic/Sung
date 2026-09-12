pragma Singleton
import QtQuick
QtObject {
    property color artworkSeed: "transparent"
    Behavior on artworkSeed {enabled:app.motion && app.artworkAccent;ColorAnimation {duration:240;easing.type:Easing.InOutCubic}}
    readonly property bool useArtwork: app.artworkAccent && artworkSeed.a > 0
    function blend(a,b,t) {return Qt.rgba(a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t,1);}
    function luminance(c) {
        function linear(v) {return v<=0.04045?v/12.92:Math.pow((v+0.055)/1.055,2.4);}
        return 0.2126*linear(c.r)+0.7152*linear(c.g)+0.0722*linear(c.b);
    }
    function contrast(a,b) {let x=luminance(a),y=luminance(b);return (Math.max(x,y)+0.05)/(Math.min(x,y)+0.05);}
    function readable(seed,surfaces) {
        const end=dark?Qt.rgba(1,1,1,1):Qt.rgba(0,0,0,1);
        for(let i=0;i<=100;++i){const color=blend(seed,end,i/100);if(surfaces.every(s=>contrast(color,s)>=4.5))return color;}
        return end;
    }
    readonly property string fontFamily: "Google Sans Flex"
    readonly property int displaySmall: 36
    readonly property int headlineMedium: 28
    readonly property int headlineSmall: 24
    readonly property int titleLarge: 22
    readonly property int rowHeight: app.viewCompactDensity ? 56 : 72
    readonly property int rowArtwork: app.viewCompactDensity ? 36 : 48
    readonly property int gridCell: app.viewCompactDensity ? 148 : 180
    readonly property int bodyLarge: 16
    readonly property int bodyMedium: 14
    readonly property int labelLarge: 14
    readonly property int labelMedium: 12
    readonly property real hoverOpacity: 0.08
    readonly property real pressedOpacity: 0.10
    readonly property bool followDesktop: app.theme === "system" && desktopTheme.available
    readonly property bool dark: followDesktop ? desktopTheme.dark : app.theme === "dark" || (app.theme === "system" && Application.styleHints.colorScheme === Qt.Dark)
    readonly property color background: followDesktop ? desktopTheme.colors.background : (dark ? "#181211" : "#fff8f6")
    readonly property color surface: followDesktop ? desktopTheme.colors.surface : (dark ? "#201a18" : "#fff1ec")
    readonly property color container: followDesktop ? desktopTheme.colors.container : (dark ? "#2b2320" : "#f6e5de")
    readonly property color high: followDesktop ? desktopTheme.colors.high : (dark ? "#382c28" : "#efddd5")
    readonly property color text: followDesktop ? desktopTheme.colors.text : (dark ? "#f5ded5" : "#281912")
    readonly property color muted: followDesktop ? desktopTheme.colors.muted : (dark ? "#d5bfb5" : "#705c53")
    readonly property color outline: followDesktop ? desktopTheme.colors.outline : (dark ? "#57443b" : "#dcc5b9")
    // Controls need a stronger boundary than decorative surface dividers.
    readonly property color controlOutline: Qt.rgba(muted.r, muted.g, muted.b, dark ? 0.65 : 0.8)
    readonly property color primary: useArtwork ? readable(artworkSeed,[background,surface,container,high]) : followDesktop ? desktopTheme.colors.primary : (dark ? "#ffb596" : "#964829")
    readonly property color primaryText: useArtwork ? (luminance(primary)>0.179?"#000000":"#ffffff") : followDesktop ? desktopTheme.colors.primaryText : (dark ? "#572008" : "#ffffff")
    readonly property color primaryContainer: useArtwork ? blend(container,primary,0.16) : followDesktop ? desktopTheme.colors.primaryContainer : (dark ? "#75351b" : "#ffdbcb")
    readonly property color containerText: useArtwork ? readable(primary,[primaryContainer]) : followDesktop ? desktopTheme.colors.containerText : (dark ? "#ffdbcb" : "#743419")
    readonly property color secondary: followDesktop ? desktopTheme.colors.secondary : (dark ? "#d8c4a0" : "#6c5b3b")
    readonly property color error: dark ? "#ffb4ab" : "#ba1a1a"
    readonly property int fast: app.motion ? 150 : 0
    readonly property int normal: app.motion ? 200 : 0
    readonly property int slow: app.motion ? 500 : 0
    readonly property int enterDuration: app.motion ? 200 : 0
    readonly property int exitDuration: app.motion ? 100 : 0
    readonly property var enterCurve: [0,0,0,1,1,1]
    readonly property var exitCurve: [0.3,0,1,1,1,1]
    // M3 published spring-to-curve conversions for non-gesture transitions.
    readonly property var fastSpatialCurve: [0.42,1.67,0.21,0.90,1,1]
    readonly property var effectsCurve: [0.34,0.80,0.34,1,1,1]
    readonly property var fastEffectsCurve: [0.31,0.94,0.34,1,1,1]
    readonly property var curve: [0.27,1.06,0.18,1,1,1]
}
