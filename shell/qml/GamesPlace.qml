import QtQuick
import TvShell

FocusScope {
    id: place

    required property SteamLibrary library
    property bool onScreen: true

    // Pick up games installed or removed while a game or Steam had the screen.
    onOnScreenChanged: if (onScreen) library.reload()

    Text {
        id: heading
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 48
        color: "white"
        font.pixelSize: 48
        text: qsTr("Games")
    }

    GridView {
        id: grid

        anchors.top: heading.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 32
        focus: true
        model: place.library
        cellWidth: 320
        cellHeight: 480
        highlightMoveDuration: 120

        delegate: GameTile {}

        Keys.onReturnPressed: place.library.launch(grid.currentItem.appId)
        Keys.onEnterPressed: place.library.launch(grid.currentItem.appId)
    }

    Text {
        anchors.centerIn: parent
        visible: grid.count === 0
        color: "white"
        font.pixelSize: 32
        text: qsTr("No installed games found")
    }
}
