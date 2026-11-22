import QtQuick
import TvShell

FocusScope {
    id: place

    required property SteamLibrary library
    property bool onScreen: true

    // Pick up games installed or removed while a game or Steam had the screen.
    onOnScreenChanged: if (onScreen) library.reload()

    FitGrid {
        id: grid

        anchors.fill: parent
        anchors.margins: 32
        focus: true
        model: place.library
        designWidth: 320
        designHeight: 480
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
