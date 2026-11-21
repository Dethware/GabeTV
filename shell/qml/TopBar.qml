import QtQuick

// The bar above the places: tabs for the places (moving onto one switches to it) and Settings.
FocusScope {
    id: bar

    property var tabs: []
    property int currentTab: 0

    signal settingsActivated()

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
                focus: bar.currentTab === index

                onActiveFocusChanged: if (activeFocus) bar.currentTab = index
                KeyNavigation.right: index + 1 < tabRepeater.count ? tabRepeater.itemAt(index + 1) : settingsButton
                KeyNavigation.left: index > 0 ? tabRepeater.itemAt(index - 1) : null
            }
        }
    }

    BarButton {
        id: settingsButton

        anchors.right: parent.right
        anchors.rightMargin: 48
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("Settings")

        KeyNavigation.left: tabRepeater.count > 0 ? tabRepeater.itemAt(tabRepeater.count - 1) : null
        Keys.onReturnPressed: bar.settingsActivated()
        Keys.onEnterPressed: bar.settingsActivated()
    }
}
