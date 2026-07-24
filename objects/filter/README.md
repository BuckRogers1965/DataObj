# Filter

A mid-flow **gate**. Each message arriving on `In` is tested against `Mode`;
passers are forwarded out `Out` (as a fresh copy). `msg_eof` always passes, so a
stream can always finish downstream.

`Mode`:
- **all** - pass everything.
- **change** - pass only when the value differs from the last one seen (dedupe).
- **ones** - pass only `1`.
- **zeros** - pass only `0`.

Wire a source into `In` and `Out` into a sink. It schedules no task and never
holds the program open.

## Controls

- **Mode** - the pass rule (above).
- **Enable** - gates the filter; any source can drive it.
- **State** - the lifecycle LED.
- **In** / **Out** - the flow ports; the panel shows the last message on each.
