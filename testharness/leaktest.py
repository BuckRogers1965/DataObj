#!/usr/bin/env python3
"""
Leak accounting, raw-protocol: the core counts every allocation and
deallocation (NodeCount/DataCount/EnvelopeCount/TaskStructCount/
BuffCount/QueueCount - node.c and friends); a Stats object publishes
the counters as ordinary properties; this suite drives create/destroy
and message-burst cycles and asserts the counts come back to rest.

The discipline: a counter that grows and never shrinks across a cycle
IS a leak, named by its type. Some growth is BY DESIGN and constant
per cycle (the flow log records each mutating command - a few nodes
per cycle even after Bridge_CompactFlow reclaims a deleted instance's
history), so cycles assert EQUAL, SMALL deltas rather than zero:
a real leak in the message path scales with traffic (a 25-pulse burst
is ~50 payload nodes), not with the constant command count.

    python3 testharness/leaktest.py --host 127.0.0.1 --port 8091
"""
import argparse, sys, time
from rawtest import Raw, Report, ensure_raw_bridge, suite_view

COUNTERS = ["Nodes", "Datas", "Envelopes", "Tasks", "Buffs", "Queues"]


def read_counters(raw, stats):
    """One sample of every published counter, as ints. The Stats tick
    publishes continuously during activity and value_of consumes the
    OLDEST matching event - reading through a backlog returns stale
    history, not the present. Purge first, so the value consumed is the
    fresh push the subscribe itself triggers."""
    raw.pump()
    raw.events = [e for e in raw.events if e.get("event") != "property-changed"]
    out = {}
    for name in COUNTERS:
        v = raw.value_of(stats, name)
        out[name] = int(v) if v not in (None, "") else -1
    return out


def stable_counters(raw, stats):
    """Counters at REST. Reading is itself bridge traffic (subscribes,
    taps, event chunks) whose transient allocations the next Stats tick
    can catch mid-flight - so never poll toward stability. Go QUIET
    long enough for everything to drain and for the tick to sample a
    genuinely idle fabric (publish-on-change then goes silent), and
    read the settled values once."""
    time.sleep(2.5)
    return read_counters(raw, stats)


def delta(a, b):
    return {k: b[k] - a[k] for k in COUNTERS}


def make(raw, cls, alias, home, x, y):
    raw.send({"cmd": "create-instance", "class": cls, "as": alias,
              "container": home, "x": str(x), "y": str(y)})
    return raw.wait_event(lambda e: e.get("event") == "instance-created"
                          and e.get("instance") == alias, timeout=4)


def delete(raw, alias):
    raw.send({"cmd": "delete-instance", "instance": alias})
    raw.wait_event(lambda e: e.get("event") == "instance-removed"
                   and e.get("instance") == alias, timeout=4)


def structural_cycle(raw, home):
    """Create two sliders, wire them, drive the wire, unwire, delete -
    everything this suite makes, it destroys. Fixed 'as' names so the
    bridge's alias table reuses its (deliberately never deleted)
    entries instead of growing one per cycle."""
    a = home + "/A"
    b = home + "/B"
    make(raw, "Slider", a, home, 20, 20)
    make(raw, "Slider", b, home, 20, 80)
    raw.send({"cmd": "connect", "from": a, "fromPort": "Value", "to": b, "toPort": "Value"})
    for i in range(5):
        raw.send({"cmd": "set-property", "instance": a, "prop": "Value", "value": str(40 + i)})
    raw.send({"cmd": "disconnect", "from": a, "fromPort": "Value", "to": b, "toPort": "Value"})
    delete(raw, a)
    delete(raw, b)


def burst_cycle(raw, pulse):
    """Re-fire a finite pulse train into a plain property (default
    delivery) - pure in-fabric message traffic, zero commands during
    the burst itself. 25 pulses = 50 edges + eof; if payloads are being
    dropped unfreed, Nodes/Datas climb by ~that much, unmissably."""
    raw.send({"cmd": "activate", "instance": pulse})
    time.sleep(1.2)   # 25 pulses at 10ms, plus drain


