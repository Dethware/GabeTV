import QtQuick

// Placeholder screensaver: black, with a clock that moves every minute (OLED).
// Immich photos come later.
Rectangle {
    id: saver

    color: "black"

    Text {
        id: clock

        color: "#808080"
        font.pixelSize: 96
        text: Qt.formatTime(new Date(), "hh:mm")
    }

    Timer {
        interval: 60000
        repeat: true
        triggeredOnStart: true
        running: saver.visible
        onTriggered: {
            clock.text = Qt.formatTime(new Date(), "hh:mm")
            clock.x = Math.random() * Math.max(0, saver.width - clock.width)
            clock.y = Math.random() * Math.max(0, saver.height - clock.height)
        }
    }
}
