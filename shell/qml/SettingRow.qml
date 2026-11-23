import QtQuick

// One line in the settings: label on the left, value (with ‹ › when focused) on the right.
// `current` marks the selected line (e.g. the open category), `focused` the one with focus.
Rectangle {
    id: row

    property string label
    property string value
    property bool current
    property bool focused: current
    // Left and Right change the value: show ‹ › around it when focused.
    property bool adjustable: true

    height: 88
    radius: 8
    color: focused ? "white" : current ? "#2a343d" : "#1d252c"

    Accessible.role: Accessible.ListItem
    Accessible.name: value.length > 0 ? label + ": " + value : label

    Text {
        anchors.left: parent.left
        anchors.leftMargin: 32
        anchors.verticalCenter: parent.verticalCenter
        color: row.focused ? "#101418" : "white"
        font.pixelSize: 32
        text: row.label
    }

    Text {
        anchors.right: parent.right
        anchors.rightMargin: 32
        anchors.verticalCenter: parent.verticalCenter
        visible: row.value.length > 0
        color: row.focused ? "#101418" : "#b0bec5"
        font.pixelSize: 32
        text: row.focused && row.adjustable ? "‹  " + row.value + "  ›" : row.value
    }
}
