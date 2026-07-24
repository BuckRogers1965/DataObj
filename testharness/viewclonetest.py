#!/usr/bin/env python3
"""
Deep-cloning a view: everything inside it has to come along AND stay
self-contained - the copies must wire to each other, not back to the
originals they were copied from, and not to nothing.

A view holding a Slider, a clone of it wired to it, and an Alias of it
covers all three intra-view relationships at once:

    Src.Value --(connect)--> Dst.In        a WIRE between two members
    Alias -> Src.Value                     a LINK to a member
    (and both, cloned, must land on the CLONES)

Every check is functional, never structural: a wire is proven by writing
the source and watching the sink move, because a Subscriber entry that
exists but points at the wrong instance looks identical to a correct one
from the outside.

Run through run.sh, or standalone against a running server:

    python3 testharness/viewclonetest.py --host 127.0.0.1 --port 8091
"""
import argparse, sys, time
from rawtest import Raw, Report, ensure_raw_bridge, suite_view


def members(raw, view):
    raw.send({"cmd": "list-instances", "container": view})
    out = []
    while True:
        e = raw.wait_event(lambda e: (e.get("event") == "instance-created"
                                      and e.get("container") == view
                                      and e.get("instance") not in [m[0] for m in out])
                           or e.get("event") == "instances-done", timeout=4)
        if not e or e.get("event") == "instances-done":
            break
        out.append((e.get("instance"), e.get("class")))
    return out


def drives(raw, src, dst, value):
    """Functional proof of a wire: write src.Value, does dst.Value follow?"""
    raw.value_of(dst, "Value")          # arm the subscription
    raw.send({"cmd": "set-property", "instance": src, "prop": "Value", "value": value})
    ev = raw.wait_event(lambda e: e.get("event") == "property-changed"
                        and e.get("instance") == dst and e.get("port") == "Value"
                        and e.get("value") == value, timeout=4)
    return bool(ev)


def connections(raw):
    """What list-connections reports - how the GUI learns which wires to
    draw. (from, fromPort, to, toPort) tuples."""
    raw.send({"cmd": "list-connections"})
    out = []
    while True:
        e = raw.wait_event(lambda e: e.get("event") in ("connected", "connections-done"), timeout=4)
        if not e or e.get("event") == "connections-done":
            break
        out.append((e.get("from"), e.get("fromPort"), e.get("to"), e.get("toPort")))
    return out


def build_view(raw, home, name):
    """A view holding: Src, Dst (a clone of Src) wired Src.Value->Dst.In,
    and an Alias of Src.Value. The shape the bug report describes - built
    inside `home`, this suite's own view."""
    raw.send({"cmd": "create-instance", "class": "View", "as": home + "/" + name,
              "container": home, "x": "20", "y": "20"})
    ev = raw.wait_event(lambda e: e.get("event") == "instance-created"
                        and e.get("class") == "View" and e.get("container") == home)
    view = ev.get("instance") if ev else None
    members(raw, view)          # mark this conn as viewing it, so member events arrive

    raw.send({"cmd": "clone-instance", "of": "/Root/Palette/Slider",
              "container": view, "x": "20", "y": "20"})
    ev = raw.wait_event(lambda e: e.get("event") == "instance-created"
                        and e.get("class") == "Slider" and e.get("container") == view)
    src = ev.get("instance") if ev else None

    raw.send({"cmd": "clone-instance", "of": src, "container": view, "x": "20", "y": "80"})
    ev = raw.wait_event(lambda e: e.get("event") == "instance-created"
                        and e.get("class") == "Slider" and e.get("container") == view
                        and e.get("instance") != src)
    dst = ev.get("instance") if ev else None

    raw.send({"cmd": "connect", "from": src, "fromPort": "Value", "to": dst, "toPort": "In"})
    raw.send({"cmd": "create-alias", "of": src, "prop": "Value",
              "container": view, "x": "20", "y": "140"})
    ev = raw.wait_event(lambda e: e.get("event") == "instance-created"
                        and e.get("class") == "Alias" and e.get("container") == view)
    al = ev.get("instance") if ev else None
    return view, src, dst, al


def parts(raw, view):
    """(sliders in order of creation, alias) of a view."""
    mem = members(raw, view)
    sl = [m for m, c in mem if c == "Slider"]
    al = next((m for m, c in mem if c == "Alias"), None)
    return sorted(sl), al


