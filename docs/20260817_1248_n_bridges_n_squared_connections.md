# N bridges, N-squared connections

*Where this is already useful, and the question I am holding open, 17 August 2026.*

Right now, on this machine, the following are running as objects in one
process and can be wired to each other by dragging a line in a browser:

- **Ollama** - chat completions against an OpenAI-format endpoint, on a
  non-blocking socket, with a model dropdown populated from `/v1/models`.
- **ComfyUI** - a prompt spliced into an API-format workflow at `%%prompt%%`,
  queued, polled to completion, and the resulting image URL published.
- **Stable Diffusion** - AUTOMATIC1111's `/sdapi/v1/txt2img`, with live
  percent-done polled off `/sdapi/v1/progress` while it renders.
- **MCPSource** - connects to an MCP-style agent service, asks it what agents
  it has, and **builds a real palette widget per agent**: one input box per
  declared input, one readout per declared output, the agent's own help text,
  and a Submit button.
- **TPLink** - a smart plug on the local network, because the same machinery
  that talks to a model talks to a relay.
- **Rest** - the framework's own tree published as a REST surface, a sibling
  of the web bridge rather than a layer on it.

Plus the primitives they are all made of: TCP and UDP in client and server
mode, HTTP, WebSocket, async DNS, three scripting languages (Lua, JavaScript
on QuickJS, Forth on atlast), and the small transforms - Base64, Bin2Hex,
RegExp, CharacterMap, Filter, LogicGate, Queue, Stack, Pulse.

That is not a demo list. Every one of those has a panel, a help file, and
survives clone, save, export and import, because the framework does those
things for everything and none of them had to ask.

## The arithmetic

Here is the pitch, and it is just multiplication.

Every one of those objects speaks the same fabric: it receives a message
carrying a value, and it sends messages carrying values. There is no
adapter *between* adapters. Ollama does not know ComfyUI exists. RegExp does
not know either of them exists. The Queue does not know what it is holding.

So the work of connecting a new system is **N**, not N-squared. You write
one object that speaks HTTP-and-JSON to your thing, or Modbus, or MQTT, or
a serial protocol, or a binary framing nobody has heard of - and the moment
it lands in the scan path it can reach **everything already there**, in both
directions, without anybody writing the pairing.

Some pairings nobody built, that already work:

    Ollama.Output  ->  RegExp  ->  ComfyUI.Prompt
        an LLM writes the prompt, a regex strips its preamble,
        an image comes out the other end

    MCPAgent.Result  ->  Filter (Mode=change)  ->  TPLink.Enable
        an agent's decision turns a physical relay on, and the
        filter means it only fires when the answer actually changes

    Pulse  ->  Queue.Clock,  requests -> Queue.In
        a rate limiter in front of an expensive model, made of two
        objects that predate every AI object in the tree

    Stats.Nodes  ->  Textbox
        a live allocation readout, because the fabric is its own
        instrument

None of those combinations were designed. They are what falls out of
everything being the same kind of thing.

## Why the adapters are cheap

An adapter here is: a poll task, a buffer, some properties for
configuration, and messages for data. That is the whole shape. What you do
*not* write:

- the socket - you contain a TCP object in client mode and drive it, which
  is what TPLink, MCPSource and the rest all do. None of them opens a
  socket.
- the UI - a widget declares its panel as a table and the framework builds
  it. One call, at construction.
- the help - a `README.md` next to the source, loaded on demand.
- clone, save, export, import, live rewiring, multi-client observation -
  all of it already true of anything that is a node, which is everything.
- the concurrency - there isn't any. Ollama's README puts it plainly: the
  request "runs on a non-blocking socket driven by a poll task, so a long
  generation never freezes anything else." No thread, no lock, no async
  runtime. Arm a task, check the socket, re-arm.

So the effort is the *protocol*, and only the protocol. That is the actual
claim behind the multiplication: N is small because N is only the part that
is genuinely specific to your system.

## Why AI systems in particular

