import QtQuick
import TvShell.Photos

// Photos from Immich: today's memories and the albums (by year, newest first).
FocusScope {
    id: place

    required property ImmichClient immich

    signal albumOpened(string albumId, string name)
    signal memoryOpened(var memory)
    // Up from the topmost row: the top bar should take over.
    signal leftUpward()

    readonly property bool hasMemories: immich.memories.length > 0

    Text {
        anchors.centerIn: parent
        width: parent.width * 0.7
        visible: !place.immich.configured || place.immich.error.length > 0
        color: "white"
        font.pixelSize: 32
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        text: place.immich.configured
              ? place.immich.error
              : qsTr("Immich isn't set up yet: put its address and an API key into ~/.config/tvshell/immich.conf.")
    }

    Text {
        id: memoriesHeading

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 48
        visible: place.hasMemories
        color: "white"
        font.pixelSize: 36
        text: qsTr("On this day")
    }

    ListView {
        id: memories

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: memoriesHeading.bottom
        anchors.margins: 32
        height: place.hasMemories ? 300 : 0
        visible: place.hasMemories
        orientation: ListView.Horizontal
        spacing: 24
        focus: place.hasMemories
        model: place.immich.memories
        highlightMoveDuration: 120

        delegate: PhotoTile {
            required property var modelData

            width: 400
            height: 300
            source: place.immich.imageUrl(modelData.assets[0].id, "preview")
            title: modelData.title
            current: ListView.isCurrentItem && memories.activeFocus
        }

        KeyNavigation.down: albums
        Keys.onUpPressed: place.leftUpward()
        Keys.onReturnPressed: place.memoryOpened(model[currentIndex])
        Keys.onEnterPressed: place.memoryOpened(model[currentIndex])
    }

    // The albums, in groups by year.
    AlbumGroups {
        id: albums

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: memories.bottom
        anchors.bottom: parent.bottom
        anchors.margins: 32
        anchors.topMargin: place.hasMemories ? 16 : 0
        focus: !place.hasMemories
        immich: place.immich
        albums: place.immich.albums

        onAlbumOpened: (albumId, name) => place.albumOpened(albumId, name)
        // From the first row: to the memories, or out of the place.
        onTopReached: {
            if (place.hasMemories)
                memories.forceActiveFocus()
            else
                place.leftUpward()
        }
    }
}
