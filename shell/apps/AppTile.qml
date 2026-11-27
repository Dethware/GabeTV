pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects

// An app in a place's grid, in the same frame as the game covers: its logo (a component,
// drawn) above its name, or a picture of its own (a Switch game's icon) across the top of the
// tile with the name under it.
Item {
    id: tile

    required property string appId
    required property string name
    property Component logo
    // A square picture in place of a drawn logo; used when set.
    property url icon
    // The app is loaded: a mark in the tile's corner.
    property bool running: false

    readonly property bool hasIcon: icon.toString() !== ""

    width: GridView.view.cellWidth
    height: GridView.view.cellHeight

    Rectangle {
        id: frame

        anchors.fill: parent
        anchors.margins: 16
        radius: 8
        color: "#1d252c"
        border.color: "white"
        border.width: tile.GridView.isCurrentItem && tile.GridView.view.activeFocus ? 6 : 0

        Column {
            anchors.centerIn: parent
            width: parent.width * 0.6
            spacing: 32
            visible: !tile.hasIcon

            // A square box for the logo, whatever its shape, so the names line up across tiles.
            Item {
                width: parent.width
                height: width

                Loader {
                    anchors.centerIn: parent
                    width: parent.width
                    active: !tile.hasIcon
                    sourceComponent: tile.logo
                    onLoaded: item.width = width
                }
            }

            Text {
                width: parent.width
                color: "white"
                font.pixelSize: 36
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: tile.name
            }
        }

        // The picture: square, the tile's full width (inside the focus border, like a cover),
        // its corners rounded like the tile's.
        Image {
            id: iconImage

            x: frame.border.width
            y: frame.border.width
            width: frame.width - 2 * frame.border.width
            height: width
            visible: tile.hasIcon
            source: tile.icon
            fillMode: Image.PreserveAspectCrop
            smooth: true
            mipmap: true
            asynchronous: true

            layer.enabled: tile.hasIcon
            layer.effect: MultiEffect {
                maskEnabled: true
                maskSource: iconMask
                maskThresholdMin: 0.5
                maskSpreadAtMin: 1.0
            }
        }

        Item {
            id: iconMask

            width: iconImage.width
            height: iconImage.height
            visible: false
            layer.enabled: true

            Rectangle {
                anchors.fill: parent
                radius: Math.max(0, frame.radius - frame.border.width / 2)
            }
        }

        Text {
            anchors.top: iconImage.bottom
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 16
            visible: tile.hasIcon
            color: "white"
            font.pixelSize: 36
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
            text: tile.name
        }

        // Loaded and waiting: a dot in the corner, over the picture too.
        Rectangle {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 24
            width: 20
            height: 20
            radius: 10
            color: "#4fc3f7"
            border.color: "#1d252c"
            border.width: tile.hasIcon ? 3 : 0
            visible: tile.running
        }
    }
}
