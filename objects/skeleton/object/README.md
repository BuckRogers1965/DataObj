# Writing a plain object

**Copy this when you are writing function.** A socket, a resolver, a codec, an
interpreter, a timer. Something that *does* a thing.

An object has no controls, no panel, no place on a canvas, and it is never
saved. It descends from `Object`, which ends the class chain.

---

## Why this pattern

Its whole interface is `skeleton.h` - message ids and the macros that wrap them.
The struct holding its state is defined in the `.c`, so a driver cannot cast a
handle and reach in. Nothing is published; there are no properties to read and
no state to poll.

That restriction is not tidiness, it buys three things:

- **It can be replaced.** Callers only ever had the header, so a rewrite that
  keeps the header breaks nobody.
- **It can be given a thread later.** An object nobody can reach into has no
  shared state to protect. Moving the blocking work off the main loop changes
  nothing for its drivers, because they were only ever sending messages.
- **Its lifetime is safe.** The bug that motivated all of this: a widget that
  held a node belonging to an instance it then deleted, and read it afterwards -
  a use-after-free that no non-instrumented build noticed. A driver that cannot
  address your guts cannot hold them across a delete.

So: **no `PublishProp`, no `PublishPosition`, no `InitPosition`.** Any of those
makes it a Control by accident.

---

## The shape

**One entry node.** `Msg` carries the object's message function as its `OnMsg`,
and every macro in the header delivers there. That is the entire surface.

```c
SetPropStr(instance, "Msg", "");
entry = GetPropNode(instance, "Msg");
SetPropLong(entry, "OnMsg", (long)Skeleton_MessageFunc);
```

**The caller says where to report back.** At creation it hands over an owner, a
message base, and a port, in the data node passed to `InstanceStart`. Answers
arrive as `base + ordinal`. The *owner* picks the base, so one owner can hold
several of your objects and still tell their answers apart.

```c
local->owner = (NodeObj) GetPropLong(data, "Owner");
local->msgID = (MsgId)   GetPropLong(data, "MsgBase");
local->port  = strdup(GetPropStr(data, "Port"));
```

**Ids start at `USER_MESSAGE_BASE`** so they cannot collide with the framework's
own. Their ORDER is your ABI - it is what your class's `Major`/`Minor` protects.
Adding an id at the end is a minor bump; reordering or removing one is a major.

**Report, do not store.** An answer delivered as a message reaches whoever asked
for it. An answer left in a property has to be found, and then someone polls.

---

## How a driver uses one

It creates the instance through your class's own `InstanceStart` - not
`CreateObject`, not `Widget_Create` - and keeps the handle privately:

```c
class = /* walk the registry for your class by name */;
start = (msgobj) GetPropLong(class, "InstanceStart");

args = NewNode(INTEGER);
SetPropLong(args, "Owner", (long) instance);      /* me */
SetPropLong(args, "MsgBase", MY_CALLBACK_BASE);   /* my choice */
SetPropStr(args, "Port", "Evt");                  /* my port */
start(class, msg_initialize, args);
handle = (NodeObj) GetPropLong(class, "LastInstance");
```

Then it drives the object through your header's macros and catches the answers
in its own handler. It never names one of your properties, because there aren't
any. `objects/udpport` and `objects/tcpport` are the worked examples.

**There is no such thing as an "Inner".** The handle is private. It is not
addressable by path, it does not appear in a palette, and nothing else in the
session can find it.

---

## If people need to operate it by hand

Write a **Control** or a **Widget** as a *separate module* that owns one of
these and drives it. `objects/udp` + `objects/udpport`, `objects/network` (which
builds `tcp.object`, class `TCPSocket`) + `objects/tcpport`.

The split is the point: the engine knows datagrams and nothing about
presentation, the panel knows controls and nothing about sockets, either can be
replaced without touching the other, and anything else - a script, a Pulse, a
flow - drives the same engine the same way.

Do not grow controls onto this file. That is how you get something that is half
an object and half a widget, and then neither.

---

## Subclassing

Set `Parent` to the class you extend, and declare it as a dependency:

```c
SetClassParent(ClassSelf, "Script");                       /* in ClassStart */
AddDependency(temp, "script.object", "Script", "1", "0");   /* in _init */
```

Answer what is yours and return `rtrn_dropped` for the rest - the walk continues
to your parent, then to `Object`. `objects/lua` and `objects/jsscript` are both
`Script`; everything common to running a script belongs up there, not copied
into both.

---

## When the work blocks: hardware and other slow I/O

Cooperative multitasking is a contract where every participant has a veto. One
`read()` on a slow disk, one `connect()` to a host that is down, one resolver
call to a nameserver that never answers, and the whole fabric stops - it does
not degrade, it stops, and the symptom is "the app froze" no matter which
object did it. A raw I/O object is where that happens, so decide this first:

**If the OS offers a nonblocking form, poll it. If it does not, quarantine it
behind a worker thread.**

Poll it - `O_NONBLOCK` plus a task that reads what is ready and re-arms. This is
what `objects/tcp` and `objects/udp` do, and threading them would be strictly
worse.

Quarantine it - for the calls with no nonblocking form:

- name resolution (`gethostbyname`, `getaddrinfo`)
- `open()`/`stat()` on a slow or network filesystem - `O_NONBLOCK` does not
  help, the open itself blocks
- `tcdrain`, modem-line waits, and most device `ioctl`s: USB, GPIO, instrument
  drivers - whatever the driver decides to do inside the call
- `fsync`/`fdatasync`
- spawning a process and waiting for it

### The worker pattern

`objects/dns` is the worked example - the first object in the tree to run
anything off the main thread. The shape, and every part of it is load bearing:

- **The worker touches ONLY its own struct.** The hostname, the result, and a
  state flag. It never sees a `NodeObj`, never allocates a node, never sends a
  message. The moment a worker touches the node tree, "single-threaded fabric"
  stops being true and every other guarantee here goes with it.
- **A sentinel, polled from a task.** The worker sets a state field when it is
  done; a task in the main loop notices, joins the thread, and only then reads
  the result. Joining before reading is what makes the handoff safe without a
  lock.
- **The answer becomes an ordinary message**, delivered on the main thread to
  the `{owner, msgID, callback}` handed over at creation - exactly as if the
  work had never left.
- **The task arms on demand and stops when nothing is owed.** An object nobody
  has asked for anything schedules nothing and holds no program open.
- **The object owns the retraction.** It keeps a list of what it asked for, so
  `InstanceEnd` can cancel everything not yet started, and a liveness check so
  the one already running - which cannot be cancelled - answers into nowhere.
  A subtree delete walks the engine's own structures; a thread's future answer
  is not in them, so this is the object's job and nobody else's.

None of that reaches your drivers. They send a message and get one back, which
is why the header being the whole interface is what made the thread possible in
the first place.

---

## Files here

- `skeleton.c` - the module. Message function, private struct, one entry node.
- `skeleton.h` - the interface. Ids, callback ordinals, macros. **This is all
  anyone gets.**

See the [top-level README](../README.md) for what every module declares, how
dependency ordering works, and the `ld -shared` trap.
