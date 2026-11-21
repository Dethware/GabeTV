import QtQuick
import QtQuick.Effects

// What the screensaver shows beside a photo: the photo blurred ("blur") or its colours as a
// gradient ("gradient"). It stays put and crossfades while the photos slide over it: the new
// one loads hidden, fades in on top, and the old one goes once it's covered.
// Drawn small and scaled up: blurring 96×54 pixels is cheap and smooth scaling hides the rest.
// For the gradient the thumbnail is decoded at 3×2 pixels, so only its broad colours remain.
Item {
    id: backdrop

    required property Item other
    property string mode: "blur"
    // Shown (fades in) or not (fades out).
    property bool shown
    property bool pending

    function load(url) {
        pending = true
        image.source = url
        if (image.status === Image.Ready)
            show()
    }

    function show() {
        pending = false
        z = 1
        other.z = 0
        other.pending = false
        shown = true
        if (opacity === 1)
            other.shown = false
    }

    function reset() {
        pending = false
        shown = false
        image.source = ""
    }

    opacity: shown ? 1 : 0
    visible: opacity > 0

    Behavior on opacity { NumberAnimation { duration: 500 } }

    // Covered: the other one can go.
    onOpacityChanged: if (shown && opacity === 1) other.shown = false

    Item {
        id: small

        width: 96
        height: 54
        visible: false

        Image {
            id: image

            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            sourceSize: backdrop.mode === "gradient" ? Qt.size(3, 2) : Qt.size(96, 54)
            asynchronous: true
            autoTransform: true

            onStatusChanged: if (status === Image.Ready && backdrop.pending) backdrop.show()
        }
    }

    MultiEffect {
        width: small.width
        height: small.height
        scale: backdrop.width / width
        transformOrigin: Item.TopLeft
        source: small
        blurEnabled: true
        blur: 1
        blurMax: 32
        brightness: -0.35
        saturation: backdrop.mode === "gradient" ? 0.2 : 0
    }
}
