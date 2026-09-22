import QtQuick

// Material 3 elevation.
//
// Material lifts a floating surface off the page with two shadows: a tight key
// light directly under it and a wider ambient one around it. Qt Quick has no
// shadow on a rectangle, and a blur shader would not survive the software
// renderer, so each shadow is drawn as a short stack of rounded rectangles
// whose alpha falls away from the surface. The offsets, blurs, spreads and
// opacities are Material's published values for the level; only the way the
// falloff is produced is ours.
//
// This belongs behind an opaque surface, never in front of one.
Item {
    id: shade
    objectName: "elevation"

    property int level: 0
    property real radius: 0
    // How many rings each shadow is built from. More is smoother and costs
    // more; five reads as a soft edge at the sizes Material uses.
    readonly property int steps: 5

    readonly property int step: Math.max(0, Math.min(5, level))
    visible: step > 0
    z: -1

    // One entry per ring: how far past the surface it reaches, how far down the
    // shadow is thrown, and how much of the shadow this ring carries.
    readonly property var rings: {
        if (shade.step <= 0) return []
        const layers = [[Theme.elevationKey[shade.step], Theme.elevationKeyOpacity],
                        [Theme.elevationAmbient[shade.step], Theme.elevationAmbientOpacity]]
        const out = []
        for (let l = 0; l < layers.length; ++l) {
            const drop = layers[l][0][0], blur = layers[l][0][1], spread = layers[l][0][2]
            for (let i = 1; i <= shade.steps; ++i)
                out.push({reach: spread + blur*i/shade.steps, drop: drop, alpha: layers[l][1]/shade.steps})
        }
        return out
    }

    // A surface at level zero casts nothing, and most of the surfaces in a
    // window sit at level zero. The repeater and the model behind it are not
    // built until there is a shadow for them to draw.
    Loader {
        anchors.fill: parent
        active: shade.step > 0
        sourceComponent: Repeater {
            model: shade.rings
            delegate: Rectangle {
                objectName: "elevationRing"
                required property var modelData
                x: -modelData.reach
                y: -modelData.reach + modelData.drop
                width: shade.width + modelData.reach*2
                height: shade.height + modelData.reach*2
                radius: shade.radius + modelData.reach
                color: Qt.rgba(0, 0, 0, modelData.alpha)
            }
        }
    }
}
