pragma ComponentBehavior: Bound

import QtQuick
import TvShell.Common

// The first place: apps, at stable positions. YouTube and Jellyfin for now; music and others
// join later. Ring Fit Adventure (in Eden) stands here until the Games place can hold games
// that aren't Steam's.
FocusScope {
    id: place

    // The appIds of the apps that are loaded right now: their tiles get a mark.
    property list<string> runningApps

    signal appOpened(string appId)
    // OK held on a tile: its menu (close, force close).
    signal menuRequested(string appId, string name)

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
            ListElement {
                appId: "ringfit"
                name: "Ring Fit Adventure"
            }
        }

        delegate: AppTile {
            logo: appId === "youtube" ? youtubeLogo : appId === "jellyfin" ? jellyfinLogo : null
            running: place.runningApps.includes(appId)
        }

        // OK opens the tile when it is let go; held for a moment it brings up the tile's
        // menu instead.
        property bool okDown: false

        function isOk(event) {
            return event.key === Qt.Key_Return || event.key === Qt.Key_Enter
        }

        Keys.onPressed: event => {
            if (!isOk(event))
                return
            event.accepted = true
            if (event.isAutoRepeat || okDown)
                return
            okDown = true
            hold.restart()
        }
        Keys.onReleased: event => {
            if (!isOk(event))
                return
            event.accepted = true
            if (event.isAutoRepeat || !okDown)
                return
            okDown = false
            if (hold.running) {
                hold.stop()
                place.appOpened((grid.currentItem as AppTile).appId)
            }
        }
        onActiveFocusChanged: {
            if (!activeFocus) {
                okDown = false
                hold.stop()
            }
        }

        Timer {
            id: hold

            interval: 600
            onTriggered: {
                const tile = grid.currentItem as AppTile
                if (tile)
                    place.menuRequested(tile.appId, tile.name)
            }
        }
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
