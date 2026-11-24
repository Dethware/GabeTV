pragma ComponentBehavior: Bound

import QtQuick
import QtQuick3D

// A controller as a simple shape, for the motion test. At rest it lies flat with its face
// up and its top away from the viewer; `orientation` turns it. The white bar marks the
// top, so that it shows which way round it is.
Node {
    id: controller

    // "left", "right" (Joy-Cons) or "both" (a Pro Controller).
    property string side: "right"
    property quaternion orientation

    readonly property bool pro: side === "both"
    readonly property color bodyColor: pro ? "#3d4650" : side === "left" ? "#2f9be8" : "#e8503a"
    // Half of the body's size across, and its thickness.
    readonly property real halfWidth: pro ? 75 : 18
    readonly property real thickness: pro ? 30 : 14

    rotation: orientation

    component Part: Model {
        id: part

        property color tint: "#20262c"

        materials: PrincipledMaterial {
            baseColor: part.tint
            roughness: 0.6
        }
    }

    // The body. Qt's cube is 100 across.
    Part {
        source: "#Cube"
        tint: controller.bodyColor
        scale: Qt.vector3d(controller.halfWidth / 50, controller.thickness / 100, 1.0)
    }

    // The top edge.
    Part {
        source: "#Cube"
        tint: "white"
        position: Qt.vector3d(0, 0, -50)
        scale: Qt.vector3d(controller.halfWidth / 50, controller.thickness / 100 + 0.02, 0.05)
    }

    // The stick: near the top on a left Joy-Con, lower on a right one.
    Part {
        source: "#Cylinder"
        visible: !controller.pro
        position: Qt.vector3d(0, controller.thickness / 2 + 3, controller.side === "left" ? -24 : 8)
        scale: Qt.vector3d(0.16, 0.06, 0.16)
    }

    // The four buttons, at the other end of the face.
    Repeater3D {
        model: controller.pro ? [] : [Qt.vector2d(0, -7), Qt.vector2d(7, 0), Qt.vector2d(0, 7), Qt.vector2d(-7, 0)]

        delegate: Part {
            required property var modelData

            source: "#Cylinder"
            position: Qt.vector3d(modelData.x, controller.thickness / 2 + 1,
                                  (controller.side === "left" ? 12 : -22) + modelData.y)
            scale: Qt.vector3d(0.06, 0.03, 0.06)
        }
    }

    // A Pro Controller: two sticks and four buttons.
    Part {
        source: "#Cylinder"
        visible: controller.pro
        position: Qt.vector3d(-42, controller.thickness / 2 + 4, -18)
        scale: Qt.vector3d(0.2, 0.08, 0.2)
    }

    Part {
        source: "#Cylinder"
        visible: controller.pro
        position: Qt.vector3d(18, controller.thickness / 2 + 4, 8)
        scale: Qt.vector3d(0.2, 0.08, 0.2)
    }

    Repeater3D {
        model: controller.pro ? [Qt.vector2d(0, -9), Qt.vector2d(9, 0), Qt.vector2d(0, 9), Qt.vector2d(-9, 0)] : []

        delegate: Part {
            required property var modelData

            source: "#Cylinder"
            position: Qt.vector3d(44 + modelData.x, controller.thickness / 2 + 1, -18 + modelData.y)
            scale: Qt.vector3d(0.08, 0.03, 0.08)
        }
    }
}
