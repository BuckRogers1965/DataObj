# Queue / Stack

A message buffer that **preserves message boundaries** (unlike a byte buffer).
One module, two classes - the only difference is pop direction:

- **Queue** - FIFO: pops the oldest entry first.
- **Stack** - LIFO: pops the newest entry first (so an EOF pushed last pops
  first - that is what reversal means).

Fully passive, no task - it is driven by whatever it is wired to:

- `In` pushes an arriving message (its id rides along, so `msg_eof` is buffered
  in-band).
- `Clock` pops exactly one entry per arriving message and sends it out `Out`
  with its stored id; an empty pop is a silent no-op.

Drive `Clock` with a Pulse for a rate limiter / clocked shift register. A
disabled buffer still accepts pushes but ignores its Clock.

## Controls

- **Enable** - gates the Clock; any source can drive it.
- **State** - the lifecycle LED.
- **In** / **Clock** / **Out** - the three ports; the panel shows the last
  message on each.
