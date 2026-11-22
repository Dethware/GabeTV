import QtQuick
import TvShell

// Settings, laid out like Steam's: categories on the left, options on the right.
// Categories: up/down, Right/OK goes into the options. Options: up/down picks, left/right
// changes the value, OK toggles or opens. Back goes out a level; from the categories it leaves.
FocusScope {
    id: page

    required property IdleController idle
    required property ScreensaverSettings screensaver
    required property ViewerSettings viewer
    required property YouTubeSettings youtube
    required property ImmichClient immich

    signal closed()

    // Seconds; 0 = never.
    readonly property var idleDurations: [0, 60, 120, 300, 600, 900, 1800, 2700, 3600, 7200, 14400]
    readonly property var photoDurations: [5, 10, 15, 30, 60, 120]
    readonly property var transitionKinds: ["slide", "fade", "none"]
    readonly property var backgroundKinds: ["blur", "gradient", "black"]

    function formatDuration(seconds) {
        if (seconds === 0)
            return qsTr("Never")
        if (seconds < 60)
            return qsTr("%1 seconds").arg(seconds)
        if (seconds < 3600) {
            const minutes = Math.round(seconds / 60)
            return minutes === 1 ? qsTr("1 minute") : qsTr("%1 minutes").arg(minutes)
        }
        const hours = Math.round(seconds / 3600)
        return hours === 1 ? qsTr("1 hour") : qsTr("%1 hours").arg(hours)
    }

    // The next value in `values` from `current` (which may not be one of them).
    function stepIn(values, current, direction) {
        let index = values.indexOf(current)
        if (index < 0) {
            index = values.findIndex(value => value > current)
            if (index < 0)
                index = values.length
            if (direction > 0)
                index -= 1
        }
        return values[Math.max(0, Math.min(values.length - 1, index + direction))]
    }

    function transitionName(transition) {
        return transition === "slide" ? qsTr("Slide") : transition === "fade" ? qsTr("Crossfade") : qsTr("None")
    }

    function backgroundName(background) {
        return background === "blur" ? qsTr("Blurred photo") : background === "gradient" ? qsTr("Colour gradient") : qsTr("Black")
    }

    function cycle(values, current, direction) {
        return values[(values.indexOf(current) + direction + values.length) % values.length]
    }

    // A switch for one SponsorBlock category.
    function skipOption(label, category) {
        return { label: label,
                 value: () => youtube.skipCategories.includes(category) ? qsTr("On") : qsTr("Off"),
                 change: d => youtube.toggleSkip(category),
                 activate: () => youtube.toggleSkip(category) }
    }

    function albumsText() {
        const count = screensaver.albums.length
        return count === 0 ? qsTr("None (clock)") : count === 1 ? qsTr("1 album") : qsTr("%1 albums").arg(count)
    }

    readonly property var categoryOptions: [
        // Power
        [
            { label: qsTr("Screensaver after"),
              value: () => formatDuration(idle.screensaverAfter),
              change: d => idle.screensaverAfter = stepIn(idleDurations, idle.screensaverAfter, d) },
            { label: qsTr("Turn the screen off after"),
              value: () => formatDuration(idle.screenOffAfter),
              change: d => idle.screenOffAfter = stepIn(idleDurations, idle.screenOffAfter, d) },
            { label: qsTr("Sleep after"),
              value: () => formatDuration(idle.suspendAfter),
              change: d => idle.suspendAfter = stepIn(idleDurations, idle.suspendAfter, d) },
        ],
        // Screensaver
        [
            { label: qsTr("Albums"),
              value: () => albumsText(),
              activate: () => albumPicker.open() },
            { label: qsTr("Show each photo for"),
              value: () => formatDuration(screensaver.interval),
              change: d => screensaver.interval = stepIn(photoDurations, screensaver.interval, d) },
            { label: qsTr("Transition"),
              value: () => transitionName(screensaver.transition),
              change: d => screensaver.transition = cycle(transitionKinds, screensaver.transition, d) },
            { label: qsTr("Background"),
              value: () => backgroundName(screensaver.background),
              change: d => screensaver.background = cycle(backgroundKinds, screensaver.background, d) },
            { label: qsTr("Show date and place"),
              value: () => screensaver.showInfo ? qsTr("On") : qsTr("Off"),
              change: d => screensaver.showInfo = !screensaver.showInfo,
              activate: () => screensaver.showInfo = !screensaver.showInfo },
        ],
        // Photos (the viewer)
        [
            { label: qsTr("Transition"),
              value: () => transitionName(viewer.transition),
              change: d => viewer.transition = cycle(transitionKinds, viewer.transition, d) },
            { label: qsTr("Background"),
              value: () => backgroundName(viewer.background),
              change: d => viewer.background = cycle(backgroundKinds, viewer.background, d) },
        ],
        // YouTube: what SponsorBlock skips
        [
            skipOption(qsTr("Skip sponsors"), "sponsor"),
            skipOption(qsTr("Skip self-promotion"), "selfpromo"),
            skipOption(qsTr("Skip like and subscribe reminders"), "interaction"),
            skipOption(qsTr("Skip intros"), "intro"),
            skipOption(qsTr("Skip outros and credits"), "outro"),
            skipOption(qsTr("Skip recaps and previews"), "preview"),
            skipOption(qsTr("Skip non-music parts of music videos"), "music_offtopic"),
            skipOption(qsTr("Skip filler and tangents"), "filler"),
        ],
    ]

    Keys.onBackPressed: options.activeFocus ? categories.forceActiveFocus() : page.closed()
    Keys.onEscapePressed: options.activeFocus ? categories.forceActiveFocus() : page.closed()

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

    ListView {
        id: categories

        anchors.left: parent.left
        anchors.top: heading.bottom
        anchors.bottom: parent.bottom
        anchors.margins: 48
        width: 400
        spacing: 8
        focus: true
        model: [qsTr("Power"), qsTr("Screensaver"), qsTr("Photos"), qsTr("YouTube")]

        delegate: SettingRow {
            required property string modelData
            required property int index

            width: ListView.view.width
            height: 72
            label: modelData
            current: categories.currentIndex === index
            focused: current && categories.activeFocus
        }

        Keys.onRightPressed: options.forceActiveFocus()
        Keys.onReturnPressed: options.forceActiveFocus()
        Keys.onEnterPressed: options.forceActiveFocus()
    }

    ListView {
        id: options

        anchors.left: categories.right
        anchors.right: parent.right
        anchors.top: heading.bottom
        anchors.bottom: parent.bottom
        anchors.margins: 48
        spacing: 8
        keyNavigationWraps: false
        model: page.categoryOptions[categories.currentIndex]

        delegate: SettingRow {
            required property var modelData

            width: ListView.view.width
            label: modelData.label
            value: modelData.value()
            current: ListView.isCurrentItem && options.activeFocus
            focused: current
        }

        Keys.onLeftPressed: { const o = model[currentIndex]; if (o.change) o.change(-1) }
        Keys.onRightPressed: { const o = model[currentIndex]; if (o.change) o.change(1) }
        Keys.onReturnPressed: { const o = model[currentIndex]; if (o.activate) o.activate() }
        Keys.onEnterPressed: { const o = model[currentIndex]; if (o.activate) o.activate() }
    }

    AlbumPicker {
        id: albumPicker

        anchors.fill: parent
        immich: page.immich
        screensaver: page.screensaver

        onClosed: options.forceActiveFocus()
    }
}
