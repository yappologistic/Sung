import QtQuick

// The current lyric line set as a poster in Google Sans Flex.
//
// Google Sans Flex has six axes, and the font's own introduction sets a line
// the way a poster does: every word takes its own weight, width, roundness and
// slant, and the words stretch or squeeze until each line fills the measure
// from edge to edge (design.google, "Google Sans Flex"). This does that to the
// line being sung, and nothing else: the line is broken exactly where the
// plain lyric wraps, each word of a row is set on the width axis so the row
// meets the measure, and the gaps take up the last few pixels.
//
// Material ties the font to its shapes ("M3 shapes and Google Sans Flex share
// roundness visual attributes", Shape, Use shapes and text in harmony), so two
// of the four word styles are fully rounded, matching the rounded Material
// shapes elsewhere in the player.
//
// A row whose words reach the widest the axis goes and still fall short of
// the measure is set larger instead, up to half as large again, so a short
// line fills the column the way the poster fills its frame. The row grows
// taller with it, and the list gives it the room.
//
// At progress 0 the words are the plain lyric exactly: weight 500, the regular
// width, no roundness, no slant, the plain size, the same breaks and the same
// colour. The delegate swaps between the two there, so the change can never be
// seen, and the spring carries the words into their poster shapes as the line
// arrives and back out as it leaves.
Item {
    id: poster
    objectName: "posterLine"
    Accessible.ignored: true

    property string text: ""
    property real pixelSize: 25
    property real rowHeight: 31
    // The plain text block's height, which the poster keeps at rest; a row
    // set larger adds only what it grows by.
    property real restHeight: 31
    property real letterSpacing: 0
    // The plain line's colour, which every word starts from.
    property color restColor: Theme.primary
    property bool shown: false
    property real progress: 0
    // The words are the plain line's again and the poster can go.
    signal restored()
    onProgressChanged: if (!shown && progress <= 0.001) restored()
    onShownChanged: if (!shown && progress <= 0.001) restored()
    // DefaultSpatial moves the letterforms, the spring Sung's lyric lines
    // already scale on (LyricsView.qml), so the poster rings once and settles.
    Behavior on progress { enabled: app.motion; NumberAnimation { objectName: "posterLineMotion"; duration: Theme.springSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial } }
    Component.onCompleted: { layoutRows(); progress = Qt.binding(()=>shown ? 1 : 0) }

    // Four word styles: heavy and wide, light and narrow, italic, and narrow
    // but round. `width` is where each starts on the width axis before the row
    // is fitted to the measure.
    readonly property var styles: [
        {weight: 850, width: 120, round: 100, slant: 0,   accent: true},
        {weight: 300, width: 70,  round: 0,   slant: 0,   accent: false},
        {weight: 650, width: 95,  round: 0,   slant: -10, accent: true},
        {weight: 450, width: 55,  round: 100, slant: 0,   accent: false}]
    readonly property int restWeight: Font.Medium
    readonly property real restWidth: Theme.regularWidth
    // How much larger than the plain line a short row may be set.
    readonly property real largest: 1.5

    // Every distinct set of axis values is a font of its own to Qt: its own
    // FreeType face, about 0.65 MiB that stays allocated, and a mapping of the
    // font file that is let go some thirty seconds after the last use. Axes
    // that changed every frame cost a face a frame, a quarter of a gigabyte
    // for one line, and a width fitted to each row still left 55 faces after
    // a few lines. So the width axis has six levels, each style has one
    // weight, roundness and slant, and the words move in two steps, halfway
    // and home: however many lines are sung, the poster draws from at most
    // 48 faces. Between steps each word is scaled to the width the spring has
    // reached, so the motion the eye follows stays continuous.
    // The width axis runs from 25 to 151 (the font's own fvar table).
    readonly property var levels: [25, 50, 75, 100, 125, 151]
    function snapped(value, step, low, high) { return Math.max(low, Math.min(high, Math.round(value/step)*step)) }
    function nearestLevel(width) {
        let best = 0
        for (let i = 1; i < levels.length; ++i) if (Math.abs(levels[i]-width) < Math.abs(levels[best]-width)) best = i
        return best
    }
    function stepped(t) { return Math.max(0, Math.min(1, Math.round(t*2)/2)) }
    function axesAt(style, level, step) {
        if (step <= 0) return {weight: restWeight, width: restWidth, round: 0, slant: 0}
        if (step >= 1) return {weight: style.weight, width: levels[level], round: style.round, slant: style.slant}
        return {weight: snapped((restWeight + style.weight)/2, 50, 1, 1000),
                width: levels[nearestLevel((restWidth + levels[level])/2)],
                round: snapped(style.round/2, 25, 0, 100),
                slant: snapped(style.slant/2, 2.5, -10, 0)}
    }

    property var rows: []
    property real spaceWidth: 0
    implicitHeight: rows.reduce((sum, row) => sum + (row.height - rowHeight)*progress, restHeight)
    // Measures a word in the given axes at this poster's size. LyricsView
    // owns the Text that does it, so it has always finished loading, and the
    // widths it keeps are shared by every line.
    property var measureWord: null
    function measure(word, weight, width, round, slant) {
        return measureWord(word, pixelSize, letterSpacing, weight, width, round, slant)
    }
    function layoutRows() {
        const words = text.split(/\s+/).filter(w => w.length)
        const measureWidth = width
        if (!words.length || measureWidth <= 0) { rows = []; return }
        const space = measure(" ", restWeight, restWidth, 0, 0)
        spaceWidth = space
        // Greedy breaks at the plain style, the way Text.Wrap breaks the
        // plain lyric, so both are cut in the same places.
        const plain = words.map(w => measure(w, restWeight, restWidth, 0, 0))
        const broken = []
        let row = [], used = 0
        for (let i = 0; i < words.length; ++i) {
            const extra = row.length ? space + plain[i] : plain[i]
            if (row.length && used + extra > measureWidth + 0.01) { broken.push(row); row = []; used = 0 }
            used += row.length ? space + plain[i] : plain[i]
            row.push(i)
        }
        if (row.length) broken.push(row)
        // A line keeps the same styles wherever it comes back, so a chorus
        // reuses the faces its first time used.
        let seed = 0
        for (let i = 0; i < text.length; ++i) seed = (seed*31 + text.charCodeAt(i)) % 997
        const laid = []
        for (const indices of broken) {
            const entries = indices.map(i => ({text: words[i], plain: plain[i], style: styles[(i + seed) % styles.length]}))
            const at = (e, level, size) => measure(e.text, e.style.weight, levels[level], e.style.round, e.style.slant)*size/pixelSize
            const total = size => entries.reduce((sum, e) => sum + at(e, e.level, size), space*(entries.length-1)*size/pixelSize)
            // Each word starts at the level nearest its style's own width,
            // narrows while the row runs past the measure and widens, the
            // biggest step that still fits first, while there is room.
            const fit = size => {
                for (const e of entries) e.level = nearestLevel(e.style.width)
                while (total(size) > measureWidth) {
                    const e = entries.filter(e => e.level > 0).sort((x, y) => at(y, y.level, size) - at(x, x.level, size))[0]
                    if (!e) break
                    --e.level
                }
                for (;;) {
                    const room = measureWidth - total(size)
                    let best = null, gain = 0
                    for (const e of entries) {
                        if (e.level >= levels.length-1) continue
                        const more = at(e, e.level+1, size) - at(e, e.level, size)
                        if (more <= room && more > gain) { best = e; gain = more }
                    }
                    if (!best) break
                    ++best.level
                }
                return total(size)
            }
            let size = pixelSize
            let reached = fit(size)
            // Still short with every word at its widest: set the row larger,
            // in whole pixels as Qt sizes fonts, up to the largest a poster
            // row may be.
            if (entries.every(e => e.level === levels.length-1) && reached < measureWidth - 0.5) {
                size = Math.max(pixelSize, Math.min(Math.floor(pixelSize*largest), Math.floor(pixelSize*measureWidth/reached)))
                reached = fit(size)
            }
            for (const e of entries) e.final = at(e, e.level, size)
            // The gaps take up what the levels leave, as long as that keeps
            // them within half again of a space each; a row that would need
            // more is left short of the margin rather than gapped open.
            const room = measureWidth - reached
            const gap = space*size/pixelSize
            const fills = entries.length > 1 ? room <= gap*0.5*(entries.length-1) : room <= measureWidth*0.015
            laid.push({words: entries, size: size, height: Math.round(size*rowHeight/pixelSize), gap: gap, fills: fills})
        }
        rows = laid
    }
    onWidthChanged: if (shown || progress > 0) layoutRows()
    onTextChanged: layoutRows()
    onPixelSizeChanged: layoutRows()

    Column {
        Repeater {
            model: poster.rows
            Row {
                id: posterRow
                objectName: "posterRow"
                required property var modelData
                readonly property real t: poster.progress
                readonly property bool fills: modelData.fills
                // The words as shown, which the gaps take up the last of the
                // measure from, so a row that fills ends exactly at the margin
                // whatever the grid rounded its widths to.
                readonly property real words: {
                    let total = 0
                    for (let i = 0; i < children.length; ++i)
                        if (children[i].objectName === "posterSlot") total += children[i].width
                    return total
                }
                readonly property int gaps: modelData.words.length - 1
                readonly property real justified: gaps > 0 && fills ? (poster.width - words)/gaps : modelData.gap
                height: poster.rowHeight + (modelData.height - poster.rowHeight)*t
                spacing: poster.spaceWidth + (justified - poster.spaceWidth)*Math.max(0, Math.min(1, t))
                Repeater {
                    model: posterRow.modelData.words
                    Item {
                        id: slot
                        objectName: "posterSlot"
                        required property var modelData
                        readonly property real t: poster.progress
                        readonly property real step: poster.stepped(t)
                        readonly property var axes: poster.axesAt(modelData.style, modelData.level, step)
                        readonly property real sizeAt: poster.pixelSize + (posterRow.modelData.size - poster.pixelSize)*t
                        readonly property real sizeDrawn: Math.round(poster.pixelSize + (posterRow.modelData.size - poster.pixelSize)*step)
                        // Where the spring has the word, against where the
                        // drawn step has it; equal at rest and at every step.
                        readonly property real widthAt: modelData.plain + (modelData.final - modelData.plain)*t
                        readonly property real widthDrawn: modelData.plain + (modelData.final - modelData.plain)*step
                        width: t === step ? word.implicitWidth : word.implicitWidth*widthAt/Math.max(1, widthDrawn)
                        height: parent.height
                        Text {
                            id: word
                            objectName: "posterWord"
                            // One row of the plain lyric is one fixed-height
                            // line, so each word sits in the row as it does there.
                            height: parent.height
                            lineHeight: parent.height; lineHeightMode: Text.FixedHeight
                            verticalAlignment: Text.AlignVCenter
                            transform: Scale { origin.y: slot.height/2; xScale: slot.width/Math.max(1, word.implicitWidth); yScale: slot.sizeAt/Math.max(1, slot.sizeDrawn) }
                            text: slot.modelData.text
                            textFormat: Text.PlainText
                            font.family: Theme.fontFamily
                            font.pixelSize: slot.sizeDrawn
                            font.letterSpacing: poster.letterSpacing
                            font.weight: slot.axes.weight
                            font.variableAxes: ({"wdth": slot.axes.width, "ROND": slot.axes.round, "slnt": slot.axes.slant})
                            color: Qt.tint(poster.restColor, Qt.rgba(ink.r, ink.g, ink.b, Math.max(0, Math.min(1, slot.t))))
                            readonly property color ink: slot.modelData.style.accent ? Theme.primary : Theme.text
                        }
                    }
                }
            }
        }
    }
}
