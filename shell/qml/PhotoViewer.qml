import QtQuick
import TvShell

// One photo, whole, on black (bars where the aspect ratio differs). Left/right move,
// Back leaves. Shows the preview first and swaps in the full-size image once it's loaded.
FocusScope {
    id: viewer

    required property ImmichClient immich
    property var assets: []
    property int index: 0

    readonly property var asset: index >= 0 && index < assets.length ? assets[index] : null

    signal closed()

    Keys.onBackPressed: closed()
    Keys.onEscapePressed: closed()
    Keys.onLeftPressed: if (index > 0) index -= 1
    Keys.onRightPressed: if (index + 1 < assets.length) index += 1

    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    Image {
        id: preview

        anchors.fill: parent
        source: viewer.asset ? viewer.immich.imageUrl(viewer.asset.id, "preview") : ""
        sourceSize.width: viewer.width
        sourceSize.height: viewer.height
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        visible: full.status !== Image.Ready
    }

    Image {
        id: full

        anchors.fill: parent
        source: viewer.asset && viewer.asset.isImage ? viewer.immich.imageUrl(viewer.asset.id, "fullsize") : ""
        sourceSize.width: viewer.width
        sourceSize.height: viewer.height
        fillMode: Image.PreserveAspectFit
        asynchronous: true
    }

    Text {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 32
        color: "#808080"
        font.pixelSize: 24
        text: (viewer.asset && !viewer.asset.isImage ? qsTr("Video (not playable yet)") + "   " : "")
              + (viewer.index + 1) + " / " + viewer.assets.length
    }
}
