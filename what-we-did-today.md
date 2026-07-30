# What we did today

## Bridge rename bug (three separate bugs, one code path)

Reported problem: renaming a view from its own options panel didn't update
live. The panel and its controls kept showing the old name until you
reloaded the page.

Found three separate bugs in `objects/bridge/bridge.c`:

1. **Rename events for a top-level thing never reached anyone.**
   `Bridge_Rename`/`Bridge_RenameName` scope every rename event to whoever
   is viewing wherever the renamed thing lives. For something at the top
   level that's `"/Root"` — but the code was collapsing `"/Root"` down to
   an empty string before sending the event. The bridge's own viewing
   registration doesn't treat `""` and `"/Root"` as the same key, so the
   event got sent to a key nobody was actually registered under. Fixed by
   not collapsing it.

2. **Only 2 of 14 properties on a renamed object's options panel actually
   updated.** `Bridge_Set` grabs the renamed thing's current path from a
   helper (`Bridge_AliasForInstance`) that hands back a pointer into a
   small 4-slot reused buffer. Renaming something with many published
   properties triggers one property-change notification per property, and
   each one calls that same helper again, rotating the buffer. By the time
   the rename code used the pointer it grabbed earlier, it had already been
   overwritten by later calls — so only the first two properties (out of
   fourteen) got the correct new path; the rest kept the old one. Fixed by
   copying the string out immediately instead of holding onto the pointer.

3. **The options panel itself would disappear on rename**, even after
   fixes 1 and 2. Opening an object's "internals" (its dissection/options
   panel) never registered the browser tab as viewing the container that
   panel actually lives in. So the rename event for the panel itself had
   nowhere to be delivered. Fixed by registering that viewing relationship
   when internals are first requested.

Added a new browser test
(`test_rename_cascades_into_own_options_panel` in `testharness/guitest.py`)
that reproduces the exact reported scenario and checks all three fixes at
once. Ran the full test suite before and after — the handful of remaining
failures are pre-existing and fail identically on the unmodified code, so
nothing else broke.

## TPLink widget

Built a new widget (`objects/tplink`) to control a TP-Link smart plug over
the network — configure host/port in a Settings sub-panel, see on/off
status, turn it on/off/toggle — using the TCP object instead of talking to
sockets directly, following the same pattern TCPPort uses.

Made several real mistakes building it, in order:

- **Deleted a contained TCP instance from inside a callback that instance
  itself was in the middle of running.** A property write in this
  framework fans out synchronously, not queued — so reacting to the TCP
  object's own "Connected" property by deleting that same object, right
  there in the handler, freed memory the TCP object's own currently-running
  task was still using. That corrupted the scheduler and pegged the whole
  framework at 100% CPU, not just this widget.

- **Had the widget automatically check status shortly after being enabled
  or placed.** Didn't account for the fact that every loaded class gets a
  real, live instance created automatically as its own palette icon at
  boot. So the palette's own copy of this widget was making a real network
  connection attempt every time the server started, before anyone ever
  dragged one onto the canvas. Removed the automatic check entirely —
  status is now only ever checked when Refresh (or On/Off/Toggle) is
  pressed.

- **Reused one timeout task object and re-armed it from a button press
  instead of from the task's own firing.** That's not safe in this
  scheduler — re-arming a task from outside its own callback while it
  might still be scheduled corrupts the task list. Fixed by creating a
  fresh task each time an operation starts, deleting the old one first.

- **The Enable checkbox didn't actually work.** Disabling the widget and
  pressing On still turned the plug on. The bug: the widget's Enable
  handler only accepted messages with a specific id (`msg_send`), but
  clicking the checkbox on the panel delivers its value change as
  `msg_change` — an ordinary property change — not `msg_send`. The handler
  was silently dropping every real click on the checkbox. Fixed by
  accepting anything except `msg_eof`, matching every other handler in the
  file. Confirmed with debug prints showing the actual message ids, which
  is what caught the exact bug.

## Skeleton widget template

The project keeps a copyable template (`objects/skeleton`) for starting new
widgets. It had the exact same Enable bug described above. Fixed it there
too, and added four new numbered lessons to its README documenting what
went wrong today, so the next widget built from it starts from these
instead of hitting them again:

- the `msg_change` vs `msg_send` trap on Enable
- never delete a contained instance from inside a callback it triggered
  synchronously
- never re-arm a reused task object from outside its own callback
- `Activate` must never do anything automatically, because it also runs on
  the palette's own seed instance at boot
