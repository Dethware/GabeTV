import QtQuick

// A full-screen photo layer in the viewer: whole photo, EXIF rotation applied. Decoded at
// the screen's real pixel size (the layout size times the UI scale).
Image {
    anchors.fill: parent
    sourceSize.width: Math.round(parent.width * Screen.devicePixelRatio)
    sourceSize.height: Math.round(parent.height * Screen.devicePixelRatio)
    fillMode: Image.PreserveAspectFit
    asynchronous: true
    autoTransform: true
}
