# Out

A **debug probe**. Subscribe its `In` port to any source port and it prints
every message that flows past to standard out, tagged with its `Label`, the
message id, and the payload size. On EOF it prints the message and byte totals.

It watches, it never consumes: a probe can be dropped onto any connection
without changing the flow. It schedules no task, so it never holds the program
open or changes when the system quiesces.

## Controls

- **Label** - the tag printed in front of every line (defaults to `probe`).
- **Echo** - on by default; turn it off to silence the probe without
  disconnecting it.
- **Enable** - gates the printing; any source can drive it.
- **State** - the lifecycle LED.
