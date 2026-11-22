pragma ComponentBehavior: Bound

import QtQuick
import QtWebEngine
import TvShell

// youtube.com/tv, the leanback UI smart TVs use. Created the first time it opens and kept
// after that: Home pauses the video and freezes the page, opening it again carries on.
// Sign-in and accounts: YouTube shows a code to enter at youtube.com/activate on a phone.
// Cookies and storage live in the "youtube" profile (~/.local/share/tvshell/QtWebEngine/youtube).
//
// SponsorBlock: the video's ID is read from the address (#/watch?v=…), SponsorBlock (C++) asks
// for its segments, and sponsorskip.js, injected into the page, skips them.
FocusScope {
    id: page

    required property YouTubeSettings settings

    // Playing sound: keeps the screen awake.
    readonly property bool audible: view ? view.recentlyAudible : false
    readonly property WebEngineView view: loader.item as WebEngineView
    // The video being watched ("" = none).
    readonly property string videoId: {
        const match = view ? /#\/watch\?(?:[^#]*&)?v=([\w-]{11})/.exec(view.url.toString()) : null
        return match ? match[1] : ""
    }

    // YouTube's own exit (Back on its home screen, then Exit).
    signal closed()

    function open() {
        loader.active = true
        page.view.lifecycleState = WebEngineView.LifecycleState.Active
        loader.forceActiveFocus()
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
