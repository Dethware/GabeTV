import QtQuick
import QtQuick.Shapes

// A picture with an optional caption, highlighted when current.
Rectangle {
    id: tile

    property url source
    property string title
    property string subtitle
    property bool current
    // Show the whole picture (photo grids) instead of filling the tile (covers).
    property bool whole
    // It is a video: a play badge in the corner.
    property bool video

    radius: 8
    color: "#1d252c"
    border.color: "white"
    border.width: current ? 6 : 0

    Accessible.role: Accessible.ListItem
    Accessible.name: title.length > 0 ? title : video ? qsTr("Video") : qsTr("Photo")

    Image {
        id: picture

        anchors.fill: parent
        anchors.margins: tile.border.width
        source: tile.source
        sourceSize.width: Math.round(480 * Screen.devicePixelRatio)
        sourceSize.height: Math.round(480 * Screen.devicePixelRatio)
        fillMode: tile.whole ? Image.PreserveAspectFit : Image.PreserveAspectCrop
        asynchronous: true
        autoTransform: true
    }

    // The play badge: a dark disc with a white triangle, bottom left.
    Rectangle {
        id: badge

        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 16
        width: 64
        height: 64
        radius: 32
        visible: tile.video
        color: "#c0000000"
        border.color: "white"
        border.width: 3

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                fillColor: "white"
                strokeColor: "transparent"
                startX: badge.width * 0.40
                startY: badge.height * 0.28
                PathLine { x: badge.width * 0.72; y: badge.height * 0.50 }
                PathLine { x: badge.width * 0.40; y: badge.height * 0.72 }
                PathLine { x: badge.width * 0.40; y: badge.height * 0.28 }
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: tile.border.width
        height: caption.implicitHeight + 24
        visible: tile.title.length > 0
        color: "#b0000000"

        Text {
            id: caption

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: 16
            color: "white"
            font.pixelSize: 26
            elide: Text.ElideRight
            text: tile.subtitle.length > 0 ? tile.title + "  ·  " + tile.subtitle : tile.title
        }
    }
}
