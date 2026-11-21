import QtQuick

// One option in the settings: label on the left, value with ‹ › on the right.
Rectangle {
    id: row

    property string label
    property string value
    property bool current

    signal decreased()
    signal increased()

    height: 88
    radius: 8
    color: current ? "white" : "#1d252c"

    Accessible.role: Accessible.ListItem
    Accessible.name: label + ": " + value

    Text {
        anchors.left: parent.left
        anchors.leftMargin: 32
        anchors.verticalCenter: parent.verticalCenter
        color: row.current ? "#101418" : "white"
        font.pixelSize: 32
        text: row.label
    }

    Text {
        anchors.right: parent.right
        anchors.rightMargin: 32
        anchors.verticalCenter: parent.verticalCenter
        color: row.current ? "#101418" : "#b0bec5"
        font.pixelSize: 32
        text: row.current ? "‹  " + row.value + "  ›" : row.value
    }
}
