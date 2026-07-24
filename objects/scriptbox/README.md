# ScriptBox

A **scripted widget shell**. It holds no interpreter itself - it CONTAINS a
language-host instance (Lua, JavaScript, ...) and drives it, so the same box
runs any installed language. Pick a **Language** and the inner host is swapped
(your Source carries over); press **Run** and the current Source is handed to
the host and executed.

- **Language** - a menu of every installed script host (discovered live: drop a
  new language `.object` in and it appears here).
- **Source** - the script.
- **Run** - runs it (= Activate).
- **Output** - the host's print output and errors, accumulated.
- **In** - a visible toggle wired straight into the script's own `In`, so an
  input-driven script can be poked by hand; a flow can drive it too.
- **Out** - the script's dataflow output, shown as a readout.

Placing a ScriptBox does not run anything - it stays quiet until you press Run.

## Controls

- **Enable** - gates the box; any source can drive it.
- **State** - the lifecycle LED.
