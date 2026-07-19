# HTML

A rich presentation sink, the Markdown widget's sibling: whatever HTML
it holds is rendered in the client. Markdown is for prose; this is for
anything with real layout — a table of results, a formatted report, a
legend, a diagram someone else's tool emitted.

## Ports and properties

- **Value** — the HTML being displayed. A data property like any
  other: set it, wire something into it, subscribe to it.
- **In** — dataflow input; whatever arrives becomes Value. EOF is
  ignored — a display keeps showing the last thing it was handed.
- **Enable** — the standard enable line: 1 shows updates, 0 freezes
  the display (arrivals while disabled are dropped, not queued).
- **State** — the standard lifecycle LED.

## Notes

Display is display, never execution: the client renders the value
inside a **sandboxed frame**, so HTML pushed through a flow can style
and lay out freely but can never run script or touch the page around
it. That guarantee is the browser's sandbox, not a sanitizer — there
is no filter to keep current.

Engine-side this is the same pure display sink Markdown and TextOut
are: no task, no editing, nothing scheduled.
