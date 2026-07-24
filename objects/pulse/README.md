# Pulse

A pure **clock** source - no file, no socket, just timing. It sends `1` out its
`Out` port, then `0` one `Interval` (milliseconds) later, and repeats.

`Count` is the number of complete pulses (a 1 then a 0) to send; `0` means pulse
forever (and intentionally holds the program open). A finite train ends like any
stream: after the last `0` it sends `msg_eof` out `Out` and stops.

Placing a Pulse does not start it - it ticks once a flow activates it. Wire `Out`
into anything that wants a clock (a Queue's `Clock`, an object's `Enable`, an LED).

## Controls

- **Interval** - the half-period, in milliseconds (read live - retune while running).
- **Count** - complete pulses to send; 0 = forever.
- **Enable** - gates the ticking; any source can drive it.
- **State** - the lifecycle LED.
- **Out** - the output edge, shown as an LED turning on and off.
