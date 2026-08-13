# Filter

A mid-flow **gate**. Each message arriving on `In` is tested against `Mode`;
what passes becomes the value of `Out`, and that write IS the delivery - the
property carries what left and its fan-out is what reaches the subscribers.
`msg_eof` always passes, so a stream can always finish downstream; it goes as a
message, having no value to hold.

`Mode`:
- **all** - pass everything.
- **change** - pass only when the value differs from the last one seen (dedupe).
- **ones** - pass only `1`.
- **zeros** - pass only `0`.
- **none** - pass nothing. A closed gate that still shows its traffic on `In`,
  which is not the same as disabling the filter (see `Enable`).

Wire a source into `In` and `Out` into a consumer. It schedules no task and never
holds the program open.

## Controls

- **Mode** - the pass rule (above).
- **Enable** - gates the filter; any source can drive it.
- **State** - the lifecycle LED.
- **In** / **Out** - the flow properties, and the readouts of them. `In` holds
  whatever arrived, passing or not; `Out` holds what most recently passed. A
  disabled filter updates neither.
