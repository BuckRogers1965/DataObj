# Two chains and a table

*A data class, the control that shows it, and which messages walk where, 17 August 2026.*

A table is three different things that every toolkit I have used fuses into
one, and never gets apart again:

1. **storage** - an array of cells, or a cursor over a query, or a mapped
   region of a file
2. **logic** - sort, filter, aggregate, recalculate, step a cursor
3. **a picture** - a grid of boxes somebody can look at and type into

Fuse them and you get a `TableWidget`: you cannot get the data out without a
callback, you cannot put a second picture on it, you cannot run it headless,
and you cannot back it with anything the widget's author did not anticipate.

Here they are three independent axes, and none of them knows about the
others.

## A control never owns data

That rule is already load-bearing everywhere else in this framework, and it
decides the whole shape. So a table is **two instances**:

    /Root/Sheet          a Table       - holds the array, does the logic
    /Root/View/Grid      a TableView   - a control, points at /Root/Sheet

The consequences are not subtle:

- **Two pictures, one table.** A grid and a chart on the same data, live,
  with no synchronization code, because neither owns anything.
- **The data outlives the panel.** Close the view; the table is still there,
  still computing, still wired.
- **A table with no picture at all.** A flow that reads a query, aggregates,
  and drives a relay never creates a control. The logic does not live in
  something that has to be on screen.
- **`Total` is a wire.** A named cell is a node, and every node is
  subscribable, so a dashboard is `Connect(Sheet, "Total", VuMeter, "Value")`
  and not an export.

## First, a view you can put inside a panel

None of the above is buildable yet, and the thing in the way is small.

A TableView is a collection of controls - a cell is a control, a header is a
control - which means a TableView **is a View**. But a View today renders as
an *icon* placed in its parent, plus a panel registered as a root-level peer
that the icon opens. That is right for "Parse Panel" and "Options", and
useless for a grid: a table cannot float on top of the panel it belongs to.

What is needed is the other presentation of the same object: **drawn in
place**, no icon, at its own `X/Y/W/H` inside its parent. One structure, two
presentations - and the structure already has everything, because a View is
a container with geometry, a name, and children placed in it.
`Widget_SubPanel` already creates exactly the right object. It just gets
drawn as another icon.

So the change is a flag in the convention that already exists -
`ReservedViewEmbedded`, next to `ReservedViewOpen` and
`ReservedViewResizeable` - and it lives entirely in
`objects/view/show/web/view.js`, shipping inside `view.object` with the rest
of that control's browser half. No new class. Nothing in the engine.

### The general case is worth more than the table

**An embedded view is a group box.** The title is the view's Name, which the
panel header already draws. Nesting is free, because views already contain
views, so a form with three labelled sections is three embedded views. The
geometry rule does not change: laid out once at creation, fixed `W/H`,
nothing moves afterward, content scrolls.

**Radio becomes a behavior on the group, not a control.** Checkboxes in a
view plus one handler that clears the others when one sets. That is exactly
the class-chain case worth having - `Object -> Widget -> View -> RadioGroup`,
whose entire content is a single overridden `ClassMsg`, spliced in with
`SetClassParent` and needing nothing recompiled at either end. And the group
having a visible name is not decoration: `/Root/Panel/Mode/Fast` addresses
the member, and `Mode` is what a script or an agent reads to ask which one
is on.

**It also completes composites.** A scripted composite widget is already a
View with bound ports and a script inside it. Today you can build one, but
you have to open it as a panel. Embedded, that same object drops into
someone else's panel as a unit - which is the point where making your own
control stops needing C.

### Two things to settle while building it

**How a group hears its members.** It already does. Placing an instance in a
container runs `AddMember(container, inst)` from `RegisterPath`, and the
Members index exists precisely because that is - in its own comment - "where
the announcement already arrives." So there is one existing choke point on
the container itself, every time something lands in it. A RadioGroup
subscribes to the new member's `Value` at that moment, and a user dragging a
fourth checkbox into the group is handled by the same line that handled the
first three. What is missing is only that the arrival is not yet punted to
the container's class - which is one call at a place that already runs, not
a new notification path.

**Z-order becomes structural.** The rule for embedded content is simply
depth: a thing embedded in a view sits one above that view, and anything
inside *it* sits one above that. Which is what a real DOM child already
does - it paints above its parent with no `z-index` at all. The counter that
exists today (`let topZ = 100;` and `++topZ` on every raise-to-front, with
the wire layers pinned at a hopeful `100000` to stay clear of it) is there
only because every panel is currently a flat sibling, so nothing orders them
but a number. Embedding retires the number for everything it touches, and
what is left - raise-to-front among floating peers - should renumber to
actual depth on each promotion instead of counting up.

