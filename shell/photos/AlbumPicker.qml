import QtQuick
import TvShell.Common
import TvShell.Photos

// Picks the screensaver's albums: a list of all albums with checkmarks. OK toggles, Back closes.
FocusScope {
    id: picker

    required property ImmichClient immich
    required property ScreensaverSettings screensaver

    signal closed()

    function open() {
        if (immich.albums.length === 0)
            immich.refresh()
        visible = true
        list.forceActiveFocus()
    }

    function close() {
        visible = false
        closed()
    }

    visible: false

    Keys.onBackPressed: close()
    Keys.onEscapePressed: close()

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
        text: qsTr("Screensaver albums")
    }

    Text {
        anchors.centerIn: parent
        visible: list.count === 0
        color: "white"
        font.pixelSize: 32
        text: picker.immich.error.length > 0 ? picker.immich.error : qsTr("Loading albums…")
    }

    ListView {
        id: list

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: heading.bottom
        anchors.bottom: parent.bottom
        anchors.margins: 48
        anchors.leftMargin: 448
        spacing: 8
        model: picker.immich.albums

        delegate: SettingRow {
            required property var modelData

            width: ListView.view.width
            label: (picker.screensaver.albums.includes(modelData.id) ? "✓  " : "      ") + modelData.name
            value: String(modelData.count)
            current: ListView.isCurrentItem && list.activeFocus
            focused: current
        }

        Keys.onReturnPressed: picker.screensaver.toggleAlbum(model[currentIndex].id)
        Keys.onEnterPressed: picker.screensaver.toggleAlbum(model[currentIndex].id)
    }
}
