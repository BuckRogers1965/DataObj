# WebSocket

A **WebSocket translator**. It sits between a TCP connection and the app fabric,
speaking RFC 6455: it completes the handshake, unframes inbound frames into
messages, and frames outbound messages back into the connection.

- `Wire` in / `Send` out - the TCP-facing side (raw bytes to and from the socket).
- `In` in / `Out` out - the app-facing side (messages to and from the flow).

Inbound bytes that aren't valid UTF-8 are sanitized for the browser at this
transport only; the data model keeps the raw bytes.

## Controls

- **Enable** - gates the translator; any source can drive it.
- **State** - the lifecycle LED.
- **In** / **Out** / **Wire** / **Send** - the four ports; the panel shows the
  last message on each.
