# objects/ — everything here is optional

The executable is a hollow host. It provides **one** class, `Object`, and five
calls' worth of plumbing. Everything that makes this system *do* anything is a
file in a subdirectory here, discovered at startup, `dlopen`ed, and registered
into a live tree.

That includes the things you would expect to be built in. `Control` — what it
takes to have a name, a place and a size — is `objects/control/`. `Widget`, the
class every instrument panel descends from, is `objects/widget/`. The palette is
built by `control.object`; the flow interpreter is `objects/flow/`; the code that
knows how to save and load a session is `objects/serializer/`. Delete any of them
and the framework still starts. It will simply have no palette, or no way to
load a file, and the log will say exactly which class nobody could find.

**Nothing here is privileged, and nothing here is required.**

## What that buys

**An application is a set of objects plus their wiring.** Shipping a different
product means shipping a different set — never a different binary. Two
installations on one machine can have entirely different palettes because they
have different directories.

**A fix is one file.** Modules are 20–30 KB (excluding the two vendored
interpreters), isolated behind the message fabric, and identified by a `UUID` and
`Company` on their library node. Support has historically meant emailing one
`.object` — drop it in the scan path and the fix is installed. Keep it that way:
never introduce a link dependency between two modules, and never merge them into
a shared blob.

**You can class them however you like.** The class tree below is not baked into
the engine. A class node carries a `Parent` property naming another class, and
that is the entire mechanism — no registration table, no vtable, no interface
list. If `Control` and `Widget` are not the right split for what you are
building, write your own layer, put your things under it, and the engine will not
notice the difference. What it enforces is only that a parent exists before its
children start, which falls out of the dependency declarations.

---

## The class tree as it stands

56 classes in 55 files. `Object` comes from the core; every other line is a file
in this directory.

```
Object                            libframework.so        (the core, ends the chain)
├── Control          control.object          a name, a place, a size; serialized
│   ├── View         view.object             a Control whose panel is a container
│   ├── Widget       widget.object           a bag of Controls, and the behaviours driving them
│   ├── Alias        alias.object            a doorway to one property of another object
│   ├── Button       button.object           ── the sixteen atoms panels are built from
│   ├── Checkbox     checkbox.object
│   ├── Dropdown     dropdown.object
│   ├── HTML         html.object
│   ├── Image        image.object
│   ├── Knob         knob.object
│   ├── LED          led.object
│   ├── Label        label.object
│   ├── Markdown     markdown.object
│   ├── MenuButton   menubutton.object
│   ├── MoButton     mobutton.object
│   ├── Slider       slider.object
│   ├── TextOut      textout.object
│   ├── Textbox      textbox.object
│   └── VUMeter      vumeter.object
├── Script           script.object           what every language host has in common
│   ├── Lua          lua.object
│   └── JSScript     jsscript.object         (QuickJS)
├── Flow             flow.object             composition recorded as data, and replayed
├── Skin             skin.object             a class's default layout
├── UDP              udp.object              a datagram socket - opaque, driven by message
├── TCPSocket        tcp.object              a stream socket, ditto  (dir: objects/network)
└── TCP              tcpshim.object          the old property surface over TCPSocket
```

And descending from `Widget`, the panels — 29 of them:

| | |
|---|---|
| **dataflow** | `Reader` `Writer` `Filter` `Queue` `Stack` `Out` `Pulse` `PulseGenerator` `LogicGate` `Stopwatch` |
| **transform** | `Base64` `Bin2Hex` `RegExp` `CharacterMap` |
| **web plumbing** | `Router` `Http` `WebSocket` `Bridge` `Serializer` |
| **network panels** | `UDPPort` `TCPPort` `TPLink` |
| **scripting** | `ScriptBox` |
| **AI / external** | `Ollama` `ComfyUI` `StableDiffusion` `MCPSource` `MCPAgent` |
| **introspection** | `Stats` |

Two things in that listing are worth noticing, because they are facts about the
design rather than accidents:

**A file's name is not its class's name.** `objects/network/` builds
`tcp.object`, which provides the class `TCPSocket`. That is why a dependency
names *both* — see versioning below.

**One file may provide several classes.** `queue.object` provides `Queue` and
`Stack` (the class name picks the pop direction); `mcpsource.object` provides
`MCPSource` and the `MCPAgent` view it generates.

---

## Versioning

Every class carries a version, and every module declares the version of every
class it uses. That is what keeps a module built against an older core out of a
newer one, instead of loading and misbehaving.

**On the class**, in `ClassStart`:

```c
SetClassVersion(ClassSelf, "1", "0");
SetClassParent(ClassSelf, "Control");
```

**On the dependent**, in `_init` — one line per class it actually uses:

```c
AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");
AddDependency(temp, "control.object", "Control", "1", "0");
AddDependency(temp, "textbox.object", "Textbox", "1", "0");
```

**Major and minor stay separate values.** A version is a tuple, not a number:
`"1.10"` sorts below `"1.9"` as a string, and converts to 1.1 as a real. The
compatibility rule is **major equal, minor at least what was asked for**. So:

- adding a message id at the END of your header's enum, or a new property — bump
  the **minor**. Existing dependents keep working.
- reordering or removing an id, renaming a property, changing what a callback
  means — bump the **major**. Every dependent must be rebuilt, and until it is,
  it will refuse to start rather than misbehave.

**Both names are required in a dependency** because a class cannot be looked up
before its own `ClassStart` has run — which is the very thing being ordered. The
file is what the loader can act on now; the class is what gets verified once that
file is up.

**When it fails, it says so.** `loadClasses` sweeps repeatedly, starting whatever
has everything it named. Anything left unsatisfied does not start, and the log
names which of the four things went wrong: the file was never loaded, the file's
own dependencies were unmet, the file loaded but never registered that class, or
the version wanted did not match the version found. A class that does not start
is simply absent — from the palette, from `CreateObject`, from everything —
rather than half-alive.

Every module's library node also carries its own `Major`/`Minor`, plus `Company`
and a `UUID`: provenance for the file you were emailed.

---

## Writing one

`objects/skeleton/` has three templates and a chooser — plain object, control, or
widget — each with a README that argues for its pattern and shows how to subclass.

```
objects/skeleton/newwidget.sh Counter           # a widget
objects/skeleton/newwidget.sh Gauge control     # one thing on screen
objects/skeleton/newwidget.sh Resolver object   # plain function, no panel
```

Read [`objects/skeleton/README.md`](skeleton/README.md) first. It covers what
every module declares, how to reach another module's code without linking against
it, and the one trap that will cost you an hour: object modules link with raw
`ld -shared`, which permits undefined symbols and resolves them at `dlopen`, so a
clean build proves nothing about whether your symbols exist.

---

## Not currently built

- `objects/msg/` — a stub from early on. Registers a library and no class.
- `objects/network/TCPObject.c` — the reference implementation from the
  predecessor system. It cannot compile here (different API) and is kept because
  it is the closest thing to a finished object: connection rings, a state
  machine, and a lifecycle that lets the system go quiet on its own.
