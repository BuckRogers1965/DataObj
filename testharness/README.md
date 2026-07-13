# testharness — browser-driven GUI unit tests

The framework's C modules already carry their own self-tests (`./framework -t`).
This directory is the same idea for the web GUI: the tests drive a **real
browser** (headless chromium) pointed at a **real running server**, using
genuine mouse and keyboard input over the Chrome DevTools Protocol. Nothing
is mocked — a clone here is the same pick-then-place gesture a person makes.

## Run

    ./testharness/run.sh

The whole ritual, every time: kills any running framework, `make clean &&
make`, starts a fresh server on the default port (8083), drives it with a
headless chromium, cleans up after itself. `PORT=9090 ./testharness/run.sh`
if you must use another port. Build, server, and browser output land in
`testharness/logs/`.

To run the tests against something already running (your own server, your own
chromium with `--remote-debugging-port`):

    python3 testharness/guitest.py --app http://127.0.0.1:8085 --cdp 9223

## What it covers

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
- **lazy** — a fresh page load knows nothing about a closed view's contents;
  opening it streams them in.

## Reading the output

Every check prints what was **expected** and what was **observed**:

    TEST     clone: data is a snapshot, then independent
      expected: clone starts at the source's current value (42); changing the
                source (99) does not move the clone
      observed: clone started at 42; after source->99 values are ['99', '42']
      result:   PASS

A summary at the end repeats any failures; the exit code is nonzero if
anything failed, so this works both by eye and in a script.

## Adding a test

Add a `test_xxx(t, r)` function in `guitest.py` and call it from `main()`.
`t` is the browser (see `cdp.py`: `js`, `wait_js`, `click`, `pick_place`,
`press_drag`, `set_mode`, `center_of`, `hook_events`/`events`); `r.expect()`
takes the test name, the expectation in words, the observation in words, and
the pass/fail condition. State accumulates across tests within a run, the
same way it does in a user's session — each run starts from a fresh server.
