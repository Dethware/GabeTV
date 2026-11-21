import QtQuick
import TvShell

Window {
    id: root

    visible: true
    visibility: Window.FullScreen
    color: "#101418"
    title: qsTr("tvshell")

    // Steam button / Quick Access → Steam gets the screen. Home → back to the shell.
    // Power → screen off (again → on).
    BaseLayer {
        id: baseLayer
        window: root
        onSteamMenuOpened: claim = false
        onHomePressed: {
            idle.activity()
            claim = true
        }
        onPowerPressed: {
            if (idle.state === IdleController.ScreenOff)
                idle.activity()
            else
                idle.screenOff()
        }
        onInputActivity: idle.activity()
    }

    IdleController {
        id: idle
        counting: baseLayer.onScreen || !baseLayer.available
    }

    // When the shell stepped aside for Quick Access (ms timestamp, 0 = not pending).
    property double quickAccessHandoff: 0

    // Quick Access gives no X11 signal; Steam's log is the only trace of it. Stepping aside moves
    // the focus to Steam, which sometimes closes Quick Access again about a second later
    // (seen once of three tries): re-open it once if that happens.
    SteamLog {
        onQuickAccessOpened: {
            if (baseLayer.onScreen && !baseLayer.gameRunning) {
                root.quickAccessHandoff = Date.now()
                baseLayer.claim = false
            }
        }
        onQuickAccessClosed: {
            if (root.quickAccessHandoff > 0 && Date.now() - root.quickAccessHandoff < 2000)
                baseLayer.toggleQuickAccess()
            root.quickAccessHandoff = 0
        }
    }

    GamepadInput {
        window: root
        enabled: root.active && (baseLayer.onScreen || !baseLayer.available)
        onActivity: idle.activity()
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

    Screensaver {
        anchors.fill: parent
        visible: idle.state === IdleController.Screensaver
    }
}
