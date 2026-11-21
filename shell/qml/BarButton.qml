import QtQuick

// A focusable text button in the top bar. Focus: white fill. Selected (the view that's
// showing): coloured underline. Both can apply at once.
Rectangle {
    id: button

    property string text
    property bool selected

    width: label.implicitWidth + 64
    height: 72
    radius: 8
    color: activeFocus ? "white" : "transparent"

    Accessible.role: Accessible.PageTab
    Accessible.name: text
    Accessible.selected: selected

    Text {
        id: label
        anchors.centerIn: parent
        color: button.activeFocus ? "#101418" : button.selected ? "white" : "#90a4ae"
        font.pixelSize: 36
        font.bold: button.selected
        text: button.text
    }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: -10
        width: label.implicitWidth
        height: 6
        radius: 3
        color: "#4fc3f7"
        visible: button.selected
    }
}
