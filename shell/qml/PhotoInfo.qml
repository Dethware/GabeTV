import QtQuick
import QtQuick.Effects

// A photo's date and place, on two lines, outlined and with a dark halo so they read over any
// photo. Used by the screensaver and the viewer. Hidden when the asset has neither.
Column {
    id: info

    // An asset map from ImmichClient (`date` "yyyy-MM-dd", `place` "City, Country").
    property var asset: null
    readonly property string date: asset && asset.date
                                   ? Qt.formatDate(new Date(asset.date + "T12:00:00"), "d MMMM yyyy") : ""
    readonly property string place: asset && asset.place ? asset.place : ""
    readonly property bool hasInfo: date.length > 0 || place.length > 0

    spacing: 4

    layer.enabled: visible
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowColor: "black"
        shadowBlur: 0.6
        shadowOpacity: 0.9
        shadowHorizontalOffset: 0
        shadowVerticalOffset: 0
    }

    Text {
        visible: text.length > 0
        color: "white"
        style: Text.Outline
        styleColor: "black"
        font.pixelSize: 64
        font.weight: Font.DemiBold
        text: info.date
    }

    Text {
        visible: text.length > 0
        color: "#e0e0e0"
        style: Text.Outline
        styleColor: "black"
        font.pixelSize: 48
        text: info.place
    }
}