The hazard to watch is CSS stacking contexts: an ancestor with a
`transform`, a `filter`, or an `opacity` below 1 traps its descendants,
and no `z-index` will lift them out. A Dropdown in a table cell has to open
*over* the cell and over the panel edge.

Which is answered by the band above the panels: an **overlay layer** that
transient things render into - a dropdown's list, a menu, a tooltip - and
that a modal dialog owns outright. A modal greys the screen with a scrim
that swallows pointer events, so until it is answered nothing below it can
be raised at all. That makes the whole stack four bounded bands and no
counter anywhere:

    structural depth    containment depth, free from the DOM
    floating panels     bounded by how many are open
    overlay             popups, menus, tooltips
    modal               scrim, and the dialog above it

And a modal here is a View with a flag - `ReservedViewModal` beside
`ReservedViewEmbedded` - because that is all it is. Note what it is *not*:
in a conventional toolkit a modal dialog spins a **nested event loop**, and
that re-entrancy is the source of a whole genus of bugs. There is one loop
here, always. A modal blocks the pointer, not the fabric - messages keep
flowing, tasks keep firing, the flow behind the dialog does not pause. And
because modality is per-connection presentation state rather than engine
state, a dialog open in one browser does not freeze another session looking
at the same instances. Both of those fall out of the GUI being a projection
rather than the program.

## What the Data class owns

`Data` is a parent class, and its job is to name the verbs that every
structure has to answer, so that its subclasses only write down where they
differ:

    Shape       Count, or Rows/Cols - how much of me is there
    Address     resolve a tail path: row_5/col_6, or key, or index
    Cursor      First / Next / Prev / Current - a position, not a copy
    Snapshot    give me a fixed version to walk
    Commit      staged writes, then one signal that the set is complete
    Freshness   AsOf, and whether I can promise a snapshot at all
    Serialize   write me out, read me back

A Table answers `Rows/Cols` and resolves `row_5/col_6`. A Tree answers
`Count` and resolves a nested path. A TimeSeries resolves a timestamp. A
Record resolves a field name. **Same verbs, different answers** - which is
the entire content of the word "subclass" here.

And the ones a subclass does not answer fall through. A Table that never
implements `Snapshot` still has one, because `Data`'s generic answer -
clone the subtree and hand back the copy - is correct for anything small.

## How the chain actually works

This is the part worth being concrete about, because the mechanism is
already built and it is smaller than it sounds.

A class declares its parent by name:

    SetClassParent(TableClass, "Data");

which resolves the name, records `Parent` and `ParentClass` on the class
node, and then **moves the class node to be a child of its parent class**.
The parent link is not a lookup table - it is the registry tree's own
parent pointer. So the chain is a walk, with nothing to consult:

    int PuntToClass(NodeObj instance, MsgId message, NodeObj data)
    {
        for (class = ClassOfInstance(instance); IsClassNode(class);
             class = GetParent(class))
        {
            handler = (msgobj) GetPropLong(class, "ClassMsg");
            if (!handler) continue;

            verdict = handler(instance, message, data);
            if (verdict != rtrn_dropped)
                return verdict;
        }
        return rtrn_dropped;
    }

`rtrn_dropped` means "not mine, keep going up." Anything else stops the
walk. There is no dispatch table, no vtable, no registration of which
message belongs to which level - a class either answers or it does not, and
saying nothing is how inheritance happens.

Two properties of this fall out that a vtable would not give you:

**The chain is re-pointable at runtime.** `SetClassParent` can be called
again, and it moves the class again. Nothing caches the resolved chain, so
a class can be **spliced in between** an existing class and its parent - a
module whose entire content is one overridden handler, dropped in the scan
path, with neither end recompiled or even aware. Drop a `SortedTable`
between `Table` and `Data` and every existing Table instance keeps working
while gaining an override on the next message.

**Order of loading is solved, not assumed.** A class naming a parent that
is not registered yet is a dependency, and bring-up is ordered by
dependency, fewest-first. A parent that never arrives is loud - a class
with no parent is broken, not degraded.

## Two chains, not one

Here is the question that decides whether this design works: **the table
and the grid are not on the same chain, and must not be.**

    Object -> Data   -> Table        the instance that holds the cells
    Object -> Widget -> Control -> TableView    the instance that draws them

