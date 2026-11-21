import QtQuick
import QtMultimedia
import QtQuick.Effects
import TvShell

// One photo, whole, over a background (the photo blurred, its colours, or black; see
// ViewerSettings). Left/right move with a transition (slide, crossfade or none), Back leaves. On a video: OK plays/pauses (through the shell's media proxy, which adds the
// Immich key), Back stops it first.
//
// Each photo goes thumbnail → preview → full size. Only the thumbnail loads right away
// (usually cached from the grid); the bigger ones once the photo has been shown for a moment,
// so holding left/right doesn't fetch every full-size image on the way. Until the new photo
// has something to show, the last one stays up: no black flashes between photos.
// Once settled, the next three and previous two full-size images are preloaded; stepping to
// one of those shows it at full size right away.
//
// `index` is the photo asked for, `shownIndex` the one on screen (or sliding in). One
// transition at a time: presses during one only move `index`, and the next transition goes
// straight there. A held key skips transitions and steps at once.
FocusScope {
    id: viewer

    required property ImmichClient immich
    required property ImmichMediaProxy mediaProxy
    required property ViewerSettings settings
    property var assets: []
    property int index: 0
    property int shownIndex: -1
    property int lastDirection: 1
    // A step() is changing `index` (other changes jump straight there).
    property bool stepping: false
    // Waiting for the incoming photo to have something to show, then animating.
    property bool waiting: false
    readonly property bool transitioning: waiting || transitionAnimation.running

    // A video is loaded (playing or paused).
    readonly property bool videoActive: video.source.toString().length > 0
    readonly property bool playing: video.playbackState === MediaPlayer.PlayingState

    function activate() {
        if (!asset || asset.isImage)
            return
        if (!videoActive) {
            video.source = mediaProxy.videoUrl(asset.id)
            video.play()
        } else if (playing) {
            video.pause()
        } else {
            video.play()
        }
    }

    function stopVideo() {
        video.stop()
        video.source = ""
    }

    readonly property var asset: shownIndex >= 0 && shownIndex < assets.length ? assets[shownIndex] : null
    // The photo has been shown long enough to load the big versions.
    property bool settled: false
    // The best image shown so far, kept while the next photo loads.
    property url lastShown
    // Where the neighbours were last preloaded around (-1: nowhere yet).
    property int preloadCenter: -1
    readonly property var preloadOffsets: [-2, -1, 1, 2, 3]
    // The current photo's full-size image is (being) preloaded: show it straight away.
    readonly property bool preloaded: preloadCenter >= 0 && shownIndex !== preloadCenter
                                      && preloadOffsets.includes(shownIndex - preloadCenter)
    // The incoming photo has something to show.
    readonly property bool stageReady: thumbnail.status === Image.Ready || preview.status === Image.Ready
                                       || full.status === Image.Ready

    signal closed()

    function step(direction, autoRepeat) {
        const next = index + direction
        if (next < 0 || next >= assets.length)
            return
        // Held key: at most ~8 photos per second.
        if (autoRepeat && repeatLimit.running)
            return
        repeatLimit.restart()
        lastDirection = direction
        stepping = true
        index = next
        stepping = false
        if (autoRepeat || settings.transition === "none")
            jump()
        else
            sync()
    }

    onIndexChanged: if (!stepping) jump()

    onShownIndexChanged: {
        stopVideo()
        settled = false
        settle.restart()
    }

    function backgroundUrl() {
        return asset && settings.background !== "black" ? immich.imageUrl(asset.id, "thumbnail").toString() : ""
    }

    function loadBackdrop() {
        backdropDelay.stop()
        const url = backgroundUrl()
        if (url.length > 0)
            (backdropA.z > backdropB.z ? backdropB : backdropA).load(url)
    }

    // Straight to `index`, no transition (opening, held keys, "none").
    function jump() {
        transitionAnimation.stop()
        waiting = false
        outgoing.visible = false
        stage.x = 0
        stage.opacity = 1
        if (shownIndex === index)
            return
        shownIndex = index
        // Held keys go through photos quickly: only the one they stop on gets its background.
        backdropDelay.restart()
    }

    // Towards `index` with a transition, unless one is running (it calls back when done).
    function sync() {
        if (transitioning || index === shownIndex)
            return
        outgoing.source = lastShown.toString()
        outgoing.x = 0
        outgoing.opacity = 1
        outgoing.visible = true
        waiting = true
        shownIndex = index
        loadBackdrop()
        if (stageReady)
            begin()
        else
            readyLimit.restart()
    }

    function begin() {
        waiting = false
        readyLimit.stop()
        const slide = settings.transition === "slide"
        stage.x = slide ? lastDirection * width : 0
        stage.opacity = slide ? 1 : 0
        stageIn.from = stage.x
        outgoingOut.to = slide ? -lastDirection * width : 0
        stageFade.from = stage.opacity
        outgoingFade.to = slide ? 1 : 0
        transitionAnimation.start()
    }

    onStageReadyChanged: if (stageReady && waiting) begin()

    Keys.onBackPressed: videoActive ? stopVideo() : closed()
    Keys.onEscapePressed: videoActive ? stopVideo() : closed()
    Keys.onReturnPressed: activate()
    Keys.onEnterPressed: activate()
    Keys.onLeftPressed: event => step(-1, event.isAutoRepeat)
    Keys.onRightPressed: event => step(1, event.isAutoRepeat)

    Timer {
        id: repeatLimit
        interval: 125
    }

    // Opened (again): forget the old neighbourhood. Closed: no video keeps playing.
    onVisibleChanged: {
        if (visible) {
            preloadCenter = -1
            jump()
        } else {
            stopVideo()
        }
    }

    // If the incoming photo takes long to load, the transition goes ahead anyway.
    Timer {
        id: readyLimit
        interval: 300
        onTriggered: viewer.begin()
    }

    Timer {
        id: backdropDelay
        interval: 200
        onTriggered: viewer.loadBackdrop()
    }

    ParallelAnimation {
        id: transitionAnimation

        NumberAnimation {
            id: stageIn
            target: stage
            property: "x"
            to: 0
            duration: 250
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            id: outgoingOut
            target: outgoing
            property: "x"
            duration: 250
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            id: stageFade
            target: stage
            property: "opacity"
            to: 1
            duration: 250
        }
        NumberAnimation {
            id: outgoingFade
            target: outgoing
            property: "opacity"
            duration: 250
        }

        onFinished: {
            outgoing.visible = false
            viewer.sync()
        }
    }

    Timer {
        id: settle
        interval: 250
        running: true
        onTriggered: {
            viewer.settled = true
            viewer.preloadCenter = viewer.shownIndex
        }
    }

    // The neighbours' full-size images, loaded invisibly at the same size as shown, so Qt's
    // image cache can hand them to the visible layer. Being referenced here also keeps the
    // previous ones around (the cache drops unreferenced large images quickly).
    Repeater {
        model: viewer.preloadOffsets

        delegate: PhotoLayer {
            required property int modelData

            readonly property var neighbour: viewer.preloadCenter >= 0
                                              ? viewer.assets[viewer.preloadCenter + modelData] ?? null : null

            visible: false
            source: neighbour && neighbour.isImage ? viewer.immich.imageUrl(neighbour.id, "fullsize") : ""
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    Item {
        anchors.fill: parent
        visible: viewer.settings.background !== "black"

        PhotoBackdrop {
            id: backdropA
            anchors.fill: parent
            other: backdropB
            mode: viewer.settings.background
        }

        PhotoBackdrop {
            id: backdropB
            anchors.fill: parent
            other: backdropA
            mode: viewer.settings.background
        }
    }

    // The previous photo, sliding or fading out.
    PhotoLayer {
        id: outgoing
        anchors.fill: undefined
        width: parent.width
        height: parent.height
        visible: false
    }

    // The current photo (moved and faded by the transitions).
    Item {
        id: stage

        width: parent.width
        height: parent.height

        // The last photo, until the current one has something to show (not in a transition: the
        // outgoing layer shows it then).
        PhotoLayer {
            source: viewer.lastShown
            visible: !viewer.transitioning && !viewer.stageReady
        }

        PhotoLayer {
            id: thumbnail
            source: viewer.asset ? viewer.immich.imageUrl(viewer.asset.id, "thumbnail") : ""
            visible: preview.status !== Image.Ready && full.status !== Image.Ready
            onStatusChanged: if (status === Image.Ready) viewer.lastShown = source
        }

        PhotoLayer {
            id: preview
            // Not needed when the full-size image is already preloaded.
            source: viewer.asset && viewer.settled && !viewer.preloaded
                    ? viewer.immich.imageUrl(viewer.asset.id, "preview") : ""
            visible: full.status !== Image.Ready
            onStatusChanged: if (status === Image.Ready) viewer.lastShown = source
        }

        PhotoLayer {
            id: full
            source: viewer.asset && viewer.asset.isImage && (viewer.settled || viewer.preloaded)
                    ? viewer.immich.imageUrl(viewer.asset.id, "fullsize") : ""
            onStatusChanged: if (status === Image.Ready) viewer.lastShown = source
        }

        Video {
            id: video

            anchors.fill: parent
            visible: viewer.videoActive
            fillMode: VideoOutput.PreserveAspectFit

            onErrorOccurred: (error, errorString) => console.warn("video:", errorString)
        }
    }

    // Date and place, bottom left (opposite the counter).
    PhotoInfo {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 64
        asset: viewer.asset
        visible: hasInfo
    }

    // Position and video state, bottom right; outlined like the screensaver's date and place.
    Text {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 64
        color: "white"
        style: Text.Outline
        styleColor: "black"
        font.pixelSize: 48
        font.weight: Font.DemiBold

        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: "black"
            shadowBlur: 0.6
            shadowOpacity: 0.9
            shadowHorizontalOffset: 0
            shadowVerticalOffset: 0
        }
        text: {
            let state = ""
            if (viewer.asset && !viewer.asset.isImage) {
                if (video.error !== MediaPlayer.NoError)
                    state = qsTr("Video can't play: %1").arg(video.errorString)
                else if (!viewer.videoActive)
                    state = qsTr("OK: play")
                else if (!viewer.playing)
                    state = qsTr("Paused")
                state += "   "
            }
            return state + (viewer.index + 1) + " / " + viewer.assets.length
        }
    }
}
