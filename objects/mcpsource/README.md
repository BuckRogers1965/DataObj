# MCPSource

Connects to an MCP-style agent service - raw TCP, newline-delimited JSON,
`{"command":"LIST_AGENTS"}` for discovery and `{"command":"EXECUTE_AGENT",
"agent":"...","params":{...}}` to run one - and turns each agent it finds
into a real, working palette entry (MCPAgent): one input box per declared
input, one readout per declared output, the agent's own help text, and a
Submit button.

It holds no socket of its own - it contains a TCP object, in client mode,
and drives it, the same shell/engine split every network widget here uses.

## Options

**Enable**
- *Checked:* the widget is live. This is the default.
- *Unchecked:* Connect is ignored, and any generated agent widgets whose
  requests are still queued on this connector get told it's disabled
  rather than silently hanging.

**NetStatus**
What the connector is doing right now: Idle (with an agent count once
you've connected), Connecting, Waiting for reply, Busy (a request is
already in flight - try again once it's done), or an Error line.

**Connect**
Asks the configured server for its agent list and builds/replaces one
MCPAgent per agent inside the view named by Settings' ViewName. Safe to
press again later - existing agent widgets are replaced with fresh ones
from the current list.

## Settings

**Host Name / Port**
Where the MCP agent service is listening.

**View Name**
The palette group these agents live under
(`/Root/Palette/<ViewName>/<agent>`). Give different connectors different
names if you're pointing at more than one server, or the same server
twice - otherwise their agents would collide in the same group.

## MCPAgent (generated - not in the palette on its own)

Each one remembers which MCPSource made it (so a clone dropped anywhere
in a flow still knows who to ask) and never opens a connection itself -
Submit hands the call to that connector, which does the actual networking
and delivers the result back into this instance's own output properties.
Its inputs and outputs are ordinary container ports, so wiring something
else's output into one of these inputs (or this agent's output into
something else) works exactly like wiring any two objects together.
