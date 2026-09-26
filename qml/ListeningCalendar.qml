import QtQuick
import QtQuick.Layouts

// The listening graph: GitHub's contribution calendar with time listened in
// place of commits. A column per week, a row per weekday, a square per day,
// shaded by which quarter of the listened days it falls in
// (Backend::listeningCalendar). Hovering a day names it and its time;
// choosing one hands its date to the dialog, which shows that day's figures.
//
// Material has no heatmap component, so this follows its colour and shape
// systems: the accent (primary) at four strengths over the dialog's surface,
// in both themes, and an empty day in outlineVariant, the empty day of the
// bar row this replaced. At 53 weeks across a 560dp dialog a square is about
// 7dp, far under the 48dp target, so the grid is one control rather than 371:
// the pointer picks a square by position, and the keyboard walks the days
// with the arrows, one day up and down, one week across.
ColumnLayout {
    id: graph
    objectName: "listeningCalendar"
    spacing: Theme.spaceSmall

    // 0 is the 365 days ending today; otherwise a calendar year.
    property int year: 0
    property var calendar: ({})
    // The chosen day, as "yyyy-MM-dd", or "" for none. The dialog owns it;
    // the graph only says which day was picked.
    property string selected: ""
    signal chosen(string date)
    // How a length of time is spelled, the dialog's own.
    required property var spell
    function refresh() { calendar = app.listeningCalendar(year) }
    onYearChanged: refresh()

    readonly property var days: calendar.days || []
    readonly property int weeks: Math.ceil(days.length/7)
    readonly property var years: [0].concat(calendar.years || [])
    readonly property int yearIndex: Math.max(0, years.indexOf(year))
    // GitHub's own spacing between days, on Material's 2dp half-step.
    readonly property real gap: 2
    readonly property real labelWidth: dayLabels.width + Theme.spaceSmall
    // The squares share the width, down to a floor below which a day can no
    // longer be told apart from its neighbours; past it the grid scrolls.
    readonly property real cell: weeks > 0 ? Math.max(7, Math.floor((width - labelWidth - gap*(weeks-1))/weeks)) : 7
    readonly property real pitch: cell + gap
    // Four strengths of one hue, as GitHub's graph uses, spaced so that each
    // step reads in both themes. Measured on screen, empty to darkest: dark
    // 1.73, 1.52, 1.47, 1.39; light 1.41, 1.37, 1.42, 1.43. GitHub-like
    // 0.3/0.5/0.75 left light's first step at 1.21, a day of a little music
    // barely apart from a silent one.
    readonly property var strengths: [0.4, 0.6, 0.8, 1]
    function tone(level) {
        return level <= 0 ? Theme.outlineVariant
             : Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, strengths[level-1])
    }
    function dateOf(day) { return new Date(day.date+"T12:00:00") }
    function describe(day) {
        if (!day) return ""
        const when = Qt.formatDate(dateOf(day), "ddd d MMM yyyy")
        return day.seconds > 0
            ? when+" · "+spell(day.seconds)+" · "+day.plays+(day.plays === 1 ? " play" : " plays")
            : when+" · Nothing played"
    }

    // The day the keyboard is on, and the one the pointer is over.
    property int cursor: -1
    property int hovered: -1
    // The ring shows for keyboard focus, never for a click (MFocusRing), so
    // focus arriving by Tab turns it on and a press turns it off.
    property bool keyboardDriven: false
    property bool pointerFocus: false
    function place(index) {
        const last = days.length-1
        let next = Math.max(0, Math.min(last, index))
        while (next < last && days[next].padding) ++next
        cursor = next
        flick.reveal(cursor)
    }
    onCalendarChanged: {
        // Read from the calendar itself: `days` may not have caught up yet.
        const list = calendar.days || []
        const at = list.findIndex(d => d.date === selected)
        cursor = at >= 0 ? at : list.length-1
        Qt.callLater(() => flick.reveal(cursor))
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 4
        // The year and what it adds up to share the row the year buttons
        // need anyway, rather than a line of their own under the grid.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            SungText {
                objectName: "calendarTitle"
                text: graph.year > 0 ? String(graph.year) : "Past year"
                font.pixelSize: Theme.titleSmall
                typeRole: "titleSmall"
            }
            SungText {
                objectName: "calendarSummary"
                Layout.fillWidth: true
                // The chosen day is named here, since the outline on a 7dp
                // square is easy to lose; otherwise the year adds up.
                text: {
                    if (graph.selected) {
                        const day = graph.days.find(d => d.date === graph.selected)
                        return day ? graph.describe(day) : Qt.formatDate(new Date(graph.selected+"T12:00:00"), "ddd d MMM yyyy")
                    }
                    const active = graph.calendar.active || 0
                    if (active === 0) return graph.year > 0 ? "Nothing played in "+graph.year : "Nothing played in the past year"
                    const parts = [active+(active === 1 ? " day" : " days")+" listened"]
                    if (graph.year === 0 && graph.calendar.streak > 0) parts.push(graph.calendar.streak+"-day streak")
                    parts.push("longest "+graph.calendar.longest+(graph.calendar.longest === 1 ? " day" : " days"))
                    return parts.join(" · ")
                }
                color: Theme.muted
                font.pixelSize: Theme.labelMedium
                labelRole: true
                elide: Text.ElideRight
            }
        }
        MButton {
            objectName: "calendarEarlier"
            symbol: "chevron_left"
            tip: "Earlier year"
            enabled: graph.yearIndex < graph.years.length-1
            onClicked: graph.year = graph.years[graph.yearIndex+1]
        }
        MButton {
            objectName: "calendarLater"
            symbol: "chevron"
            tip: "Later year"
            enabled: graph.yearIndex > 0
            onClicked: graph.year = graph.years[graph.yearIndex-1]
        }
    }

    // The weekday names stay put while a narrow window scrolls the weeks.
    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: flick.contentHeight

        // Every other weekday is named, as GitHub names three of seven: a
        // label is taller than a row, so each is centred on its own.
        Item {
            id: dayLabels
            y: months.height
            width: dayMetrics.width
            height: 7*graph.pitch
            Repeater {
                model: [0, 2, 4, 6]
                SungText {
                    required property int modelData
                    y: modelData*graph.pitch + graph.cell/2 - height/2
                    text: graph.days[modelData] ? Qt.formatDate(graph.dateOf(graph.days[modelData]), "ddd") : ""
                    color: Theme.muted
                    font.pixelSize: Theme.labelSmall
                    labelRole: true
                }
            }
            TextMetrics { id: dayMetrics; font.pixelSize: Theme.labelSmall; text: "Wed" }
        }

        Flickable {
            id: flick
            objectName: "calendarScroll"
            x: graph.labelWidth
            width: parent.width - graph.labelWidth
            height: parent.height
            readonly property real needed: graph.weeks*graph.pitch - graph.gap
            // Scrolling, the grid keeps room past its last week for the focus
            // ring, which stands outside the square it rings.
            contentWidth: needed > width ? needed + Theme.focusRingOutset : width
            contentHeight: months.height + 7*graph.pitch - graph.gap + Theme.focusRingOutset
            clip: interactive
            interactive: contentWidth > width
            boundsBehavior: Flickable.StopAtBounds
            function reveal(index) {
                if (index < 0 || !interactive) return
                const x = Math.floor(index/7)*graph.pitch
                const ring = Theme.focusRingOutset
                if (x - ring < contentX) contentX = Math.max(0, x - ring)
                else if (x + graph.cell + ring > contentX + width) contentX = Math.min(contentWidth - width, x + graph.cell + ring - width)
            }
            // A window that narrows keeps the day in hand, today to begin
            // with, in view rather than scrolling back to the oldest week.
            onWidthChanged: Qt.callLater(() => reveal(graph.cursor))
            onContentWidthChanged: Qt.callLater(() => reveal(graph.cursor))

            // A month's name sits over the first week that starts in it,
            // unless it would crowd the one before.
            Item {
                id: months
                height: monthMetrics.height + 4
                width: flick.contentWidth
                Repeater {
                    model: {
                        const marks = []
                        let lastWeek = -3
                        for (let w = 0; w < graph.weeks; ++w) {
                            const day = graph.days[w*7]
                            if (!day) continue
                            const date = graph.dateOf(day)
                            if (date.getDate() <= 7 && w - lastWeek >= 3) {
                                marks.push({week: w, name: Qt.formatDate(date, "MMM")})
                                lastWeek = w
                            }
                        }
                        return marks
                    }
                    SungText {
                        required property var modelData
                        x: modelData.week*graph.pitch
                        text: modelData.name
                        color: Theme.muted
                        font.pixelSize: Theme.labelSmall
                        labelRole: true
                    }
                }
                TextMetrics { id: monthMetrics; font.pixelSize: Theme.labelSmall; text: "Mar" }
            }
            Item {
                id: grid
                objectName: "calendarGrid"
                y: months.height
                width: graph.weeks*graph.pitch - graph.gap
                height: 7*graph.pitch - graph.gap
                activeFocusOnTab: true
                onActiveFocusChanged: if (activeFocus) { graph.keyboardDriven = !graph.pointerFocus; graph.pointerFocus = false }
                Accessible.role: Accessible.Chart
                Accessible.name: "Listening graph"
                Accessible.description: graph.describe(graph.days[graph.cursor])
                Keys.onPressed: event => {
                    const steps = {[Qt.Key_Up]: -1, [Qt.Key_Down]: 1, [Qt.Key_Left]: -7, [Qt.Key_Right]: 7}
                    if (event.key in steps) {
                        graph.keyboardDriven = true
                        graph.place(graph.cursor + steps[event.key])
                        event.accepted = true
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                        graph.keyboardDriven = true
                        graph.choose(graph.cursor)
                        event.accepted = true
                    }
                }
                Repeater {
                    id: squares
                    model: graph.days
                    Rectangle {
                        required property var modelData
                        required property int index
                        objectName: "calendarDay_"+modelData.date
                        x: Math.floor(index/7)*graph.pitch
                        y: (index%7)*graph.pitch
                        width: graph.cell; height: graph.cell
                        visible: !modelData.padding
                        // The smallest corner on Material's scale, held to a
                        // quarter of the square so a 7dp day is not drawn round.
                        radius: Math.min(Theme.shapeExtraSmall, graph.cell/4)
                        color: graph.tone(modelData.level)
                        // The chosen day wears an outline in the text colour, the
                        // strongest ink on the surface, so it reads at any level;
                        // the one under the pointer a thinner one, its hover state.
                        border.width: modelData.date === graph.selected ? 2 : index === graph.hovered ? 1 : 0
                        border.color: Theme.text
                    }
                }
                // The keyboard's place, ringed only while the keyboard is driving.
                Item {
                    objectName: "calendarCursor"
                    x: Math.floor(graph.cursor/7)*graph.pitch
                    y: (graph.cursor%7)*graph.pitch
                    width: graph.cell; height: graph.cell
                    visible: graph.cursor >= 0
                    MFocusRing {
                        targetRadius: Math.min(Theme.shapeExtraSmall, graph.cell/4)
                        visible: grid.activeFocus && graph.keyboardDriven
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -graph.gap/2
                    hoverEnabled: true
                    // The area starts half a gap before the first square, so each
                    // square owns the half gaps on either side of it.
                    function indexAt(point) {
                        const column = Math.floor(point.x/graph.pitch)
                        const row = Math.floor(point.y/graph.pitch)
                        const index = column*7 + row
                        return row >= 0 && row < 7 && index >= 0 && index < graph.days.length && !graph.days[index].padding ? index : -1
                    }
                    onPositionChanged: mouse => graph.hovered = indexAt(mouse)
                    onExited: graph.hovered = -1
                    onPressed: mouse => { graph.keyboardDriven = false; graph.pointerFocus = true; grid.forceActiveFocus(Qt.MouseFocusReason); graph.pointerFocus = false }
                    onClicked: mouse => { const at = indexAt(mouse); if (at >= 0) { graph.cursor = at; graph.choose(at) } }
                }
                MTooltip {
                    objectName: "calendarTip"
                    parent: graph.hovered >= 0 ? squares.itemAt(graph.hovered) : grid
                    visible: graph.hovered >= 0
                    delay: 300
                    text: graph.describe(graph.days[graph.hovered])
                }
            }
        }
    }

    // Choosing the chosen day again lets it go.
    function choose(index) {
        const day = days[index]
        if (!day || day.padding) return
        chosen(selected === day.date ? "" : day.date)
    }
}
