# Stats

The fabric's own **leak detector**. On a timer (every `Interval` ms) it samples
the core's live alloc counters and writes the changed ones into its readouts, so
a glance shows whether anything grows and never shrinks across a create/destroy
cycle.

The counters, by allocation type:
- **Nodes** / **Datas** - tree nodes and typed values.
- **Envelopes** - queued messages (0 at rest).
- **Tasks** - scheduler task structs (pooled).
- **Buffs** / **Queues** - dynamic buffers and message queues.

## Controls

- **Interval** - the sampling period in milliseconds.
- **Enable** - 1 samples, 0 pauses.
- **State** - the lifecycle LED.
