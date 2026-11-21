import QtQuick
import QtQuick.Effects
import TvShell

// The photo screensaver: photos from the chosen Immich albums, one at a time on a timer, in an
// order shuffled once when it starts. Left/right browse (the screensaver stays), OK opens the
// photo in the viewer, other keys end it (IdleController). Without albums: a moving clock.
FocusScope {
    id: saver

    required property ImmichClient immich
    required property ScreensaverSettings settings
    // True while shown: starts a new run (new order) each time.
    property bool running

    readonly property var assets: immich.screensaverAssets
    readonly property bool slideshow: settings.albums.length > 0 && assets.length > 0
    // The photo to show (left/right and the timer move it) and the one shown or sliding in.
    property int index: 0
    property int shownIndex: -1
    // The direction of the last move, for the slide.
    property int lastDirection: 1
    property bool started

    signal photoOpened(int index)

    function urlAt(i) {
        return assets.length > 0 ? immich.imageUrl(assets[(i + assets.length) % assets.length].id, "fullsize") : ""
    }

    // The background needs only a small rendition: Immich's thumbnail.
    function backgroundUrlAt(i) {
        return assets.length > 0 && settings.background !== "black"
                ? immich.imageUrl(assets[(i + assets.length) % assets.length].id, "thumbnail") : ""
    }

    function step(direction) {
        if (!slideshow)
            return
        index = (index + direction + assets.length) % assets.length
        lastDirection = direction
        advance.restart()
        sync()
    }

    // Brings the screen to `index`. One transition at a time: presses during a slide only move
    // `index`, and the next slide (straight to the latest photo) starts when this one ends.
    // The photo loads into the hidden layer and is revealed once loaded.
    function sync() {
        if (!slideshow || layerA.animating || layerB.animating)
            return
        const incoming = layerA.front ? layerB : layerA
        if (index === shownIndex) {
            incoming.pending = false
            return
        }
        incoming.direction = shownIndex < 0 ? 0 : lastDirection
        incoming.photoIndex = index
        incoming.pending = true
        incoming.source = urlAt(index)
        if (incoming.ready)
            incoming.reveal()
    }

    function photoRevealing(layer) {
        shownIndex = layer.photoIndex
        if (settings.background !== "black")
            (backdropA.z > backdropB.z ? backdropB : backdropA).load(backgroundUrlAt(shownIndex))
    }

    onRunningChanged: {
        started = false
        index = 0
        shownIndex = -1
        layerA.reset()
        layerB.reset()
        backdropA.reset()
        backdropB.reset()
        if (running && settings.albums.length > 0)
            immich.loadScreensaver(settings.albums)
    }

    onAssetsChanged: {
        if (running && !started && assets.length > 0) {
            started = true
            index = 0
            advance.restart()
            sync()
        }
    }

    Keys.onLeftPressed: step(-1)
    Keys.onRightPressed: step(1)
    Keys.onReturnPressed: if (slideshow) photoOpened(index)
    Keys.onEnterPressed: if (slideshow) photoOpened(index)

    Timer {
        id: advance
        interval: saver.settings.interval * 1000
        repeat: true
        running: saver.running && saver.slideshow
        onTriggered: saver.step(1)
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    // Backgrounds: they crossfade in place under the sliding photos.
    Item {
        anchors.fill: parent
        visible: saver.settings.background !== "black"

        ScreensaverBackdrop {
            id: backdropA
            anchors.fill: parent
            other: backdropB
            mode: saver.settings.background
        }

        ScreensaverBackdrop {
            id: backdropB
            anchors.fill: parent
            other: backdropA
            mode: saver.settings.background
        }
    }

    ScreensaverLayer {
        id: layerA
        other: layerB
        transition: saver.settings.transition
        onRevealing: saver.photoRevealing(layerA)
        onRevealed: saver.sync()
    }

    ScreensaverLayer {
        id: layerB
        other: layerA
        transition: saver.settings.transition
        onRevealing: saver.photoRevealing(layerB)
        onRevealed: saver.sync()
    }

    // The neighbours, preloaded at the same size (Qt's image cache hands them over).
    Repeater {
        model: saver.slideshow ? [-1, 1] : []
        delegate: PhotoLayer {
            required property int modelData
            visible: false
            source: saver.urlAt(saver.index + modelData)
        }
    }

    // And their backgrounds' thumbnails (decoded at a different size, so only fetched here).
    Repeater {
        model: saver.slideshow && saver.settings.background !== "black" ? [-1, 1] : []
        delegate: Image {
            required property int modelData
            visible: false
            asynchronous: true
            sourceSize: Qt.size(96, 54)
            source: saver.backgroundUrlAt(saver.index + modelData)
        }
    }

    // Date and place, bottom left. It moves to a new spot nearby every few minutes, gliding
    // slowly, so it never sits on the same OLED pixels for long without jumping around.
    Column {
        id: info

        readonly property var asset: saver.slideshow ? saver.assets[saver.index] : null
        readonly property string date: asset && asset.date
                                       ? Qt.formatDate(new Date(asset.date + "T12:00:00"), "d MMMM yyyy") : ""
        readonly property string place: asset && asset.place ? asset.place : ""

        function drift() {
            x = 48 + Math.random() * 64
            y = saver.height - height - 48 - Math.random() * 48
        }

        x: 72
        y: saver.height - height - 72
        // Above the photo layers (they raise themselves to z 1 when revealed).
        z: 2
        spacing: 4
        visible: saver.slideshow && saver.settings.showInfo && (date.length > 0 || place.length > 0)

        layer.enabled: visible
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: "black"
            shadowBlur: 0.6
            shadowOpacity: 0.9
            shadowHorizontalOffset: 0
            shadowVerticalOffset: 0
        }

        Behavior on x { NumberAnimation { duration: 4000; easing.type: Easing.InOutSine } }
        Behavior on y { NumberAnimation { duration: 4000; easing.type: Easing.InOutSine } }

        Text {
            visible: text.length > 0
            color: "white"
            style: Text.Outline
            styleColor: "black"
            font.pixelSize: 64
            font.weight: Font.DemiBold
            text: info.date
        }

        Text {
            visible: text.length > 0
            color: "#e0e0e0"
            style: Text.Outline
            styleColor: "black"
            font.pixelSize: 48
            text: info.place
        }

        Timer {
            interval: 5 * 60 * 1000
            repeat: true
            running: info.visible
            onTriggered: info.drift()
        }
    }

    // No albums chosen (or nothing loaded yet): the clock, moving every minute.
    Text {
        id: clock

        z: 2
        visible: !saver.slideshow
        color: "#808080"
        font.pixelSize: 96
        text: Qt.formatTime(new Date(), "hh:mm")

        Timer {
            interval: 60000
            repeat: true
            triggeredOnStart: true
            running: saver.running && !saver.slideshow
            onTriggered: {
                clock.text = Qt.formatTime(new Date(), "hh:mm")
                clock.x = Math.random() * Math.max(0, saver.width - clock.width)
                clock.y = Math.random() * Math.max(0, saver.height - clock.height)
            }
        }
    }
}
