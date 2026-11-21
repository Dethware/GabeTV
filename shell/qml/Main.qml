import QtQuick
import TvShell

Window {
    id: root

    visible: true
    visibility: Window.FullScreen
    color: "#101418"
    title: qsTr("tvshell")

    // "home" or "settings".
    property string page: "home"
    // When the shell stepped aside for Quick Access (ms timestamp, 0 = not pending).
    property double quickAccessHandoff: 0

    function showPage(name) {
        page = name
        if (name === "settings")
            settingsPage.forceActiveFocus()
        else
            home.forceActiveFocus()
    }

    function openPowerDialog() {
        // Not over a running game for now.
        if (baseLayer.gameRunning)
            return
        idle.activity()
        baseLayer.claim = true
        powerDialog.open()
    }

    // Steam button / Quick Access → Steam gets the screen. Home → back to the shell.
    // Power: short → screen off (again → on); held → power dialog.
    BaseLayer {
        id: baseLayer
        window: root
        onSteamMenuOpened: claim = false
        onHomePressed: {
            idle.activity()
            claim = true
            if (powerDialog.visible)
                powerDialog.close()
            root.showPage("home")
        }
        onPowerPressed: {
            if (powerDialog.visible)
                powerDialog.close()
            else
                idle.togglePower()
        }
        onPowerHeld: root.openPowerDialog()
        onInputActivity: idle.activity()
    }

    IdleController {
        id: idle
        counting: baseLayer.onScreen || !baseLayer.available
    }

    PowerActions {
        id: powerActions
    }

    // The remote's Assistant button and microphone. Only shown for now.
    RemoteAssistant {
        id: assistant
        onListeningChanged: if (listening) idle.activity()
    }

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

    FocusScope {
        id: home

        anchors.fill: parent
        focus: true
        visible: root.page === "home"

        // The controller's Start/≡ opens the settings from anywhere on the home page.
        Keys.onMenuPressed: root.showPage("settings")

        TopBar {
            id: topBar

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            title: qsTr("Games")

            KeyNavigation.down: games
            onSettingsActivated: root.showPage("settings")
        }

        GamesPlace {
            id: games

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: topBar.bottom
            anchors.bottom: parent.bottom
            focus: true
            library: steamLibrary
            onScreen: baseLayer.onScreen || !baseLayer.available

            KeyNavigation.up: topBar
            KeyNavigation.priority: KeyNavigation.AfterItem
        }
    }

    SettingsPage {
        id: settingsPage

        anchors.fill: parent
        visible: root.page === "settings"
        idle: idle

        onClosed: root.showPage("home")
    }

    PowerDialog {
        id: powerDialog

        anchors.fill: parent
        actions: powerActions

        onClosed: root.showPage(root.page)
    }

    Screensaver {
        anchors.fill: parent
        visible: idle.state === IdleController.Screensaver
    }

    AssistantIndicator {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 64
        assistant: assistant
    }
}
