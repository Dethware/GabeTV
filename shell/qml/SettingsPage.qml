import QtQuick
import TvShell

// Settings, laid out like Steam's: categories on the left, options on the right.
// Up/down picks an option, left/right changes its value, Back leaves.
FocusScope {
    id: page

    required property IdleController idle

    signal closed()

    // Seconds; 0 = never.
    readonly property var durations: [0, 60, 120, 300, 600, 900, 1800, 2700, 3600, 7200, 14400]

    function formatDuration(seconds) {
        if (seconds === 0)
            return qsTr("Never")
        if (seconds < 3600) {
            const minutes = Math.round(seconds / 60)
            return minutes === 1 ? qsTr("1 minute") : qsTr("%1 minutes").arg(minutes)
        }
        const hours = Math.round(seconds / 3600)
        return hours === 1 ? qsTr("1 hour") : qsTr("%1 hours").arg(hours)
    }

    function step(seconds, direction) {
        const values = page.durations
        let index = values.indexOf(seconds)
        if (index < 0) {
            index = values.findIndex(value => value > seconds)
            if (index < 0)
                index = values.length
            if (direction > 0)
                index -= 1
        }
        return values[Math.max(0, Math.min(values.length - 1, index + direction))]
    }

    Keys.onBackPressed: page.closed()
    Keys.onEscapePressed: page.closed()

    Rectangle {
        anchors.fill: parent
        color: "#101418"
    }

    Text {
        id: heading
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 48
        color: "white"
        font.pixelSize: 48
        text: qsTr("Settings")
    }

    Column {
        id: categories

        anchors.left: parent.left
        anchors.top: heading.bottom
        anchors.margins: 48
        width: 400

        Rectangle {
            width: categories.width
            height: 72
            radius: 8
            color: "#2a343d"

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 24
                anchors.verticalCenter: parent.verticalCenter
                color: "white"
                font.pixelSize: 32
                text: qsTr("Power")
            }
        }
    }

    ListView {
        id: options

        anchors.left: categories.right
        anchors.right: parent.right
        anchors.top: heading.bottom
        anchors.bottom: parent.bottom
        anchors.margins: 48
        spacing: 8
        focus: true
        keyNavigationWraps: false

        model: [
            { key: "screensaverAfter", label: qsTr("Screensaver after") },
            { key: "screenOffAfter", label: qsTr("Turn the screen off after") },
            { key: "suspendAfter", label: qsTr("Sleep after") },
        ]

        delegate: SettingRow {
            required property var modelData

            width: ListView.view.width
            label: modelData.label
            value: page.formatDuration(page.idle[modelData.key])
            current: ListView.isCurrentItem && options.activeFocus

            onDecreased: page.idle[modelData.key] = page.step(page.idle[modelData.key], -1)
            onIncreased: page.idle[modelData.key] = page.step(page.idle[modelData.key], 1)
        }

        Keys.onLeftPressed: currentItem.decreased()
        Keys.onRightPressed: currentItem.increased()
    }
}
