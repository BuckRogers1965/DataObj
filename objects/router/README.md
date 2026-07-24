# Router

Front-of-fabric **protocol demux**. Raw bytes from a TCP connection arrive on
`Wire`; the router peeks at each connection's first bytes and routes it to the
right protocol handler - an `Http` target for a plain HTTP request, a `WebSocket`
target once a connection upgrades. The two targets are wired in after creation
(`HttpTarget` / `WsTarget`).

## Controls

- **Enable** - gates routing; any source can drive it.
- **State** - the lifecycle LED.
- **Wire** - the raw input port; the panel shows the last bytes seen.
