# The library stopped carrying its own tests, and the data got smaller

Two changes tonight that have nothing to do with features, and everything to
do with what `libframework.so` *is*. The tests came out of it, and the single
most-allocated struct in the system lost forty bytes. The library ended the
day at roughly half the size it started.

## Testing was built into the modules themselves

From the beginning, every core module carried its own self-test, compiled in
and declared in its header as part of its interface: `DataTest()` in data.c,
`NodeTest()` in node.c, `SchedTest()` in sched.c, `NameSpaceTest()` in
namespace.c, and `dyn/bufftest.c`, which was nothing but `BuffTest`. A `-t`
flag on the executable ran them.

That was the right call when the point was to prove a mechanism worked at all.
It stopped being right the moment the framework became something you embed.
The whole hosting contract is five calls; anything that can `dlopen` a shared
object gains the entire object system. What that host should not gain is a
copy of the type-conversion matrix printer.

So the tests moved out to `unit_test`, an executable that links the library
like any other host and calls its own `UT_` copies. Deleted from the library:
`DataTest`, `TestFunc`, `SerializationTest`, `SubscriberTestCallback`,
`InterceptTest`, `LinkTest`, `NodeTest`, `NameSpaceTest`, `testcallback`,
`SchedTest`, two `#ifdef TESTBUILD main()` blocks, and the files
`src/dyn/bufftest.*` and `src/schedtest.*` outright. `bufftest.c` had been in
the Makefile's `SOURCE` list, so the library was linking a file that was 100%
test code. The prototypes came out of the four headers, and `-t` came out of
both `main.c` and `unit_test.c` - running the tests IS the second program, so
there is nothing to opt into.

The boundary turned out to be honest, and there is a small proof of it.
`UT_SchedTest` had been sitting `#if 0`'d in the harness with a note saying it
needed sched.c internals - `TaskPtr`, a static callback, an internal
`AddTaskDelay` signature. It needed none of them. `TaskObj`, `CreateList`,
`CreateTask` and all four `AddTask*` calls are exported in sched.h; the test
wanted `TaskPtr` renamed to `TaskObj`, its own callback, and `AddTaskDelay`'s
seventh argument. It runs from outside now. A test that can only be written
from inside a module is telling you the module has no interface; this one
turned out to have one all along.

Three more tests came back from the dead in passing. `UT_FlowTest`,
`UT_InterfaceTest` and `UT_SkinTest` were gated on a `UnitTest` property that
nothing set any more - they had silently stopped running at some point and
nobody noticed, which is its own argument for tests living somewhere they are
the whole point of the program.

## The library ships mechanism, not measurements

Combined with the day's other work - the flow interpreter, the serializer,
skins, and the whole presentation layer moving out into loadable `.object`
modules - the library came down to about half what it was. That is the same
principle twice: `libframework.so` holds mechanism, and everything that is a
*decision* about how to use the mechanism lives in an object you can replace
by copying a file.

The shape of the result is worth staring at. The host executable is 21 KB -
smaller than any object it loads. The core library, holding the node tree,
DataObj with automatic conversion, the scheduler, message routing, the
namespace index, dynamic loading and allocation accounting, is 104 KB. The
53 objects that are not vendored interpreters average about 31 KB each, and
that includes the TCP stack, HTTP, WebSocket, the bridge, MCP, and the entire
widget palette.

## Forty bytes off the most-allocated struct

The other half of the night was a goal stated in one line: *the core should
stay in cache.*

On the instruction side it already does, by a wide margin. All of `.text` is
44.8 KB. The per-message hot cycle - `ExecTasks`, `SndMsg`, `DispatchMsg`,
`DeliverToSubscriber`, the property get/set it lands on, task alloc and
recycle, the sleep calculation - is 1,820 bytes across seventeen functions.
Twenty-nine cache lines, about 5.5% of one core's L1i, resident forever. The
big functions are all cold and that is the good news: `CloneGroupPass` at
2,004 bytes, `ParseJsonString` at 1,713, `EncodeNode` at 1,325. Per-gesture
code, evictable without ever touching the fabric's throughput.

The data side is where the constraint actually lives, because every message
walks nodes. `struct node` is 72 bytes - an `int` and eight pointers, twenty-
four bytes from fitting a line exactly. But every node owns two `DataObj`s,
one for its name and one for its value, and `struct Data` was **96 bytes**:

```c
struct Data {
        int type;
        func_ptr call;
        int str_set;
        char * str_val;
        int str_len;
        int int_set;
        int int_val;
        int hex_set;
        char * hex_val;
        int real_set;
        double real_val;
        ...
```

Seven `int` flags saying which representation is currently valid, each one
interleaved between the values it describes. Twenty-eight bytes of flags, and
because each `int` sits between a pointer and a `double`, they force padding
on both sides.

The obvious fix does not work. Declaring them `unsigned x : 1` and leaving them
where they are gains **exactly zero bytes** - each bit-field gets its own
storage unit when other members separate them, and the struct stays at 96. The
saving is entirely in *grouping* them. Moved together into the four-byte
alignment hole that already existed after `int type` and was being wasted
anyway, with `bool_val` tucked in beside them:

```c
        int type;

        unsigned char str_set  : 1;
        unsigned char int_set  : 1;
        unsigned char hex_set  : 1;
        unsigned char real_set : 1;
        unsigned char long_set : 1;
        unsigned char bool_set : 1;

        char bool_val;
```

**96 bytes to 56.** Under one cache line, and glibc's chunk for it is exactly
64 - so a DataObj is now one aligned line where it used to straddle two. Per
node, counting what malloc actually hands out rather than `sizeof`:

| | before | after |
|---|---|---|
| `struct Data` | 96 (112 chunk) | 56 (64 chunk) |
| node + name + value | 304 bytes of heap | 208 bytes |

About 32% less heap per node, and a node's value read touches one line instead
of two. `.text` came down slightly as well, 44,803 to 44,643, from narrower
flag loads.

Every one of the fifty-four uses of those flags is an assignment of a literal
`0` or `1` or a truth test, and all of them are inside data.c - `struct Data`
never escapes the file, since `DataObj` is a pointer to an incomplete type
everywhere else. That is what made a layout change safe to make at all, and it
is worth noticing that the opacity which exists for design reasons is the same
opacity that makes this kind of optimisation free.

One line had to change beyond the struct. `clear()` zeroed five of the six
flags and left `bool_set` to whatever `malloc` returned - harmless when each
had its own `int`, but they share a byte now, and a partly-undefined byte in
the hottest struct in the system is not something to leave lying around.

## What it cost

Honesty about the ledger: moving the tests out surfaced a leak. Under ASAN,
`unit_test` now reports 12,374 bytes in 306 allocations, and the two likely
sources are both mine and both from this work - `UT_SchedTest`, revived after
years switched off, creates a task list and four tasks and frees none of them
(the original in sched.c leaked identically; it simply never ran anywhere a
leak checker was watching), and the three class-dependent tests that had
silently stopped running now run every time and allocate accordingly.

Which is the point of a harness that watches its own counters. The tests moved
out of the library and immediately started reporting on themselves.
