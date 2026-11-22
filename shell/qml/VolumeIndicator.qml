import QtQuick
import QtQuick.Shapes
import TvShell

// A small volume display that appears for a moment when the volume or mute changes.
// Sits above everything (YouTube, the screensaver, dialogs).
Rectangle {
    id: indicator

    required property SystemVolume volume

    readonly property real level: Math.max(0, Math.min(1, volume.volume))

    width: 440
    height: 88
    radius: height / 2
    color: "#e6101418"
    border.color: "#40ffffff"
    border.width: 2
    opacity: 0
    visible: opacity > 0

    Accessible.role: Accessible.Indicator
    Accessible.name: volume.muted ? qsTr("Muted") : qsTr("Volume %1 %").arg(Math.round(volume.volume * 100))

    Behavior on opacity { NumberAnimation { duration: 150 } }

    Connections {
        target: indicator.volume
        function onAdjusted() {
            indicator.opacity = 1
            hide.restart()
        }
    }

    Timer {
        id: hide
        interval: 2000
        onTriggered: indicator.opacity = 0
    }

    // A speaker: body and cone.
    Shape {
        id: speaker

        anchors.left: parent.left
        anchors.leftMargin: 32
        anchors.verticalCenter: parent.verticalCenter
        width: 36
        height: 36
        preferredRendererType: Shape.CurveRenderer
        opacity: indicator.volume.muted ? 0.4 : 1

        ShapePath {
            fillColor: "white"
            strokeColor: "transparent"
            startX: 0; startY: 12
            PathLine { x: 10; y: 12 }
            PathLine { x: 22; y: 2 }
            PathLine { x: 22; y: 34 }
            PathLine { x: 10; y: 24 }
            PathLine { x: 0; y: 24 }
            PathLine { x: 0; y: 12 }
        }
    }

    Rectangle {
        id: track

        anchors.left: speaker.right
        anchors.leftMargin: 20
        anchors.right: label.left
        anchors.rightMargin: 20
        anchors.verticalCenter: parent.verticalCenter
        height: 10
        radius: 5
        color: "#40ffffff"

        Rectangle {
            width: parent.width * indicator.level
            height: parent.height
            radius: parent.radius
            color: indicator.volume.muted ? "#80ffffff" : "white"
        }
    }

    Text {
        id: label

        anchors.right: parent.right
        anchors.rightMargin: 32
        anchors.verticalCenter: parent.verticalCenter
        width: 96
        horizontalAlignment: Text.AlignRight
        color: "white"
        font.pixelSize: 30
        font.weight: Font.DemiBold
        text: indicator.volume.muted ? qsTr("Muted") : Math.round(indicator.volume.volume * 100) + " %"
    }
}
