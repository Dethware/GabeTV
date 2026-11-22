import QtQuick

// What's playing in the background, in the top bar: the source's icon, a state mark (three
// bars: playing, two: paused) and the title. Focus: white fill, like the other bar buttons.
// OK toggles pause.
Rectangle {
    id: button

    // Where it plays from: "youtube" (more to come).
    property string source
    property string title
    property string artist
    property bool paused

    readonly property color ink: activeFocus ? "#101418" : "#cfd8dc"

    width: row.implicitWidth + 64
    height: 72
    radius: 8
    color: activeFocus ? "white" : "#1d252c"

    Accessible.role: Accessible.Button
    Accessible.name: (paused ? qsTr("Paused: %1") : qsTr("Playing: %1")).arg(title)

    Row {
        id: row

        anchors.left: parent.left
        anchors.leftMargin: 32
        anchors.verticalCenter: parent.verticalCenter
        spacing: 20

        Loader {
            anchors.verticalCenter: parent.verticalCenter
            active: button.source === "youtube"
            visible: active
            sourceComponent: YouTubeLogo { width: 52 }
        }

        // Playing: three bars of different heights. Paused: two equal ones.
        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            Repeater {
                model: button.paused ? [28, 28] : [16, 28, 20]

                delegate: Rectangle {
                    required property int modelData

                    anchors.bottom: parent.bottom
                    width: button.paused ? 8 : 6
                    height: modelData
                    radius: 2
                    color: button.ink
                }
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            // A fixed limit: sizing it from the button, which is sized from this row, loops.
            width: Math.min(implicitWidth, 1100)
            elide: Text.ElideRight
            color: button.ink
            font.pixelSize: 30
            text: button.artist.length > 0 ? button.title + "  ·  " + button.artist : button.title
        }
    }
}
