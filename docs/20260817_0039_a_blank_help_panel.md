# A blank help panel

*What one missing README cost, 17 August 2026.*

Clone a TPLink, open its Help, get an empty box. The source's help works.
That is the whole bug report, and it turned out to be a report about how
every widget in the tree was constructed.

## Two tests that failed for the wrong reason

The first test cloned `/Root/Palette/TCPPort` and asserted the copy's help
filled. It failed on the SOURCE - zero characters - because the harness
stages only `objects/*/*.object` into a variant directory, flat, and a
widget's help is declared as `objects/tcpport/README.md`, read relative to
the cwd. The file has never existed under test. Help has never been
testable, which is a decent part of why "clones lose help" survived.

Staged the READMEs. The second test then PASSED, which was worse. The
clone's box did contain the text - because `HelpText.Value` is an ordinary
string property, and cloning copies it. The text rode across as DATA while
the thing that produces it was dead. The assertion compared the text, so it
could not tell those apart.

Blanking the copy's box before opening it is what made the test honest, and
then the log said it in one line each way:

    SET 'Help'.ReservedViewOpen = '1' -> DELIVER    (source: loads 5605 bytes)
    SET 'Help'.ReservedViewOpen = '1' -> STORE      (clone: nothing)

DELIVER means the property had a handler. STORE means it did not.

## The first answer, which was true and not the cause

`Widget_AddHelp` stamps `Widget_OnHelpOpen` as an `OnMsg` on the Help
view's `ReservedViewOpen`. That is a function pointer. `IsPortableProp`
refuses LONG-valued properties, so a clone cannot carry it. Copy a widget
and its handlers do not come.

True, and the wrong thing to fix, because handlers are not supposed to be
copied - they are supposed to be re-installed by the widget's own build.
`Widget_Create`'s comment has said so all along: a load restores a panel
from a file, and the build runs over it to put back "the compiled handlers
and wires, which are LONG properties the serializer drops on purpose."

So the question was not why the handler was missing. It was why the build
did not run.

