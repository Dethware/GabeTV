pragma ComponentBehavior: Bound

import QtQuick
import TvShell.Photos

// The albums in groups by the year of their latest photo, newest first: a heading per year,
// under it that year's albums in rows (the albums arrive sorted, newest first).
//
// Moving between rows keeps an intended column: Left and Right set it, Up and Down go to it
// or, in a shorter row, as close to it as the row reaches. So going down through a short row
// and on into a full one lands in the column you started from. Left and Right don't stop at
// a row's ends: they continue in the neighbouring row.
FocusScope {
    id: groups

    required property ImmichClient immich
    // { id, name, count, cover, year } each, newest first.
    property var albums: []
    property real designWidth: 400
    property real designHeight: 316
    property real gap: 24

    signal albumOpened(string albumId, string name)
    // Up from the first row.
    signal topReached()

    readonly property int count: albums.length
    readonly property int columns: Math.max(1, Math.floor((width + gap) / (designWidth + gap)))
    readonly property real cardWidth: Math.floor((width - (columns - 1) * gap) / columns)
    readonly property real cardHeight: Math.round(cardWidth * designHeight / designWidth)

    // What the list shows: { heading: "2026" } or { items: [album, …] }, top to bottom.
    readonly property var rows: {
        const rows = []
        let year = null
        let row = null
        for (const album of albums) {
            if (album.year !== year) {
                year = album.year
                rows.push({ heading: year > 0 ? String(year) : qsTr("Undated") })
                row = null
            }
            if (row === null || row.items.length === columns) {
                row = { items: [] }
                rows.push(row)
            }
            row.items.push(album)
        }
        return rows
    }

    // The selected album: its row (an index into rows) and its column in that row.
    property int row: -1
    property int column: 0
    // The column Up and Down aim for.
    property int intendedColumn: 0
    // The selected album's id, to find it again when the rows are rebuilt (a refresh, or a
    // different number of columns).
    property string selectedId

    readonly property var current: row >= 0 && row < rows.length && rows[row].items
                                   ? rows[row].items[Math.min(column, rows[row].items.length - 1)] : null

    function select(newRow, newColumn) {
        row = newRow
        column = newColumn
        selectedId = current ? current.id : ""
        list.currentIndex = row
    }

    // The next row of albums in a direction (+1 down, -1 up), or -1.
    function albumRow(from, direction) {
        for (let i = from + direction; i >= 0 && i < rows.length; i += direction) {
            if (rows[i].items)
                return i
        }
        return -1
    }

    function moveRow(direction) {
        const next = albumRow(row, direction)
        if (next < 0)
            return false
        select(next, Math.min(intendedColumn, rows[next].items.length - 1))
        return true
    }

    // Left and Right. Past a row's end they carry on in the next row (its first album), and
    // before its start in the previous one (its last), across the years' headings too.
    function moveColumn(direction) {
        if (!current)
            return
        let nextRow = row
        let next = column + direction
        if (next < 0 || next >= rows[row].items.length) {
            nextRow = albumRow(row, direction)
            if (nextRow < 0)
                return
            next = direction > 0 ? 0 : rows[nextRow].items.length - 1
        }
        select(nextRow, next)
        intendedColumn = next
    }

    onRowsChanged: {
        // Stay on the same album if it is still there; otherwise the first one.
        for (let i = 0; i < rows.length; ++i) {
            const index = rows[i].items ? rows[i].items.findIndex(album => album.id === selectedId) : -1
            if (index >= 0) {
                select(i, index)
                return
            }
        }
        const first = albumRow(-1, 1)
        select(first, 0)
        intendedColumn = 0
    }

    ListView {
        id: list

        anchors.fill: parent
        focus: true
        clip: true
        model: groups.rows
        // Keys are handled here, not by the list (its rows aren't the things selected).
        keyNavigationEnabled: false
        highlightMoveDuration: 120
        // Room above the current row, so a year's heading shows with its first row.
        highlightRangeMode: ListView.ApplyRange
        preferredHighlightBegin: 72
        preferredHighlightEnd: height

        delegate: Item {
            id: rowItem

            required property var modelData
            required property int index

            readonly property bool isHeading: modelData.heading !== undefined

            width: ListView.view.width
            height: isHeading ? 72 : groups.cardHeight + groups.gap

            Text {
                id: heading

                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                visible: rowItem.isHeading
                color: "white"
                font.pixelSize: 36
                text: rowItem.isHeading ? rowItem.modelData.heading : ""
            }

            Rectangle {
                anchors.left: heading.right
                anchors.right: parent.right
                anchors.leftMargin: 24
                anchors.verticalCenter: parent.verticalCenter
                height: 2
                visible: rowItem.isHeading
                color: "#2a343d"
            }

            Row {
                visible: !rowItem.isHeading
                spacing: groups.gap

                Repeater {
                    model: rowItem.isHeading ? [] : rowItem.modelData.items

                    delegate: PhotoTile {
                        required property var modelData
                        required property int index

                        width: groups.cardWidth
                        height: groups.cardHeight
                        source: groups.immich.imageUrl(modelData.cover, "thumbnail")
                        title: modelData.name
                        subtitle: modelData.count
                        current: list.activeFocus && rowItem.index === groups.row && index === groups.column
                    }
                }
            }
        }

        Keys.onLeftPressed: groups.moveColumn(-1)
        Keys.onRightPressed: groups.moveColumn(1)
        Keys.onDownPressed: groups.moveRow(1)
        Keys.onUpPressed: {
            if (!groups.moveRow(-1))
                groups.topReached()
        }
        Keys.onReturnPressed: if (groups.current) groups.albumOpened(groups.current.id, groups.current.name)
        Keys.onEnterPressed: if (groups.current) groups.albumOpened(groups.current.id, groups.current.name)
    }
}
