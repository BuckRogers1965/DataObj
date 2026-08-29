# Writing a data shape

A **data object holds values in nodes**, and the nodes are the only place a
value lives — anything showing one holds a link to it, never a copy. A
**shape** is a child of `data_ext` that says two things and nothing else:

- **how its nodes are addressed** — a grid by row and column, a list by
  index, a record by field name;
- **how it writes itself out** — its own text form, and the inverse.

Everything else it declines, and the class walk carries on up to `data_ext`
and then to `Object`, where the plain node tree is what you get. That is why
a shape is small: `objects/table` is a grid in about 500 lines, and most of
those are the two coordinates.

The first shape was the Table. There will be more — a tree, a time series, a
ring buffer, a graph — and each one is a `.object` you drop in the scan path.

---

## What a shape is NOT

- **Not a control and not a widget.** It has no panel, no X/Y, no label and
  nothing to lay out. If you are placing anything, you are writing a widget,
  and the widget is the thing that *owns* one of these.
- **Not a member of a view.** It is not a control, so it does not live in
  one. A day was lost to exactly this.

It **is** in the tree, like everything else - a registered instance, a node
with properties, and its entries are ordinary properties on that node. What
its owner holds is just a `long` whose value is the instance:

```c
SetPropLong(instance, "Data", (long)shape);
```

What is private is **the grid** - and the grid is nothing but a grid of this
node's own properties, reached by name. Nothing is placed in a view, nothing
gives it a path, so nobody else addresses those properties; that is the whole
of its privacy. The `long` is refused by `IsPortableProp`, which is exactly
right - a pointer is not a value and must never reach a file - while the
properties themselves travel with a clone and an export as they should.

---

## The five verbs

`data_ext.h` is the whole interface. Your class answers what it differs on
and returns `rtrn_dropped` for the rest:

| verb | what it means | this template |
|---|---|---|
| `DATA_EXT_ADDRESS_MSG` | hand back the node at this address | reads `Index`, answers on `Node` |
| `DATA_EXT_SHAPE_MSG` | how much of me there is, in my own terms | writes `Count` |
| `DATA_EXT_DROP_MSG` | throw the contents away, keep the shape | deletes the entry nodes |
| `DATA_EXT_SERIALIZE_MSG` | write me out the way I store myself | answers on `Text` |
| `DATA_EXT_DESERIALIZE_MSG` | become that text | reads `Text` |

**What an address IS belongs to the shape.** There is no universal
coordinate; a grid reads `Row`/`Col`, this template reads `Index`. Only the
answer is fixed: the node comes back on the data node's `Node` property.

**`SERIALIZE` and `DESERIALIZE` ship together or not at all.** A
representation and its inverse are one answer and cannot be allowed to drift
apart. A shape that declines both is written out as its nodes, which is
already correct — so only implement them when your storage really is a
shape rather than a tree.

**`DROP` cannot be left to a caller.** Sparse means an empty slot has no
node, so dropping is deleting nodes, and whatever indexes them has to let go
in the same breath.

---

## The three things that make it work

1. **The door onto the class chain.** One property named `Msg` whose `OnMsg`
   is `PuntToClass` itself:

   ```c
   SetPropStr(instance, "Msg", "");
   entry = GetPropNode(instance, "Msg");
   SetPropLong(entry, "OnMsg", (long)PuntToClass);
   ```

   Without it the verbs reach nothing at all.

2. **`ClassMsg` goes on the CLASS node**, not the library node:
   `SetPropLong(ClassSelf, "ClassMsg", (long)Skeleton_ClassMsg)`. `PuntToClass`
   walks class nodes and reads `ClassMsg` off each one — a shape that writes
   it on its library node is never reached, and the failure is silent.

3. **`SetClassParent(ClassSelf, "data_ext")`**, plus
   `AddDependency(temp, "data_ext.object", "data_ext", "1", "0")` in `_init`.
   A class cannot be looked up before its own `ClassStart` has run; get the
   dependency wrong and your class simply never starts, and the log says what
   was missing.

---

## Storage: what clones and what does not

**Your data is named properties on yourself.** That is the whole trick —
`CloneInstance` copies properties, and the serializer writes them, so
contents survive a clone, an export and a save with no help from this module.
Name them something legible in a dump: the grid says `A1`, this says `E0`.

**An index is a shortcut, never a second copy.** The grid IS the properties;
a `NodeObj *` array on `local` only saves looking one up by name. Keep it on
`local` as a `long` so it can never reach a clone or a file, and make it
*self-healing*: a miss looks the property up by name and refills. Then
nothing has to keep it in step with the properties it points at, and a `DROP`
that frees them just empties it.

**Sparse: no value, no node.** An empty slot does not exist. That is what
makes a large shape cheap, and it is why `Count` (or `Rows`/`Cols`) is an
ordinary property rather than something counted from the nodes.

**The text form IS the file format.** Write what *defines* an entry, never
something derived from it. The Table writes a computed cell as `=SUM(B1:B4)`
and not as `580`, because saving the answer loses the question.

---

## Files here

- `skeleton.c` — a one-dimensional shape (an indexed list): the smallest
  thing with a real address and a real text form. A grid is the same module
  with two coordinates.
- `Makefile.copy` — becomes the new module's `Makefile`. No `show.mk`: a
  shape has no browser half, because nothing presents it.

The shipped reference is `objects/table` (the grid) with `objects/tableview`
as an example of an owner — a widget that creates one privately, points at it
with a `long`, and drives it. `objects/data_ext` is the parent class itself.
