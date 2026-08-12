#!/usr/bin/env python3
"""
JSScript host, raw-protocol: the SECOND language, proven the same way the
first mechanisms were - through the JSON protocol, no browser.

A LANGUAGE HOST HAS NO PROTOCOL SURFACE OF ITS OWN. It publishes nothing, has
no name and no path, and nothing can address it - the entire interface is
script.h, in C. So these tests drive it the only way anything drives it: they
create a ScriptBox (an ordinary widget anyone can create by class name), set
its Language to JSScript, and watch what the OWNER does.

That is not a workaround, it is the claim. A script's effects are visible on
the thing that owns it, and if they were visible anywhere else the host would
be addressable and the use-after-free that motivated all this would be back.

Four things to prove:

 1. a JS script is an ordinary dataflow object - wire a Pulse into its owner's
    In, oninput counts rising edges, send() lands in the owner's Output
 2. print() reaches the owner's Output box
 3. a broken script is never silent - the error lands in the same place
 4. a script speaks the engine's verbs directly (create/exists), which is what
    replaced the old cmd()-a-JSON-blob-at-a-Bridge shape

    python3 testharness/jstest.py --host 127.0.0.1 --port 8091
"""
import argparse, sys, time
from rawtest import Raw, Report, ensure_raw_bridge, suite_view, group_view, close_group


def make(raw, cls, alias, home, x, y, hidden=False):
    cmd = {"cmd": "create-instance", "class": cls, "as": alias,
           "container": home, "x": str(x), "y": str(y)}
    if hidden:
        cmd["hidden"] = "1"
    raw.send(cmd)
    return raw.wait_event(lambda e: e.get("event") == "instance-created"
                          and e.get("instance") == alias, timeout=4)


def js_box(raw, alias, home, x, y, src):
    """A ScriptBox running JavaScript, with `src` loaded and RUN.

    ONE activate. Setting Language builds the inner host, so by the time Run
    arrives the box is ready and that first Run executes. A second activate
    here re-runs a box that is already running, and doing so wedges the
    engine in an unbounded message loop - so this is not a stylistic choice,
    it is the shape scriptboxtest proved and the only one that is safe."""
    make(raw, "ScriptBox", alias, home, x, y)
    raw.send({"cmd": "set-property", "instance": alias, "prop": "Language",
              "value": "JSScript"})
    raw.send({"cmd": "set-property", "instance": alias, "prop": "Source", "value": src})
    time.sleep(0.3)
    raw.send({"cmd": "activate", "instance": alias})
    time.sleep(0.8)


def values_on(raw, inst, port):
    raw.pump()
    return [e.get("value") for e in raw.events
            if e.get("event") in ("property-changed", "message-flowed")
            and e.get("instance") == inst and e.get("port") == port]


def test_js_dataflow(raw, r, home):
    """A JS script counts pulses - the language works as a flow object."""
    box = home + "/Counter"
    pulse = home + "/P"

    src = ("var c = 0;\n"
           "oninput(function(v, k) {\n"
           "  if (v === '1') {\n"
           "    c = c + 1;\n"
           "    send(String(c));\n"
           "  }\n"
           "});\n")
    js_box(raw, box, home, 20, 20, src)

    make(raw, "Pulse", pulse, home, 20, 90)
    raw.send({"cmd": "set-property", "instance": pulse, "prop": "Interval", "value": "40"})
    raw.send({"cmd": "set-property", "instance": pulse, "prop": "Count", "value": "3"})
    raw.send({"cmd": "connect", "from": pulse, "fromPort": "Out",
              "to": box, "toPort": "In"})
    raw.send({"cmd": "subscribe", "instance": box, "port": "Output"})
    time.sleep(0.3)
    raw.events = []
    raw.send({"cmd": "activate", "instance": pulse})
    time.sleep(1.5)

    got = values_on(raw, box, "Output")
    r.expect("js dataflow: the script counts pulses and speaks",
             "oninput fires per rising edge; the owner's Output carries 1,2,3",
             "Output values: %s" % got,
             got == ["1", "2", "3"])


