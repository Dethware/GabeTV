import QtQuick

// A focusable text button in the top bar.
Rectangle {
    id: button

    property string text
    // The tab whose place is shown.
    property bool selected

    width: label.implicitWidth + 64
    height: 72
    radius: 8
    color: activeFocus ? "white" : selected ? "#2a343d" : "transparent"

    Accessible.role: Accessible.Button
    Accessible.name: text

    Text {
        id: label
        anchors.centerIn: parent
        color: button.activeFocus ? "#101418" : "white"
        font.pixelSize: 36
        text: button.text
    }
}