An instance has exactly one parent chain. A message about presentation -
open the panel, load the help, hand the browser your renderer, resize -
walks the Widget chain and never reaches the data. A message about content -
sort, aggregate, step the cursor, commit - walks the Data chain and never
reaches the widget.

**They meet at a path, not at a base class.** The TableView holds
`/Root/Sheet` as an ordinary property, resolves it, and renders what it
finds. That is the same relationship a Textbox already has with the
property it displays, applied to a subtree instead of a value.

The alternative - one `Table` class inheriting both "is a data structure"
and "is a widget" - is multiple inheritance, and this chain is single-parent
by construction because it is literally the registry tree's parent pointer.
That is not a limitation to work around. It is the constraint that keeps
the split honest.

## The third axis: where the bytes are

Storage is neither the class nor the control. It is the DataObj:

    struct Data { int type; ...; func_ptr call; ... };

    char *GetStr(DataObj this) { return this->call(this, GET, STRING, NULL); }
    int   SetStr(DataObj this, char *v) { return this->call(this, SET, STRING, v); }

Every value in the system is already an indirect call to a storage engine,
and there is currently exactly one engine: "in this malloc'd struct." Point
`call` somewhere else and the cell is a database field, a mapped file, a
device register, another process's shared memory.

Which is what keeps the class tree from exploding. There is no
`TableInMemory`, `TableSQL`, `TableCSV`. **There is one `Table` class, and
the backing is a property of the cells, not a species of table.** Sort works
the same. The cursor works the same. The grid renders the same. The only
thing that changes is where `call` goes.

So the three axes are genuinely independent:

    logic     the class chain      Table -> Data -> Object
    picture   a separate control   TableView, pointing by path
    storage   the DataObj call     memory, SQL, file, device

Fusing any two of those is what makes every other framework's table a dead
end.

## Why a table, specifically

Because rows and columns is the shape of almost everything anyone wants to
look at: a query result, a CSV, a log, a spreadsheet, a matrix, a record
set, an agent's output, a device's register map. One data class and one grid
renderer cover all of them, and every one of them arrives already wirable
because the cells are nodes.

And the payoff compounds with the parsers and connectors. A SQL connector
fills a Table. A delimited-text parser fills a Table. An XML document fills
a Tree that a Table can window onto. The grid does not know which. **Same
widget, same wiring, three sources** - and the only thing that had to be
built for each was the part that is genuinely specific to it.

## Cursors, because one table has many readers

A cursor is a node holding a position, not a copy: `Current`, `Next`,
`Prev`, `Order`. Ten observers on one table cost ten small objects.

- **Order lives on the cursor**, so ten people can sort the same live table
  ten different ways at once - ten permutations, not ten copies.
- **It is wirable**, because it is an object: a Pulse on `Next` is an
  automatic walker, a slider on `Current` is a scrubber, `Current` wired to
  a Textbox is a detail pane that follows.
- **It holds a path, not a pointer**, so a node deleted under it is a clean
  miss rather than a dangling read - which is exactly the distinction
  between a probe and an assertion that the naming work put in place.
- **Its consistency policy is a wire.** Free-running sees every change as it
  lands; `Next` driven by a commit sentinel only advances between settled
  generations. Those are isolation levels, expressed as a connection.

## What is not settled

**When is the graph settled?** A cell depending on two cells that both
depend on a third recomputes more than once, because dispatch is
breadth-first per message. Coalescing fixes what is *observed* - do not
publish until quiet - which is the same trick the bridge is going to play
on the browser. But "quiet" needs a definition, and the honest cheap one is
the number the main loop already computes every pass: nothing due.

**Who finishes the path walk?** `/Root/Sheet/Total` is a named cell in the
trie. `/Root/Sheet/row_5/col_6` is positional, and either every materialized
cell registers itself or the Table resolves its own tail. Resolve-through is
not foreign here - container ports already do it - but it is the difference
between a sheet and a window onto something too big to name.

**Data you do not own cannot be snapshotted.** A database has its own
transactions and its own writers; the right move is to hold the source's
cursor rather than fake one locally. Where a source offers nothing, the
value needs to say so - `AsOf` as an annotation, so the *consumer* picks its
tolerance instead of the bridge guessing one for everybody. That vocabulary
does not exist yet, and every value in the system is currently present and
definite, which is exactly the wrong default for a bridged read.

None of those three are table problems. They are what a data class is for.
