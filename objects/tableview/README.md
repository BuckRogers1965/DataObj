# TableView

A window onto a Table.

The widget owns a **Table of its own**, ten by ten, and shows three by
three of its cells. Type into a box and the value goes into the cell
behind it.

Nothing on the panel holds a value. Each box is an alias: its `Value`
links to the cell it stands for, so there is one holder of every value
and nothing to keep in step. The Table is a member of the widget, so a
clone or an export carries it along and the links are re-pointed at the
copy — the values come with the widget.

The Table is **sparse**: a cell becomes a node when something refers to
it. Nine of the hundred exist because nine controls stand for them.
