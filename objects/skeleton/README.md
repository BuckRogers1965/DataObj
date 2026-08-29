# Which kind of thing are you writing?

This directory is a **template**, not a module. Nothing here builds (there is no
`Makefile`, only `Makefile.copy`, so the framework's `objects/*/Makefile` scan
skips it) and nothing here loads. Copy the subdirectory that matches what you
are writing:

| subdirectory | write one when | descends from | renders as |
|---|---|---|---|
| [`object/`](object/README.md) | it is **function** - a socket, a resolver, a codec, an interpreter. No controls, no panel, never saved. | `Object` | nothing. It has no place on a canvas. |
| [`control/`](control/README.md) | it is **one thing on screen** - a box, a light, a knob. What panels are built from. | `Control` | an atom: a control and a label. |
| [`widget/`](widget/README.md) | it is **a bag of controls with behaviours** - an instrument panel. | `Widget` | an icon that opens a panel. |
| [`data_ext/`](data_ext/README.md) | it is **a way of holding values** - a grid, a list, a tree, a time series. Says how its nodes are addressed and how it writes itself out. | `data_ext` | nothing. Something else owns it privately. |

Answer honestly. Most mistakes in this codebase came from one module trying to
be two of these at once.

---

## The class tree

```
Object                       the core provides this one, and it ends the chain
 |
 +- Control                  a name, a place, a size - and it is serialized
 |   |
 |   +- View                  a Control whose panel is a container
 |   +- Widget                a bag of Controls, with the behaviours that drive them
 |   +- Textbox, LED, Checkbox, Button, Slider, Knob, Label, Dropdown,
 |      MoButton, MenuButton, TextOut, VUMeter, Markdown, HTML, Image, Alias
 |
 +- Script                   a language host: Lua, JSScript
 +- data_ext                 a way of holding values, addressed its own way
 |   |
 |   +- Table                 a grid of nodes (objects/table)
 |
 +- UDP, TCPSocket, Flow, Serializer, Skin, ...   plain function
```

`Object` is the only class the core itself provides. Everything else - including
`Control` and `Widget` - is a loadable `.object` like yours. That is deliberate:
fixing what is common to every control ships one file, not a new framework.

---

## What every module declares, whichever kind it is

Three things, and getting them wrong means your class silently never starts
(the log says exactly which one was missing - read it).

**1. What it IS.** In `ClassStart`, right after `RegisterClass`:

```c
SetClassVersion(ClassSelf, "1", "0");
SetClassParent(ClassSelf, "Control");      /* or "Object", or "Widget" */
```

**2. What version it is.** Major and minor stay separate values. A version is a
tuple, not a number: `"1.10"` sorts below `"1.9"` as a string and converts to
1.1 as a real. Compatible means **major equal, minor at least what was asked**.

**3. Every class it uses.** In `_init`, one line per class, naming the FILE as
well as the class:

```c
AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");
AddDependency(temp, "control.object", "Control", "1", "0");
AddDependency(temp, "textbox.object", "Textbox", "1", "0");
```

Both names are needed because **a class cannot be looked up before its own
`ClassStart` has run** - which is the very thing being ordered. The file is what
the loader can act on now; the class is what gets verified once that file is up.
They differ more often than you would expect: `tcp.object` provides `TCPSocket`,
`queue.object` provides both `Queue` and `Stack`.

Declare exactly what you use. Under-declare and your class starts before a
control exists, and builds without it. Over-declare and your class refuses to
start when something it never needed is absent.

And free them in `_fini`, since `_init` built them:

```c
ClearDependencies(LibrarySelf);
```

---

## How subclassing works

A class node carries a `Parent` property. That is the whole mechanism - there is
no class-registration table, no vtable, no interface list. Bring-up order falls
out of the dependency declarations above: `loadClasses` sweeps repeatedly,
starting whichever libraries have everything they named already loaded, so a
parent is always registered before its children even though the files were
scanned in whatever order the directory happened to give.

A message your handler does not answer returns `rtrn_dropped` and the walk
continues up the chain to your parent, and eventually to `Object`. So put in
your class only what is genuinely yours, and let the level above answer what it
owns - Help and the panel conventions belong to `Widget`, a name and a place
belong to `Control`.

---

## Reaching another module's code

**Never link one `.object` against another.** A module links only against
`libframework.so`. When you need something another class provides, you include
its header and call the function - the header turns that into a lookup of a
function-pointer property on that class's node:

```c
#include "control.h"     /* InitPosition, PublishPosition, Widget_Create, the palette */
#include "widget.h"      /* Widget_Publish, Widget_Init, Widget_DeferBuild, ... */
#include "flow.h"        /* record composition as data, replay it */
#include "serializer.h"  /* export / import / load a view */
#include "skin.h"        /* a class's default layout */
```

If the class providing it is not loaded, the wrapper logs an ERROR naming the
entry point it could not reach and returns harmlessly. A missing capability says
so; it does not disappear quietly.

**A trap worth knowing:** object modules link with raw `ld -shared`, which
permits undefined symbols and resolves them at `dlopen`. A clean build therefore
proves **nothing** about whether your symbols resolve - the framework will load
your module and die with `undefined symbol: ...` instead. If that happens, you
are calling something you did not include a header for. Check with:

```
nm -u objects/yourthing/yourthing.object | less
```

Also: a new `#include` is not in the modules' `makedepend` output, so `make`
may not rebuild what needs rebuilding. When symbols look impossible, `make
clean && make`.

---

## Stamping one out

```
objects/skeleton/newwidget.sh Counter          # a widget (the default)
objects/skeleton/newwidget.sh Counter control  # a control
objects/skeleton/newwidget.sh Counter object   # a plain object
objects/skeleton/newwidget.sh Ring data_ext   # a data shape
```

That creates `objects/counter/` with the source, a real `Makefile`, and a
starter `README.md`, with every `Skeleton`/`skeleton` token and the UUID
rewritten. Then `make -C objects/counter`, restart the framework, and it is in
the palette (a plain object is not - it has no place on a canvas, which is the
point).
