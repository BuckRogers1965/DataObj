#!/usr/bin/env python3
"""
JSScript host, raw-protocol: the SECOND language, proven the same way
the first mechanisms were - through the JSON protocol, no browser.

Two things to prove:

 1. A JS script is an ordinary dataflow object: wire a Pulse into its
    In, its oninput callback counts rising edges and send()s the count
    out Out - identical observable behavior to the Lua Script's twin.
 2. A JS script is a BRIDGE CLIENT: cmd() out its Cmd port, wired to a
    Bridge's In, drives the real protocol - it can create an instance
    and read it back through onevent(). This is the whole point of the
    "language host as a bridge" shape.

    python3 testharness/jstest.py --host 127.0.0.1 --port 8091
"""
import argparse, sys, time
from rawtest import Raw, Report, ensure_raw_bridge, suite_view


def make(raw, cls, alias, home, x, y, hidden=False):
    cmd = {"cmd": "create-instance", "class": cls, "as": alias,
           "container": home, "x": str(x), "y": str(y)}
    if hidden:
        cmd["hidden"] = "1"
    raw.send(cmd)
    return raw.wait_event(lambda e: e.get("event") == "instance-created"
                          and e.get("instance") == alias, timeout=4)


def test_js_dataflow(raw, r, home):
    """A JS script counts pulses - the language works as a flow object."""
    js = home + "/Counter"
    pulse = home + "/P"
    make(raw, "JSScript", js, home, 20, 20)
    make(raw, "Pulse", pulse, home, 20, 90)

    src = ("var c = 0;\n"
           "oninput(function(v, k) {\n"
           "  if (v === '1') {\n"
           "    c = c + 1;\n"
           "    send(String(c));\n"
           "  }\n"
           "});\n")
    raw.send({"cmd": "set-property", "instance": js, "prop": "Source", "value": src})
    raw.send({"cmd": "set-property", "instance": pulse, "prop": "Interval", "value": "40"})
    raw.send({"cmd": "set-property", "instance": pulse, "prop": "Count", "value": "3"})
    raw.send({"cmd": "connect", "from": pulse, "fromPort": "Out", "to": js, "toPort": "In"})
    raw.send({"cmd": "subscribe", "instance": js, "port": "Out"})
    time.sleep(0.3)
    raw.events = []
    raw.send({"cmd": "activate", "instance": js})
    raw.send({"cmd": "activate", "instance": pulse})
    time.sleep(1.5)

    got = []
    raw.pump()
    for e in raw.events:
        if e.get("event") == "message-flowed" and e.get("instance") == js and e.get("port") == "Out":
            got.append(e.get("value"))
    r.expect("js dataflow: the script counts pulses and speaks",
             "oninput fires per rising edge; Out carries 1,2,3",
             "Out values: %s" % got,
             got == ["1", "2", "3"])


def test_js_print(raw, r, home):
    """print() reaches the Print port - what the ScriptBox Output wires to."""
    js = home + "/Printer"
    make(raw, "JSScript", js, home, 120, 20)
    raw.send({"cmd": "set-property", "instance": js, "prop": "Source",
              "value": "print('hello from js ' + (2 + 3));"})
    raw.send({"cmd": "subscribe", "instance": js, "port": "Print"})
    time.sleep(0.2)
    raw.events = []
    raw.send({"cmd": "activate", "instance": js})
    ev = raw.wait_event(lambda e: e.get("event") == "message-flowed"
                        and e.get("instance") == js and e.get("port") == "Print", timeout=4)
    r.expect("js print: output reaches the Print port",
             "print('hello from js 5') emerges on Print",
             "Print value: %s" % (ev.get("value") if ev else None),
             bool(ev) and ev.get("value") == "hello from js 5")


def test_js_error_loud(raw, r, home):
    """A broken script fails LOUD: State stops, the error goes out Print."""
    js = home + "/Broken"
    make(raw, "JSScript", js, home, 220, 20)
    raw.send({"cmd": "set-property", "instance": js, "prop": "Source",
              "value": "this is not valid javascript )("})
    raw.send({"cmd": "subscribe", "instance": js, "port": "Print"})
    time.sleep(0.2)
    raw.events = []
    raw.send({"cmd": "activate", "instance": js})
    ev = raw.wait_event(lambda e: e.get("event") == "message-flowed"
                        and e.get("instance") == js and e.get("port") == "Print", timeout=4)
    r.expect("js error: a broken script is never silent",
             "the syntax error surfaces on Print",
             "Print value: %s" % (ev.get("value") if ev else None),
             bool(ev) and "error" in (ev.get("value") or "").lower())


def test_js_bridge_client(raw, r, home):
    """THE shape this host exists to prove: a JS script speaks the JSON
    protocol. Its Cmd port wired to a real Bridge's In, its Evt port fed
    the Bridge's Out - the script create-instances something and reads
    the resulting event back through onevent()."""
    js = home + "/Agent"
    tcp = home + "/AgentTcp"
    br = home + "/AgentBridge"
    make(raw, "JSScript", js, home, 320, 20)
    # a private bridge for the script to drive (its own transport-less
    # loop: Cmd -> Bridge.In, Bridge.Out -> Evt), activated so it answers
    make(raw, "Bridge", br, home, 320, 90, hidden=True)
    raw.send({"cmd": "connect", "from": js, "fromPort": "Cmd", "to": br, "toPort": "In"})
    raw.send({"cmd": "connect", "from": br, "fromPort": "Out", "to": js, "toPort": "Evt"})
    raw.send({"cmd": "activate", "instance": br})

    # the script: on activate, ask the bridge to list instances; echo each
    # event's "event" field out Print (an ordinary out port we can watch)
    src = ("onevent(function(txt) {\n"
           "  try {\n"
           "    var e = JSON.parse(txt);\n"
           "    print('E:' + e.event);\n"
           "  } catch (x) {}\n"
           "});\n"
           "cmd({cmd:'list-instances'});\n")
    raw.send({"cmd": "set-property", "instance": js, "prop": "Source", "value": src})
    raw.send({"cmd": "subscribe", "instance": js, "port": "Print"})
    time.sleep(0.2)
    raw.events = []
    raw.send({"cmd": "activate", "instance": js})
    time.sleep(1.0)

    raw.pump()
    seen = [e.get("value") for e in raw.events
            if e.get("event") == "message-flowed" and e.get("instance") == js
            and e.get("port") == "Print"]
    r.expect("js bridge client: a script drives the JSON protocol",
             "cmd({cmd:'list-instances'}) round-trips; onevent sees an instances-done event",
             "events echoed: %s" % seen,
             "E:instances-done" in seen)


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

    test_js_dataflow(raw, r, home)
    test_js_print(raw, r, home)
    test_js_error_loud(raw, r, home)
    test_js_bridge_client(raw, r, home)

    raw.close()
    sys.exit(1 if r.summary() else 0)


if __name__ == "__main__":
    main()
