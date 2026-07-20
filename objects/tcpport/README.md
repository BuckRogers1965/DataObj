# TCPPort

The TCP instrument panel, ported from the VNOS control of the same name
(`objects/demo/tcpport`). It is a **front end, not a networking
object**: it holds no socket, it contains a TCP engine instance and
drives it — the same shell/engine split ScriptBox uses for language
hosts.

Default input connection is to Transmit Data; default output connection
is to Receive Data. So it drops into a flow like any other object, and
what it *is* underneath stays invisible from outside.

## Connection

- **HostName** — the host, normalized on Activate exactly as the
  original did: leading whitespace stripped, cut at the first
  whitespace, lower-cased. No protocol prefix (`www.host.com`, not
  `http://www.host.com`).
- **StandardPort** — the six-service menu (FTP, TELNET, SMTP, HTTP,
  POP, HTTPS); picking one writes **Port** (21/23/25/80/110/443).
- **Port** — the numeric port.

## Commands

`Open`, `Listen`, `Close`, `Send`, `ClearTx`, `ClearRx` are ordinary
**in ports** that act on a `1`. That means the panel's MoButtons press
them, and so can a Pulse, a script, or another object — a command is
not a privileged kind of thing here.

- **Listen** — brings the engine up as a server on `Port`.
- **Close** — a full stop: sockets close and the engine shuts down.
- **Send** — sends `TxData`, only when connected and only if there is
  something to send; clears the box afterwards if **ClearOnSend**.
- **Open** — client connect. The engine's connecting state machine is
  still to come (see ROADMAP, TCP client mode); until it lands, Open
  reports the gap through the debug log rather than failing silently.

## Data

- **TxData** / **RxData** — the Transmit and Receive boxes.
- **AutoSend** — send as soon as something arrives on `In`.
- **AccumulateRx** — append received data rather than replacing it.
- **BytesReady** — how much is sitting in `RxData`.

## Options and status

- **AutoOpen / AutoListen / AutoClose** — mutually exclusive, as in the
  original: setting one clears the other two. Honored on Activate and
  whenever Enable goes high.
- **StreamState** — `DISABLED`, `IDLING`, `LISTEN`, `OPENING`,
  `CONNECTED`, `CLOSING`.
- **Listening / Connected / Idling / Disabled / RxReady / TxReady** —
  status LEDs, set together with StreamState so a readout and its
  lights can never disagree.
- **Enable** — the standard enable line; dropping it is a full stop.

## Notes

The TCP engine's activation is one-shot (`Enable=0` is a shutdown, not
a pause), so listening again after a close means a fresh inner
instance. The widget hides that lifecycle — which is what a front end
is for.
