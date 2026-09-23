pragma Singleton
import QtQuick
QtObject {
    property color artworkSeed: "transparent"
    Behavior on artworkSeed {enabled:app.motion && app.artworkAccent;ColorAnimation {duration:springEffectsMs;easing.type:Easing.BezierSpline;easing.bezierCurve:springEffects}}
    readonly property bool useArtwork: app.artworkAccent && artworkSeed.a > 0
    // A hand-picked Material source color, used when no cover is driving the theme.
    property color accentSeed: app.accentColor ? app.accentColor : "transparent"
    Behavior on accentSeed {enabled:app.motion;ColorAnimation {duration:springEffectsMs;easing.type:Easing.BezierSpline;easing.bezierCurve:springEffects}}
    readonly property bool useAccent: !useArtwork && accentSeed.a > 0
    readonly property bool useSource: useArtwork || useAccent
    readonly property color sourceColor: useArtwork ? artworkSeed : accentSeed
    // The warm default source runs through MCU's role resolver. Its standard
    // Tonal Spot roles retain the original palette, while contrast and variant
    // alternatives take the published curves (color_spec_2021.ts:130-739).
    readonly property color defaultSeed: "#b75f38"
    function standardDefaultRoles(generated) {
        // These are the existing standard palette, kept pixel-for-pixel for
        // the no-source Tonal Spot state. Contrast and variant alternatives
        // are always the generated MCU roles.
        const old = dark ? ({background:"#181211",surface:"#181211",surfaceContainerLow:"#201a18",surfaceContainer:"#2b2320",surfaceContainerHigh:"#382c28",surfaceContainerHighest:"#433733",onSurface:"#f5ded5",onSurfaceVariant:"#d5bfb5",outlineVariant:"#57443b",primary:"#ffb596",onPrimary:"#572008",primaryContainer:"#75351b",onPrimaryContainer:"#ffdbcb",secondary:"#d8c4a0",onSecondary:"#3b2f15",secondaryContainer:"#54432a",onSecondaryContainer:"#f5e0bb",tertiary:"#b8ceb0",onTertiary:"#243420",tertiaryContainer:"#3b5236",onTertiaryContainer:"#d4eacb",inverseSurface:"#f5ded5",inverseOnSurface:"#392e2a",inversePrimary:"#964829",error:"#ffb4ab",onError:"#690005",errorContainer:"#93000a",onErrorContainer:"#ffdad6",primaryFixed:"#ffdbcb",onPrimaryFixed:"#360f00",onPrimaryFixedVariant:"#743419"})
                         : ({background:"#fff8f6",surface:"#fff8f6",surfaceContainerLow:"#fff1ec",surfaceContainer:"#f6e5de",surfaceContainerHigh:"#efddd5",surfaceContainerHighest:"#e9d8d0",onSurface:"#281912",onSurfaceVariant:"#705c53",outlineVariant:"#dcc5b9",primary:"#964829",onPrimary:"#ffffff",primaryContainer:"#ffdbcb",onPrimaryContainer:"#743419",secondary:"#6c5b3b",onSecondary:"#ffffff",secondaryContainer:"#f5e0bb",onSecondaryContainer:"#221a04",tertiary:"#3b5236",onTertiary:"#ffffff",tertiaryContainer:"#d4eacb",onTertiaryContainer:"#233a1f",inverseSurface:"#3c2c25",inverseOnSurface:"#ffede6",inversePrimary:"#ffb596",error:"#ba1a1a",onError:"#ffffff",errorContainer:"#ffdad6",onErrorContainer:"#410002",primaryFixed:"#ffdbcb",onPrimaryFixed:"#360f00",onPrimaryFixedVariant:"#743419"})
        // Qt color values expose normalized r/g/b channels. Resolve the hex
        // entries before blending them into an animated scheme.
        function asColor(hex) {return Qt.rgba(parseInt(hex.slice(1,3),16)/255,parseInt(hex.slice(3,5),16)/255,parseInt(hex.slice(5,7),16)/255,1)}
        for(const name in old)old[name]=asColor(old[name])
        old.outline=blend(old.outlineVariant,old.onSurfaceVariant,0.5)
        return Object.assign({},generated,old)
    }
    // QML must read these settings here even with artwork active: an
    // invokable's internal C++ reads do not create binding dependencies.
    readonly property var roles: {
        const variant=app.colorVariant, contrast=app.colorContrast
        const map=app.colorScheme(useSource?sourceColor:defaultSeed,dark)
        return !useSource && variant==="tonalSpot" && contrast===0
               ? standardDefaultRoles(map) : map
    }
    function role(name,fallback) {const c=roles[name];return c===undefined?fallback:c;}
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
    // --- Spacing --------------------------------------------------------------
    // Material lays out on a 4dp grid and names the steps it uses. A gap that
    // is not one of these is a number somebody picked, and the interface audit
    // says so. Two values below the grid are Material's own: components hold
    // 6dp between an icon and the label beside it, which several component
    // token files publish, and a hairline is a hairline.
    readonly property int spaceSmall: 4
    readonly property int space: 8
    readonly property int spaceMedium: 12
    readonly property int spaceLarge: 16
    readonly property int spaceExtraLarge: 24

    // --- Shape ---------------------------------------------------------------
    // Material's ten step corner radius scale. Components map to a step by how
    // round they should look, not by how big they are, and `full` is a real
    // half-height rounding rather than a large fixed number.
    readonly property int shapeNone: 0
    readonly property int shapeExtraSmall: 4
    readonly property int shapeSmall: 8
    readonly property int shapeMedium: 12
    readonly property int shapeLarge: 16
    readonly property int shapeLargeIncreased: 20
    readonly property int shapeExtraLarge: 28
    readonly property int shapeExtraLargeIncreased: 32
    readonly property int shapeExtraExtraLarge: 48
    function shapeFull(size) { return size/2 }
    // Nested shapes look unbalanced sharing a radius. Material subtracts the
    // padding between them instead.
    function shapeInside(outer,padding) { return Math.max(0,outer-padding) }

    // --- Elevation -----------------------------------------------------------
    // Material's six levels, and the two shadows it casts at each one: a tight
    // key light at 30% and a wider ambient at 15%. Levels are dp of elevation;
    // the shadows are the published values for that level.
    // [vertical offset, blur, spread] for the key shadow and then the ambient.
    readonly property var elevationKey: [[0,0,0],[1,2,0],[1,2,0],[1,3,0],[2,3,0],[4,4,0]]
    readonly property var elevationAmbient: [[0,0,0],[1,3,1],[2,6,2],[4,8,3],[6,10,4],[8,12,6]]
    readonly property real elevationKeyOpacity: 0.30
    readonly property real elevationAmbientOpacity: 0.15

    // --- Buttons -------------------------------------------------------------
    // Material's five button sizes. A size is not just a height: it carries its
    // own corner for the squarer pressed and selected states, its own icon size,
    // its own padding and its own gap between icon and label.
    // Material drops the small button to 36dp when a precision pointer is
    // driving it, and stops reserving the 48dp target that exists to
    // disambiguate touches.
    readonly property bool precisePointer: app.precisePointer
    readonly property int minimumTarget: precisePointer ? 0 : 48
    readonly property var buttonHeights: precisePointer ? ({xsmall:32, small:36, medium:56, large:96})
                                                        : ({xsmall:32, small:40, medium:56, large:96})
    readonly property var buttonSquare: ({xsmall:shapeMedium, small:shapeMedium, medium:shapeLarge, large:shapeExtraLarge})
    readonly property var buttonIcon: ({xsmall:20, small:20, medium:24, large:32})
    // XSmallIconButtonTokens, SmallIconButtonTokens, MediumIconButtonTokens and
    // LargeIconButtonTokens keep their container heights with every pointer.
    readonly property var iconButtonHeights: ({xsmall:32, small:40, medium:56, large:96})
    // The same IconSize tokens give icon-only buttons their own glyph sizes;
    // ButtonSmallTokens.IconSize stays 20dp for a labelled small button.
    readonly property var iconButtonIcon: ({xsmall:20, small:24, medium:24, large:32})
    readonly property var buttonInset: ({xsmall:16, small:16, medium:24, large:48})
    readonly property var buttonGap: ({xsmall:8, small:8, medium:8, large:12})
    // An icon button is its own component in Material, with its own widths and
    // its own shapes. The width is the space kept either side of the glyph, in
    // three steps: a dense row takes the narrow one, a lone primary action the
    // wide one. [narrow, default, wide]
    readonly property var iconButtonSpace: ({xsmall:[4,6,10], small:[4,8,14], medium:[12,16,24], large:[16,32,48]})
    // Pressing moves the shape one step squarer, and the step belongs to the
    // size rather than being the same everywhere.
    readonly property var buttonPressed: ({xsmall:shapeSmall, small:shapeSmall, medium:shapeMedium, large:shapeLarge})
    // A boundary thickens with the size it draws around.
    readonly property var buttonOutline: ({xsmall:1, small:1, medium:1, large:2})
    readonly property var buttonLabel: ({xsmall:labelLarge, small:labelLarge, medium:titleMedium, large:headlineSmall})
    // The role the label is set in, which a size alone cannot name: 16 is
    // title medium on a medium button but body large on a list row. Compose
    // picks it by height and never emphasizes it (ButtonDefaults.textStyleFor).
    readonly property var buttonLabelRole: ({xsmall:"labelLarge", small:"labelLarge", medium:"titleMedium", large:"headlineSmall"})
    // Material's optical centering: content inside an asymmetric shape is
    // nudged by this much of the difference between its two corner radii, so it
    // looks centred rather than measuring centred.
    readonly property real opticalCentering: 0.11
    function opticalShift(startRadius,endRadius) { return opticalCentering*(startRadius-endRadius) }

    // --- Sliders -------------------------------------------------------------
    // Material's expressive slider is a track you can see rather than a rule
    // with a dot on it: a 16dp track at the extra small size, a handle that is
    // a 4dp bar the height of the touch target, and a gap held open around the
    // handle on both sides. The larger sizes are the same anatomy at a bigger
    // track; only the track and the handle grow.
    readonly property var sliderTrack: ({xsmall:16, small:24, medium:40, large:56, xlarge:96})
    readonly property var sliderHandleHeight: ({xsmall:44, small:44, medium:52, large:68, xlarge:108})
    readonly property int sliderHandle: 4
    // Material narrows the handle while it is held, so the value under it is
    // not hidden by the thing setting it.
    readonly property int sliderHandlePressed: 2
    readonly property int sliderGap: 6
    readonly property int sliderStop: 4
    // A disabled slider goes quiet rather than fading as a whole: the track it
    // has covered and the handle drop to 38 percent of onSurface, and the
    // track it has not to 12.
    readonly property real disabledTrackOpacity: 0.12
    function sliderQuiet(amount) { return Qt.rgba(text.r,text.g,text.b,amount) }

    // --- Lists ---------------------------------------------------------------
    // An expressive list item carries a shape that answers the pointer: it
    // rests nearly square, rounds as the pointer arrives, and rounds further
    // while it is pressed or holds focus.
    readonly property int listRest: shapeExtraSmall
    readonly property int listHovered: shapeMedium
    readonly property int listActive: shapeLarge
    // A segmented run is drawn as one group: the outer corners of the run are
    // the full step, the corners inside it are the resting one, and the items
    // are set apart rather than divided by a rule.
    readonly property int listSegmentedGap: 2

    // --- App bars ------------------------------------------------------------
    // Material's flexible app bars hug what is in them, so a bar with a
    // subtitle is taller than one without. The headline and the subtitle each
    // take a type role from the size.
    readonly property var appBarHeight: ({small:64, medium:112, large:120})
    readonly property var appBarHeightSubtitled: ({small:64, medium:136, large:152})
    readonly property var appBarSubtitle: ({small:labelMedium, medium:labelLarge, large:titleMedium})

    // --- Sheets --------------------------------------------------------------
    // Material's side sheet: a pane holding a headline and a close button over
    // its content, with this much room kept clear at its edges. Its width is
    // the supporting pane layout's, because this one is laid beside the page
    // rather than anchored over it.
    readonly property int sideSheetPadding: 24
    readonly property int sideSheetTopSpacing: 12

    // --- Toolbars ------------------------------------------------------------
    // Material replaced the bottom app bar with a docked toolbar: the same
    // 64dp strip on a container, with its items spaced between a floor and a
    // ceiling rather than spread to the edges.
    readonly property int toolbarHeight: 64
    readonly property int toolbarInset: 16
    readonly property int toolbarSpacingMin: 4

    // --- Selection controls --------------------------------------------------
    // A checkbox is a small square with a large target; a radio is a ring.
    // Both carry a state layer wider than the control and narrower than the
    // target, which is what the pointer lights up.
    readonly property int checkboxSize: 18
    readonly property int checkboxCorner: 2
    readonly property int radioSize: 20
    readonly property int selectionStateLayer: 40
    readonly property int selectionTarget: 48

    // --- Tabs ----------------------------------------------------------------
    // Material's active indicator: 3dp under a primary tab, as wide as its
    // label and never shorter than 24dp, and 2dp across a secondary one.
    readonly property int tabIndicatorPrimary: 3
    readonly property int tabIndicatorSecondary: 2

    // --- Chips ---------------------------------------------------------------
    readonly property int chipHeight: 32
    readonly property int chipIcon: 18
    readonly property int chipAvatar: 24

    // --- Extended floating action button -------------------------------------
    // Material's small extended FAB, which it now recommends in place of the
    // one that used to be the only size.
    readonly property int extendedFabHeight: 56
    readonly property int extendedFabInset: 16
    readonly property int extendedFabGap: 8

    // --- Motion --------------------------------------------------------------
    // Material describes motion as springs, published as a damping ratio and a
    // stiffness, not as a duration and a curve. The conversion lives in
    // src/m3motion.cpp, which solves each spring's own step response and fits
    // the curve Qt Quick animates on to it; the duration is the settling time
    // that falls out of the physics. Nothing here is chosen by eye.
    //
    // Two schemes. Expressive rings; standard barely does. Spatial springs move
    // things and are underdamped, so they pass their target and come back.
    // Effects springs carry colour and opacity, where passing the target would
    // mean showing a wrong value, so they are critically damped and do not.
    readonly property bool expressiveMotion: app.motionScheme !== "standard"
    readonly property var springs: app.motionSprings(expressiveMotion)
    readonly property var springFastSpatial: springs.fastSpatial.curve
    readonly property var springSpatial: springs.defaultSpatial.curve
    readonly property var springSlowSpatial: springs.slowSpatial.curve
    readonly property var springFastEffects: springs.fastEffects.curve
    readonly property var springEffects: springs.defaultEffects.curve
    readonly property var springSlowEffects: springs.slowEffects.curve
    readonly property int springFastSpatialMs: app.motion ? springs.fastSpatial.ms : 0
    readonly property int springSpatialMs: app.motion ? springs.defaultSpatial.ms : 0
    readonly property int springSlowSpatialMs: app.motion ? springs.slowSpatial.ms : 0
    readonly property int springFastEffectsMs: app.motion ? springs.fastEffects.ms : 0
    readonly property int springEffectsMs: app.motion ? springs.defaultEffects.ms : 0
    readonly property int springSlowEffectsMs: app.motion ? springs.slowEffects.ms : 0
    // LoadingIndicator.kt:400-419 uses a dedicated 0.6/200 spring with a 0.1
    // visibility threshold, so each shape finishes before the next 650ms slot.
    readonly property var loadingMorphSpring: springs.loadingMorph.curve
    readonly property int loadingMorphSpringMs: app.motion ? springs.loadingMorph.ms : 0

    // --- Typography ----------------------------------------------------------
    readonly property string fontFamily: "Google Sans Flex"
    // Material's emphasized styles lean on a variable font's weight and width
    // to carry hierarchy, rather than only its size. Google Sans Flex is
    // variable, so the emphasis is real rather than a synthesised bold.
    readonly property int emphasizedWidth: 110
    readonly property int regularWidth: 100
    // The weight belongs to the role, not to whether the style is a label:
    // title medium and title small sit at medium and go bold with the labels,
    // while everything else sits at regular and goes medium.
    function weightFor(emphasized,label,size,named) {
        const role = typeScale[typeRole(size,label,named)]
        return emphasized ? role[5] : role[4]
    }
    // Material's type scale. A role is a size, a line height and a letter
    // spacing together; setting the size alone leaves two thirds of the style
    // at whatever the toolkit happens to default to.
    readonly property int displayLarge: 57
    readonly property int displayMedium: 45
    readonly property int displaySmall: 36
    readonly property int headlineLarge: 32
    readonly property int headlineMedium: 28
    readonly property int headlineSmall: 24
    readonly property int titleLarge: 22
    readonly property int titleMedium: 16
    readonly property int titleSmall: 14
    readonly property int bodySmall: 12
    // ListTokens.ItemTwoLineContainerHeight is 72dp; compact density is the
    // user's 56dp option, matching ItemOneLineContainerHeight.
    readonly property int rowHeight: app.viewCompactDensity ? 56 : 72
    // ListTokens.ItemLeadingImageWidth/Height is 56dp. The compact option
    // uses ItemLeadingAvatarSize (40dp) to leave room within its 56dp row.
    readonly property int rowArtwork: app.viewCompactDensity ? 40 : 56
    readonly property int gridCell: app.viewCompactDensity ? 148 : 180
    readonly property int bodyLarge: 16
    readonly property int bodyMedium: 14
    readonly property int labelLarge: 14
    readonly property int labelMedium: 12
    readonly property int labelSmall: 11
    // [size, line height, letter spacing] for each role Material publishes.
    // [size, line height, tracking, emphasized tracking, weight, emphasized
    // weight]. Emphasis is a role of its own rather than a weight laid over
    // one: the tracking moves with it, and not by a constant, so body large
    // tightens from 0.5 to 0.15 while body medium opens from 0.2 to 0.25.
    readonly property var typeScale: ({
        displayLarge:  [57, 64, -0.2, 0.0,  Font.Normal, Font.Medium],
        displayMedium: [45, 52,  0.0, 0.0,  Font.Normal, Font.Medium],
        displaySmall:  [36, 44,  0.0, 0.0,  Font.Normal, Font.Medium],
        headlineLarge: [32, 40,  0.0, 0.0,  Font.Normal, Font.Medium],
        headlineMedium:[28, 36,  0.0, 0.0,  Font.Normal, Font.Medium],
        headlineSmall: [24, 32,  0.0, 0.0,  Font.Normal, Font.Medium],
        titleLarge:    [22, 28,  0.0, 0.0,  Font.Normal, Font.Medium],
        titleMedium:   [16, 24,  0.2, 0.15, Font.Medium, Font.Bold],
        titleSmall:    [14, 20,  0.1, 0.1,  Font.Medium, Font.Bold],
        bodyLarge:     [16, 24,  0.5, 0.15, Font.Normal, Font.Medium],
        bodyMedium:    [14, 20,  0.2, 0.25, Font.Normal, Font.Medium],
        bodySmall:     [12, 16,  0.4, 0.4,  Font.Normal, Font.Medium],
        labelLarge:    [14, 20,  0.1, 0.1,  Font.Medium, Font.Bold],
        labelMedium:   [12, 16,  0.5, 0.5,  Font.Medium, Font.Bold],
        labelSmall:    [11, 16,  0.5, 0.5,  Font.Medium, Font.Bold]
    })
    // A size on its own does not say which role it is: 16 is both title medium
    // and body large. Whether the text is a label settles the ones that matter,
    // and a style can name its role outright when it needs to.
    function typeRole(size,label,named) {
        if (named && typeScale[named]) return named
        if (label) return size <= 11 ? "labelSmall" : size <= 12 ? "labelMedium" : "labelLarge"
        if (size >= 51) return "displayLarge"
        if (size >= 40) return "displayMedium"
        if (size >= 34) return "displaySmall"
        if (size >= 30) return "headlineLarge"
        if (size >= 26) return "headlineMedium"
        if (size >= 23) return "headlineSmall"
        if (size >= 20) return "titleLarge"
        if (size >= 15) return "bodyLarge"
        if (size >= 13) return "bodyMedium"
        return "bodySmall"
    }
    // Material's line heights are absolute, and grow with a style that has been
    // scaled past the role it was named for.
    function lineFor(size,label,named) {
        const role = typeScale[typeRole(size,label,named)]
        return Math.round(size*(role[1]/role[0]))
    }
    function trackingFor(size,label,named,emphasized) {
        return typeScale[typeRole(size,label,named)][emphasized ? 3 : 2]
    }
    // The ring a keyboard leaves around whatever it has reached. Material draws
    // it in secondary rather than primary, so it still reads as a ring when it
    // lands on something already painted in the accent.
    readonly property color focusRing: secondary

    // Material's four state layers, and what it does to a disabled control: the
    // container drops to a tenth of onSurface and the content to 38%, rather
    // than the whole control fading together. The content is onSurface on most
    // components; a labelled button dims onSurfaceVariant instead
    // (FilledButtonTokens.DisabledLabelTextColor and its siblings).
    readonly property real hoverOpacity: 0.08
    readonly property real focusOpacity: 0.10
    readonly property real pressedOpacity: 0.10
    readonly property real draggedOpacity: 0.16
    readonly property real disabledContainerOpacity: 0.10
    // The tenth belongs to the expressive buttons (FilledButtonTokens). Chips,
    // the switch track and the tonal button still dim to 12%
    // (FilterChipTokens.FlatDisabled*Opacity, SwitchTokens.DisabledTrackOpacity,
    // FilledTonalButtonTokens.DisabledContainerOpacity).
    readonly property real disabledSurfaceOpacity: 0.12
    readonly property real disabledContentOpacity: 0.38
    readonly property bool followDesktop: app.theme === "system" && desktopTheme.available
    readonly property bool dark: followDesktop ? desktopTheme.dark : app.theme === "dark" || (app.theme === "system" && Application.styleHints.colorScheme === Qt.Dark)
    readonly property color background: followDesktop ? desktopTheme.colors.background : role("background", dark ? "#181211" : "#fff8f6")
    // Material's surface is the plainest one there is, the tone a page starts
    // from. The ladder of containers is measured against it, and the app had
    // been using the first step of that ladder under this name while the role
    // itself went unread.
    readonly property color surface: followDesktop ? desktopTheme.colors.background : role("surface", dark ? "#181211" : "#fff8f6")
    readonly property color surfaceLow: followDesktop ? desktopTheme.colors.surface : role("surfaceContainerLow", dark ? "#201a18" : "#fff1ec")
    readonly property color container: followDesktop ? desktopTheme.colors.container : role("surfaceContainer", dark ? "#2b2320" : "#f6e5de")
    readonly property color high: followDesktop ? desktopTheme.colors.high : role("surfaceContainerHigh", dark ? "#382c28" : "#efddd5")
    readonly property color highest: role("surfaceContainerHighest", dark ? "#433733" : "#e9d8d0")
    readonly property color text: followDesktop ? desktopTheme.colors.text : role("onSurface", dark ? "#f5ded5" : "#281912")
    readonly property color muted: followDesktop ? desktopTheme.colors.muted : role("onSurfaceVariant", dark ? "#d5bfb5" : "#705c53")
    // Material has two outline roles and they do different jobs. The outline is
    // a boundary that has to hold on its own: a text field, a switch track, a
    // connected button group. The variant is decorative separation: a divider,
    // or the edge of a container that is already legible without one. The
    // scheme computes both, at neutral variant tone 60/50 and 30/80.
    readonly property color outline: followDesktop ? desktopTheme.colors.outline
                                   : role("outline", blend(outlineVariant, muted, 0.5))
    readonly property color outlineVariant: followDesktop ? desktopTheme.colors.outline
                                          : role("outlineVariant", dark ? "#57443b" : "#dcc5b9")
    readonly property color primary: useSource ? role("primary",sourceColor) : followDesktop ? desktopTheme.colors.primary : role("primary",dark ? "#ffb596" : "#964829")
    readonly property color primaryText: useSource ? role("onPrimary",luminance(primary)>0.179?"#000000":"#ffffff") : followDesktop ? desktopTheme.colors.primaryText : role("onPrimary",dark ? "#572008" : "#ffffff")
    readonly property color primaryContainer: useSource ? role("primaryContainer",blend(container,primary,0.16)) : followDesktop ? desktopTheme.colors.primaryContainer : role("primaryContainer",dark ? "#75351b" : "#ffdbcb")
    readonly property color containerText: useSource ? role("onPrimaryContainer",readable(primary,[primaryContainer])) : followDesktop ? desktopTheme.colors.containerText : role("onPrimaryContainer",dark ? "#ffdbcb" : "#743419")
    readonly property color secondaryContainer: role("secondaryContainer", dark ? "#54432a" : "#f5e0bb")
    // Material's third accent. A vibrant surface takes it where the usual
    // container would disappear into what it is sitting over.
    readonly property color tertiary: role("tertiary", dark ? "#b8ceb0" : "#3b5236")
    readonly property color tertiaryText: role("onTertiary", dark ? "#243420" : "#ffffff")
    readonly property color tertiaryContainer: role("tertiaryContainer", dark ? "#3b5236" : "#d4eacb")
    readonly property color tertiaryContainerText: role("onTertiaryContainer", dark ? "#d4eacb" : "#233a1f")
    readonly property color secondaryContainerText: role("onSecondaryContainer", dark ? "#f5e0bb" : "#221a04")
    readonly property color secondary: followDesktop ? desktopTheme.colors.secondary : role("secondary", dark ? "#d8c4a0" : "#6c5b3b")
    // The ink that goes on the secondary role itself, which is what a tonal
    // toggle takes once it is on.
    readonly property color secondaryText: role("onSecondary", dark ? "#3b2f15" : "#ffffff")
    // The inverse roles. A snackbar sits against the theme rather than in it,
    // so it takes the surface and the accent the other theme would have used.
    readonly property color inverseSurface: role("inverseSurface", dark ? "#f5ded5" : "#3c2c25")
    readonly property color inverseSurfaceText: role("inverseOnSurface", dark ? "#392e2a" : "#ffede6")
    readonly property color inversePrimary: role("inversePrimary", dark ? "#964829" : "#ffb596")
    // Error comes off the scheme's own error palette, so it answers the
    // contrast setting with everything else instead of sitting at one value.
    readonly property color error: role("error", dark ? "#ffb4ab" : "#ba1a1a")
    readonly property color errorText: role("onError", dark ? "#690005" : "#ffffff")
    // Material's scrim, and the opacity it dims with.
    readonly property color scrim: role("scrim","#000000")
    readonly property real scrimOpacity: 0.32
    function scrimColor(amount) { return Qt.rgba(scrim.r,scrim.g,scrim.b,amount===undefined?scrimOpacity:amount) }
    readonly property color errorContainer: role("errorContainer", dark ? "#93000a" : "#ffdad6")
    readonly property color errorContainerText: role("onErrorContainer", dark ? "#ffdad6" : "#410002")
    // Material's fixed accents keep one tone in both themes, so anything drawn
    // with them holds its identity when the rest of the window flips.
    readonly property color primaryFixed: role("primaryFixed","#ffdbcb")
    readonly property color primaryFixedText: role("onPrimaryFixed","#360f00")
    readonly property color primaryFixedVariantText: role("onPrimaryFixedVariant","#743419")
    // The names the rest of the application already uses, now resolved through
    // the spring tokens above rather than carrying their own numbers. Changing
    // the motion scheme therefore reaches every animation in the app at once.
    readonly property int fast: springFastEffectsMs
    readonly property int normal: springEffectsMs
    readonly property int slow: springSlowEffectsMs
    readonly property int enterDuration: springFastEffectsMs
    // Menu.kt:1829-1831 and NavigationDrawer.kt:351-355 use FastEffects to
    // close. Opacity must settle without spatial overshoot.
    readonly property int exitDuration: springFastEffectsMs
    readonly property var enterCurve: springFastEffects
    readonly property var exitCurve: springFastEffects
    readonly property var fastSpatialCurve: springFastSpatial
    readonly property var effectsCurve: springEffects
    readonly property var fastEffectsCurve: springFastEffects
    readonly property var curve: springSpatial
}
