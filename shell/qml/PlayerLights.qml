pragma ComponentBehavior: Bound

import QtQuick

// The four player lights of a Nintendo controller, drawn as the controller shows them:
// `player` lights lit from the left (1 to 4), or all four flashing while it waits to be
// told how it is held.
Row {
    id: lights

    property int player: 0
    property bool flashing: false
    // On a light background (the focused line) the unlit lights need another shade.
    property bool onLight: false

    // The flashing lights' common phase.
    property bool phase: true

    spacing: 8

    Accessible.role: Accessible.StaticText
    Accessible.name: flashing ? qsTr("Waiting") : qsTr("Player %1").arg(player)

    Timer {
        interval: 400
        repeat: true
        running: lights.flashing && lights.visible
        onTriggered: lights.phase = !lights.phase
    }

    Repeater {
        model: 4

        delegate: Rectangle {
            required property int index

            readonly property bool lit: lights.flashing ? lights.phase : index < lights.player

            width: 16
            height: 16
            radius: 3
            color: lit ? "#5fe35f" : lights.onLight ? "#c3cdd3" : "#39444d"
        }
    }
}
