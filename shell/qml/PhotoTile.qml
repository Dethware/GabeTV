import QtQuick

// A picture with an optional caption, highlighted when current.
Rectangle {
    id: tile

    property url source
    property string title
    property string subtitle
    property bool current
    // Show the whole picture (photo grids) instead of filling the tile (covers).
    property bool whole

    radius: 8
    color: "#1d252c"
    border.color: "white"
    border.width: current ? 6 : 0

    Accessible.role: Accessible.ListItem
    Accessible.name: title

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
