pragma ComponentBehavior: Bound

import QtQuick

// The first place: apps, at stable positions. YouTube and Jellyfin for now; music and others
// join later.
FocusScope {
    id: place

    // The appIds of the apps that are loaded right now: their tiles get a mark.
    property list<string> runningApps

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
            }
            ListElement {
                appId: "jellyfin"
                name: "Jellyfin"
            }
        }

        delegate: AppTile {
            logo: appId === "youtube" ? youtubeLogo : appId === "jellyfin" ? jellyfinLogo : null
            running: place.runningApps.includes(appId)
        }

        Keys.onReturnPressed: place.appOpened((grid.currentItem as AppTile).appId)
        Keys.onEnterPressed: place.appOpened((grid.currentItem as AppTile).appId)
    }

    Component {
        id: youtubeLogo
        YouTubeLogo {}
    }

    Component {
        id: jellyfinLogo
        JellyfinLogo {}
    }
}
