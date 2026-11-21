import QtQuick

// One of the screensaver's two photo layers. The screensaver loads the next photo into the
// hidden layer; once loaded it is revealed over the other: slide (from the side of travel),
// fade, or at once. A visible layer is never given a new photo.
Image {
    id: layer

    required property Image other
    property string transition: "slide"
    // -1 previous, +1 next, 0 first photo.
    property int direction: 1
    // Which photo (index in the screensaver's order) this layer holds.
    property int photoIndex: -1
    // Waiting to be revealed once loaded.
    property bool pending
    // This layer shows the current photo.
    property bool front
    readonly property bool animating: revealAnimation.running
    readonly property bool ready: status === Image.Ready

    // The reveal started (revealing) and ended (revealed).
    signal revealing()
    signal revealed()

    function reveal() {
        pending = false
        z = 1
        other.z = 0
        const slide = transition === "slide" && direction !== 0
        const fade = transition === "fade" || (transition === "slide" && direction === 0)
        x = slide ? direction * width : 0
        other.x = 0
        opacity = fade ? 0 : 1
        visible = true
        slideIn.from = x
        slideOut.to = slide ? -direction * width : 0
        revealing()
        if (slide || fade)
            revealAnimation.start()
        else
            finish()
    }

    function finish() {
        x = 0
        opacity = 1
        front = true
        other.front = false
        // Hidden, not just underneath: the bars beside a photo would show it.
        other.visible = false
        other.x = 0
        revealed()
    }

    function reset() {
        revealAnimation.stop()
        pending = false
        front = false
        visible = false
        photoIndex = -1
        source = ""
        x = 0
        opacity = 1
    }

    width: parent.width
    height: parent.height
    visible: false
    sourceSize.width: width
    sourceSize.height: height
    fillMode: Image.PreserveAspectFit
    asynchronous: true
    autoTransform: true

    onReadyChanged: if (ready && pending) reveal()

    ParallelAnimation {
        id: revealAnimation

        NumberAnimation {
            id: slideIn
            target: layer
            property: "x"
            to: 0
            duration: 600
            easing.type: Easing.InOutCubic
        }
        NumberAnimation {
            id: slideOut
            target: layer.other
            property: "x"
            duration: 600
            easing.type: Easing.InOutCubic
        }
        NumberAnimation {
            target: layer
            property: "opacity"
            to: 1
            duration: 800
        }

        onFinished: layer.finish()
    }
}
