# MoButton

The **momentary** button, ported from the VNOS drop-in control of the
same name. Three button-ish controls now exist and they are genuinely
different things:

- **Button** — fires once; an Activate trigger.
- **Checkbox** — latches; it holds the state you left it in.
- **MoButton** — is *held*; pressing sends one edge, releasing the other.

Pressing sends `1` out `Out`, releasing sends `0` — the same
rising-then-falling convention a Pulse emits, so a MoButton is a
hand-driven Pulse and every sink already knows what to do with it.

## Ports and properties

- **Out** — the edges: `1` on press, `0` on release.
- **Press** — the gesture as an ordinary in port (`1` down, `0` up).
  The projector writes it on pointerdown/up, and so can anything else:
  a script or a Pulse can press this button exactly as a finger does.
- **Label** — the button's caption. Engine state, subscribed like any
  property.
- **Value** — `1` while held, `0` otherwise; readable and subscribable.
- **Interval** — auto-repeat in milliseconds while held (the VNOS
  `AUTO_TRACK` variant), for jog/scroll behavior. `0` (the default)
  means no repeat.
- **Enable** — the standard enable line. A button disabled mid-press
  releases first, so a sink is never left latched on.
- **State** — the standard lifecycle LED.

## Uses

Wire `Out` to an `Enable` to hold something on only while pressed; to a
command port (a TCPPort's `Send`) to invoke it; to a Queue's `Clock` to
step a flow by hand. Releasing outside the button counts as a release
without a click, exactly as the original control behaved.
