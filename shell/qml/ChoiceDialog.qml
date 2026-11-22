import QtQuick

// A question with a few answers, over everything. OK picks the highlighted answer (chosen),
// Back closes it without one. Either way it closes (closed).
FocusScope {
    id: dialog

    property string title
    property string message
    // The answers' labels.
    property var choices: []

    signal chosen(int index)
    signal closed()

    function open() {
        list.currentIndex = 0
        visible = true
        forceActiveFocus()
    }

    function close() {
        if (!visible)
            return
        visible = false
        closed()
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
                width: parent.width
                color: "white"
                font.pixelSize: 48
                wrapMode: Text.Wrap
                text: dialog.title
            }

            Text {
                width: parent.width
                visible: dialog.message.length > 0
                color: "#b0bec5"
                font.pixelSize: 30
                wrapMode: Text.Wrap
                text: dialog.message
            }

            ListView {
                id: list

                width: parent.width
                height: contentHeight
                spacing: 8
                focus: true
                interactive: false
                // Off by default when not interactive.
                keyNavigationEnabled: true
                model: dialog.choices

                delegate: Rectangle {
                    id: choice

                    required property string modelData

                    readonly property bool current: ListView.isCurrentItem

                    width: ListView.view.width
                    height: 80
                    radius: 8
                    color: current ? "white" : "#2a343d"

                    Accessible.role: Accessible.MenuItem
                    Accessible.name: modelData

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 32
                        anchors.verticalCenter: parent.verticalCenter
                        color: choice.current ? "#101418" : "white"
                        font.pixelSize: 32
                        text: choice.modelData
                    }
                }

                Keys.onReturnPressed: { const index = currentIndex; dialog.close(); dialog.chosen(index) }
                Keys.onEnterPressed: { const index = currentIndex; dialog.close(); dialog.chosen(index) }
            }
        }
    }
}