## The actual cause: construction remembered itself

    int Widget_BuildOnce(NodeObj instance, WidgetItem *table)
    {
        if (!instance || !table || GetPropInt(instance, "PanelBuilt"))
            return 0;
        SetPropInt(instance, "PanelBuilt", 1);

`PanelBuilt` is an ordinary INTEGER property. Ordinary properties are data.
`IsPortableProp` passes it, so a clone, a save and an import all carry
`PanelBuilt = 1` - and skip the build. The copy comes up holding every
control, every value, the right `HelpFile`, and nothing behind any of it.

The clone was created correctly and then the data copy overwrote the one
fact that said it still needed building.

## Why that flag existed at all

Every widget ended its constructor with

    Widget_DeferBuild(instance, XPanel);   /* panel built one tick from now */

which armed a 1ms task. A tick later the task built the panel and called
the widget's placement setup, which called `Widget_BuildOnce` again - hence
a flag, to stop the second call rebuilding.

The tick existed because a widget's controls are created INSIDE it, and
creating anything requires its container to have a path, and at
`InstanceStart` the instance had no name and no path: the engine named it
after the constructor returned. So half the constructor was deferred until
the identity showed up, and a property remembered which half had run.

That is the whole chain. A missing name at construction became a deferred
phase, which became a flag, which became data, which a copy inherited.

## The rule that was already there

*A thing is built in a location.* The engine has enforced that at creation
for a month - `CreateObject` refuses a container with no path of its own.
What it did not do was tell the constructor where that location was.

    InstanceStart(class, msg_initialize, NULL);

NULL. The constructor was asked to build something without being told where
it was building it.

Hand it over - the container, and the name it will be called - and every
consequence unwinds:

    RegisterInstance(class, instance);
    Widget_Place(instance, data, TPLinkPanel);

`Widget_Place` registers the instance at the location under the name it was
given, then walks the table. The table lists panels before controls, which
is the same rule one level down: a control names the panel it goes in, so
the panel has to exist first. No task, no second phase, nothing to
remember, and therefore nothing for a copy to inherit.

## The engine was also renaming everything it was handed

With the constructor placing itself, the log showed what creation had been
doing all along:

    Checkbox_1  -> Enable
    TextOut_1   -> NetStatus
    LED_1       -> Status
    View_1      -> Help
    Markdown_1  -> HelpText
    TPLink_1    -> TPLink

Every object created twice-named. `CreateObject` minted a name from the
class, registered it, and the caller - who had known the real name before
it called - renamed it immediately.

Waste, for a control. For a widget it was the bug: the constructor placed
itself at `/Root/Palette/TPLink_1`, built nine controls INSIDE that path,
and was then renamed to `/Root/Palette/TPLink`. The controls kept the old
container. The widget looked empty.

So creation takes the name and keeps it. Minting is for a caller with
genuinely no name - a palette drop. Renames at boot went from every object
to zero.

## What the clone became

`CloneObject` used to be a second creation path: it called the class's
`InstanceStart` directly, took `LastInstance`, and copied properties. It
skipped naming, registration, and everything else creation does.

Now it calls `CreateObject`, so a clone is made the one way instances are
made, and `CloneData` copies only values onto it. Identity is not data -
`Name` and `Container` are skipped, because the clone was created in its
own place under its own name.

And pass 0 of the group walk stopped making duplicates. Creating the clone
builds its declared panel, so the member already exists; the walk writes
the source's values onto it. Only something the class does NOT declare -
a thing dropped into a plain view - is still created.

Structure comes from creation. Data comes from the copy. One rule instead
of two paths that had to be kept agreeing.

## What came out

Deleted, with no references left: `Widget_BuildOnce`, `Widget_BuildTask`,
`Widget_BuildTaskQuiet`, `Widget_BuildDone`, `Widget_DeferBuild`,
`Widget_DeferBuildQuiet`, `Widget_CancelBuild`, the `PanelBuilt`,
`WidgetTable` and `WidgetBuildTask` properties, and the private copies of
the same pattern that ComfyUI, Ollama and StableDiffusion each carried.

`CreateTask` no longer appears in widget.c at all. Every widget used to arm
one; the comment on `Widget_BuildDone` admitted the palette alone burned
about twenty-eight task entries before anyone touched anything.

Each widget went from three calls - defer in the constructor, build-once in
the placement setup, cancel in the destructor - to one line.

Thirty-three instances place and build themselves at boot, none empty, zero
errors, zero renames, and the default app still serves its page.

## The detour, which is the part worth keeping

Told the panel would not build, I did not go and look. I redesigned. The
class chain existed, so I made `CreateObject` punt `msg_initialize` up it
and had the Widget class build the panel on the way past - a mechanism, a
stamp, a per-instance table property, and a walk. It was the Alias mistake
again: a relationship turned into a thing.

Asked why a flag was needed for something that happens once, I gave three
explanations without opening the code. Two racing entry points - invented.
A re-entrancy guard - invented. The widget's own Activate - the function is
called `*_Activate` because that is the hook name, and the comment two
lines above it says the framework "has no notion" of a user activate step.
I cited the line number without reading the line.

And `objects/demo/udpwidget` had answered all of it years ago. The panel is
declared data compiled beside the code. The C file is handlers and nothing
else. Per-instance bookkeeping (`already_active`) lives in the private
struct where no copy can reach it. Cloning is a message the widget answers,
and the clone is explicitly brought to life rather than assumed to arrive
alive. Four answers, sitting in the repo, while I invented four wrong ones.

The bug was one line of my own construction bookkeeping being visible to a
copy. Finding it took reading the working code, which was the one thing I
kept not doing.

---

## The fallout, which was most of the night

The widget fix went in and the harness went from green to sixty-five
failures. All of it was one thing arriving late.

**Creation now does more than it used to.** It takes a name and keeps it,
and a widget's constructor builds its children inside itself. Every caller
written against the old contract - create it, then name it; create it, then
ask whether the name is free - was suddenly wrong, and each was wrong in its
own way.

### The engine named its own regression

689 errors, all from one line, all the same:

    '/Root/Palette/Writer' calls itself 'Enable' but that path is not in
    the index - a sibling holds the name, or a rename never re-keyed

That is `RequirePathOf`, written this morning for exactly this - the
assertion that used to be a silent `if (!PathOfInstance(...)) continue;`.
It named the victims, the reporting site, and the shape of the fault before
anyone had a theory. The morning's diagnostic work paid for itself the same
day it shipped.

### Six sites, one rule

**Create-then-name** broke wherever a thing was made unnamed and registered
afterwards. `Bridge_Create` did it for every client create: the constructor
built nine controls under a minted `TCPPort_1`, then the bridge registered
the client's alias, and nine controls were left pointing at a container that
no longer existed. `CloneObject` did the same. Both now hand the name in.

**Ask-then-mint** broke wherever "is this name taken?" ran after creating
the thing that took it. `ImportCreate` renamed every imported instance;
`ImportAliasesPass` took the correct `Slider_1_Value_1` that `CreateAlias`
had just produced and renamed it to `Slider_3` one line later;
`Bridge_Internals` would have done it to every panel member. The rule they
were all missing: **an instance holding its own name is not a collision.**

**Adoption**, which the clone had already learned, had to be taught to the
import - and then taught its limit. A widget builds its declared panel when
it is created, so by the time the file's copy of `Enable` is read, `Enable`
exists; making a second leaves two instances answering to one path. But
adopting *everything* meant an import into the container that already held
the original adopted the original and wrote the file over it. The line is
the one the code already drew: internals adopt (`force=1`), the dropped-in
top does not.

### Init work with nothing left to call it

`ScriptBox` brought its interpreter up in `ScriptBox_Activate`, and the only
thing calling that at creation was the deferred build I had just deleted.
Fresh boxes still worked, because setting `Language` or `Source` fires a
handler; a *restored* box did not, because a load writes those back with
`SetPropStr` - restoring state deliberately is not a user typing - so no
handler fired and no host was ever made.

My fix was to build the interpreter in the constructor. That started a Lua
interpreter for the palette's own seed at every boot, made the palette slow
to open, and did not fix the test. Reverted. Construction is not the place
for it: a palette seed is a real instance, and "no action on startup" means
no interpreter either.

### The one the suite could not see

Then, by hand, in the browser: an imported widget came back with its
checkbox blank, no language selected, and no script.

Three symptoms, one cause. Every control in every panel has its `Value`
**linked** to the widget's own property - that is what `Widget_Ctl` does -
and the import was writing the file's saved value straight into the link
slot, replacing a live pointer with a dead string. The widget still had its
`Source`; nothing pointed at it any more.

`CloneData` already refuses this going out: *a link is not a value*. The
import needed the same rule coming in.

**That would have killed every widget on import** - every Slider, Checkbox,
Dropdown and Textbox in all twenty-nine of them, showing a frozen value and
writing nowhere. And the suite was green. `viewclonetest`'s import test
builds plain Sliders in a plain View, so the only link in it is an alias,
which is re-made rather than restored. `scriptedwidgettest` caught it only
because its script *content* rides on a linked control, so a dead link reads
as "the script vanished".

### The test that should have existed

Export a widget, import it, export the copy, compare the two files. Same
instances by path, same classes, same property bags, same wires - only the
top-level name and the drop position may move.

`tools/dfdiff.py` already did the comparison, keying instances by path
relative to the export root and comparing property bags as sets, so it was
twenty lines of test rather than a new tool.

Proven by putting the bug back:

    Port.AccumulateRx: '1' -> ''
    Port.AutoClose:    '1' -> ''
    Port.BinaryRx:     '0' -> ''

Every widget property emptied by one round trip, named individually. A test
that has never been seen to fail is a test nobody has checked.

Two harness faults surfaced doing it. `dfdiff.load` calls `sys.exit(2)` on a
missing file, which killed the suite mid-run and reported as a bare exit
code with no failing test attached. And the engine writes flow files
relative to ITS cwd - the variant directory - while the test process runs
from the repo root; the older export test never noticed, because only the
engine ever read its file.

### What it cost, and what it was worth

One line of construction bookkeeping visible to a copy. Finding it took a
night, most of which was me theorising instead of reading, and the fix took
out seven functions, three properties, a task per widget, a whole second
creation path in the clone, and forty-odd renames per boot that nobody had
ever noticed happening.

The engine found its own regression twice - once with an assertion written
that morning, once with a `DELIVER` against a `STORE` in a log. Both times
the evidence was already there before the first guess.
