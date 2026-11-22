import QtQuick

// An app in a place's grid: its logo (a component, drawn) above its name, in the same frame
// as the game covers.
Item {
    id: tile

    required property string appId
    required property string name
    property Component logo

    width: GridView.view.cellWidth
    height: GridView.view.cellHeight

    Rectangle {
        id: frame

        anchors.fill: parent
        anchors.margins: 16
        radius: 8
        color: "#1d252c"
        border.color: "white"
        border.width: tile.GridView.isCurrentItem && tile.GridView.view.activeFocus ? 6 : 0

        Column {
            anchors.centerIn: parent
            width: parent.width * 0.6
            spacing: 32

            Loader {
                width: parent.width
                sourceComponent: tile.logo
                onLoaded: item.width = width
            }

            Text {
                width: parent.width
                color: "white"
                font.pixelSize: 36
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: tile.name
            }
        }
    }
}
