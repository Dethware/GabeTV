pragma ComponentBehavior: Bound

import QtQuick
import TvShell

// The Controllers category of the settings, shown in place of the options: how to add a
// controller, and the controllers that are connected. Like a Switch's "change grip and
// order" screen, the controller service searches for controllers in sync mode while the
// focus is in here. OK on a line offers what can be done with it.
FocusScope {
    id: pane

    required property ControllerClient controllers
    // The shell is awake: no searching behind a screensaver.
    property bool awake: true
    // The dialog covers this item, usually the whole settings page.
    property Item overlay: pane

    // Left from the list: back to the categories.
    signal leftPressed()

    // One line per controller, then one per Joy-Con that is connected but waiting.
    readonly property var rows: {
        const rows = []
        for (const unit of controllers.units)
            rows.push({ unit: unit, joyCons: unit.joyCons })
        for (const joyCon of controllers.waiting)
            rows.push({ unit: null, joyCons: [joyCon] })
        return rows
    }
    // Searching takes radio time from the controllers in use, so only while someone is in here.
    readonly property bool searchWanted: visible && awake && (list.activeFocus || actions.visible)
    // The line the dialog is about.
    property var chosenRow: null

    function batteryText(joyCon) {
        const levels = [qsTr("empty"), qsTr("critical"), qsTr("low"), qsTr("medium"), qsTr("full")]
        const level = levels[Math.max(0, Math.min(4, joyCon.battery))]
        return joyCon.charging ? qsTr("%1, charging").arg(level) : level
    }

    function rowLabel(row) {
        const names = row.joyCons.map(joyCon => joyCon.name).join(" + ")
        if (row.unit === null)
            return qsTr("%1, waiting").arg(names)
        const player = qsTr("Player %1").arg(row.unit.player)
        return row.unit.kind === "sideways" ? qsTr("%1:  %2, sideways").arg(player).arg(names)
                                            : qsTr("%1:  %2").arg(player).arg(names)
    }

    function act(row) {
        chosenRow = row
        // A Pro Controller has one grip; only Joy-Cons can be regrouped.
        const regroup = row.unit !== null && row.unit.kind !== "pro"
        actions.title = row.joyCons.map(joyCon => joyCon.name).join(" + ")
        actions.choices = regroup ? [qsTr("Change grip"), qsTr("Switch off"), qsTr("Cancel")]
                                  : [qsTr("Switch off"), qsTr("Cancel")]
        actions.open()
    }

    onVisibleChanged: if (visible) controllers.refresh()
    onSearchWantedChanged: searchWanted ? controllers.startSearch() : controllers.stopSearch()

    // The service ends a search after two minutes; this keeps it going.
    Timer {
        interval: 60000
        repeat: true
        running: pane.searchWanted
        onTriggered: pane.controllers.startSearch()
    }

    // Battery levels aren't announced.
    Timer {
        interval: 5000
        repeat: true
        running: pane.visible
        onTriggered: pane.controllers.refresh()
    }

    Column {
        id: help

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: 12

        Text {
            width: parent.width
            color: "white"
            font.pixelSize: 28
            wrapMode: Text.Wrap
            text: qsTr("To add a controller, come in here and hold its small sync button until the lights sweep.")
        }

        Text {
            width: parent.width
            color: "white"
            font.pixelSize: 28
            wrapMode: Text.Wrap
            text: qsTr("Then show how you hold it: L and R on two Joy-Cons for a pair, or SL and SR on one to hold it sideways.")
        }

        Text {
            width: parent.width
            height: 40
            color: "#b0bec5"
            font.pixelSize: 28
            text: !pane.controllers.available ? qsTr("The controller service isn't running.")
                  : pane.controllers.searching ? qsTr("Searching for new controllers…") : ""
        }
    }

    ListView {
        id: list

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: help.bottom
        anchors.bottom: parent.bottom
        anchors.topMargin: 16
        spacing: 8
        clip: true
        focus: true
        model: pane.rows

        delegate: SettingRow {
            required property var modelData

            width: ListView.view.width
            label: pane.rowLabel(modelData)
            value: modelData.joyCons.map(joyCon => pane.batteryText(joyCon)).join(" / ")
            adjustable: false
            current: ListView.isCurrentItem && list.activeFocus
            focused: current
        }

        Keys.onLeftPressed: pane.leftPressed()
        Keys.onReturnPressed: if (currentIndex >= 0 && currentIndex < count) pane.act(model[currentIndex])
        Keys.onEnterPressed: if (currentIndex >= 0 && currentIndex < count) pane.act(model[currentIndex])
    }

    Text {
        anchors.left: list.left
        anchors.right: list.right
        anchors.top: list.top
        anchors.topMargin: 24
        visible: list.count === 0
        color: "#b0bec5"
        font.pixelSize: 28
        wrapMode: Text.Wrap
        text: qsTr("No controller is connected. Press a button on one that has been added before.")
    }

    ChoiceDialog {
        id: actions

        parent: pane.overlay
        anchors.fill: parent

        onChosen: index => {
            const row = pane.chosenRow
            const choice = choices[index]
            if (choice === qsTr("Change grip")) {
                pane.controllers.endUnit(row.unit.player)
            } else if (choice === qsTr("Switch off")) {
                for (const joyCon of row.joyCons)
                    pane.controllers.switchOff(joyCon.address)
            }
        }
        onClosed: list.forceActiveFocus()
    }
}
