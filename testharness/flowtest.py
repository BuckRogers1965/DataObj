#!/usr/bin/env python3
"""
The dataflow test flows that used to boot inside main.c's CreateTestApp,
rebuilt over the raw protocol (port 8091) - the same wiring, made of the
same verbs any client uses, but ASSERTING on subscribed events instead
of printing probes for eyeballing:

    cat            Reader -> Writer copies a file, EOF drains the flow
    filter/gate    a pulse train through a "ones" Filter whose Enable is
                   driven by a second pulse - anything gates anything
    queue/clock    edges pushed as they arrive, popped FIFO at a slower
                   clock's pace, EOF riding in-band
    stack/clock    same feed, popped newest-first - the stream reversed
                   piecewise, EOF included
    tcp echo       a TCP server wired back into itself; Enable=0 is a
                   full shutdown

Run through run.sh (after rawtest.py), or standalone against a running
server:

    python3 testharness/flowtest.py --host 127.0.0.1 --port 8091
"""
import argparse, os, socket, sys, time
from rawtest import Raw, Report, ensure_raw_bridge, suite_view


LOGDIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs")


def edges(values):
    """The 0/1 edge stream out of a subscription, with whatever shape the
    in-band EOF arrives in filtered away."""
    return [v for v in values if v in ("0", "1")]


def collect(raw, instance, port, seconds):
    """Every message-flowed value the tap delivers for instance.port over
    a fixed window."""
    out = []
    deadline = time.time() + seconds
    while time.time() < deadline:
        ev = raw.wait_event(
            lambda e: e.get("event") == "message-flowed"
            and e.get("instance") == instance and e.get("port") == port,
            timeout=min(1.0, max(0.1, deadline - time.time())))
        if ev is not None:
            out.append(ev.get("value"))
    return out


def test_cat(raw, r, home):
    """Reader -> Writer emulating cat: the first end-to-end dataflow,
    exactly as CreateTestApp used to build it at boot."""
    content = "the quick brown fox proves the routing fan-out\n" * 3
    src = os.path.join(LOGDIR, "rawcat_in.txt")
    dst = os.path.join(LOGDIR, "rawcat_out.txt")
    with open(src, "w") as f:
        f.write(content)
    if os.path.exists(dst):
        os.remove(dst)

    raw.send({"cmd": "create-instance", "class": "Reader", "as": home + "/CatReader", "container": home})
    raw.send({"cmd": "create-instance", "class": "Writer", "as": home + "/CatWriter", "container": home})
    raw.send({"cmd": "set-property", "instance": home + "/CatReader", "prop": "Filename", "value": src})
    raw.send({"cmd": "set-property", "instance": home + "/CatWriter", "prop": "Filename", "value": dst})

    # the sink subscribes to the source - and so does this test, the same
    # fan-out the old boot probe proved by printing
    raw.send({"cmd": "connect", "from": home + "/CatReader", "fromPort": "Out",
              "to": home + "/CatWriter", "toPort": "In"})
    raw.send({"cmd": "subscribe", "instance": home + "/CatReader", "port": "Out"})

    # sinks first so they never miss a chunk, then the source
    raw.send({"cmd": "activate", "instance": home + "/CatWriter"})
    raw.send({"cmd": "activate", "instance": home + "/CatReader"})

    ev = raw.wait_event(lambda e: e.get("event") == "message-flowed"
                        and e.get("instance") == home + "/CatReader"
                        and e.get("port") == "Out", timeout=6)
    time.sleep(1.5)   # let the writer drain and close

    copied = None
    if os.path.exists(dst):
        with open(dst) as f:
            copied = f.read()
    r.expect("cat: Reader -> Writer copies the file through the flow",
             "%d bytes arrive at the tap and land identical in %s" % (len(content), os.path.basename(dst)),
             "tap saw a chunk: %s, file matches: %s" % (bool(ev), copied == content),
             bool(ev) and copied == content)


