import QtQuick

// Material 3 badge.
//
// Two sizes. The small badge is a 6dp dot that says only that something is
// there; the large badge is 16dp tall with a label-small count inside it and
// 4dp of padding either side, growing past 16dp wide once the number needs the
// room. Both are drawn in the error role, and Material hangs them off the top
// end corner of whatever they annotate.
Item {
    id: badge

    // Below nought the badge is a plain dot. At nought or above it counts, and
    // a count of nought means there is nothing to say.
    property int count: -1
    property int limit: 999
    // What the badge is counting, for anyone listening rather than looking.
    property string subject: ""
    // Whether there is anything to announce at all.
    property bool present: true

    // Material hangs a badge off the top end corner of what it annotates: a
    // dot 6dp in from that corner, a labelled one 12dp in and lifted so it
    // overhangs the top by 2dp.
    readonly property real inset: labelled ? 12 : 6
    readonly property real lift: labelled ? 14 : 6

    readonly property bool labelled: count >= 0
    readonly property string display: count > limit ? limit + "+" : String(count)

    visible: present && (!labelled || count > 0)
    implicitWidth: labelled ? Math.max(16, tally.implicitWidth+8) : 6
    implicitHeight: labelled ? 16 : 6
    Accessible.role: Accessible.StaticText
    Accessible.name: labelled ? display + " " + subject : subject
    Accessible.ignored: !visible || subject.length === 0

    Rectangle {
        anchors.fill: parent
        radius: Theme.shapeFull(Math.min(width, height))
        color: Theme.error
    }
    SungText {
        id: tally
        anchors.centerIn: parent
        visible: badge.labelled
        text: badge.display
        // BadgeTokens.LargeLabelTextFont: label small, whose tracking and
        // weight come with the role rather than the size alone.
        font.pixelSize: Theme.labelSmall
        labelRole: true
        color: Theme.errorText
    }
}
