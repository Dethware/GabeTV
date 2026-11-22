import QtQuick
import TvShell

Window {
    id: root

    visible: true
    visibility: Window.FullScreen
    color: "#101418"
    title: qsTr("tvshell")

    // "home", "settings", "photos" (an album's or memory's grid), "viewer" or "youtube".
    property string page: "home"
    // When the shell stepped aside for Quick Access (ms timestamp, 0 = not pending).
    property double quickAccessHandoff: 0
    // Where leaving YouTube goes back to: the page it was opened from, or Steam if Steam had
    // the screen when the remote's YouTube key opened it.
    property string youtubeReturnPage: "home"
    property bool youtubeReturnToSteam: false

    function openYouTube() {
        if (page !== "youtube") {
            youtubeReturnPage = page
            youtubeReturnToSteam = baseLayer.available && !baseLayer.onScreen
        }
        showPage("youtube")
    }

    function leaveYouTube() {
        youtube.leave()
        showPage(youtubeReturnPage)
        if (youtubeReturnToSteam)
            baseLayer.claim = false
    }

    function showPage(name) {
        page = name
        if (name === "settings")
            settingsPage.forceActiveFocus()
        else if (name === "photos")
            photoGrid.forceActiveFocus()
        else if (name === "viewer")
            photoViewer.forceActiveFocus()
        else if (name === "youtube")
            youtube.open()
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

    // Starting a game from the shell. With music playing in the background: ask first.
    property string pendingGame

    function launchGame(appId, name) {
        if (youtube.background && youtube.playing) {
            pendingGame = appId
            gameDialog.title = qsTr("Start %1?").arg(name)
            gameDialog.open()
        } else {
            steamLibrary.launch(appId)
        }
    }

    function openPowerDialog() {
        // Not over a running game for now.
        if (baseLayer.gameRunning)
            return
        idle.wake()
        baseLayer.claim = true
        powerDialog.open()
    }

    // Steam button / Quick Access → Steam gets the screen. Home → back to the shell.
    // Power: short → screen off (again → on); held → power dialog.
    BaseLayer {
        id: baseLayer
        window: root
        onSteamMenuOpened: claim = false
        // Home: back to the shell, where it was left. Pressed there: to the Home place.
        onHomePressed: {
            const alreadyHome = onScreen && root.page === "home" && !powerDialog.visible
                              && idle.state === IdleController.Active
            idle.wake()
            claim = true
            if (powerDialog.visible)
                powerDialog.close()
            if (gameDialog.visible)
                gameDialog.close()
            if (root.page === "youtube")
                youtube.leave()
            root.showPage("home")
            if (alreadyHome) {
                topBar.currentTab = 0
                homePlace.forceActiveFocus()
            }
        }
        // The remote's YouTube key: YouTube, from anywhere (not over a running game for now).
        onYoutubePressed: {
            if (gameRunning)
                return
            idle.wake()
            if (powerDialog.visible)
                powerDialog.close()
            root.openYouTube()
            claim = true
        }
        onMutePressed: systemVolume.toggleMute()
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
        // Music (in YouTube, shown or in the background): screensaver and a black screen as
        // usual, but the sound's signal stays and the machine doesn't sleep.
        audioPlaying: youtube.playing && youtube.music
        onAboutToSleep: youtube.pause()
    }

    PowerActions {
        id: powerActions
    }

    // The remote's Assistant button and microphone. Only shown for now.
    RemoteAssistant {
        id: assistant
        onListeningChanged: if (listening) idle.wake()
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
        // Screensaver and screen off end with a button, never with a brushed stick or trackpad.
        sticks: idle.state === IdleController.Active
        onActivity: idle.activity()
        // LB/RB: previous/next place, directly (only on the home screen, not over dialogs).
        onShoulderPressed: direction => {
            if (root.page !== "home" || powerDialog.visible || gameDialog.visible)
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

    // Videos keep the screen awake while they play (YouTube: while it plays sound; not for a
    // music track that only shows its cover).
    Timer {
        interval: 30000
        repeat: true
        running: photoViewer.playing || (root.page === "youtube" && youtube.audible && !youtube.coverOnly)
        onTriggered: idle.activity()
    }

    // One source of sound at a time: a photo's video pauses the music.
    Connections {
        target: photoViewer
        function onPlayingChanged() {
            if (photoViewer.playing && youtube.playing)
                youtube.pause()
        }
    }

    FocusScope {
        id: home

        readonly property Item currentPlace: [homePlace, games, photos][topBar.currentTab]

        // Fresh albums and memories each time Photos is opened. (Not from the tab's own change
        // signal: its handler can run before currentPlace has followed the tab.)
        onCurrentPlaceChanged: if (currentPlace === photos) immich.refresh()

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
            tabs: [qsTr("Home"), qsTr("Games"), qsTr("Photos")]

            KeyNavigation.down: home.currentPlace
            onSettingsActivated: root.showPage("settings")

            nowPlayingSource: "youtube"
            nowPlayingTitle: !youtube.background ? "" : youtube.title.length > 0 ? youtube.title : qsTr("YouTube")
            nowPlayingArtist: youtube.artist
            nowPlayingPaused: !youtube.playing
            onNowPlayingActivated: youtube.togglePlay()
        }

        HomePlace {
            id: homePlace

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: topBar.bottom
            anchors.bottom: parent.bottom
            visible: home.currentPlace === homePlace
            // Starts here; afterwards only Down, LB/RB, Back and Home move the focus between the
            // bar and the places, so switching tabs with OK keeps it on the bar.
            focus: true

            KeyNavigation.up: topBar
            KeyNavigation.priority: KeyNavigation.AfterItem
            onAppOpened: appId => {
                if (appId === "youtube")
                    root.openYouTube()
            }
        }

        GamesPlace {
            id: games

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: topBar.bottom
            anchors.bottom: parent.bottom
            visible: home.currentPlace === games
            library: steamLibrary
            onLaunchRequested: (appId, name) => root.launchGame(appId, name)
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
            immich: immich

            onLeftUpward: topBar.forceActiveFocus()
            onAlbumOpened: (albumId, name) => root.openAlbum(albumId, name)
            onMemoryOpened: memory => root.openMemory(memory)
        }
    }

    YouTubePage {
        id: youtube

        anchors.fill: parent
        // Playing in the background it stays "visible" to the page, so YouTube carries on as
        // if it were on screen, but it isn't drawn.
        visible: root.page === "youtube" || background
        opacity: root.page === "youtube" ? 1 : 0
        settings: youtubeSettings

        onClosed: root.leaveYouTube()
    }

    // In YouTube, Back reaches the page as Escape (its "back"); on its account picker, Back
    // leaves YouTube instead.
    WebKeys {
        id: webKeys

        window: root
        enabled: root.page === "youtube" && idle.state === IdleController.Active && !powerDialog.visible
        onBackPressed: youtube.checkAccountPicker(atPicker => {
            if (atPicker)
                root.leaveYouTube()
            else
                webKeys.sendBack()
        })
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
        settings: viewerSettings
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
        screensaver: screensaverSettings
        viewer: viewerSettings
        youtube: youtubeSettings
        immich: immich

        onClosed: root.showPage("home")
    }

    ChoiceDialog {
        id: gameDialog

        anchors.fill: parent
        message: qsTr("Music is playing.")
        choices: [qsTr("Pause the music and play"), qsTr("Keep the music playing"), qsTr("Cancel")]

        onChosen: index => {
            if (index === 0)
                youtube.pause()
            if (index <= 1)
                steamLibrary.launch(root.pendingGame)
        }
        onClosed: root.showPage(root.page)
    }

    PowerDialog {
        id: powerDialog

        anchors.fill: parent
        actions: powerActions

        onClosed: root.showPage(root.page)
    }

    ScreensaverSettings {
        id: screensaverSettings
    }

    ViewerSettings {
        id: viewerSettings
    }

    YouTubeSettings {
        id: youtubeSettings
    }

    SystemVolume {
        id: systemVolume
        window: root
    }

    Screensaver {
        id: saver

        anchors.fill: parent
        visible: idle.state === IdleController.Screensaver
        running: visible
        immich: immich
        settings: screensaverSettings

        // The photo opens in the viewer, with the screensaver's order to browse.
        onPhotoOpened: index => {
            photoGrid.title = qsTr("Screensaver")
            photoGrid.assets = saver.assets
            photoGrid.loading = false
            photoGrid.currentIndex = index
            photoViewer.index = index
            root.page = "viewer"
            idle.wake()
        }
    }

    // The screensaver takes the keys while it runs; afterwards they go back to the page.
    Connections {
        target: idle
        function onStateChanged() {
            if (idle.state === IdleController.Screensaver)
                saver.forceActiveFocus()
            else if (saver.activeFocus)
                root.showPage(root.page)
        }
    }

    // "Screen off" while music plays keeps the signal (IdleController): black instead.
    Rectangle {
        anchors.fill: parent
        visible: idle.state === IdleController.ScreenOff
        color: "black"
    }

    AssistantIndicator {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 64
        assistant: assistant
    }

    // Last, so it draws over everything.
    VolumeIndicator {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 48
        volume: systemVolume
    }
}
