import QtQuick
import TvShell

// The photos of an album or a memory. OK opens the viewer, Back leaves.
FocusScope {
    id: page

    required property ImmichClient immich
    property string title
    property var assets: []
    property bool loading
    property alias currentIndex: grid.currentIndex

    signal photoOpened(int index)
    signal closed()

    Keys.onBackPressed: closed()
    Keys.onEscapePressed: closed()

    Rectangle {
        anchors.fill: parent
        color: "#101418"
    }

    Text {
        id: heading

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 48
        color: "white"
        font.pixelSize: 48
        text: page.loading ? qsTr("%1 (loading…)").arg(page.title) : page.title
    }

    FitGrid {
        id: grid

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: heading.bottom
        anchors.bottom: parent.bottom
        anchors.margins: 32
        anchors.rightMargin: 32 - gap
        designWidth: 352
        designHeight: 260
        gap: 16
        focus: true
        model: page.assets
        highlightMoveDuration: 120
        cacheBuffer: 1200

        delegate: PhotoTile {
            required property var modelData

            width: grid.cardWidth
            height: grid.cardHeight
            source: page.immich.imageUrl(modelData.id, "thumbnail")
            whole: true
            video: !modelData.isImage
            current: GridView.isCurrentItem && grid.activeFocus
        }

        Keys.onReturnPressed: page.photoOpened(currentIndex)
        Keys.onEnterPressed: page.photoOpened(currentIndex)
    }
}