def test_structural(raw, r, stats, home):
    structural_cycle(raw, home)               # warm-up x2: first-use (cold)
    structural_cycle(raw, home)               # allocations must not pollute the measured cycles
    base = stable_counters(raw, stats)

    structural_cycle(raw, home)
    after1 = stable_counters(raw, stats)
    structural_cycle(raw, home)
    after2 = stable_counters(raw, stats)

    d1 = delta(base, after1)
    d2 = delta(after1, after2)

    r.expect("structural: per-cycle growth is constant (no compounding leak)",
             "cycle deltas equal within sampling noise (+/-4 nodes/datas): one cycle's cost is every cycle's cost",
             "d1=%s d2=%s" % (d1, d2),
             abs(d1["Nodes"] - d2["Nodes"]) <= 4 and abs(d1["Datas"] - d2["Datas"]) <= 8
             and d1["Tasks"] == d2["Tasks"] == 0
             and d1["Buffs"] == d2["Buffs"] == 0
             and d1["Queues"] == d2["Queues"] == 0)

    r.expect("structural: per-cycle growth is only the command log, not the instances",
             "node delta per cycle < 40 (a leaked instance is ~15+ nodes, a leaked message ~2)",
             "nodes/cycle=%d datas/cycle=%d" % (d2["Nodes"], d2["Datas"]),
             d2["Nodes"] < 40 and d2["Datas"] < 80)

    r.expect("structural: nothing transient outlives the cycle",
             "envelopes 0 at rest; buffs, queues and task structs flat",
             "envelopes=%d dBuffs=%d dQueues=%d dTasks=%d"
             % (after2["Envelopes"], d2["Buffs"], d2["Queues"], d2["Tasks"]),
             after2["Envelopes"] == 0 and d2["Buffs"] == 0 and d2["Queues"] == 0 and d2["Tasks"] == 0)


def test_message_burst(raw, r, stats, home):
    """THE hypothesis this suite exists to test: are message payloads
    being dropped on the ground instead of freed after delivery?"""
    pulse = home + "/P"
    sink = home + "/S"
    make(raw, "Slider", sink, home, 120, 20)
    make(raw, "Pulse", pulse, home, 120, 80)
    raw.send({"cmd": "set-property", "instance": pulse, "prop": "Interval", "value": "10"})
    raw.send({"cmd": "set-property", "instance": pulse, "prop": "Count", "value": "25"})
    raw.send({"cmd": "connect", "from": pulse, "fromPort": "Out", "to": sink, "toPort": "Value"})

    burst_cycle(raw, pulse)                   # warm-up x2: first-use (cold)
    burst_cycle(raw, pulse)                   # allocations must not pollute the measured cycles
    base = stable_counters(raw, stats)

    burst_cycle(raw, pulse)
    after1 = stable_counters(raw, stats)
    burst_cycle(raw, pulse)
    after2 = stable_counters(raw, stats)

    d1 = delta(base, after1)
    d2 = delta(after1, after2)

    r.expect("burst: 50 delivered messages leave zero payloads behind",
             "per-cycle cost is the activate log record only (<=4 nodes, <=8 datas), independent of message count",
             "d1=%s d2=%s" % (d1, d2),
             d1["Nodes"] <= 4 and d2["Nodes"] <= 4 and d1["Datas"] <= 8 and d2["Datas"] <= 8
             and after2["Envelopes"] == 0)

    r.expect("burst: re-activation does not leak scheduler or buffer structs",
             "task/buff/queue deltas 0 per re-run",
             "d1=%s d2=%s" % ({k: d1[k] for k in ("Tasks", "Buffs", "Queues")},
                              {k: d2[k] for k in ("Tasks", "Buffs", "Queues")}),
             d1["Tasks"] == 0 and d2["Tasks"] == 0
             and d1["Buffs"] == 0 and d2["Buffs"] == 0
             and d1["Queues"] == 0 and d2["Queues"] == 0)

    delete(raw, pulse)
    delete(raw, sink)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8091)
    ap.add_argument("--webport", type=int, default=8083)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    ensure_raw_bridge(args.host, args.port, args.webport)

    raw = Raw(args.host, args.port)
    r = Report("leak tests", verbose=args.verbose)

    home = suite_view(raw, "LeakTest")

    stats = home + "/Stats"
    make(raw, "Stats", stats, home, 220, 20)
    raw.send({"cmd": "set-property", "instance": stats, "prop": "Interval", "value": "200"})
    raw.send({"cmd": "activate", "instance": stats})
    time.sleep(0.5)

    ok = read_counters(raw, stats)
    r.expect("stats: the counters are alive and published",
             "every counter readable through an ordinary property subscribe, values > 0 where expected",
             "%s" % ok,
             ok["Nodes"] > 100 and ok["Datas"] > 100 and ok["Tasks"] >= 0)

    test_structural(raw, r, stats, home)
    test_message_burst(raw, r, stats, home)

    raw.close()
    sys.exit(1 if r.summary() else 0)


if __name__ == "__main__":
    main()
