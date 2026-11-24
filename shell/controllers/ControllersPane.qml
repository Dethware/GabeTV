pragma ComponentBehavior: Bound

import QtQuick
import TvShell.Common
import TvShell.Controllers

// The Controllers category of the settings, shown in place of the options: how to add a
// controller, and the controllers that are connected. Like a Switch's "change grip and
// order" screen, the controller service searches for controllers in sync mode while the
// focus is in here.
//
// Each line has two parts (ControllerRow). Up and Down pick a line, Left and Right a part.
// OK on the body offers what can be done with the controller. OK on the handle grabs it:
// then Up and Down move the controller along the player numbers, one step a press, until
// OK or Back lets go. Reordering takes that deliberate grab because a stray push of a
// stick must not reassign the players.
FocusScope {
    id: pane

    required property ControllerClient controllers
    // The shell is awake: no searching behind a screensaver.
    property bool awake: true
    // The dialog covers this item, usually the whole settings page.
    property Item overlay: pane

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
    // A controller can only trade places with another one.
    readonly property bool canMove: controllers.units.length > 1

    // The selected line, by its first Joy-Con: the list is rebuilt on every change, and a
    // controller that moves must keep the selection.
    property string selectedAddress
    readonly property int selectedIndex: rows.findIndex(row => row.joyCons[0].address === selectedAddress)
    readonly property var selectedRow: selectedIndex >= 0 ? rows[selectedIndex] : null
    // The handle of the selected line has the focus, not its body.
    property bool onHandle: false
    // The handle is grabbed: Up and Down move the controller.
    property bool holding: false
    // A move was asked for and the list hasn't changed yet: no second one on top.
    property bool moving: false
    // The line the dialog is about.
    property var chosenRow: null

    // The motion test of this unit was asked for.
    signal testRequested(var unit)

    function select(index) {
        if (index < 0 || index >= rows.length)
            return
        selectedAddress = rows[index].joyCons[0].address
        if (rows[index].unit === null || !canMove)
            onHandle = false
    }

    // Trades the player number of the held controller with the one before (-1) or after
    // (1) it.
    function move(direction) {
        const other = rows[selectedIndex + direction]
        if (moving || !selectedRow || !other || other.unit === null)
            return
        moving = true
        moveTimeout.restart()
        controllers.swapPlayers(selectedRow.unit.player, other.unit.player)
    }

    function batteryText(joyCon) {
        const levels = [qsTr("empty"), qsTr("critical"), qsTr("low"), qsTr("medium"), qsTr("full")]
        const level = levels[Math.max(0, Math.min(4, joyCon.battery))]
        return joyCon.charging ? qsTr("%1, charging").arg(level) : level
    }

    function rowLabel(row) {
        const names = row.joyCons.map(joyCon => joyCon.name).join(" + ")
        if (row.unit === null)
            return qsTr("%1, waiting").arg(names)
        return row.unit.kind === "sideways" ? qsTr("%1, sideways").arg(names) : names
    }

    function act(row) {
        chosenRow = row
        // A Pro Controller has one grip; only Joy-Cons can be regrouped.
        const regroup = row.unit !== null && row.unit.kind !== "pro"
        actions.title = row.joyCons.map(joyCon => joyCon.name).join(" + ")
        const choices = []
        if (row.unit !== null)
            choices.push(qsTr("Motion test"))
        if (regroup)
            choices.push(qsTr("Change grip"))
        choices.push(qsTr("Switch off"), qsTr("Cancel"))
        actions.choices = choices
        actions.open()
    }

    onVisibleChanged: if (visible) controllers.refresh()
    onSearchWantedChanged: searchWanted ? controllers.startSearch() : controllers.stopSearch()
    onRowsChanged: {
        moving = false
        // The selected controller is gone (or nothing was selected): take a line nearby.
        let index = rows.findIndex(row => row.joyCons[0].address === selectedAddress)
        if (index < 0) {
            index = Math.min(Math.max(list.currentIndex, 0), rows.length - 1)
            select(index)
        }
        if (index < 0 || rows[index].unit === null || !canMove) {
            onHandle = false
            holding = false
        }
        // After the list has taken the new lines.
        Qt.callLater(syncList)
    }
    onSelectedIndexChanged: syncList()

    // The list only follows the selection, for scrolling.
    function syncList() {
        list.currentIndex = selectedIndex
    }

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

    // If a move changes nothing, don't stay blocked.
    Timer {
        id: moveTimeout

        interval: 1000
        onTriggered: pane.moving = false
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
            color: pane.holding ? "#4fc3f7" : "#b0bec5"
            font.pixelSize: 28
            text: !pane.controllers.available ? qsTr("The controller service isn't running.")
                  : pane.holding ? qsTr("Up and Down move this controller. OK when it is where it should be.")
                  : pane.onHandle ? qsTr("OK grabs this controller to change its player number.")
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
        // The selection is the pane's, kept by address; the list only follows it.
        keyNavigationEnabled: false
        highlightMoveDuration: 120

        delegate: ControllerRow {
            required property var modelData
            required property int index

            readonly property bool selected: list.activeFocus && index === pane.selectedIndex

            width: ListView.view.width
            player: modelData.unit !== null ? modelData.unit.player : 0
            waiting: modelData.unit === null
            label: pane.rowLabel(modelData)
            battery: modelData.joyCons.map(joyCon => pane.batteryText(joyCon)).join(" / ")
            movable: modelData.unit !== null && pane.canMove
            handleFocused: selected && pane.onHandle
            held: selected && pane.holding
            bodyFocused: selected && !pane.onHandle
        }

        onActiveFocusChanged: {
            if (!activeFocus)
                pane.holding = false // letting go of the focus lets go of the controller
            else if (pane.selectedIndex < 0)
                pane.select(0)
        }

        Keys.onUpPressed: event => {
            if (!pane.holding)
                pane.select(pane.selectedIndex - 1)
            else if (!event.isAutoRepeat)
                pane.move(-1)
        }
        Keys.onDownPressed: event => {
            if (!pane.holding)
                pane.select(pane.selectedIndex + 1)
            else if (!event.isAutoRepeat)
                pane.move(1)
        }
        Keys.onLeftPressed: {
            if (!pane.holding && pane.selectedRow && pane.selectedRow.unit !== null && pane.canMove)
                pane.onHandle = true
        }
        Keys.onRightPressed: if (!pane.holding) pane.onHandle = false
        Keys.onPressed: event => {
            const ok = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
            const back = event.key === Qt.Key_Back || event.key === Qt.Key_Escape
            if (pane.holding) {
                // Either way it stays where it was moved to.
                if (ok || back) {
                    pane.holding = false
                    event.accepted = true
                }
            } else if (ok && !event.isAutoRepeat && pane.selectedRow) {
                if (pane.onHandle)
                    pane.holding = true
                else
                    pane.act(pane.selectedRow)
                event.accepted = true
            }
        }
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
            // The dialog has closed by now (it closes first, then says what was chosen),
            // and the list has the focus back; the test takes it from there.
            if (choice === qsTr("Motion test")) {
                pane.testRequested(row.unit)
            } else if (choice === qsTr("Change grip")) {
                pane.controllers.endUnit(row.unit.player)
            } else if (choice === qsTr("Switch off")) {
                for (const joyCon of row.joyCons)
                    pane.controllers.switchOff(joyCon.address)
            }
        }
        onClosed: list.forceActiveFocus()
    }
}
