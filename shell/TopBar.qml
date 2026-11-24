import QtQuick
import TvShell.Audio
import TvShell.Common

// The bar above the places: a tab per place and Settings. Left/right only move the highlight;
// OK switches to the highlighted place (the highlight stays on the tab; Down goes into it).
// While something plays in the background, it sits left of Settings; OK there toggles pause.
FocusScope {
    id: bar

    property var tabs: []
    property int currentTab: 0

    // What plays in the background ("" = nothing; then the item is hidden).
    property string nowPlayingSource
    property string nowPlayingTitle
    property string nowPlayingArtist
    property bool nowPlayingPaused

    signal tabActivated(int index)
    signal settingsActivated()
    signal nowPlayingActivated()

    // The controller's shoulder buttons: switch directly to the previous/next place.
    function step(direction) {
        const next = currentTab + direction
        if (next >= 0 && next < tabs.length)
            currentTab = next
    }

    implicitHeight: 120

    Row {
        id: tabRow

        anchors.left: parent.left
        anchors.leftMargin: 32
        anchors.verticalCenter: parent.verticalCenter
        spacing: 16

        Repeater {
            id: tabRepeater

            model: bar.tabs

            delegate: BarButton {
                required property string modelData
                required property int index

                text: modelData
                selected: bar.currentTab === index
                // Entering the bar lands on the current place's tab.
                focus: bar.currentTab === index

                KeyNavigation.right: index + 1 < tabRepeater.count ? tabRepeater.itemAt(index + 1)
                                     : nowPlaying.visible ? nowPlaying : settingsButton
                KeyNavigation.left: index > 0 ? tabRepeater.itemAt(index - 1) : null
                Keys.onReturnPressed: {
                    bar.currentTab = index
                    bar.tabActivated(index)
                }
                Keys.onEnterPressed: {
                    bar.currentTab = index
                    bar.tabActivated(index)
                }
            }
        }
    }

    NowPlayingButton {
        id: nowPlaying

        anchors.right: settingsButton.left
        anchors.rightMargin: 32
        anchors.verticalCenter: parent.verticalCenter
        visible: bar.nowPlayingTitle.length > 0
        source: bar.nowPlayingSource
        title: bar.nowPlayingTitle
        artist: bar.nowPlayingArtist
        paused: bar.nowPlayingPaused

        KeyNavigation.left: tabRepeater.count > 0 ? tabRepeater.itemAt(tabRepeater.count - 1) : null
        KeyNavigation.right: settingsButton
        Keys.onReturnPressed: bar.nowPlayingActivated()
        Keys.onEnterPressed: bar.nowPlayingActivated()
        // It can go away while highlighted.
        onVisibleChanged: if (!visible && activeFocus) settingsButton.forceActiveFocus()
    }

    BarButton {
        id: settingsButton

        anchors.right: parent.right
        anchors.rightMargin: 48
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("Settings")

        KeyNavigation.left: nowPlaying.visible ? nowPlaying
                            : tabRepeater.count > 0 ? tabRepeater.itemAt(tabRepeater.count - 1) : null
        Keys.onReturnPressed: bar.settingsActivated()
        Keys.onEnterPressed: bar.settingsActivated()
    }
}
