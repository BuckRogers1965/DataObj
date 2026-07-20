# Findings — 2026-07-19 session

Working the harness one case at a time. Only clone and alias have been
looked at so far.

## Status

| case | result |
|---|---|
| clone (palette -> view) | PASS |
| clone is a snapshot, then independent | PASS |
| alias -> alias atom bound to the original | PASS |
| writing through an alias writes the original | PASS |

Both were verified on a **freshly started server**. That matters — see
"testing landmine" below.

## Bug found and fixed while getting there

**`create-instance` with no container silently did nothing.**

The harness's `make_test_view` creates a View with no `container`. The
instance was created, but its `instance-created` event was addressed to
container `""` — and a connection only receives events for containers it
has actually listed (`Bridge_MarkViewing` / `Bridge_SendEventScoped`).
Nobody views `""`, so the event went nowhere: no error, no event, the
client waited forever for a view that existed on the engine.

Fix (bridge.c, `Bridge_Create`): **no container means the root view, all
the way through** — lookup, placement, and the event's scope. Previously
only the lookup defaulted to `/Root` while placement still used `""`.

Cause: mine, from making the canvas a real root view earlier today.

## Testing landmine (cost me two wrong diagnoses)

A long-lived test server accumulates instances across runs. `test_clone`
looks up "a Slider in CloneTest" and grabbed a leftover from an earlier
run, so:

- the snapshot test read 99 instead of 42 and "failed"
- `test_alias` timed out looking for an atom bound to the slider it
  thought it had

Both were artifacts. **Restart the server before drawing any conclusion
from a harness case.** run.sh already does this; running cases by hand
does not.

## REPRODUCED: cloning a view drops its aliases

Steps (raw protocol, clean server):

1. `/Root/VC` — a View
2. `/Root/VC/S` — a Slider in it
3. `/Root/VC/S_1` — clone of that Slider, in the same view
4. `/Root/VC/Alias_1` — an Alias of `S.Value`, in the same view
5. connect `S.Value -> S_1.Value`
6. clone `/Root/VC`

Result — list the two views and compare:

    /Root/VC    -> S (Slider), S_1 (Slider), Alias_1 (Alias)
    /Root/VC_1  -> S (Slider), S_1 (Slider)          <-- no Alias

Both Sliders copy. **The Alias is dropped silently** - no error, nothing
in the log, the clone is just one member short.

Where to look: `CloneView` (object.c) walks the source view's members by
their `Container` and copies each one. The Alias case fails in that walk.
A likely shape: the copy is made, `LinkPropertyAs` fails for it (the link
has to be re-pointed at the copied target, which is the fiddly part of
cloning a view - see readmefirst's note that CloneView "remaps the alias
links and the wires"), and the failure path deletes the copy and
continues without saying anything.

First thing to do: make that walk say why it dropped a member
(DebugPrint, CLONE category already exists) rather than guessing.

### "The second clone does nothing" is a SEPARATE bug: the gesture misses

Verified on a fresh server driving real mouse events. When the view's
icon is actually clickable, clone gestures work every time - 1st, 2nd,
3rd, and clone-of-a-clone all send `clone-instance`. What makes it look
dead:

1. **A view can only be grabbed by its icon or its header.** The panel
   body has no drag handler, so with the panel OPEN (icon hidden) a
   clone/move gesture on the view reaches nothing at all.
2. **An icon under another panel swallows the click.** A view placed
   under the palette is unreachable - `elementFromPoint` at the icon's
   own centre returns the palette's `view-inner`. Clones also land
   overlapping, so a second gesture aimed where the original was can hit
   the clone, or nothing.

Both fail **silently** - no error, no ghost, the gesture just evaporates.
Two things worth fixing: give a view the same whole-surface grab in a
mode that cards got, and make a gesture that lands on nothing say so.

Every clone made this way still comes back WITHOUT the alias, confirming
the CloneView bug above is independent of how the clone was triggered:

    /Root/VC    -> Slider, Slider, Alias
    /Root/VC_1  -> Slider, Slider
    /Root/VC_2  -> Slider, Slider

### Second landmine, same family as the first

The clone's members are created inside the NEW view, so their
`instance-created` events are scoped to a container the client is not
viewing yet. Watching the event stream makes a clone look **completely
empty**. Always `list-instances` the new view to see what really came
over.

## REPRODUCED: delete a view, clone again -> a garbage-named EMPTY view

Raw protocol, fresh server:

    V1 members:  ['Alias_1', 'S']
    clone A ->   /Root/V1_1        ['S']       (alias dropped - see above)
    delete       /Root/V1_1
    clone B ->   /Root/V1_1/S      []          <-- named as a MEMBER path, empty
    clone C ->   /Root/V1_2        ['S']       (recovers)

Deleting a view unregisters the VIEW but leaves its MEMBERS' paths in the
namespace index: `/Root/V1_1/S` still resolves to a dead instance. The
next clone mints `/Root/V1_1` (now free), collides with that stale member
entry, and ends up named `/Root/V1_1/S` with nothing in it. The clone
after that skips to `V1_2` and works - which is why it looks intermittent
rather than broken.

Where to look: the delete path (`DeleteInstance` in object.c and
`Bridge_Delete`). Deleting a container has to `UnregisterPath` every
descendant, not just the container itself - the same subtree walk
`Bridge_RepathSubtree` already does for renames. Compare the two: rename
walks the subtree, delete does not.

Worth checking at the same time: an earlier probe deleting a palette
entry returned neither a refusal nor an `instance-removed` event.

## Not yet looked at

- clone-of-alias
- alias-of-clone
- options/internals
- move, open-close, rename, lazy contents

## Known-outstanding from earlier today (unrelated to clone/alias)

- **44 `CreateObject` refusals at boot**, all logged as errors. These are
  `BuildSettingsView` controls (LED/Button/Textbox/TextOut/Knob) and
  ScriptBox's inner host, created during `InstanceStart` — before the
  instance has a path, so they have nowhere to live. They used to land
  silently in the root; now they are refused and named. The objects
  concerned lose their settings controls until this is addressed.
- **A palette entry delete returned neither a refusal nor an
  `instance-removed`** in one raw probe. Not chased. May be timing.
