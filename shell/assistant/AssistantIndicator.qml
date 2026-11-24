import QtQuick
import TvShell.Assistant

// Shown while the remote's Assistant button is held: a ring that grows with the voice level.
Item {
    id: indicator

    required property RemoteAssistant assistant

    visible: assistant.listening || ring.scale > 1.01
    width: 240
    height: 240

    Accessible.role: Accessible.Indicator
    Accessible.name: qsTr("Listening")

    Rectangle {
        id: ring

        anchors.centerIn: parent
        width: 160
        height: 160
        radius: width / 2
        color: "transparent"
        border.width: 8
        border.color: indicator.assistant.listening ? "white" : "#808080"
        scale: 1 + indicator.assistant.level * 0.5

        Behavior on scale {
            NumberAnimation {
                duration: 80
            }
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: 48
        height: 48
        radius: width / 2
        color: "white"
        visible: indicator.assistant.listening
    }
}