def test_filter_gate(raw, r, home):
    """A pulse train through a 'ones' Filter; a Gate pulse drives the
    Filter's Enable - anything can drive anything's Enable, the whole
    point of the convention. Unlike the old boot flow (which activated
    everything in one C tick), each raw command costs a network round
    trip, so the schedule leaves wide margins: pulse ones at ~500/1500/
    2500ms, the gate's falling edge lands ~2000ms and swallows only the
    third."""
    raw.send({"cmd": "create-instance", "class": "Pulse", "as": home + "/FgPulse", "container": home})
    raw.send({"cmd": "create-instance", "class": "Filter", "as": home + "/FgFilter", "container": home})
    raw.send({"cmd": "create-instance", "class": "Pulse", "as": home + "/FgGate", "container": home})

    raw.send({"cmd": "set-property", "instance": home + "/FgPulse", "prop": "Interval", "value": "500"})
    raw.send({"cmd": "set-property", "instance": home + "/FgPulse", "prop": "Count", "value": "3"})
    raw.send({"cmd": "set-property", "instance": home + "/FgFilter", "prop": "Mode", "value": "ones"})
    raw.send({"cmd": "set-property", "instance": home + "/FgGate", "prop": "Interval", "value": "900"})
    raw.send({"cmd": "set-property", "instance": home + "/FgGate", "prop": "Count", "value": "1"})

    raw.send({"cmd": "connect", "from": home + "/FgPulse", "fromPort": "Out",
              "to": home + "/FgFilter", "toPort": "In"})
    raw.send({"cmd": "connect", "from": home + "/FgGate", "fromPort": "Out",
              "to": home + "/FgFilter", "toPort": "Enable"})
    raw.send({"cmd": "subscribe", "instance": home + "/FgFilter", "port": "Out"})

    raw.send({"cmd": "activate", "instance": home + "/FgFilter"})
    raw.send({"cmd": "activate", "instance": home + "/FgPulse"})
    raw.send({"cmd": "activate", "instance": home + "/FgGate"})

    seen = edges(collect(raw, home + "/FgFilter", "Out", 3.4))
    r.expect("filter/gate: the gate's falling edge swallows the third one",
             "filter.Out passes the first two ones only: ['1', '1']",
             "%s" % seen, seen == ["1", "1"])


def test_queue_clock(raw, r, home):
    """The pulse's edges pushed into a Queue as they arrive, popped back
    out FIFO one per tick of a slower clock, in-band EOF and all."""
    raw.send({"cmd": "create-instance", "class": "Pulse", "as": home + "/QPulse", "container": home})
    raw.send({"cmd": "create-instance", "class": "Queue", "as": home + "/QQueue", "container": home})
    raw.send({"cmd": "create-instance", "class": "Pulse", "as": home + "/QClock", "container": home})

    raw.send({"cmd": "set-property", "instance": home + "/QPulse", "prop": "Interval", "value": "200"})
    raw.send({"cmd": "set-property", "instance": home + "/QPulse", "prop": "Count", "value": "3"})
    raw.send({"cmd": "set-property", "instance": home + "/QClock", "prop": "Interval", "value": "350"})
    raw.send({"cmd": "set-property", "instance": home + "/QClock", "prop": "Count", "value": "4"})

    raw.send({"cmd": "connect", "from": home + "/QPulse", "fromPort": "Out",
              "to": home + "/QQueue", "toPort": "In"})
    raw.send({"cmd": "connect", "from": home + "/QClock", "fromPort": "Out",
              "to": home + "/QQueue", "toPort": "Clock"})
    raw.send({"cmd": "subscribe", "instance": home + "/QQueue", "port": "Out"})

    raw.send({"cmd": "activate", "instance": home + "/QQueue"})
    raw.send({"cmd": "activate", "instance": home + "/QClock"})
    raw.send({"cmd": "activate", "instance": home + "/QPulse"})

    seen = edges(collect(raw, home + "/QQueue", "Out", 3.4))
    r.expect("queue/clock: the stream replays FIFO at the clock's pace",
             "queue.Out replays the six edges in order: ['1','0','1','0','1','0']",
             "%s" % seen, seen == ["1", "0", "1", "0", "1", "0"])


