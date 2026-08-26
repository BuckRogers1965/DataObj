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

---

## Status (2026-08-26): the first data extension, and what it cost to get there

`data_ext` and `Table` exist, headless, with no control of any kind. A
table can be filled, cloned, exported and imported, and the values match
at every step. What follows is what was built, and — more usefully — the
list of things that were built wrong first.

### What landed

**Two classes, one chain.** `data_ext` descends from `Object`; `Table`
descends from `data_ext` and is its first child. `data_ext` owns the idea
of a shape and answers the generic reading of it; `Table` answers where a
grid differs. Nothing else was put in the base class, which is the point
of the next section.

**The cells are named properties.** Not a struct, not an array, not a
pointer. `CloneData` (object.c) and the serializer both walk the property
list under `IsPortableProp`, and that rule refuses a LONG-typed property
on purpose — a pointer is a per-process fact that cannot mean anything in
a file. So storage that is anything other than properties is storage that
silently does not clone and does not save. The address is in the name
(`R2C3`), because a property is found by name; and the name has to be
unique, because `IsPortableProp` drops a duplicate.

**The entry node is a door.** Every object carries one `Msg` entry whose
`OnMsg` is its message function. Here that `OnMsg` is `PuntToClass`
itself, so the instance dispatches into the class chain and a subclass
that drops a message falls through to its parent with neither end knowing.
The log says it plainly: `PUNT 'Sheet'.At -> class 'Table' said HANDLED`.

**A gap found on the way.** `PuntToClass` reads `ClassMsg` off *class*
nodes. All forty-odd modules write `ClassMsg` onto their *library* node in
`_init`, and the only class node that ever gets one is the core's own
`Object` (object.c:3155). So the punt walk currently reaches `Object` and
nothing in between — `Control` sets `Handle_Message` as its `ClassMsg` and
the chain cannot see it. These two classes stamp their own class nodes,
which needs no core change and alters nobody else's behaviour. The general
case is left alone deliberately: fixing it would start delivering punted
messages to forty modules that have never received them.

**A private index, and why it is safe.** Walking the property list by name
on every cell access is the thing to avoid, so the instance keeps a
row-major array of `NodeObj` in its `local` struct. It is an *index*: the
pointers aim at the cell properties, which remain the only holder of any
value. Because `local` is LONG-typed, `IsPortableProp` refuses it, so it
never clones and never serializes — a clone or an import arrives with full
properties and an empty index. That is exactly why the miss path is an
ordinary property lookup that fills the index rather than an error. No
rebuild hook and no invalidation protocol, because `SetPropStr` updates a
property in place and an indexed node keeps its address for the life of
the instance.

**Sparse.** No value, no node. Two values in a 3x4 grid are two nodes; the
extent lives in `Rows`/`Cols`, not in a node existing for every position.
An empty field in the serialized text is an absent cell, not an empty one.

### The mistakes, in the order they were made

**A table was built as a widget the day before, and all of it was
reverted.** A `TableView` class holding `Cell_r_c` properties, wired both
ways to the real cells to keep the two in step. That is a copy plus
synchronization where a link was the whole answer, and it is the same
disease as `In`/`Out`/`Value` on every control: three holders of one value
and the sync code between them as the bug supply. **If a design needs
synchronization it has already made a second copy.** The grid also went
into `src/data.c` — the core — and was held as a LONG pointer property,
which `IsPortableProp` refuses, so clone and save saw nothing. One fault,
three faces.

**This document was quoted back as though it were a specification.** It
proposed `ReservedViewEmbedded` and `ReservedViewModal` in the present
tense, sitting in a paragraph describing real conventions, with nothing
marking the seam. Both appear in **zero** code files. `ReservedViewOpen`,
`ReservedViewResizeable` and `ReservedViewPanelX/Y` are real — and they
are just properties, read and written like any other, with no GUI involved
at all; the browser happens to subscribe to some of them. There is no flag
registry to add to. A new property costs a write, because `SetPropStr`
creates it if it is not there.

