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
from rawtest import Raw, Report, ensure_raw_bridge, suite_view, container_children, close_new_children


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


def is_alias(raw, inst, timeout=1.5):
    """Does this instance stand for somebody else's property?

    It carries a Target - it is NOT of some class called Alias. Aliasing is a
    gesture, not a species: the engine makes the control that renders the
    property being pointed at, so an alias of a slider's Value IS a Slider and
    an alias of an Interval IS a Knob. What it is drawn as and what it stands
    for are two different questions, and only the second one is asked here.

    This is the same rule the serializer uses to recognise one in a saved
    file, which is why a flow written before any of this still loads.

    A Target is a PATH, and the test is written that way for a reason that is
    not fussiness: subscribing to a property that does not exist CREATES it
    (Connect grows a missing source property - late binding, working as
    designed), and a fresh one reads "0". Since bool("0") is true in Python,
    "did the subscribe return anything" made every member look like an alias -
    it picked the plain dst slider and then reported its conjured Target of 0.
    Nothing conjured ever starts with a slash."""
    t = raw.value_of(inst, "Target", timeout=timeout)
    return bool(t) and t.startswith("/")


def build_view(raw, home, name):
    """A view holding: Src, Dst (a clone of Src) wired Src.Value->Dst.In,
    and an alias of Src.Value. The shape the bug report describes - built
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

    raw.send({"cmd": "connect", "from": src, "fromPort": "Value", "to": dst, "toPort": "Value"})
    raw.send({"cmd": "create-alias", "of": src, "prop": "Value",
              "container": view, "x": "20", "y": "140"})
    # it arrives as whatever draws a slider's Value - a Slider. The only thing
    # that distinguishes it from src and dst is that it stands for one of them.
    ev = raw.wait_event(lambda e: e.get("event") == "instance-created"
                        and e.get("container") == view
                        and e.get("instance") not in (src, dst))
    al = ev.get("instance") if ev else None
    return view, src, dst, al


def parts(raw, view):
    """(the view's own sliders, its alias).

    The alias is a Slider too, so it is taken out of the slider list rather
    than counted as one: what these tests mean by "the view's two sliders" is
    the two that hold a value of their own."""
    mem = members(raw, view)
    al = next((m for m, c in mem if is_alias(raw, m)), None)
    sl = sorted(m for m, c in mem if c == "Slider" and m != al)
    return sl, al


def widget_tree(raw, root):
    """Every member under `root`, keyed by its path RELATIVE to root, at
    every depth. A compiled widget keeps its controls inside sub-views, so
    a shallow listing of one looks healthy while the panel is empty."""
    out = {}

    def walk(path, rel):
        for inst, cls in members(raw, path):
            name = inst[len(path) + 1:]
            key = (rel + "/" + name) if rel else name
            out[key] = cls
            walk(inst, key)

    walk(root, "")
    return out


def controls_of(tree):
    """The members that are not containers - what a panel actually shows."""
    return sorted(k for k, cls in tree.items() if cls != "View")


def test_clone_compiled_widget(raw, r, home):
    """A COMPILED widget (one whose panel Widget_BuildTable builds) is the
    shape no other test here clones. Widget_Ctl LINKS each control's Value
    to the property it shows, so every one of those controls is a member
    the clone walk handles on its LINK pass - while a View full of plain
    Sliders, which is what the rest of this suite clones, only ever
    exercises the concrete pass.

    Both halves are checked because they fail separately: the copy has to
    HOLD the controls, and their arrival has to be ANNOUNCED, since a
    client draws what it is told about. The bug this was written for left
    every sub-view in place and every control inside them missing."""
    source = "/Root/Palette/TCPPort"
    src = widget_tree(raw, source)
    src_ctls = controls_of(src)
    r.expect("widget clone: the palette holds a TCPPort with controls to copy",
             "the source widget has sub-views and controls inside them",
             "%d members, %d of them controls" % (len(src), len(src_ctls)),
             len(src_ctls) > 0)
    if not src_ctls:
        return

    raw.send({"cmd": "clone-instance", "of": source, "container": home,
              "x": "40", "y": "40"})
    ev = raw.wait_event(lambda e: e.get("event") == "instance-created"
                        and e.get("container") == home
                        and e.get("class") == "TCPPort", timeout=6)
    clone = ev.get("instance") if ev else None
    r.expect("widget clone: the widget cloned at all",
             "a TCPPort instance appears in %s" % home,
             "clone=%s" % clone,
             bool(clone))
    if not clone:
        return

    # widget_tree LISTS each container, which is how a client learns what is
    # inside one: a connection is told about creations in containers it is
    # viewing and nothing else (bridge.c, connViews - "it only receives events
    # about what it looked at"), so a browser hears about a widget's controls
    # when it opens the widget. Asking is the announcement.
    got = widget_tree(raw, clone)
    missing = [k for k in src if k not in got]
    r.expect("widget clone: the copy holds every member the source has",
             "all %d members of the source, at every depth" % len(src),
             "%d of %d present, missing: %s" % (len(got), len(src),
                                                ", ".join(missing[:8]) or "none"),
             not missing)

    got_ctls = controls_of(got)
    r.expect("widget clone: the sub-views brought their controls",
             "%d controls, the same ones the source has" % len(src_ctls),
             "%d controls: %s" % (len(got_ctls),
                                  ", ".join(got_ctls[:6]) or "none"),
             got_ctls == src_ctls)


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
             len(clone_wire) == 1 and clone_wire[0][1] == "Value" and clone_wire[0][3] == "Value")

    # --- the alias inside the clone points at the clone's own slider
    tgt = raw.value_of(cal, "Target") if cal else None
    r.expect("view clone: the alias remaps onto the clone's own slider",
             "the cloned alias targets a slider INSIDE the clone (%s)" % csl,
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

    raw.send({"cmd": "save-flow", "file": "wiretwin"})
    raw.wait_event(lambda e: e.get("event") == "flow-saved")

    # load restores IN PLACE: the container's contents are destroyed and
    # rebuilt from the file under their recorded names, so the view comes
    # back at exactly the path it was saved from.
    raw.send({"cmd": "load-flow", "file": "wiretwin"})
    raw.wait_event(lambda e: e.get("event") == "flow-loaded", timeout=8)
    # the load destroyed everything this connection had open - come back
    # fresh or it cannot see what was just loaded
    raw.reconnect()

    copy = view
    csl, cal = parts(raw, copy)
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


def newest_flow(base):
    """A save writes base_YYYYMMDD_HHMMSS.flow - never a fixed name, so
    nothing is ever overwritten. The stamp is fixed width, so the newest
    version is just the greatest matching filename."""
    import glob, os
    hits = sorted(glob.glob(base + "_*.flow"))
    if hits:
        return hits[-1]
    return base + ".flow"          # written before saves were versioned


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
    wait_file(newest_flow("saved/eitwin"))

    # import it back INTO this suite's view, so the copy's members announce here
    raw.events = []
    raw.send({"cmd": "import-flow", "file": "eitwin", "into": home})
    raw.wait_event(lambda e: e.get("event") == "flow-loaded", timeout=8)
    time.sleep(0.4)

    # ask what is in home now, rather than waiting to be told: the import
    # drops the copy straight in, so listing is what sees it (the same
    # thing the client does when flow-loaded lands)
    fresh = [m for m, c in members(raw, home) if c == "View" and m != view]
    copy, csl, cal = None, [], None
    for v in fresh:
        sl, a = parts(raw, v)
        if len(sl) == 2 and a:
            copy, csl, cal = v, sl, a
            break

    r.expect("import: the members came along",
             "the imported view holds two Sliders and an alias of one of them",
             "copy=%s sliders=%s alias=%s" % (copy, csl, cal),
             len(csl) == 2 and bool(cal))

    # the connection: find it among the imported sliders (list-connections),
    # then prove it functionally - from drives to
    wires = [w for w in connections(raw) if w[0] in csl and w[2] in csl]
    r.expect("import: the connection survived the round trip",
             "a wire between the imported view's own two sliders",
             "imported wires: %s" % wires,
             len(wires) == 1 and wires[0][1] == "Value" and wires[0][3] == "Value")

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
             "the imported alias targets a slider inside the copy (%s)" % csl,
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

    for fn in (test_view_clone_wiring, test_clone_of_clone, test_clone_into_self_refused,
               test_save_load_view_wiring, test_export_import_view_wiring,
               test_clone_compiled_widget):
        before = [m for m, _ in container_children(raw, home)]
        guarded(fn, raw, r, home)
        close_new_children(raw, home, before)

    raw.close()
    sys.exit(min(r.summary(), 254))	# the return code IS the failure count


if __name__ == "__main__":
    main()
