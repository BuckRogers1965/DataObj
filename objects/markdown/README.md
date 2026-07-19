# Markdown

A rich-text presentation sink: whatever markdown text it holds is
*rendered* in the client — headings, emphasis, `code`, lists — instead
of shown as raw characters. The first per-object README in the tree,
written for the object that will one day render all the others
(roadmap Phase 2.7: Help = a View holding a Markdown control fed a
class's README).

## Ports and properties

- **Value** — the markdown text being displayed. A data property like
  any other: set it, wire something into it, subscribe to it.
- **In** — dataflow input; whatever arrives becomes Value. Wire any
  source's output here to display it formatted. EOF is ignored — a
  display keeps showing the last thing it was handed.
- **Enable** — the standard enable line: 1 shows updates, 0 freezes
  the display (arrivals while disabled are dropped, not queued).
- **State** — the standard lifecycle LED.

## Notes

Engine-side this is a pure display sink (no task, no editing, nothing
scheduled) — the same shape as TextOut. The rendering itself is
presentation and lives with the projector, keyed off the published
`PROP_MARKDOWN` widget type; the engine only stores and fans out text.