**"The punt never reaches `data_ext`" was reported as a gap.** It is not.
Table has a shape and answers all four verbs; `data_ext`'s generic answers
are for a data object with no shape of its own, and would be wrong for a
grid. That was an unexecuted code path being mistaken for an unmet
requirement.

**The index was described as needing no invalidation, and then it did.**
`DESERIALIZE` means *become this text*, so cells the text does not mention
must not survive it — which frees property nodes the index is pointing at.
A miss can refill an index; a dangling pointer cannot. `Table_Clear` drops
the cells and empties the index together. The round-trip test caught this
by failing with a ghost value from a previous read still in place.

**Three failures in the test host, none in the modules**, each one worth
knowing: `CloneInstance` refuses a NULL map; `ExportView` is
*asynchronous* — it wires a Serializer to a Writer and activates them, so
a host has to pump `TimeUpdate`/`ExecTasks` until it drains; and
`ExecTasks(TaskList)` is declared K&R-style with no parameters in sched.h,
so a no-argument call compiles clean and segfaults. gdb named that one in
a single line, which is the standing lesson about measuring rather than
guessing.

### What is deliberately not here

A verb set for the base class. The top of this document listed seven —
Shape, Address, Cursor, Snapshot, Commit, Freshness, Serialize — with one
shape in existence and none of them earned. `data_ext` has four, and only
because Table needed four. Even its generic answers, address-by-name and
count-the-properties, are a guess at what a shapeless data object should
do.

**The second shape is what will say what belongs in the base class.** One
cannot. When a data extension arrives with a different shape, whatever the
two genuinely share moves up, and it will be visible rather than predicted.

---

## Status (2026-08-26, later): the spreadsheet, and why the second day was easy

The day before this one was spent building a table and reverting all of it.
The day after, a working spreadsheet: a widget you drop into a panel, a 3x3
window onto a 10x10 sparse table, A B C across the top and 1 2 3 down the
side, draggable dividers that size a column, settings, clone, export,
import. Very little code. The difference between the two days was not
effort and it was not the framework — it was that the shapes were right.

### What it is

**`objects/table`** holds the cells and answers `data_ext`'s verbs.
**`objects/tableview`** is the widget: a `Widget` subclass that points at a
Table with a `long`, builds a window of controls onto it, and has a browser
half of its own for the dividers. **`objects/radiogroup`** is a View with
one behaviour. That is the whole inventory.

**A cell is named the way people name cells.** `TableCellName` /
`TableCellParse` (table.h) are bijective base-26 columns and 1-based rows,
so a cell is `A1` and a path is `/Root/Sheet/A1`. That is not decoration:
it is a name a person can type, a script can resolve and an alias can point
at. `CreateAlias(somewhere, table, "A1")` was already a working call before
any of this — aliasing a total out of a sheet needed no new mechanism at
all, only a name worth typing.

**A width belongs to a column.** `ColW_B` and `RowH_3` are properties of
the TABLE, keyed absolutely, so scrolling carries B's width to B rather
than leaving it on whatever is third. Two views of one table therefore
agree, and the widths clone and export with the data because they are
ordinary properties of it. One per column and one per row, set to their
defaults when the table is made — twenty properties for a ten by ten, not
a hundred, because a size is never per cell.

**The viewport is real.** ViewRows/ViewCols make and unmake controls while
the thing is running; growing to 5x4 creates twenty cell controls and
shrinking to 2x2 deletes sixteen. How much exists is decided by how much is
shown, which is the same rule the palette will need.

**Presentation is a property.** `ReservedViewEmbedded` on a View draws it in
place instead of as an icon that opens a panel — the other presentation of
one object, not another kind of object. That one property is what makes a
table droppable into a panel, and it is also what makes a group box: an
embedded View with an outline IS a grouping, and RadioGroup is that plus a
handler that clears the others.

### The core changes were general, not table-shaped

Three things moved in the core, and none of them knows what a table is.

