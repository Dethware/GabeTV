import QtQuick
import TvShell

// Opened by holding the remote's Power. Back or Cancel closes it.
FocusScope {
    id: dialog

    required property PowerActions actions

    signal closed()

    property string message

    function open() {
        message = ""
        // Start on Cancel, so a stray OK does nothing.
        choices.currentIndex = choices.count - 1
        visible = true
        forceActiveFocus()
    }

    function close() {
        visible = false
        closed()
    }

    function activate(action) {
        switch (action) {
        case "sleep":
            close()
            actions.suspend()
            break
        case "powerOff":
            close()
            actions.powerOff()
            break
        case "controllers": {
            const count = actions.turnOffControllers()
            if (count === 0)
                message = qsTr("No controllers to switch off.")
            else if (count === 1)
                message = qsTr("Switched off 1 controller.")
            else
                message = qsTr("Switched off %1 controllers.").arg(count)
            break
        }
        default:
            close()
        }
    }

    visible: false

    Keys.onBackPressed: close()
    Keys.onEscapePressed: close()

    Rectangle {
        anchors.fill: parent
        color: "#d0000000"
    }

    Rectangle {
        anchors.centerIn: parent
        width: 900
        height: content.implicitHeight + 96
        radius: 16
        color: "#1d252c"

        Column {
            id: content

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 48
            spacing: 24

            Text {
                color: "white"
                font.pixelSize: 48
                text: qsTr("Power")
            }

            ListView {
                id: choices

                width: parent.width
                height: contentHeight
                spacing: 8
                focus: true
                interactive: false
                // Off by default when not interactive.
                keyNavigationEnabled: true

                model: [
                    { action: "sleep", label: qsTr("Sleep") },
                    { action: "powerOff", label: qsTr("Power off") },
                    { action: "controllers", label: qsTr("Turn off controllers") },
                    { action: "cancel", label: qsTr("Cancel") },
                ]

                delegate: Rectangle {
                    id: choice

                    required property var modelData

                    readonly property bool current: ListView.isCurrentItem

                    width: ListView.view.width
                    height: 80
                    radius: 8
                    color: current ? "white" : "#2a343d"

                    Accessible.role: Accessible.MenuItem
                    Accessible.name: modelData.label

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 32
                        anchors.verticalCenter: parent.verticalCenter
                        color: choice.current ? "#101418" : "white"
                        font.pixelSize: 32
                        text: choice.modelData.label
                    }
                }

                Keys.onReturnPressed: dialog.activate(model[currentIndex].action)
                Keys.onEnterPressed: dialog.activate(model[currentIndex].action)
            }

            Text {
                width: parent.width
                visible: dialog.message.length > 0
                color: "#b0bec5"
                font.pixelSize: 28
                wrapMode: Text.Wrap
                text: dialog.message
            }
        }
    }
}
