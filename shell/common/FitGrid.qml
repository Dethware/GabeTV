import QtQuick

// A grid whose rows end flush with the right edge: as many columns as fit at the design card
// size, then the cards grow a little to take up the leftover width.
//
// Cells are a card plus `gap`, so the last column's gap would sit inside the grid: give the grid
// `gap` more width on the right (anchors.rightMargin smaller by `gap`) and the cards' right edge
// lines up with the margin. Delegates size themselves to cardWidth × cardHeight.
GridView {
    id: grid

    required property real designWidth
    required property real designHeight
    property real gap: 0

    readonly property int columns: Math.max(1, Math.floor(width / (designWidth + gap)))
    readonly property real cardWidth: cellWidth - gap
    readonly property real cardHeight: Math.round(cardWidth * designHeight / designWidth)

    // Rounded down, so GridView's own column count (width / cellWidth) is never one short.
    cellWidth: Math.floor(width / columns)
    cellHeight: cardHeight + gap
}
