import QtQuick
import QtQuick.Shapes

// YouTube's play button mark: a red rounded rectangle with a white triangle. Drawn, so it
// scales cleanly and needs no image file. Size it by width; the height follows (≈ 1.43 : 1).
Item {
    id: logo

    implicitWidth: 160
    implicitHeight: Math.round(width / 1.43)

    Rectangle {
        anchors.fill: parent
        radius: height * 0.24
        color: "#ff0000"
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            fillColor: "white"
            strokeColor: "transparent"
            startX: logo.width * 0.40
            startY: logo.height * 0.29
            PathLine { x: logo.width * 0.65; y: logo.height * 0.50 }
            PathLine { x: logo.width * 0.40; y: logo.height * 0.71 }
            PathLine { x: logo.width * 0.40; y: logo.height * 0.29 }
        }
    }
}