def test_view_clone_wiring(raw, r, home):
    view, src, dst, al = build_view(raw, home, "WireView")

    r.expect("view: the source view is wired as built",
             "writing %s drives %s through the connect" % (src, dst),
             "driven: %s" % drives(raw, src, dst, "11"),
             drives(raw, src, dst, "12"))

    # --- clone the whole view, into this suite's own view alongside it
    raw.events = []
    raw.send({"cmd": "clone-instance", "of": view, "container": home, "x": "260", "y": "20"})
    ev = raw.wait_event(lambda e: e.get("event") == "instance-created"
                        and e.get("class") == "View" and e.get("container") == home
                        and e.get("instance") != view)
    clone = ev.get("instance") if ev else None
    time.sleep(0.5)

    csl, cal = parts(raw, clone) if clone else ([], None)
    csrc = csl[0] if len(csl) > 1 else None
    cdst = csl[1] if len(csl) > 1 else None

    r.expect("view clone: the members came along",
             "the cloned view holds two Sliders and an Alias",
             "clone=%s sliders=%s alias=%s" % (clone, csl, cal),
             len(csl) == 2 and bool(cal))

    # --- the clone is named after the SOURCE, not its class
    src_base = view.split("/")[-1]          # "WireView"
    clone_base = clone.split("/")[-1] if clone else ""
    r.expect("view clone: the clone is named after the source, not 'View'",
             "cloning %s yields %s_N, not View_N" % (src_base, src_base),
             "clone basename = %s" % clone_base,
             clone_base.startswith(src_base + "_"))

    # --- the wire is REPORTED to the GUI (list-connections), not just live:
    # this is how the browser learns to draw a line, and a cloned wire that
    # only exists in the subscription graph never reached it
    wires = connections(raw)
    clone_wire = [w for w in wires if w[0] in csl and w[2] in csl]
    r.expect("view clone: the clone's wire is reported by list-connections",
             "a connected event names the clone's own two sliders (so the GUI can draw it)",
             "clone wires reported: %s" % clone_wire,
             len(clone_wire) == 1 and clone_wire[0][1] == "Value" and clone_wire[0][3] == "In")

    # --- the alias inside the clone points at the clone's own slider
    tgt = raw.value_of(cal, "Target") if cal else None
    r.expect("view clone: the alias remaps onto the clone's own slider",
             "the cloned Alias targets a slider INSIDE the clone (%s)" % csl,
             "Target=%s" % tgt,
             tgt in csl)

    # --- THE REPORTED BUG: the wire between the members
    r.expect("view clone: the members are wired to EACH OTHER",
             "writing the clone's first slider drives the clone's second one",
             "driven: %s" % (drives(raw, csrc, cdst, "21") if csrc and cdst else "no members"),
             bool(csrc) and bool(cdst) and drives(raw, csrc, cdst, "22"))

    # --- and the clone must be independent: driving it must not move the original
    orig_dst_before = raw.value_of(dst, "Value")
    if csrc:
        raw.send({"cmd": "set-property", "instance": csrc, "prop": "Value", "value": "77"})
        time.sleep(0.6)
    orig_dst_after = raw.value_of(dst, "Value")
    r.expect("view clone: the clone's wire does not reach back into the original",
             "driving the clone leaves the original view's sink where it was (%s)" % orig_dst_before,
             "before=%s after=%s" % (orig_dst_before, orig_dst_after),
             orig_dst_before == orig_dst_after)


def test_clone_of_clone(raw, r, home):
    """Cloning a view that was itself cloned - the copy's copy has to be
    just as self-contained (the first clone is an ordinary view, so this
    must need no special case at all)."""
    view, src, dst, al = build_view(raw, home, "WireView2")

    raw.events = []
    raw.send({"cmd": "clone-instance", "of": view, "container": home, "x": "260", "y": "120"})
    ev = raw.wait_event(lambda e: e.get("event") == "instance-created"
                        and e.get("class") == "View" and e.get("container") == home
                        and e.get("instance") != view)
    first = ev.get("instance") if ev else None
    time.sleep(0.4)

    raw.events = []
    raw.send({"cmd": "clone-instance", "of": first, "container": home, "x": "500", "y": "120"})
    ev = raw.wait_event(lambda e: e.get("event") == "instance-created"
                        and e.get("class") == "View" and e.get("container") == home
                        and e.get("instance") not in (view, first))
    second = ev.get("instance") if ev else None
    time.sleep(0.4)

    ssl, sal = parts(raw, second) if second else ([], None)
    tgt = raw.value_of(sal, "Target") if sal else None
    wired = drives(raw, ssl[0], ssl[1], "33") if len(ssl) > 1 else False

    r.expect("clone of a clone: still self-contained",
             "the copy's copy has both members wired to each other and its alias on its own slider",
             "clone2=%s sliders=%s aliasTarget=%s wired=%s" % (second, ssl, tgt, wired),
             len(ssl) == 2 and tgt in ssl and wired)


