#!/usr/bin/env python3
"""
Connections, raw-protocol twins: what the GUI's Connect mode needs the
ENGINE to answer for. The reported bugs (2026-07-16): entering Connect
mode showed no existing wires, and a wire the GUI drew itself landed on
the wrong layer - both because the truth about wires lived on the wrong
side of the bridge.

The contract these tests pin down:

 1. list-connections reports EVERY user-made wire, including the GUI's
    own default gesture (Value -> Value, a property with no compiled
    handler) - the shape Connect() routes through the PropertyBinding
    adapter today, which the graph walk skips. THE reported bug.
 2. a live `connect` is ANNOUNCED (a `connected` event to every
    connection viewing the endpoints' container) - the client never
    draws its own line; readmefirst repair #5's law applied to wires.
 3. `disconnect` - the missing inverse of connect (ROADMAP Phase 3.3) -
    removes exactly one wire: `disconnected` event out, gone from the
    listing, and the value stops following. The GUI's mid-wire "x".
 4. a wire into Activate is an ordinary, listable wire (Button.Out ->
    X.Activate), not a separate bind-activate species.
 5. deleting a wire's SINK scrubs the wire: nothing left in the
    listing, and driving the old source must not touch freed memory
    (the adapter's dangling Target today - ASan's department, but the
    listing half is assertable here).
 6. wires CHAIN: A.Value -> B.Value -> C.Value, one write at A arrives
    at C (default property delivery must fan back out, not dead-end).

Run through run.sh, or standalone against a running server:

    python3 testharness/connectiontest.py --host 127.0.0.1 --port 8091
"""
import argparse, sys, time
from rawtest import Raw, Report, ensure_raw_bridge, suite_view


def make_slider(raw, home, x, y):
    raw.send({"cmd": "create-instance", "class": "Slider", "container": home,
              "x": str(x), "y": str(y)})
    ev = raw.wait_event(lambda e: e.get("event") == "instance-created"
                        and e.get("class") == "Slider" and e.get("container") == home)
    return ev.get("instance") if ev else None


def make_button(raw, home, x, y):
    raw.send({"cmd": "create-instance", "class": "Button", "container": home,
              "x": str(x), "y": str(y)})
    ev = raw.wait_event(lambda e: e.get("event") == "instance-created"
                        and e.get("class") == "Button" and e.get("container") == home)
    return ev.get("instance") if ev else None


def connections(raw):
    """(from, fromPort, to, toPort) tuples, as list-connections reports
    them - exactly what the GUI has to draw from. Live connected events
    from this connection's own earlier connects may still be queued
    (announcing wires is the new contract) - drop them first, so this
    reads what the server says NOW, not what it said at connect time."""
    raw.pump()
    raw.events = [e for e in raw.events
                  if e.get("event") not in ("connected", "disconnected", "connections-done")]
    raw.send({"cmd": "list-connections"})
    out = []
    while True:
        e = raw.wait_event(lambda e: e.get("event") in ("connected", "connections-done"),
                           timeout=4)
        if not e or e.get("event") == "connections-done":
            break
        out.append((e.get("from"), e.get("fromPort"), e.get("to"), e.get("toPort")))
    return out


def drives(raw, src, dst, value):
    """Functional proof of a wire: write src.Value, does dst.Value follow?"""
    raw.value_of(dst, "Value")          # arm the subscription
    raw.send({"cmd": "set-property", "instance": src, "prop": "Value", "value": value})
    ev = raw.wait_event(lambda e: e.get("event") == "property-changed"
                        and e.get("instance") == dst and e.get("port") == "Value"
                        and e.get("value") == value, timeout=4)
    return bool(ev)


def test_property_wire_listed(raw, r, home):
    """THE reported bug: the GUI's own gesture wires Value -> Value; on
    re-entering Connect mode, list-connections must name that wire."""
    src = make_slider(raw, home, 20, 20)
    dst = make_slider(raw, home, 20, 80)
    raw.send({"cmd": "connect", "from": src, "fromPort": "Value",
              "to": dst, "toPort": "Value"})

    r.expect("property wire: it actually drives",
             "writing %s.Value moves %s.Value" % (src, dst),
             "driven: %s" % drives(raw, src, dst, "37"),
             drives(raw, src, dst, "38"))

    wires = [w for w in connections(raw) if w[0] == src]
    r.expect("property wire: list-connections reports it",
             "a connected event (%s, Value, %s, Value) - what Connect mode draws from" % (src, dst),
             "wires from %s: %s" % (src, wires),
             (src, "Value", dst, "Value") in wires)
    return src, dst


def test_connect_event(raw, r, home, host, port):
    """A live connect is announced, unasked, to every connection viewing
    the endpoints' container - the clicking window draws from this same
    event instead of inventing its own line, and every OTHER window
    learns the wire exists at all."""
    watcher = Raw(host, port)
    try:
        watcher.send({"cmd": "list-instances", "container": home})
        watcher.wait_event(lambda e: e.get("event") == "instances-done", timeout=4)
        watcher.events = []

        src = make_slider(raw, home, 120, 20)
        dst = make_slider(raw, home, 120, 80)
        raw.events = []
        raw.send({"cmd": "connect", "from": src, "fromPort": "Value",
                  "to": dst, "toPort": "Value"})

        ev = raw.wait_event(lambda e: e.get("event") == "connected"
                            and e.get("from") == src and e.get("to") == dst, timeout=4)
        r.expect("connect event: the asking connection is told",
                 "a connected event for %s -> %s arrives without list-connections" % (src, dst),
                 "event: %s" % (ev,),
                 bool(ev) and ev.get("fromPort") == "Value" and ev.get("toPort") == "Value")

        wev = watcher.wait_event(lambda e: e.get("event") == "connected"
                                 and e.get("from") == src and e.get("to") == dst, timeout=4)
        r.expect("connect event: a second viewing connection is told too",
                 "the same connected event on a connection that merely views %s" % home,
                 "event: %s" % (wev,),
                 bool(wev))
    finally:
        watcher.close()