def test_js_print(raw, r, home):
    """print() reaches the owner's Output box - SCRIPT_PRINT, not a port on
    the host (it has none)."""
    box = home + "/Printer"
    js_box(raw, box, home, 120, 20, "print('hello from js ' + (2 + 3));")
    raw.events = []
    raw.send({"cmd": "subscribe", "instance": box, "port": "Output"})
    time.sleep(0.5)

    got = values_on(raw, box, "Output")
    hit = [v for v in got if v and "hello from js 5" in v]
    r.expect("js print: output reaches the owner",
             "print('hello from js 5') lands in the ScriptBox Output",
             "Output saw: %s" % (hit[-1] if hit else got[-1:] or None),
             bool(hit))


def test_js_error_loud(raw, r, home):
    """A broken script fails LOUD: the error arrives the same way output does,
    as SCRIPT_ERROR on the owner's callback port."""
    box = home + "/Broken"
    js_box(raw, box, home, 220, 20, "this is not valid javascript )(")
    raw.events = []
    raw.send({"cmd": "subscribe", "instance": box, "port": "Output"})
    time.sleep(0.5)

    got = values_on(raw, box, "Output")
    loud = [v for v in got if v and ("error" in v.lower() or "expecting" in v.lower())]
    r.expect("js error: a broken script is never silent",
             "the syntax error surfaces in the owner's Output",
             "Output saw: %s" % (loud[-1] if loud else got[-1:] or None),
             bool(loud))


def test_js_speaks_the_verbs(raw, r, home):
    """THE shape this host exists to prove, in its current form: a script is a
    peer of the protocol. It used to prove that by cmd()-ing a JSON blob at a
    wired Bridge; the verbs are first class now (script.object's table), so the
    script simply CREATES an instance and asks whether it exists. Same claim,
    no JSON, no bridge to wire."""
    box = home + "/Maker"
    made = home + "/MadeByScript"

    src = ("create('Textbox', '%s');\n"
           "print('exists:' + exists('%s'));\n" % (made, made))
    js_box(raw, box, home, 320, 20, src)
    raw.events = []
    raw.send({"cmd": "subscribe", "instance": box, "port": "Output"})
    time.sleep(0.6)

    said = [v for v in values_on(raw, box, "Output") if v and "exists:" in v]

    # and the engine agrees - the instance is really there
    raw.events = []
    raw.send({"cmd": "list-instances", "container": home})
    time.sleep(0.8)
    raw.pump()
    there = any(e.get("event") == "instance-created" and e.get("instance") == made
                for e in raw.events)

    r.expect("js verbs: a script drives the engine directly",
             "create() makes a real instance and exists() sees it",
             "script said %s; registry has it: %s" % (said[-1] if said else None, there),
             bool(said) and "exists:1" in said[-1] and there)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8091)
    ap.add_argument("--webport", type=int, default=8083)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    ensure_raw_bridge(args.host, args.port, args.webport)

    raw = Raw(args.host, args.port)
    r = Report("js script tests", verbose=args.verbose)

    home = suite_view(raw, "JSTest")

    g = group_view(raw, home, "JSDataflow")
    test_js_dataflow(raw, r, g)
    close_group(raw, g)

    g = group_view(raw, home, "JSPrint")
    test_js_print(raw, r, g)
    close_group(raw, g)

    g = group_view(raw, home, "JSErrorLoud")
    test_js_error_loud(raw, r, g)
    close_group(raw, g)

    g = group_view(raw, home, "JSVerbs")
    test_js_speaks_the_verbs(raw, r, g)
    close_group(raw, g)

    raw.close()
    sys.exit(min(r.summary(), 254))	# the return code IS the failure count


if __name__ == "__main__":
    main()