def test_save_load_view_wiring(raw, r, home):
    """The same view through save + load: a flow records connects by name,
    so the loaded copy's wire must land on the loaded members."""
    view, src, dst, al = build_view(raw, home, "WireView3")
    known = [view]

    raw.send({"cmd": "save-flow", "file": "wiretwin"})
    raw.wait_event(lambda e: e.get("event") == "flow-saved")

    # a save records the WHOLE session, so the replay rebuilds every view
    # this suite made; the copy we want is whichever one comes back
    # holding exactly two Sliders and an Alias
    raw.events = []
    raw.send({"cmd": "load-flow", "file": "wiretwin"})
    raw.wait_event(lambda e: e.get("event") == "flow-loaded", timeout=8)
    # A save records the WHOLE session, so the replay rebuilds every suite's
    # view, and several of those copies happen to hold two Sliders and an
    # Alias too (rawtest's, whose sliders were never meant to be wired) -
    # matching on shape alone finds one of THOSE and reports our fix broken.
    # Our copy is precisely: inside the copy of THIS suite's view, which is
    # the one root-level view holding nothing but views.
    fresh = [e.get("instance") for e in raw.events
             if e.get("event") == "instance-created" and e.get("class") == "View"
             and e.get("instance") not in known]

    ours = []
    for v in fresh:
        mem = members(raw, v)   # also starts viewing it, or its members stay unannounced
        if mem and all(c == "View" for _, c in mem):
            ours += [m for m, _ in mem]

    copy, csl, cal = None, [], None
    for v in ours:
        sl, a = parts(raw, v)
        if len(sl) == 2 and a:
            copy, csl, cal = v, sl, a
            break

    tgt = raw.value_of(cal, "Target") if cal else None
    wired = drives(raw, csl[0], csl[1], "44") if len(csl) == 2 else False
    r.expect("load: the loaded view's members are wired to each other",
             "the loaded copy drives its own sink and aliases its own slider",
             "copy=%s sliders=%s aliasTarget=%s wired=%s" % (copy, csl, tgt, wired),
             bool(copy) and tgt in csl and wired)


def test_clone_into_self_refused(raw, r, home):
    """A view cannot be cloned INTO itself (or anything it contains) - the
    gesture of dropping a view's icon inside its own panel. Allowed, it
    would build clones inside clones forever. The engine must refuse it,
    say so, and leave the view exactly as it was."""
    view, src, dst, al = build_view(raw, home, "SelfClone")
    before = parts(raw, view)

    # clone the view into its OWN container
    raw.events = []
    raw.send({"cmd": "clone-instance", "of": view, "container": view, "x": "20", "y": "200"})
    err = raw.wait_event(lambda e: e.get("event") == "error"
                         and e.get("cmd") == "clone-instance", timeout=3)
    r.expect("clone into self: refused with an error",
             "an error event, not a clone",
             "error=%s" % (err.get("message") if err else None),
             bool(err))

    # nothing was created; the view is unchanged
    after = parts(raw, view)
    r.expect("clone into self: the view is untouched",
             "same members as before the refused clone: %s" % (before,),
             "%s" % (after,),
             after == before)

    # and a nested descendant is refused too
    raw.events = []
    raw.send({"cmd": "clone-instance", "of": view, "container": src, "x": "0", "y": "0"})
    err2 = raw.wait_event(lambda e: e.get("event") == "error"
                          and e.get("cmd") == "clone-instance", timeout=3)
    r.expect("clone into a descendant: refused too",
             "cloning a view into one of its own members is also refused",
             "error=%s" % (err2.get("message") if err2 else None),
             bool(err2))


def wait_file(path, timeout=8.0):
    """Export is async (the Serializer walk and Writer drain run as scheduler
    tasks); wait for the file to exist and stop growing before importing it."""
    import os
    deadline = time.time() + timeout
    last = -1
    while time.time() < deadline:
        try:
            sz = os.path.getsize(path)
            if sz > 0 and sz == last:
                return True
            last = sz
        except OSError:
            pass
        time.sleep(0.3)
    return last > 0


