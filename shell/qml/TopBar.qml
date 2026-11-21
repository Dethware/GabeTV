import QtQuick

// The bar above the places. For now: a title and a Settings entry.
FocusScope {
    id: bar

    property string title

    signal settingsActivated()

    implicitHeight: 120

    Text {
        anchors.left: parent.left
        anchors.leftMargin: 48
        anchors.verticalCenter: parent.verticalCenter
        color: "white"
        font.pixelSize: 48
        text: bar.title
    }

    Rectangle {
        id: settingsButton

        anchors.right: parent.right
        anchors.rightMargin: 48
        anchors.verticalCenter: parent.verticalCenter
        width: settingsLabel.implicitWidth + 64
        height: 72
        radius: 8
        focus: true
        color: activeFocus ? "white" : "#1d252c"

        Accessible.role: Accessible.Button
        Accessible.name: settingsLabel.text

        Text {
            id: settingsLabel
            anchors.centerIn: parent
            color: settingsButton.activeFocus ? "#101418" : "white"
            font.pixelSize: 32
            text: qsTr("Settings")
        }

        Keys.onReturnPressed: bar.settingsActivated()
        Keys.onEnterPressed: bar.settingsActivated()
    }
}