def test_disconnect(raw, r, home):
    """The mid-wire 'x': disconnect is the inverse of connect - announced,
    delisted, and the value stops following. One wire, exactly."""
    src = make_slider(raw, home, 220, 20)
    dst = make_slider(raw, home, 220, 80)
    raw.send({"cmd": "connect", "from": src, "fromPort": "Value",
              "to": dst, "toPort": "Value"})

    ok_before = drives(raw, src, dst, "41")
    raw.events = []
    raw.send({"cmd": "disconnect", "from": src, "fromPort": "Value",
              "to": dst, "toPort": "Value"})

    ev = raw.wait_event(lambda e: e.get("event") == "disconnected"
                        and e.get("from") == src and e.get("to") == dst, timeout=4)
    r.expect("disconnect: announced",
             "a disconnected event naming %s.Value -> %s.Value" % (src, dst),
             "event: %s" % (ev,),
             bool(ev) and ev.get("fromPort") == "Value" and ev.get("toPort") == "Value")

    wires = [w for w in connections(raw) if w[0] == src and w[2] == dst]
    r.expect("disconnect: delisted",
             "no wire %s -> %s in list-connections any more" % (src, dst),
             "wires: %s" % wires,
             wires == [])

    r.expect("disconnect: the value stops following",
             "the wire drove before (%s) and does not after" % ok_before,
             "after disconnect, driven: %s" % drives(raw, src, dst, "42"),
             ok_before and not drives(raw, src, dst, "43"))


def test_activate_wire_listed(raw, r, home):
    """Button.Out -> X.Activate is one ordinary wire: made with connect,
    reported by list-connections - not a separate bind-activate species
    the listing (and the clone, and the scrub) can't see."""
    btn = make_button(raw, home, 320, 20)
    dst = make_slider(raw, home, 320, 80)
    raw.send({"cmd": "connect", "from": btn, "fromPort": "Out",
              "to": dst, "toPort": "Activate"})

    wires = [w for w in connections(raw) if w[0] == btn and w[2] == dst]
    r.expect("activate wire: list-connections reports it",
             "a connected event (%s, Out, %s, Activate)" % (btn, dst),
             "wires from %s: %s" % (btn, wires),
             (btn, "Out", dst, "Activate") in wires)


def test_delete_sink_scrubs(raw, r, home):
    """Deleting a property-wire's sink must take the wire with it - the
    listing shows nothing, and driving the old source is a no-op, never
    a walk into freed memory."""
    src = make_slider(raw, home, 420, 20)
    dst = make_slider(raw, home, 420, 80)
    raw.send({"cmd": "connect", "from": src, "fromPort": "Value",
              "to": dst, "toPort": "Value"})

    raw.send({"cmd": "delete-instance", "instance": dst})
    raw.wait_event(lambda e: e.get("event") == "instance-removed"
                   and e.get("instance") == dst, timeout=4)

    wires = [w for w in connections(raw) if w[0] == src]
    r.expect("delete sink: the wire is scrubbed from the listing",
             "no wires from %s after its sink was deleted" % src,
             "wires: %s" % wires,
             wires == [])

    # drive the orphaned source, then prove the server still answers -
    # if the scrub missed, this is the write that lands in freed memory
    raw.send({"cmd": "set-property", "instance": src, "prop": "Value", "value": "51"})
    v = raw.value_of(src, "Value")
    r.expect("delete sink: driving the old source is safe",
             "the server survives and reads %s.Value back as 51" % src,
             "read back: %s" % v,
             v == "51")


def test_chained_wires(raw, r, home):
    """A -> B -> C by Value: default property delivery must fan back out
    of B, or every mid-flow widget becomes a dead end."""
    a = make_slider(raw, home, 520, 20)
    b = make_slider(raw, home, 520, 80)
    c = make_slider(raw, home, 520, 140)
    raw.send({"cmd": "connect", "from": a, "fromPort": "Value", "to": b, "toPort": "Value"})
    raw.send({"cmd": "connect", "from": b, "fromPort": "Value", "to": c, "toPort": "Value"})

    r.expect("chained wires: a write at the head arrives at the tail",
             "writing %s.Value shows up on %s.Value two hops later" % (a, c),
             "driven: %s" % drives(raw, a, c, "61"),
             drives(raw, a, c, "62"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8091)
    ap.add_argument("--webport", type=int, default=8083)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    ensure_raw_bridge(args.host, args.port, args.webport)

    raw = Raw(args.host, args.port)
    r = Report("connection tests", verbose=args.verbose)

    home = suite_view(raw, "ConnTest")

    test_property_wire_listed(raw, r, home)
    test_connect_event(raw, r, home, args.host, args.port)
    test_disconnect(raw, r, home)
    test_activate_wire_listed(raw, r, home)
    test_delete_sink_scrubs(raw, r, home)
    test_chained_wires(raw, r, home)

    raw.close()
    sys.exit(1 if r.summary() else 0)


if __name__ == "__main__":
    main()
