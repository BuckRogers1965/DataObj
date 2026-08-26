# TableView

A window onto a table, drawn in place.

TableView holds no data. **Table** names the instance whose cells it shows,
and the grid is wired to whatever is at `<Table>/row_<R>/col_<C>`. The wire
runs both ways: a change in the table appears here, and an edit made here
lands in the table, where anything else wired to that cell hears it.

Sliding the window rewires; it does not rebuild. The boxes stay where they
are, so what you were typing in survives a move.

Two windows onto the same table are two TableViews. Neither owns anything,
so there is nothing to keep in step.

## The window

- **Table** - path of the table instance, e.g. `/Root/Sheet`.
- **Row**, **Col** - the cell at the top-left corner. Sliding is nothing
  more than changing these.
- **VisibleRows**, **VisibleCols** - how much of the table is drawn.
- **CellW**, **CellH** - the size of one cell, in pixels.

All of them are ordinary properties, so they can be typed into the options
panel, written by a script, or driven down a wire - a Slider connected to
`Row` is a scrollbar nobody had to build.

## Navigation

- **Left**, **Right**, **Up**, **Down** - slide the window one cell.
- **Home** - back to row 0, column 0.

These are properties too. A Pulse wired to `Down` walks the table on its
own.

## Controls

- **Enable** - checked, the view operates (the default). Unchecked, the
  navigation and the window properties stop responding; what is on screen
  stays as it was.

## Notes

The panel is the size it was declared. A window bigger than the panel
scrolls inside it - the panel never grows to fit its contents.

Row and column numbers down the left and across the top say where the
window currently is. A cell with nothing behind it is simply blank: a
window past the end of the table is an ordinary thing, not an error.
