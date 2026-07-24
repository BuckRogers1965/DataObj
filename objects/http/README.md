# Http

A minimal **HTTP server front-end**. Requests arrive on `In` (from a TCP
object's `Out`); it serves files from under `Root` and sends the response out
`Out` (back to the TCP object's `In`). It holds no socket of its own - the TCP
object owns the connection; Http just speaks the protocol.

Wire `TCP.Out -> Http.In` and `Http.Out -> TCP.In`.

## Controls

- **Root** - the document root directory (defaults to `.`).
- **Enable** - gates the server; any source can drive it.
- **State** - the lifecycle LED.
- **In** / **Out** - request in, response out; the panel shows the last of each.