Because the impedance match is almost embarrassing.

Everything in this space is stringly-typed JSON over HTTP, one slow request
at a time, with a second endpoint you poll for progress. The framework's
DataObj is a value that holds every representation of itself and converts on
demand, so a number that arrives as text is a number when something wants a
number - no schema, no codegen, no marshalling layer. The scheduler is built
out of poll-and-re-arm, which is exactly the shape of "queue a job, ask if
it's done." And messages carry payloads by pointer with no copies, so
passing a 40 KB model response between six objects costs six pointer moves.

The pieces of an AI pipeline are also *exactly* the pieces this framework
was already good at: something slow and remote, something that transforms
text, something that gates or rate-limits, something that shows a human what
happened, and something that decides. The last three weeks of core work were
not aimed at AI. They just left a fabric where an agent is one more object.

MCPSource is the sharpest version of it. Point it at an agent service and
each remote agent becomes **a real class in the palette** - draggable,
clonable, wirable, with its declared inputs as boxes and its outputs as
readouts. The agent is not consumed through a special client. It becomes a
peer of the Queue and the LED. And the roadmap's other half - exposing this
framework itself as an MCP server, so an agent can build flows in it - is
the same trick pointed the other way.

## The question I am holding open

I am not telling you to put this in production this month. Here is exactly
where it has to grow, and I would rather say it than have you find it.

**There is no security boundary.** Objects are isolated by message
discipline, not by enforcement - a loaded `.object` is code in the process,
with everything that implies. The authenticated bridge flow exists but is
currently disabled in the default app. Run it on a network you trust.

**The fabric is single-threaded on purpose, and that is a contract you have
to keep.** One blocking call inside one badly written object stalls
everything, which is why the DNS resolver had to be quarantined behind a
worker thread and a sentinel flag rather than invited in. Every adapter
must be written non-blocking. Today that is a discipline, not something the
engine enforces.

**Stringly-typed is a feature until it isn't.** Text-with-conversion is why
integration is so cheap here, and it will stay cheap right up to the point
an agent needs genuinely nested, genuinely typed structure. There is no
schema layer. What that should look like - and whether it can arrive without
becoming the very type system this design avoided - is an open question,
not a solved one.

**Only the web surface is finished.** A control's browser half already ships
inside its own `.object` - `show/web/*.js` and `*.css` compiled in by
`show.mk` and published at ClassStart, sixteen controls converted, and no
per-control branch left in `web/app.js`. The other surfaces are not built:
there is no `show/mcp` or `show/rest` yet, and the roadmap's own open
question is whether a schema-shaped presentation is even the same kind of
artifact as a code-shaped one. So an agent composing flows still works
through the JSON bridge like a browser would, rather than reading a contract
the object declares about itself.

**Hot reload is half-built.** `_fini`, `UnregisterLibrary` and
`UnloadClasses` all exist, so swapping one `.object` in a running system is
finish-the-plumbing rather than a design problem. Until it is finished,
installing a fix is copy-the-file-and-restart, not copy-the-file.

**The core has been moving daily.** Three weeks of one to two core mechanism
changes a day is what got it here, and it is why I would not yet promise a
stable API to somebody building on it from outside. That pace is slowing
deliberately, starting now - the interesting work has moved out to the
objects - but "the API has been still for a while" is not a claim I can make
today.

**Quiescence is right for tools and needs thought for daemons.** When
nothing is scheduled the program exits, with nobody calling exit. That is a
lovely property for a pipeline that finishes. If you want a service that sits
idle for a week, you currently keep it alive by having something scheduled,
and the honest answer is that unattended long-run behavior has not had the
attention the interactive path has had.

## So

If you have a system with a protocol and no good way to wire it to the other
systems you own, the interesting question is not whether this framework can
talk to it. It is how small the object would be. My guess, based on the ones
already here, is a few hundred lines and an afternoon - and then it can
reach everything else in the tree, and everything else in the tree can reach
it, and neither side had to be told.

What should the next adapter be?
