import QtQuick
import TvShell

Window {
    id: root

    visible: true
    visibility: Window.FullScreen
    color: "#101418"
    title: qsTr("tvshell")

    // "home", "settings", "photos" (an album's or memory's grid) or "viewer".
    property string page: "home"
    // When the shell stepped aside for Quick Access (ms timestamp, 0 = not pending).
    property double quickAccessHandoff: 0

    function showPage(name) {
        page = name
        if (name === "settings")
            settingsPage.forceActiveFocus()
        else if (name === "photos")
            photoGrid.forceActiveFocus()
        else if (name === "viewer")
            photoViewer.forceActiveFocus()
        else
            home.forceActiveFocus()
    }

    function openAlbum(albumId, name) {
        immich.loadAlbum(albumId)
        photoGrid.title = name
        photoGrid.assets = Qt.binding(() => immich.albumAssets)
        photoGrid.loading = Qt.binding(() => immich.albumLoading)
        photoGrid.currentIndex = 0
        showPage("photos")
    }

    function openMemory(memory) {
        photoGrid.title = memory.title
        photoGrid.assets = memory.assets
        photoGrid.loading = false
        photoGrid.currentIndex = 0
        showPage("photos")
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
        // LB/RB: previous/next place, directly (only on the home screen, not over dialogs).
        onShoulderPressed: direction => {
            if (root.page !== "home" || powerDialog.visible)
                return
            topBar.step(direction)
            home.currentPlace.forceActiveFocus()
        }
    }

    SteamLibrary {
        id: steamLibrary
    }

    ImmichClient {
        id: immich
    }

    ImmichMediaProxy {
        id: mediaProxy
    }

    // Videos keep the screen awake while they play.
    Timer {
        interval: 30000
        repeat: true
        running: photoViewer.playing
        onTriggered: idle.activity()
    }

    FocusScope {
        id: home

        readonly property Item currentPlace: topBar.currentTab === 1 ? photos : games

        anchors.fill: parent
        focus: true
        visible: root.page === "home"

        // The controller's Start/≡ opens the settings from anywhere on the home page.
        Keys.onMenuPressed: root.showPage("settings")
        // Back from inside a place: up to the top bar, so there's always a way out.
        Keys.onBackPressed: topBar.forceActiveFocus()
        Keys.onEscapePressed: topBar.forceActiveFocus()

        TopBar {
            id: topBar

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            tabs: [qsTr("Games"), qsTr("Photos")]

            KeyNavigation.down: home.currentPlace
            onSettingsActivated: root.showPage("settings")
            onCurrentTabChanged: if (currentTab === 1) immich.refresh()
        }

        GamesPlace {
            id: games

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: topBar.bottom
            anchors.bottom: parent.bottom
            visible: home.currentPlace === games
            focus: home.currentPlace === games
            library: steamLibrary
            onScreen: baseLayer.onScreen || !baseLayer.available

            KeyNavigation.up: topBar
            KeyNavigation.priority: KeyNavigation.AfterItem
        }

        PhotosPlace {
            id: photos

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: topBar.bottom
            anchors.bottom: parent.bottom
            visible: home.currentPlace === photos
            focus: home.currentPlace === photos
            immich: immich

            onLeftUpward: topBar.forceActiveFocus()
            onAlbumOpened: (albumId, name) => root.openAlbum(albumId, name)
            onMemoryOpened: memory => root.openMemory(memory)
        }
    }

    PhotoGrid {
        id: photoGrid

        anchors.fill: parent
        visible: root.page === "photos"
        immich: immich

        onClosed: root.showPage("home")
        onPhotoOpened: index => {
            photoViewer.index = index
            root.showPage("viewer")
        }
    }

    PhotoViewer {
        id: photoViewer

        anchors.fill: parent
        visible: root.page === "viewer"
        immich: immich
        mediaProxy: mediaProxy
        assets: photoGrid.assets

        onClosed: {
            photoGrid.currentIndex = index
            root.showPage("photos")
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
