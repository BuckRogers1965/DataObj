# Resolver

Names in, addresses out.

Type a host name and press **Lookup**, or wire a name into the widget: a name
arriving is the request, so both do the same thing.

- **HostName** — the name to resolve. Also the default input: `In` writes it.
- **Address** — the answer, empty when the name did not resolve. Also the
  default output, so a wire from this widget carries addresses.
- **Lookup** / **Cancel** — ordinary in-ports as well as buttons, so a Pulse or
  a script can press them.
- **Busy** — lit while an answer is owed. **Found** — lit when the last answer
  had an address.
- **Enable** — off means commands are ignored.

## What it is

The panel holds no resolver of its own. It contains a DNS engine object and
drives it through `dns.h`, the same way TCPPort drives a socket. Resolving a
name blocks, sometimes for minutes; the engine keeps that off the main thread
and answers as an ordinary message, so the fabric never stops.

Cancel does what it can: a lookup already running cannot be stopped, but its
answer stops being yours.
