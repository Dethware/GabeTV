import QtQuick

// Video apps. YouTube for now; Jellyfin and others join later, at stable positions.
FocusScope {
    id: place

    signal appOpened(string appId)

    FitGrid {
        id: grid

        anchors.fill: parent
        anchors.margins: 32
        focus: true
        designWidth: 320
        designHeight: 480
        highlightMoveDuration: 120

        model: ListModel {
            ListElement {
                appId: "youtube"
                name: "YouTube"
                cover: ""
            }
        }

        delegate: GameTile {}

        Keys.onReturnPressed: place.appOpened((grid.currentItem as GameTile).appId)
        Keys.onEnterPressed: place.appOpened((grid.currentItem as GameTile).appId)
    }
}
