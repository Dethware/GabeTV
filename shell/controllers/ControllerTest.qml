pragma ComponentBehavior: Bound

import QtQuick
import QtQuick3D
import TvShell.Controllers

// The motion test of one controller: each of its parts drawn in 3D in a box, turning as
// it is turned and pushed off the middle by as much as it is being pushed, with the
// readings next to it. It reads the motion as an emulator would, from the controller
// service's cemuhook server, so what it shows is what an emulator gets.
// Two actions, picked with Left and Right and run with OK: Recenter lines the picture up
// with the controller as it lies; Calibrate has the service measure the sensors at rest
// and correct what it passes on from then on. Both wait a moment first, because pressing
// OK on the controller itself shakes it. Back closes.
// While it is open the service looks for a Ring-Con on a right Joy-Con and reads it; its
// flex is shown raw, with the value at the last Recenter as the resting point.
FocusScope {
    id: test

    required property ControllerClient controllers
    // The unit from ControllerClient: { player, kind, joyCons }.
    property var unit: null
    property bool opened: false

    signal closed()

    readonly property var joyCons: unit !== null ? unit.joyCons : []
    // The motion server's pads: two for each of players 1 and 2.
    readonly property bool served: unit !== null && unit.player <= 2

    // 0: Recenter, 1: Calibrate.
    property int action: 0
    // What is going on or how it went, shown under the actions.
    property string message
    // Controllers still being measured.
    property int measuring: 0

    function open(unit) {
        test.unit = unit
        action = 0
        message = ""
        opened = true
        forceActiveFocus()
    }

    function run() {
        if (settle.running || measuring > 0)
            return
        message = action === 0 ? qsTr("Hold it still…") : qsTr("Don't touch it: measuring in a moment…")
        settle.restart()
    }

    // After the press has stopped shaking the controller.
    function runNow() {
        if (action === 0) {
            recenter()
            message = ""
            return
        }
        message = qsTr("Measuring for two seconds…")
        measuring = joyCons.length
        results = []
        for (const joyCon of joyCons)
            controllers.calibrateMotion(joyCon.address)
    }

    property var results: []

    function calibrated(address, ok, strength, drift) {
        const joyCon = joyCons.find(candidate => candidate.address === address)
        if (!joyCon || measuring === 0)
            return
        results.push(ok ? qsTr("%1: calibrated. At rest it showed %2 g and turned %3°/s; both are corrected from now on.")
                              .arg(joyCon.name).arg(strength.toFixed(3)).arg(drift.toFixed(2))
                        : qsTr("%1: not calibrated. It moved, or the reading made no sense. Lay it on a table and try again.")
                              .arg(joyCon.name))
        measuring -= 1
        if (measuring === 0) {
            message = results.join("\n")
            recenter()
        }
    }

    Connections {
        target: test.controllers

        function onMotionCalibrated(address, ok, strength, drift) {
            test.calibrated(address, ok, strength, drift)
        }
    }

    Timer {
        id: settle

        interval: 900
        onTriggered: test.runNow()
    }

    function close() {
        if (!opened)
            return
        opened = false
        closed()
    }

    function number(value, digits) {
        return (value >= 0 ? "+" : "") + value.toFixed(digits)
    }

    visible: opened

    Keys.onBackPressed: close()
    Keys.onEscapePressed: close()
    Keys.onReturnPressed: run()
    Keys.onEnterPressed: run()
    Keys.onLeftPressed: action = 0
    Keys.onRightPressed: action = 1

    signal recentered()

    function recenter() {
        recentered()
        for (let i = 0; i < joyCons.length; ++i)
            motion.pad(firstPad + i).recenter()
        const marks = {}
        for (const joyCon of joyCons)
            marks[joyCon.address] = lostNow(joyCon.address)
        lostAtRecenter = marks
    }

    // Reports the controller service counts as lost on the radio for a controller (their
    // readings are filled in by the service): now, and since the last recenter.
    property var lostAtRecenter: ({})

    // The controller as the service describes it now; `unit` is as it was when opened.
    function current(address) {
        for (const unit of controllers.units) {
            for (const joyCon of unit.joyCons) {
                if (joyCon.address === address)
                    return joyCon
            }
        }
        return null
    }

    function lostNow(address) {
        const joyCon = current(address)
        return joyCon !== null ? joyCon.lostReports : 0
    }

    // The service reads a ring only for as long as it keeps being asked to.
    function watchRings(watch) {
        for (const joyCon of joyCons) {
            if (joyCon.side === "right")
                controllers.watchRing(joyCon.address, watch)
        }
    }

    readonly property bool watching: opened && visible

    onWatchingChanged: watchRings(watching)

    function lostSince(address) {
        const mark = lostAtRecenter[address]
        return lostNow(address) - (mark !== undefined ? mark : 0)
    }

    // The count comes with the controllers' description, which is only fetched on request.
    Timer {
        interval: 1000
        repeat: true
        running: test.watching
        onTriggered: {
            test.controllers.refresh()
            test.watchRings(true)
        }
    }

    readonly property int firstPad: unit !== null ? 2 * (unit.player - 1) : 0

    MotionClient {
        id: motion

        running: test.opened && test.visible && test.served
    }

    Rectangle {
        anchors.fill: parent
        color: "#101418"
    }

    Text {
        id: heading

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 48
        color: "white"
        font.pixelSize: 48
        text: qsTr("Motion test")
    }

    Row {
        id: actions

        anchors.left: heading.right
        anchors.leftMargin: 64
        anchors.verticalCenter: heading.verticalCenter
        spacing: 16

        Repeater {
            model: [qsTr("Recenter"), qsTr("Calibrate")]

            delegate: Rectangle {
                id: button

                required property string modelData
                required property int index

                readonly property bool current: test.action === index

                width: label.implicitWidth + 64
                height: 64
                radius: 8
                color: current ? "white" : "#1d252c"

                Accessible.role: Accessible.Button
                Accessible.name: modelData

                Text {
                    id: label

                    anchors.centerIn: parent
                    color: button.current ? "#101418" : "white"
                    font.pixelSize: 30
                    text: button.modelData
                }
            }
        }
    }

    Text {
        id: hint

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: heading.bottom
        anchors.margins: 48
        anchors.topMargin: 24
        height: 76
        color: test.message.length > 0 ? "#ffd54f" : "#b0bec5"
        font.pixelSize: 28
        wrapMode: Text.Wrap
        text: test.message.length > 0 ? test.message
              : test.action === 0 ? qsTr("Lay the controller flat, face up, its top away from you. Recenter draws it that way. Turn it and the shape turns; push or shake it and the shape is pushed off the middle.")
                                  : qsTr("Lay the controller on a table and leave it alone. Calibrate measures what its sensors show at rest and corrects it from then on, for games too. It is remembered for this controller.")
    }

    Text {
        anchors.centerIn: parent
        visible: !test.served
        color: "#b0bec5"
        font.pixelSize: 32
        text: qsTr("Motion is only offered for players 1 and 2 so far.")
    }

    Row {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: hint.bottom
        anchors.bottom: parent.bottom
        anchors.margins: 48
        anchors.topMargin: 16
        spacing: 48
        visible: test.served

        Repeater {
            model: test.joyCons

            delegate: Item {
                id: part

                required property var modelData
                required property int index

                readonly property MotionPad pad: motion.pad(test.firstPad + index)

                // The Ring-Con on this Joy-Con's rail: its flex as reported, the value
                // at the last Recenter, and how far it has been either way since.
                readonly property var now: test.current(modelData.address)
                readonly property bool ring: now !== null && now.ring === true
                property int flex: 0
                property int flexRest: 0
                property int flexLow: 0
                property int flexHigh: 0
                property bool flexSeen: false

                onRingChanged: flexSeen = false

                Connections {
                    target: test.controllers

                    function onRingFlex(address, raw) {
                        if (address !== part.modelData.address)
                            return
                        part.flex = raw
                        if (!part.flexSeen) {
                            part.flexSeen = true
                            part.flexRest = raw
                            part.flexLow = raw
                            part.flexHigh = raw
                        }
                        part.flexLow = Math.min(part.flexLow, raw)
                        part.flexHigh = Math.max(part.flexHigh, raw)
                    }
                }

                Connections {
                    target: test

                    function onRecentered() {
                        part.flexSeen = false
                    }
                }

                width: (parent.width - (test.joyCons.length - 1) * 48) / test.joyCons.length
                height: parent.height

                View3D {
                    id: view

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: readings.top
                    anchors.bottomMargin: 24

                    environment: SceneEnvironment {
                        backgroundMode: SceneEnvironment.Color
                        clearColor: "#161c22"
                        antialiasingMode: SceneEnvironment.MSAA
                        antialiasingQuality: SceneEnvironment.High
                    }

                    PerspectiveCamera {
                        position: Qt.vector3d(0, 300, 560)
                        eulerRotation.x: -28
                    }

                    DirectionalLight {
                        eulerRotation.x: -50
                        eulerRotation.y: -25
                        ambientColor: Qt.rgba(0.45, 0.45, 0.45, 1)
                    }

                    // A box that stays put on the screen. The shape sits in its middle
                    // and is pushed off it by as much as the controller is being pushed
                    // (gravity taken out), up to the walls. It shows the force, not where
                    // the controller is.
                    Node {
                        id: box

                        // Scene units to the metre, and the box's half size: the reach
                        // of the movement plus room for the shape itself.
                        readonly property real unitsPerMetre: 400
                        readonly property real half: part.pad.reach * unitsPerMetre + 45

                        // The twelve edges: four along each axis.
                        Repeater3D {
                            model: 12

                            delegate: Model {
                                required property int index

                                readonly property int axis: Math.floor(index / 4)
                                readonly property real a: index % 2 === 0 ? -box.half : box.half
                                readonly property real b: index % 4 < 2 ? -box.half : box.half

                                source: "#Cube"
                                position: axis === 0 ? Qt.vector3d(0, a, b) : axis === 1 ? Qt.vector3d(a, 0, b)
                                                                                         : Qt.vector3d(a, b, 0)
                                scale: axis === 0 ? Qt.vector3d(box.half / 50, 0.012, 0.012)
                                       : axis === 1 ? Qt.vector3d(0.012, box.half / 50, 0.012)
                                                    : Qt.vector3d(0.012, 0.012, box.half / 50)
                                materials: PrincipledMaterial {
                                    baseColor: "#4a5863"
                                    lighting: PrincipledMaterial.NoLighting
                                }
                            }
                        }

                        ControllerModel {
                            side: part.modelData.side
                            orientation: part.pad.orientation
                            position: Qt.vector3d(part.pad.position.x, part.pad.position.y, part.pad.position.z)
                                      .times(box.unitsPerMetre)
                            scale: Qt.vector3d(0.7, 0.7, 0.7)
                        }
                    }
                }

                Column {
                    id: readings

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    spacing: 8

                    Text {
                        color: "white"
                        font.pixelSize: 32
                        text: part.modelData.name
                    }

                    Text {
                        color: part.pad.active ? "#b0bec5" : "#e8503a"
                        font.pixelSize: 28
                        text: part.pad.active ? qsTr("%1 readings a second").arg(part.pad.rate) : qsTr("No readings")
                    }

                    // The push as a bar, 0 to 4 g, with a mark at the strongest lately.
                    Row {
                        spacing: 16

                        Text {
                            width: 300
                            color: "white"
                            font.pixelSize: 28
                            text: qsTr("Push  %1 g").arg(part.pad.push.toFixed(2))
                        }

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.max(100, readings.width - 316)
                            height: 20
                            radius: 4
                            color: "#1d252c"

                            Rectangle {
                                width: parent.width * Math.min(part.pad.push, 4) / 4
                                height: parent.height
                                radius: 4
                                color: "#ffd54f"
                            }

                            Rectangle {
                                x: (parent.width - width) * Math.min(part.pad.pushPeak, 4) / 4
                                width: 4
                                height: parent.height
                                color: "white"
                                visible: part.pad.pushPeak > 0.05
                            }
                        }
                    }

                    // The ring's flex as a bar around its resting point: as wide as it
                    // has been bent so far, and at least 500 either way.
                    Row {
                        spacing: 16
                        visible: part.modelData.side === "right"

                        Text {
                            width: 300
                            color: part.ring ? "white" : "#b0bec5"
                            font.pixelSize: 28
                            text: part.ring ? qsTr("Ring-Con  %1").arg(part.flex) : qsTr("No Ring-Con")
                        }

                        Rectangle {
                            id: flexBar

                            readonly property real span: Math.max(500, part.flexHigh - part.flexRest,
                                                                  part.flexRest - part.flexLow)
                            readonly property real share: (part.flex - part.flexRest) / span

                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.max(100, readings.width - 316)
                            height: 20
                            radius: 4
                            color: "#1d252c"
                            visible: part.ring

                            Rectangle {
                                x: flexBar.width / 2 + Math.min(0, flexBar.share) * flexBar.width / 2
                                width: Math.abs(flexBar.share) * flexBar.width / 2
                                height: parent.height
                                color: "#4fc3f7"
                            }

                            Rectangle {
                                x: (parent.width - width) / 2
                                width: 4
                                height: parent.height
                                color: "white"
                            }
                        }
                    }

                    Text {
                        visible: part.ring
                        color: "#b0bec5"
                        font.pixelSize: 28
                        text: qsTr("At rest  %1      Lowest  %2      Highest  %3").arg(part.flexRest).arg(part.flexLow)
                              .arg(part.flexHigh)
                    }

                    // Two checks of the sensor itself. Lying still, the strength is 1 g
                    // if the calibration is right. Turned once round flat on the table,
                    // the turn is 360 degrees if the gyroscope's scale is right.
                    Text {
                        color: "white"
                        font.pixelSize: 28
                        text: qsTr("Strength  %1 g      Turned  %2°      Lost reports  %3").arg(part.pad.strength.toFixed(3))
                              .arg(part.pad.turned.toFixed(1)).arg(test.lostSince(part.modelData.address))
                    }

                    Text {
                        color: "#b0bec5"
                        font.pixelSize: 28
                        font.family: "monospace"
                        text: qsTr("Sensor (g)     x %1   y %2   z %3").arg(test.number(part.pad.acceleration.x, 2))
                              .arg(test.number(part.pad.acceleration.y, 2)).arg(test.number(part.pad.acceleration.z, 2))
                    }

                    Text {
                        color: "#b0bec5"
                        font.pixelSize: 28
                        font.family: "monospace"
                        text: qsTr("Turning (°/s)  x %1   y %2   z %3").arg(test.number(part.pad.rotation.x, 0))
                              .arg(test.number(part.pad.rotation.y, 0)).arg(test.number(part.pad.rotation.z, 0))
                    }
                }
            }
        }
    }
}
