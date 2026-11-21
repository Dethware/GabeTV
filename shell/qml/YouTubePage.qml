pragma ComponentBehavior: Bound

import QtQuick
import QtWebEngine

// youtube.com/tv, the leanback UI smart TVs use. Created the first time it opens and kept
// after that: Home pauses the video and freezes the page, opening it again carries on.
// Sign-in and accounts: YouTube shows a code to enter at youtube.com/activate on a phone.
// Cookies and storage live in the "youtube" profile (~/.local/share/tvshell/QtWebEngine/youtube).
FocusScope {
    id: page

    // Playing sound: keeps the screen awake.
    readonly property bool audible: view ? view.recentlyAudible : false
    readonly property WebEngineView view: loader.item as WebEngineView

    // YouTube's own exit (Back on its home screen, then Exit).
    signal closed()

    function open() {
        loader.active = true
        page.view.lifecycleState = WebEngineView.LifecycleState.Active
        loader.forceActiveFocus()
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
            onRenderProcessTerminated: (status, exitCode) => {
                console.warn("youtube: page process ended:", status, exitCode, "- reloading")
                reload()
            }

            Component.onCompleted: {
                profile = youtubeProfile.instance()
                // Without a TV user agent, youtube.com/tv sends the browser to the desktop site.
                profile.httpUserAgent = "Mozilla/5.0 (SMART-TV; Linux; Tizen 7.0) AppleWebKit/537.36 "
                        + "(KHTML, like Gecko) Chrome/134.0.0.0 TV Safari/537.36"
                url = "https://www.youtube.com/tv"
            }
        }
    }
}
