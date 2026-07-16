# testharness — engine and GUI tests against a real running server

The framework's C modules already carry their own self-tests (`./framework -t`).
This directory is the same idea for everything above them, in two layers:

- **raw-protocol suites** (`rawtest.py`, `flowtest.py`, `viewclonetest.py`) —
  no browser at all: a plain TCP socket speaking the same one-line JSON any
  client speaks. This is readmefirst.md's **harness rule** — a mechanism is
  proven in the engine, reachable by any client in one command, before any
  browser is involved. (The raw port isn't open by default; `ensure_raw_bridge`
  composes its own TCP+Bridge through the web socket first — the protocol
  standing up its own transport.)
- **the browser suite** (`guitest.py`) — a **real** headless chromium driving a
  **real** server with genuine mouse/keyboard input over CDP, proving only
  *presentation*: gestures emit the right verb, events paint the right pixels.
  Nothing is mocked — a clone here is the same pick-then-place a person makes.

## Run

    ./testharness/run.sh

The whole ritual, every time: kills any running framework, `make clean &&
make`, starts a fresh server on the default port (8083), runs the raw suites,
then drives the browser, cleans up after itself. `PORT=9090
./testharness/run.sh` if you must use another port; `VERBOSE=1` to see every
check. Build, server, and browser output land in `testharness/logs/`.

To run one suite against something already running (your own server, your own
chromium with `--remote-debugging-port`):

    python3 testharness/rawtest.py --host 127.0.0.1 --port 8091
    python3 testharness/guitest.py --app http://127.0.0.1:8085 --cdp 9223

## What it covers

Raw-protocol (no browser):

- **rawtest** — the verbs themselves: atomic birth with server naming,
  engine-stamped Widget/Direction, the internals view, save/load into `saved/`,
  one-verb move with containment refusal, event-driven delete.
- **flowtest** — the dataflows that used to boot inside main.c: cat
  (Reader→Writer), filter/gate, queue/clock, stack/clock, TCP echo.
- **viewclonetest** — deep-copying a view: members, aliases, **and the wires
  between them**, over clone, clone-of-clone, and save/load.

Browser (presentation):

- **boot** — the palette view opens with its class seeds; only visible
  containers are loaded (the GUI never hears about closed panels).
- **clone** — pick-then-place from the palette to the canvas; a clone is an
  engine-side snapshot of the source's data, independent afterward.
- **esc** — a picked-up carry cancels cleanly, creating nothing.
- **alias** — an alias atom binds to the original's property; writing through
  the alias writes the original and emits exactly one event, in the
  original's name.
- **move** — dragging writes X/Y back as shared properties; dropping into a
  view re-containers (and renames) the instance.
- **open/close** — icons are permanent and live in the containment hierarchy;
  every panel opens at the ROOT as a peer with its own PanelX/PanelY;
  the corner symbol closes the panel and the icon remains.
- **rename** — a renamed view still resizes and still drags: a gesture
  resolves the CURRENT name at the moment you make it, never the name the
  thing was born with.
- **lazy** — a fresh page load knows nothing about a closed view's contents;
  opening it streams them in.
- **lua** — a Script object's Lua callback counts pulses and speaks out its
  Out port.

## Reading the output

**A passing test prints nothing.** A green run is one summary line per suite:

    raw-protocol: 20 tests, 20 passed, 0 failed
    dataflow flows: 6 tests, 6 passed, 0 failed
    view clone: 7 tests, 7 passed, 0 failed
    browser GUI: 26 tests, 26 passed, 0 failed

This is deliberate: there will eventually be hundreds of tests, and a
passing report nobody reads is pure cost to print, scroll, and (for an
agent) pay tokens for. Silence means it worked.

A **failure** says everything, on the spot:

    TEST     view clone: the members are wired to EACH OTHER
      expected: writing the clone's first slider drives the clone's second one
      observed: driven: False
      result:   FAIL

The exit code is nonzero if anything failed, so this works by eye and in a
script. To watch every check go by, run a suite with `-v` (or
`VERBOSE=1 ./testharness/run.sh`).

## Adding a test

**Start with a test that FAILS.** For a bug, reproduce it as a test and *see
it fail for the reported reason* before writing the fix — a test written
after the fix only proves the code does what you just wrote. Prove behavior
**functionally** (drive it and observe), never structurally: a subscription
pointing at the *wrong* instance looks identical to a correct one from the
outside. And cover the variations — clone, clone-of-clone, alias, connect,
save/load.

Prefer a raw-protocol suite: add a `test_xxx(raw, r)` to `rawtest.py` (verbs),
`flowtest.py` (dataflows), or `viewclonetest.py` (copying groups), and call it
from that file's `main()` through `guarded()`. `raw.send()` sends one command;
`raw.wait_event(pred)` consumes the first matching event; `raw.value_of(inst,
port)` reads a property. Only if the thing under test is *presentation* does it
belong in `guitest.py` as `test_xxx(t, r)` — `t` is the browser (see `cdp.py`:
`js`, `wait_js`, `click`, `pick_place`, `press_drag`, `set_mode`, `center_of`,
`hook_events`/`events`).

`r.expect()` takes the test name, the expectation in words, the observation in
words, and the pass/fail condition — write the first two so a failure explains
itself without opening the code.

State accumulates across tests within a run, the same way it does in a user's
session — each run starts from a fresh server. Two landmines that have bitten
this suite: the real browser viewport is about **1400x811**, so a panel placed
low has its bottom-right corner *outside* the window where synthetic clicks hit
nothing; and `make_test_view` consumes a layout slot, pushing later tests'
panels down a row toward that edge.
