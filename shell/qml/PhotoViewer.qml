import QtQuick
import QtMultimedia
import TvShell

// One photo, whole, on black (bars where the aspect ratio differs). Left/right move,
// Back leaves. On a video: OK plays/pauses (through the shell's media proxy, which adds the
// Immich key), Back stops it first.
//
// Each photo goes thumbnail → preview → full size. Only the thumbnail loads right away
// (usually cached from the grid); the bigger ones once the photo has been shown for a moment,
// so holding left/right doesn't fetch every full-size image on the way. Until the new photo
// has something to show, the last one stays up: no black flashes between photos.
// Once settled, the next three and previous two full-size images are preloaded; stepping to
// one of those shows it at full size right away.
FocusScope {
    id: viewer

    required property ImmichClient immich
    required property ImmichMediaProxy mediaProxy
    property var assets: []
    property int index: 0

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

    readonly property var asset: index >= 0 && index < assets.length ? assets[index] : null
    // The photo has been shown long enough to load the big versions.
    property bool settled: false
    // The best image shown so far, kept while the next photo loads.
    property url lastShown
    // Where the neighbours were last preloaded around (-1: nowhere yet).
    property int preloadCenter: -1
    readonly property var preloadOffsets: [-2, -1, 1, 2, 3]
    // The current photo's full-size image is (being) preloaded: show it straight away.
    readonly property bool preloaded: preloadCenter >= 0 && index !== preloadCenter
                                      && preloadOffsets.includes(index - preloadCenter)

    signal closed()

    function step(direction, autoRepeat) {
        const next = index + direction
        if (next < 0 || next >= assets.length)
            return
        // Held key: at most ~8 photos per second.
        if (autoRepeat && repeatLimit.running)
            return
        repeatLimit.restart()
        index = next
    }

    onIndexChanged: {
        stopVideo()
        settled = false
        settle.restart()
    }

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
        if (visible)
            preloadCenter = -1
        else
            stopVideo()
    }

    Timer {
        id: settle
        interval: 250
        running: true
        onTriggered: {
            viewer.settled = true
            viewer.preloadCenter = viewer.index
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

    // The last photo, until the current one has something to show.
    PhotoLayer {
        source: viewer.lastShown
        visible: thumbnail.status !== Image.Ready && preview.status !== Image.Ready && full.status !== Image.Ready
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

    Text {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 32
        color: "#808080"
        font.pixelSize: 24
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
