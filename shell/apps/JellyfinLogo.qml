import QtQuick
import QtQuick.Shapes

// Jellyfin's mark: a triangle outline with a smaller triangle inside, purple to blue. Drawn,
// so it scales cleanly and needs no image file. Size it by width; it is square.
Item {
    id: logo

    implicitWidth: 160
    implicitHeight: width

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        // The outline: the outer triangle with a triangular hole.
        ShapePath {
            strokeColor: "transparent"
            fillRule: ShapePath.OddEvenFill
            fillGradient: LinearGradient {
                x1: logo.width * 0.15; y1: logo.height * 0.3
                x2: logo.width * 0.9; y2: logo.height * 0.9
                GradientStop { position: 0; color: "#aa5cc3" }
                GradientStop { position: 1; color: "#00a4dc" }
            }
            startX: logo.width * 0.5; startY: logo.height * 0.04
            PathLine { x: logo.width * 0.98; y: logo.height * 0.9 }
            PathLine { x: logo.width * 0.02; y: logo.height * 0.9 }
            PathLine { x: logo.width * 0.5; y: logo.height * 0.04 }
            PathMove { x: logo.width * 0.5; y: logo.height * 0.3 }
            PathLine { x: logo.width * 0.79; y: logo.height * 0.81 }
            PathLine { x: logo.width * 0.21; y: logo.height * 0.81 }
            PathLine { x: logo.width * 0.5; y: logo.height * 0.3 }
        }

        // The triangle inside.
        ShapePath {
            strokeColor: "transparent"
            fillGradient: LinearGradient {
                x1: logo.width * 0.15; y1: logo.height * 0.3
                x2: logo.width * 0.9; y2: logo.height * 0.9
                GradientStop { position: 0; color: "#aa5cc3" }
                GradientStop { position: 1; color: "#00a4dc" }
            }
            startX: logo.width * 0.5; startY: logo.height * 0.47
            PathLine { x: logo.width * 0.66; y: logo.height * 0.74 }
            PathLine { x: logo.width * 0.34; y: logo.height * 0.74 }
            PathLine { x: logo.width * 0.5; y: logo.height * 0.47 }
        }
    }
}