def test_stack_clock(raw, r, home):
    """The same feed through a Stack: pops newest-first, so the stream
    comes back reversed piecewise - the EOF pushed last pops before the
    stranded zeros. That is what reversal means."""
    raw.send({"cmd": "create-instance", "class": "Pulse", "as": home + "/SPulse", "container": home})
    raw.send({"cmd": "create-instance", "class": "Stack", "as": home + "/SStack", "container": home})
    raw.send({"cmd": "create-instance", "class": "Pulse", "as": home + "/SClock", "container": home})

    raw.send({"cmd": "set-property", "instance": home + "/SPulse", "prop": "Interval", "value": "200"})
    raw.send({"cmd": "set-property", "instance": home + "/SPulse", "prop": "Count", "value": "3"})
    raw.send({"cmd": "set-property", "instance": home + "/SClock", "prop": "Interval", "value": "350"})
    raw.send({"cmd": "set-property", "instance": home + "/SClock", "prop": "Count", "value": "4"})

    raw.send({"cmd": "connect", "from": home + "/SPulse", "fromPort": "Out",
              "to": home + "/SStack", "toPort": "In"})
    raw.send({"cmd": "connect", "from": home + "/SClock", "fromPort": "Out",
              "to": home + "/SStack", "toPort": "Clock"})
    raw.send({"cmd": "subscribe", "instance": home + "/SStack", "port": "Out"})

    raw.send({"cmd": "activate", "instance": home + "/SStack"})
    raw.send({"cmd": "activate", "instance": home + "/SClock"})
    raw.send({"cmd": "activate", "instance": home + "/SPulse"})

    seen = edges(collect(raw, home + "/SStack", "Out", 3.4))
    r.expect("stack/clock: newest-first reverses the stream piecewise",
             "stack.Out pops the freshest edge each tick: ['1','1','1','0','0','0']",
             "%s" % seen, seen == ["1", "1", "1", "0", "0", "0"])


def test_tcp_echo(raw, r, home, host):
    """A TCP server wired straight back into itself: everything a peer
    sends comes back. Enable=0 is a FULL shutdown - sockets close and the
    flow quiesces, the same line any timer (or anything else) can drive."""
    raw.send({"cmd": "create-instance", "class": "TCP", "as": home + "/EchoTcp", "container": home})
    raw.send({"cmd": "set-property", "instance": home + "/EchoTcp", "prop": "LocalPort", "value": "8095"})
    raw.send({"cmd": "connect", "from": home + "/EchoTcp", "fromPort": "Out",
              "to": home + "/EchoTcp", "toPort": "In"})
    raw.send({"cmd": "activate", "instance": home + "/EchoTcp"})
    time.sleep(0.5)

    echoed = None
    try:
        s = socket.create_connection((host, 8095), timeout=3)
        s.sendall(b"hello, flow")
        s.settimeout(3)
        echoed = s.recv(1024)
        s.close()
    except Exception as e:
        echoed = ("error: %s" % e).encode()

    r.expect("tcp echo: the server echoes through its own wiring",
             "b'hello, flow' comes straight back",
             "%r" % echoed, echoed == b"hello, flow")

    raw.send({"cmd": "set-property", "instance": home + "/EchoTcp", "prop": "Enable", "value": "0"})
    time.sleep(0.8)
    refused = False
    try:
        s2 = socket.create_connection((host, 8095), timeout=1.5)
        s2.close()
    except Exception:
        refused = True
    r.expect("tcp echo: Enable=0 is a full shutdown",
             "the port stops answering",
             "connection refused: %s" % refused, refused)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8091)
    ap.add_argument("--webport", type=int, default=8083)
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="print every check, not just the failures")
    args = ap.parse_args()

    ensure_raw_bridge(args.host, args.port, args.webport)

    r = Report("dataflow flows", args.verbose)
    raw = Raw(args.host, args.port)
    raw.send({"cmd": "list-instances"})
    raw.wait_event(lambda e: e.get("event") == "instances-done", timeout=8)

    def guarded(fn, *a):
        try:
            return fn(*a)
        except Exception as e:
            r.expect(fn.__name__, "no exception", "%s: %s" % (type(e).__name__, e), False)

    home = suite_view(raw, "FlowTests")   # everything this suite builds lives in here

    guarded(test_cat, raw, r, home)
    guarded(test_filter_gate, raw, r, home)
    guarded(test_queue_clock, raw, r, home)
    guarded(test_stack_clock, raw, r, home)
    guarded(test_tcp_echo, raw, r, home, args.host)

    raw.close()
    sys.exit(1 if r.summary() else 0)


if __name__ == "__main__":
    main()
