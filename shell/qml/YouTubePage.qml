pragma ComponentBehavior: Bound

import QtQuick
import QtWebEngine
import TvShell

// youtube.com/tv, the leanback UI smart TVs use. Created the first time it opens and kept
// after that. Leaving it (leave()) pauses the video and freezes the page, and opening it
// again carries on; music goes on playing in the background instead (`background`), where
// togglePlay() pauses and resumes it. What counts as music is YouTube's own mark on the video
// (musicVideoType in the player's response); if it can't be read, it's a video.
// Sign-in and accounts: YouTube shows a code to enter at youtube.com/activate on a phone.
// Cookies and storage live in the "youtube" profile (~/.local/share/tvshell/QtWebEngine/youtube).
//
// Resolution: mediacaps.js makes the browser answer the player's questions about what it can
// play honestly; without it the player stays at 720p.
//
// SponsorBlock: the video's ID is read from the address (#/watch?v=…), SponsorBlock (C++) asks
// for its segments, and sponsorskip.js, injected into the page, skips them.
FocusScope {
    id: page

    required property YouTubeSettings settings

    // Playing sound: keeps the screen awake.
    readonly property bool audible: view ? view.recentlyAudible : false
    readonly property WebEngineView view: loader.item as WebEngineView
    // What the page's player is doing, read once a second while the page runs.
    property bool playing: false
    // YouTube's music mark: "" (not music), or MUSIC_VIDEO_TYPE_ATV (a track with its cover),
    // _OMV (official music video), _UGC (music uploaded by a user), …
    property string musicType: ""
    property string title: ""
    property string artist: ""
    readonly property bool music: musicType.length > 0
    // A track showing only its cover: nothing on screen worth keeping it awake for.
    readonly property bool coverOnly: musicType === "MUSIC_VIDEO_TYPE_ATV"
    // Left with music on: the page stays alive (and counts as visible, see Main.qml) until it
    // is opened again, or has been paused for a while.
    property bool background: false
    // The page has been opened and is still loaded: shown, playing in the background, or
    // frozen where it was left.
    readonly property bool running: loader.active

    // The video being watched ("" = none).
    readonly property string videoId: {
        const match = view ? /#\/watch\?(?:[^#]*&)?v=([\w-]{11})/.exec(view.url.toString()) : null
        return match ? match[1] : ""
    }

    // YouTube's own exit (Back on its home screen, then Exit).
    signal closed()

    function open() {
        loader.active = true
        background = false
        page.view.lifecycleState = WebEngineView.LifecycleState.Active
        loader.forceActiveFocus()
    }

    // Leaving YouTube: music plays on, anything else pauses.
    function leave() {
        if (playing && music)
            background = true
        else
            suspend()
    }

    function togglePlay() {
        if (view)
            view.runJavaScript("(() => { const v = document.querySelector('video.html5-main-video');"
                               + " if (v) { if (v.paused) v.play(); else v.pause(); } })()")
    }

    function pause() {
        if (view)
            view.runJavaScript("document.querySelectorAll('video').forEach(v => v.pause())")
    }

    function readPlayer() {
        view.runJavaScript("(() => {"
            + " const v = document.querySelector('video.html5-main-video');"
            + " if (!v || !/#\\/watch/.test(location.hash)) return null;"
            + " const p = document.querySelector('.html5-video-player');"
            + " let type = '';"
            + " try { const r = p && p.getPlayerResponse && p.getPlayerResponse();"
            + "       type = (r && r.videoDetails && r.videoDetails.musicVideoType) || ''; } catch (e) {}"
            + " const m = navigator.mediaSession && navigator.mediaSession.metadata;"
            + " return { playing: !v.paused && !v.ended, type: type,"
            + "          title: m ? m.title : '', artist: m ? m.artist : '' }; })()",
            state => {
                page.playing = state ? state.playing === true : false
                page.musicType = state && typeof state.type === "string" ? state.type : ""
                page.title = state && typeof state.title === "string" ? state.title : ""
                page.artist = state && typeof state.artist === "string" ? state.artist : ""
            })
    }

    Timer {
        interval: 1000
        repeat: true
        running: page.view !== null && page.view.lifecycleState === WebEngineView.LifecycleState.Active
        onTriggered: page.readPlayer()
    }

    // Paused in the background for this long: the session ends (pause, freeze as usual).
    Timer {
        interval: 10 * 60 * 1000
        running: page.background && !page.playing
        onTriggered: page.suspend()
    }

    // Calls back with whether YouTube shows its "Who's watching?" account picker (its address
    // is the same as the home screen's, so this looks at the page).
    function checkAccountPicker(callback) {
        if (!view) {
            callback(false)
            return
        }
        view.runJavaScript("(() => { const e = document.querySelector('ytlr-account-selector');"
                           + " if (!e) return false; const r = e.getBoundingClientRect();"
                           + " return r.width > 0 && r.height > 0; })()", result => callback(result === true))
    }

    function pushSegments() {
        if (view)
            view.runJavaScript("globalThis.tvshellSetSegments && tvshellSetSegments("
                               + JSON.stringify(sponsorBlock.segments) + ")", WebEngineScript.ApplicationWorld)
    }

    SponsorBlock {
        id: sponsorBlock

        videoId: page.videoId
        categories: page.settings.skipCategories
        onSegmentsChanged: page.pushSegments()
    }

    // Pause, then freeze once the page is hidden (a visible page can't be frozen).
    function suspend() {
        background = false
        playing = false
        const view = page.view
        if (!view)
            return
        view.runJavaScript("document.querySelectorAll('video').forEach(v => v.pause())", () => {
            if (!page.visible)
                view.lifecycleState = WebEngineView.LifecycleState.Frozen
        })
    }

    WebEngineProfilePrototype {
        id: youtubeProfile

        storageName: "youtube"
        persistentCookiesPolicy: WebEngineProfile.ForcePersistentCookies
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    Loader {
        id: loader

        anchors.fill: parent
        active: false
        focus: true

        sourceComponent: WebEngineView {
            focus: true
            backgroundColor: "black"
            settings.playbackRequiresUserGesture: false
            settings.showScrollBars: false

            onWindowCloseRequested: page.closed()
            // A (re)loaded page starts without segments.
            onLoadingChanged: request => {
                if (request.status === WebEngineView.LoadSucceededStatus)
                    page.pushSegments()
            }
            onJavaScriptConsoleMessage: (level, message) => {
                if (message.startsWith("tvshell:"))
                    console.info("youtube page:", message.substring(8).trim())
            }
            onRenderProcessTerminated: (status, exitCode) => {
                console.warn("youtube: page process ended:", status, exitCode, "- reloading")
                reload()
            }

            Component.onCompleted: {
                profile = youtubeProfile.instance()
                userScripts.collection = [{
                    name: "sponsorskip",
                    sourceUrl: Qt.resolvedUrl("sponsorskip.js"),
                    injectionPoint: WebEngineScript.DocumentReady,
                    worldId: WebEngineScript.ApplicationWorld
                }, {
                    // Before the page's own scripts, in their world: they call what it replaces.
                    name: "mediacaps",
                    sourceUrl: Qt.resolvedUrl("mediacaps.js"),
                    injectionPoint: WebEngineScript.DocumentCreation,
                    worldId: WebEngineScript.MainWorld
                }]
                // Without a TV user agent, youtube.com/tv sends the browser to the desktop site,
                // and the user agent decides the look: YouTube reads brand and model from the
                // format its TV app platform (Cobalt) uses and gives this one its full-animation
                // version. A plain Samsung Tizen one got the limited-animation version.
                profile.httpUserAgent = "Mozilla/5.0 (Linux x86_64) Cobalt/25.lts.30.1034943-gold (unlike Gecko) "
                        + "v8/12.4 gles Starboard/16, Valve_OTT_SteamMachine_2026/3.9.5 (Valve, SteamMachine, Wired)"
                url = "https://www.youtube.com/tv"
            }
        }
    }
}
