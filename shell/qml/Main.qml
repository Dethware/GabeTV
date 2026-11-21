import QtQuick
import TvShell

Window {
    id: root

    visible: true
    visibility: Window.FullScreen
    color: "#101418"
    title: qsTr("tvshell")

    BaseLayer {
        id: baseLayer
        window: root
    }

    GamepadInput {
        window: root
        enabled: root.active && (baseLayer.onScreen || !baseLayer.available)
    }

    SteamLibrary {
        id: steamLibrary
    }

    GamesPlace {
        anchors.fill: parent
        focus: true
        library: steamLibrary
        onScreen: baseLayer.onScreen || !baseLayer.available
    }
}
