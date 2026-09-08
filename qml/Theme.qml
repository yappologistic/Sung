pragma Singleton
import QtQuick
QtObject {
    readonly property string fontFamily: "Google Sans Flex"
    readonly property int displaySmall: 36
    readonly property int headlineMedium: 28
    readonly property int headlineSmall: 24
    readonly property int titleLarge: 22
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
    readonly property color primary: followDesktop ? desktopTheme.colors.primary : (dark ? "#ffb596" : "#964829")
    readonly property color primaryText: followDesktop ? desktopTheme.colors.primaryText : (dark ? "#572008" : "#ffffff")
    readonly property color primaryContainer: followDesktop ? desktopTheme.colors.primaryContainer : (dark ? "#75351b" : "#ffdbcb")
    readonly property color containerText: followDesktop ? desktopTheme.colors.containerText : (dark ? "#ffdbcb" : "#743419")
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
