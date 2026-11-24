import QtQuick

// One line of the controllers list, in two parts that take the focus separately:
//   the handle   the player lights as the controller shows them. OK grabs it; while it is
//                held, Up and Down move the controller along the player numbers;
//   the body     what the controller is made of and its batteries. OK offers what can be
//                done with it.
// A Joy-Con that is connected but waiting has no handle, only its flashing lights.
Item {
    id: row

    property int player: 0
    // Connected, not yet part of a controller: the lights flash.
    property bool waiting: false
    property string label
    property string battery
    // There is another controller to trade places with.
    property bool movable: false
    property bool handleFocused: false
    property bool held: false
    property bool bodyFocused: false

    height: 88

    Accessible.role: Accessible.ListItem
    Accessible.name: (waiting ? qsTr("Waiting") : qsTr("Player %1").arg(player)) + ": " + label + ", " + battery

    Rectangle {
        id: handle

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 184
        radius: 8
        color: row.held ? "#4fc3f7" : row.handleFocused ? "white" : "#1d252c"

        Row {
            anchors.centerIn: parent
            spacing: 16

            // The grip: there is something to move here. Keeps its place when hidden, so
            // the lights line up in every row.
            Text {
                anchors.verticalCenter: parent.verticalCenter
                opacity: row.movable ? 1 : 0
                color: row.held || row.handleFocused ? "#101418" : "#b0bec5"
                font.pixelSize: 32
                text: row.held ? "▲▼" : "↕"
                width: 44
                horizontalAlignment: Text.AlignHCenter
            }

            PlayerLights {
                anchors.verticalCenter: parent.verticalCenter
                player: row.player
                flashing: row.waiting
                onLight: row.held || row.handleFocused
            }
        }
    }

    Rectangle {
        anchors.left: handle.right
        anchors.leftMargin: 8
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        radius: 8
        color: row.bodyFocused ? "white" : "#1d252c"

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 32
            anchors.right: batteryText.left
            anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            color: row.bodyFocused ? "#101418" : "white"
            font.pixelSize: 32
            elide: Text.ElideRight
            text: row.label
        }

        Text {
            id: batteryText

            anchors.right: parent.right
            anchors.rightMargin: 32
            anchors.verticalCenter: parent.verticalCenter
            color: row.bodyFocused ? "#101418" : "#b0bec5"
            font.pixelSize: 32
            text: row.battery
        }
    }
}
