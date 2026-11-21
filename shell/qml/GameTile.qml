import QtQuick

Item {
    id: tile

    required property string appId
    required property string name
    required property url cover

    width: GridView.view.cellWidth
    height: GridView.view.cellHeight

    Rectangle {
        id: frame

        anchors.fill: parent
        anchors.margins: 16
        radius: 8
        color: "#1d252c"
        border.color: "white"
        border.width: tile.GridView.isCurrentItem ? 6 : 0

        Image {
            id: coverImage

            anchors.fill: parent
            anchors.margins: frame.border.width
            source: tile.cover
            sourceSize.width: 600
            sourceSize.height: 900
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
        }

        Text {
            anchors.fill: parent
            anchors.margins: 16
            visible: coverImage.status !== Image.Ready
            color: "white"
            font.pixelSize: 28
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: tile.name
        }
    }
}