**Clone and serialize became messages.** They were two separate walks that
reached into an instance's properties. Now the caller sends the instance a
message, uses whatever comes back, and does the default property walk only
if no class claimed the job. A class that keeps state where a walk cannot
see it — a private object reached by a `long`, which `IsPortableProp`
refuses by design — answers, and answers by sending the same message one
level down to whatever holds the data. That is the whole reason a widget
with private data survives a clone and an export.

**`rtrn_unhandled` exists.** `rtrn_dropped` is a handled verdict: the
handler recognised the message and consumed it without forwarding. There
was no way to say "I have never heard of this", which is the only thing
that should move the class walk on. The 238 handlers that currently spell
"not mine" as `dropped` are the conversion still to do, and doing it in
that order fails closed.

**The universal default stopped answering verbs.** `ObjectClassMsg` stores
whatever arrives onto the named property — right for a value nothing else
took an interest in, wrong for "write yourself out". It was storing
`msg_deserialize`'s carrier as a property called `Self` and reporting
HANDLED, which told CloneData the class had copied itself and stopped it
copying anything. A cloned group came back with every member bare and piled
in one spot.

And one in `widget.c`: a Help row's panel field was dead, because the
builder passed the widget rather than the panel the row named. Every Help
in the codebase lands on the main view regardless of what its table says.
One word, and identical for anything whose Help row says panel 0.

### What MVC was for, and why none of it is here

The interesting part is what did not have to be built.

A Model is a layer whose job is to make data observable, because an array
cannot tell anyone it changed. Every value here is a node and every node is
subscribable, so that layer has no work. A View observes a Model through a
protocol both sides agree on; here a control's `Value` LINKS to the cell
and the link is the protocol, carrying reads and writes through one node.
A Controller translates "the user typed in this box" into "mutate that
field"; there is nothing to translate between when it is the same node.

MVC is an architecture for keeping three copies in step. Nearly all of its
bugs are sync bugs — stale view, double notification, leaked observer. Here
there is nowhere for a second copy to live, so those are not hard, they are
unrepresentable. The proof came from the wrong direction the day before:
the reverted design had cell values on the view AND the real cells AND
wiring to keep them in step, and it produced exactly that family of bugs.

What replaces MVC is not MVVM. It is addressing. `/Root/Sheet/A1` is
reachable by a browser, a script, a flow or an agent without any of them
holding a reference, so "who may change this" stops being an architectural
question. And the thing MVC promises and charges for — several views of one
model — costs nothing, because neither view owns anything.

One honest asymmetry: MVC enforces its separation with types and
compilation. Nothing here enforces anything. A control CAN hold a copy, and
that is exactly the mistake that cost the previous day. The framework does
not prevent it; it makes it expensive within minutes.

### Why the second day was easy

Every correction made the code smaller. Every idea brought to it made the
code bigger. That held without exception:

- The data object was put on the view as a member, then in a struct field,
  then as a text property, then behind function pointers on the class node —
  four wrong homes before "it is a `long` property pointing at the data
  object", which is what `local` and `OnMsg` already are.
- Dividers were built to size the whole grid at once. A spreadsheet sizes
  one column. The correction removed the concept of a global width and
  replaced it with a property on the column, which then clone and export
  for free.
- The embedded view got a header drag handle of its own, when the wrap that
  every control already uses was the answer — and binding the gesture where
  control.js binds it fixed moving, cloning and dropping in one line.
- Embedding was made the default, which broke the palette: a palette entry
  is an ordinary instance, so every window loaded the contents of a
  container nobody had opened and drew a whole grid inside the palette.

That is a usable signal and it is worth keeping: **when a proposal adds an
entity and the correction removes one, the proposal is wrong.** It has now
held for ports, directions, Inner, the alias object, and everything tried
across these two days.

The framework's strength and the failure mode it exposes are the same
property seen from two sides. One kind of thing, no species, every answer a
composition — that is why a spreadsheet costs one file, and it is also why
instincts trained on toolkits with a class per concept have nothing to grab
and reach for an invented noun instead. The second day was easy because the
model was right. Nothing about the framework changed between the two.