def test_export_import_view_wiring(raw, r, home):
    """Export ONE view to disk and import it back - a clone with a side trip
    to disk. The imported copy must be as self-contained as a clone: its wire
    lands on its OWN members, its alias on its OWN slider, and driving it never
    reaches back into the original. This is the round-trip flowdiff caught
    breaking (dangling alias, dropped connection)."""
    view, src, dst, al = build_view(raw, home, "WireViewEI")

    # export just THIS view (not the whole session, unlike save-flow)
    raw.send({"cmd": "export-flow", "file": "eitwin", "of": view})
    raw.wait_event(lambda e: e.get("event") == "flow-saved", timeout=6)
    wait_file("saved/eitwin.flow")

    # import it back INTO this suite's view, so the copy's members announce here
    raw.events = []
    raw.send({"cmd": "import-flow", "file": "eitwin", "into": home})
    raw.wait_event(lambda e: e.get("event") == "flow-loaded", timeout=8)
    time.sleep(0.4)

    # the imported view is the fresh View inside home
    fresh = [e.get("instance") for e in raw.events
             if e.get("event") == "instance-created" and e.get("class") == "View"
             and e.get("container") == home and e.get("instance") != view]
    copy, csl, cal = None, [], None
    for v in fresh:
        sl, a = parts(raw, v)
        if len(sl) == 2 and a:
            copy, csl, cal = v, sl, a
            break

    r.expect("import: the members came along",
             "the imported view holds two Sliders and an Alias",
             "copy=%s sliders=%s alias=%s" % (copy, csl, cal),
             len(csl) == 2 and bool(cal))

    # the connection: find it among the imported sliders (list-connections),
    # then prove it functionally - from drives to
    wires = [w for w in connections(raw) if w[0] in csl and w[2] in csl]
    r.expect("import: the connection survived the round trip",
             "a wire between the imported view's own two sliders",
             "imported wires: %s" % wires,
             len(wires) == 1 and wires[0][1] == "Value" and wires[0][3] == "In")

    isrc = wires[0][0] if wires else None
    idst = wires[0][2] if wires else None
    r.expect("import: the members are wired to EACH OTHER, not the original",
             "writing the imported source drives the imported sink",
             "driven: %s" % (drives(raw, isrc, idst, "51") if isrc and idst else "no wire"),
             bool(isrc) and bool(idst) and drives(raw, isrc, idst, "52"))

    # the alias points at a slider INSIDE the imported copy (the user's ask:
    # "the alias has to link to the thing in its view")
    tgt = raw.value_of(cal, "Target") if cal else None
    r.expect("import: the alias links to the thing in ITS view",
             "the imported Alias targets a slider inside the copy (%s)" % csl,
             "Target=%s" % tgt,
             tgt in csl)

    # independence: driving the copy must not move the original's sink
    before = raw.value_of(dst, "Value")
    if isrc:
        raw.send({"cmd": "set-property", "instance": isrc, "prop": "Value", "value": "88"})
        time.sleep(0.6)
    after = raw.value_of(dst, "Value")
    r.expect("import: the copy does not reach back into the original",
             "driving the imported source leaves the original sink at %s" % before,
             "before=%s after=%s" % (before, after),
             before == after)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8091)
    ap.add_argument("--webport", type=int, default=8083)
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="print every check, not just the failures")
    args = ap.parse_args()

    ensure_raw_bridge(args.host, args.port, args.webport)

    r = Report("view clone", args.verbose)
    raw = Raw(args.host, args.port)
    raw.send({"cmd": "list-instances"})
    raw.wait_event(lambda e: e.get("event") == "instances-done", timeout=8)

    def guarded(fn, *a):
        try:
            return fn(*a)
        except Exception as e:
            r.expect(fn.__name__, "no exception", "%s: %s" % (type(e).__name__, e), False)

    home = suite_view(raw, "ViewCloneTests")   # everything this suite builds lives in here

    guarded(test_view_clone_wiring, raw, r, home)
    guarded(test_clone_of_clone, raw, r, home)
    guarded(test_clone_into_self_refused, raw, r, home)
    guarded(test_save_load_view_wiring, raw, r, home)
    guarded(test_export_import_view_wiring, raw, r, home)

    raw.close()
    sys.exit(1 if r.summary() else 0)


if __name__ == "__main__":
    main()
