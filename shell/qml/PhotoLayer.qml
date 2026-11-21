import QtQuick

// A full-screen photo layer in the viewer: whole photo, EXIF rotation applied.
Image {
    anchors.fill: parent
    sourceSize.width: parent.width
    sourceSize.height: parent.height
    fillMode: Image.PreserveAspectFit
    asynchronous: true
    autoTransform: true
}
