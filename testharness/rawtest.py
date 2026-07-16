#!/usr/bin/env python3
"""
Raw-protocol twins: the same mechanisms the GUI tests exercise, driven
with NO browser - a plain TCP socket to the Bridge on port 8091, one
JSON command per message, JSON events back.

This is readmefirst.md's "harness rule" made real: every GUI scenario
has a twin here proving the mechanism lives in the engine, reachable by
any client (nc, a script, an MCP agent) in the same one command the GUI
sends. The GUI tests then only prove presentation.

A passing check prints NOTHING - only failures speak, saying what was
EXPECTED and what was actually OBSERVED, plus one summary line. Add -v
to watch every check go by.

Run through run.sh, or standalone against a running server:

    python3 testharness/rawtest.py --host 127.0.0.1 --port 8091
"""
import argparse, json, socket, sys, time


def ensure_raw_bridge(host, rawport, webport=8083):
    """The default app boots web-only (main.c) - the raw TCP surface is
    not a birthright, it's one composition away. If nothing answers on
    rawport, connect over the WebSocket bridge and BUILD it with the
    protocol itself: a TCP and a Bridge (hidden plumbing), wired together,
    activated. The transport the raw tests run on is the first thing the
    protocol proves it can make."""
    try:
        s = socket.create_connection((host, rawport), timeout=0.5)
        s.close()
        return
    except Exception:
        pass

    from cdp import WS
    ws = WS("ws://%s:%d/" % (host, webport))
    for cmd in [
        {"cmd": "create-instance", "class": "TCP", "as": "/Root/RawTcp", "hidden": "1"},
        {"cmd": "set-property", "instance": "/Root/RawTcp", "prop": "LocalPort", "value": str(rawport)},
        {"cmd": "create-instance", "class": "Bridge", "as": "/Root/RawBridge", "hidden": "1"},
        {"cmd": "connect", "from": "/Root/RawTcp", "fromPort": "Out", "to": "/Root/RawBridge", "toPort": "In"},
        {"cmd": "connect", "from": "/Root/RawBridge", "fromPort": "Out", "to": "/Root/RawTcp", "toPort": "In"},
        {"cmd": "activate", "instance": "/Root/RawBridge"},
        {"cmd": "activate", "instance": "/Root/RawTcp"},
    ]:
        ws.send(json.dumps(cmd))
        time.sleep(0.15)
    ws.s.close()
    time.sleep(0.5)


def suite_view(raw, name):
    """EVERY suite builds inside its own View. The session is shared with
    the browser suite, so anything left loose on the canvas can land on
    top of what guitest is trying to click - failing a test in a file it
    has never heard of, intermittently. Inside a view, nothing can: a
    closed view's contents are never even rendered.

    Returns the view's alias, already being VIEWED by this connection -
    without which nothing built inside it would be announced back (you
    only hear about the insides of a container you have opened)."""
    raw.events = []     # the boot replay left its own View events in here
    raw.send({"cmd": "create-instance", "class": "View", "as": "/Root/" + name,
              "container": "", "x": "40", "y": "8"})
    ev = raw.wait_event(lambda e: e.get("event") == "instance-created"
                        and e.get("class") == "View" and e.get("container") == "")
    home = ev.get("instance") if ev else "/Root/" + name

    raw.send({"cmd": "list-instances", "container": home})
    raw.wait_event(lambda e: e.get("event") == "instances-done", timeout=4)
    return home


# --------------------------------------------------------------------------
# expected-vs-observed reporting - shared by every suite (flowtest,
# viewclonetest; guitest keeps its own copy of the same contract)
# --------------------------------------------------------------------------

class Report:
    """A PASSING TEST PRINTS NOTHING. Only failures speak, and they say
    everything (what was expected, what was actually observed) - plus one
    summary line per suite. There will eventually be hundreds of these:
    a green run must cost a line to read, not a screenful. Pass
    verbose=True (-v) when a human wants to watch every check go by."""

    def __init__(self, label="tests", verbose=False):
        self.results = []
        self.label = label
        self.verbose = verbose

    def expect(self, name, expected, observed, ok):
        self.results.append((name, expected, observed, bool(ok)))
        if ok and not self.verbose:
            return
        print("TEST     %s" % name)
        print("  expected: %s" % expected)
        print("  observed: %s" % observed)
        print("  result:   %s" % ("PASS" if ok else "FAIL"))
        print()

    def summary(self):
        failed = [r for r in self.results if not r[3]]
        print("%s: %d tests, %d passed, %d failed"
              % (self.label, len(self.results), len(self.results) - len(failed), len(failed)))
        return len(failed)


# --------------------------------------------------------------------------
# the raw client: a socket, a send pacer, and a JSON-object splitter
# --------------------------------------------------------------------------

class Raw:
    """One bridge session. Commands go out one JSON object per send (paced,
    so the TCP object's one-message-per-recv framing never sees two
    commands glued into one segment); events accumulate in a byte buffer
    and are split back into objects by brace depth (several events CAN
    share one segment on the way back - the bridge's out-buffer drains in
    blocks)."""

    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(0.2)
        self.buf = b""
        self.events = []

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass

    def send(self, obj):
        self.sock.sendall(json.dumps(obj).encode())
        time.sleep(0.15)          # one command per recv on the far side
        self.pump()

    def pump(self):
        """Read whatever has arrived and split it into parsed events."""
        while True:
            try:
                data = self.sock.recv(65536)
            except socket.timeout:
                break
            if not data:
                break
            self.buf += data
        for text in self._split():
            try:
                self.events.append(json.loads(text))
            except ValueError:
                self.events.append({"event": "_unparsed", "raw": text})

    def _split(self):
        """Return the complete top-level JSON objects in the buffer,
        keeping any trailing partial object for the next pump."""
        s = self.buf.decode(errors="replace")
        out = []
        depth, start, instr, esc = 0, -1, False, False
        for i, ch in enumerate(s):
            if instr:
                if esc:
                    esc = False
                elif ch == "\\":
                    esc = True
                elif ch == '"':
                    instr = False
                continue
            if ch == '"':
                instr = True
            elif ch == "{":
                if depth == 0:
                    start = i
                depth += 1
            elif ch == "}" and depth > 0:
                depth -= 1
                if depth == 0:
                    out.append(s[start:i + 1])
                    start = -1
        # mid-object: keep the partial tail; otherwise anything left over
        # is inter-object noise (an object can only start at '{')
        self.buf = s[start:].encode() if depth > 0 and start >= 0 else b""
        return out

    def wait_event(self, pred, timeout=5.0, desc="event"):
        """Poll until an event matching pred arrives. A matched event is
        CONSUMED (removed) so a later wait for "the next instances-done"
        can never be satisfied by a stale one; unmatched events stay for
        later, more specific matchers. Returns the event or None."""
        deadline = time.time() + timeout
        while True:
            self.pump()
            for i, ev in enumerate(self.events):
                if pred(ev):
                    del self.events[i]
                    return ev
            if time.time() >= deadline:
                return None
            time.sleep(0.1)

    def value_of(self, instance, port, timeout=5.0):
        """subscribe pushes a data prop's current value immediately - the
        raw client's way of reading one property."""
        self.send({"cmd": "subscribe", "instance": instance, "port": port})
        ev = self.wait_event(
            lambda e: e.get("event") == "property-changed"
            and e.get("instance") == instance and e.get("port") == port,
            timeout, "%s.%s" % (instance, port))
        return ev.get("value") if ev else None


# --------------------------------------------------------------------------
# the twins
# --------------------------------------------------------------------------

def test_atomic_birth(raw, r, home):
    """Twin of the GUI's palette-drop birth: ONE create-instance carrying
    class/container/x/y, no client-supplied name - the server mints one
    and the instance-created event teaches it back."""
    raw.send({"cmd": "create-instance", "class": "Slider",
              "container": home, "x": "510", "y": "40"})
    ev = raw.wait_event(lambda e: e.get("event") == "instance-created"
                        and e.get("class") == "Slider")
    name = ev.get("instance") if ev else None
    ok_name = bool(name) and name.startswith(home + "/Slider_")
    r.expect("atomic birth: one verb, server-minted name",
             "instance-created for class Slider named %s/Slider_N, in that container" % home,
             "event=%s name=%s container=%r"
             % (bool(ev), name, ev.get("container") if ev else None),
             ok_name and ev.get("container") == home)

    if not name:
        return None

    x = raw.value_of(name, "X")
    y = raw.value_of(name, "Y")
    r.expect("atomic birth: placement rode in the same command",
             "X=510 Y=40 readable back through subscribe",
             "X=%s Y=%s" % (x, y),
             x == "510" and y == "40")

    honored = home + "/RawAsName"
    raw.send({"cmd": "create-instance", "class": "Slider", "as": honored, "container": home})
    ev2 = raw.wait_event(lambda e: e.get("event") == "instance-created"
                         and e.get("instance") == honored)
    r.expect("atomic birth: a client-supplied 'as' is still honored",
             "instance-created named %s (flow replay compatibility)" % honored,
             "event=%s" % (ev2.get("instance") if ev2 else None),
             bool(ev2))
    return name


def test_widget_stamp(raw, r, home, source):
    """Twin of the GUI's alias rendering: the ENGINE stamps Widget (and
    Direction) on an alias at birth from what the target's class published.
    The client never sends or deduces them - so here they must come back
    without this test ever mentioning them."""
    raw.send({"cmd": "create-alias", "of": source, "prop": "Value",
              "container": home, "x": "600", "y": "40"})
    ev = raw.wait_event(lambda e: e.get("event") == "instance-created"
                        and e.get("class") == "Alias")
    name = ev.get("instance") if ev else None
    if not name:
        r.expect("widget stamp: alias created", "an Alias instance", "no event", False)
        return

    w = raw.value_of(name, "Widget")
    d = raw.value_of(name, "Direction")
    r.expect("widget stamp: create-alias stamps the published type",
             "Widget=5 (PROP_SLIDER, Slider.Value's published type), Direction=data",
             "Widget=%s Direction=%s" % (w, d),
             w == "5" and d == "data")

    raw.send({"cmd": "create-alias", "of": source, "prop": "Open",
              "container": home, "x": "600", "y": "90"})
    ev2 = raw.wait_event(lambda e: e.get("event") == "instance-created"
                         and e.get("class") == "Alias"
                         and e.get("instance") != name)
    name2 = ev2.get("instance") if ev2 else None
    w2 = raw.value_of(name2, "Widget") if name2 else None
    r.expect("widget stamp: an alias of Open is a doorway",
             "Widget=12 (PROP_ICON - Open's published type)",
             "Widget=%s" % w2,
             w2 == "12")


def test_internals_members_stamped(raw, r, home, source):
    """Twin of the Options panel: the internals view's member aliases carry
    the engine-stamped Widget/Direction, and asking twice reuses the ONE
    cached view."""
    raw.send({"cmd": "internals", "instance": source})
    ev = raw.wait_event(lambda e: e.get("event") == "internals"
                        and e.get("instance") == source)
    view = ev.get("view") if ev else None
    r.expect("internals: engine names the panel view",
             "an internals event carrying the panel view's alias",
             "view=%s" % view, bool(view))
    if not view:
        return

    raw.send({"cmd": "list-instances", "container": view})
    members = []
    while True:
        e = raw.wait_event(lambda e: (e.get("event") == "instance-created"
                                      and e.get("container") == view
                                      and e.get("instance") not in members)
                           or e.get("event") == "instances-done", timeout=4)
        if not e or e.get("event") == "instances-done":
            break
        members.append(e.get("instance"))

    value_member = None
    for m in members:
        if raw.value_of(m, "TargetProp") == "Value":
            value_member = m
            break
    w = raw.value_of(value_member, "Widget") if value_member else None
    d = raw.value_of(value_member, "Direction") if value_member else None
    r.expect("internals: members carry the stamped presentation",
             "the Value member has Widget=5, Direction=data (%d members total)" % len(members),
             "members=%d valueMember=%s Widget=%s Direction=%s"
             % (len(members), value_member, w, d),
             len(members) > 5 and w == "5" and d == "data")

    raw.send({"cmd": "internals", "instance": source})
    ev2 = raw.wait_event(lambda e: e.get("event") == "internals"
                         and e.get("instance") == source)
    r.expect("internals: asking twice reuses the one view",
             "same view alias %s" % view,
             "view=%s" % (ev2.get("view") if ev2 else None),
             bool(ev2) and ev2.get("view") == view)


def test_clone_panel(raw, r, home, source):
    """The recursion's payoff (readmefirst repair #6): a control panel is a
    real view of real aliases, so "clone my panel" is the ordinary
    clone-instance verb - members, layout, and bindings ride along with no
    special mechanism."""
    raw.send({"cmd": "internals", "instance": source})
    ev = raw.wait_event(lambda e: e.get("event") == "internals"
                        and e.get("instance") == source)
    view = ev.get("view") if ev else None
    if not view:
        r.expect("clone panel: staging", "the internals view", "no view", False)
        return

    raw.send({"cmd": "clone-instance", "of": view, "container": home, "x": "40", "y": "700"})
    # clones of a View mint /Root/View_N - anything else (a replayed
    # Palette event, say) is stale history, not our clone
    ev2 = raw.wait_event(lambda e: e.get("event") == "instance-created"
                         and e.get("class") == "View"
                         and str(e.get("instance", "")).startswith(home + "/View_"))
    clone = ev2.get("instance") if ev2 else None
    if not clone:
        r.expect("clone panel: a view clone arrives", "instance-created for a View", "none", False)
        return

    raw.send({"cmd": "list-instances", "container": clone})
    members = []
    while True:
        e = raw.wait_event(lambda e: (e.get("event") == "instance-created"
                                      and e.get("container") == clone
                                      and e.get("instance") not in members)
                           or e.get("event") == "instances-done", timeout=4)
        if not e or e.get("event") == "instances-done":
            break
        members.append(e.get("instance"))
    r.expect("clone panel: one verb clones the whole panel",
             "the clone is a View with the panel's member aliases inside it",
             "clone=%s members=%d" % (clone, len(members)),
             len(members) > 5)


def test_save_load(raw, r, home):
    """Save/Load with named files in saved/: an edit made THROUGH a panel
    member records as the ORIGINAL's fact (the member won't exist at
    replay - internals views are lazily rebuilt, never recorded), so the
    value survives a save, a delete, and a named load."""
    raw.send({"cmd": "create-instance", "class": "Slider", "as": home + "/SaveMe",
              "container": home, "x": "610", "y": "300"})
    raw.wait_event(lambda e: e.get("event") == "instance-created"
                   and e.get("instance") == home + "/SaveMe")

    # the edit goes through an internals member - the doorway, not the fact
    raw.send({"cmd": "internals", "instance": home + "/SaveMe"})
    ev = raw.wait_event(lambda e: e.get("event") == "internals"
                        and e.get("instance") == home + "/SaveMe")
    view = ev.get("view") if ev else None
    raw.send({"cmd": "list-instances", "container": view})
    members = []
    while True:
        e = raw.wait_event(lambda e: (e.get("event") == "instance-created"
                                      and e.get("container") == view
                                      and e.get("instance") not in members)
                           or e.get("event") == "instances-done", timeout=4)
        if not e or e.get("event") == "instances-done":
            break
        members.append(e.get("instance"))
    vm = None
    for m in members:
        if raw.value_of(m, "TargetProp") == "Value":
            vm = m
            break
    raw.send({"cmd": "set-property", "instance": vm, "prop": "Value", "value": "88"})

    raw.send({"cmd": "save-flow", "file": "rawtwin"})
    ev = raw.wait_event(lambda e: e.get("event") == "flow-saved")
    r.expect("save: a named flow lands in saved/",
             "flow-saved for saved/rawtwin.flow",
             "%s" % (ev.get("file") if ev else None),
             bool(ev) and ev.get("file") == "saved/rawtwin.flow")

    raw.send({"cmd": "list-flows"})
    files = []
    while True:
        e = raw.wait_event(lambda e: e.get("event") in ("flow-file", "flows-done"), timeout=4)
        if not e or e.get("event") == "flows-done":
            break
        files.append(e.get("file"))
    r.expect("list-flows: the dialog's list is engine fact",
             "rawtwin.flow among the listed flows",
             "%s" % files, "rawtwin.flow" in files)

    # destroy, then a named load resurrects it - value and all
    raw.send({"cmd": "delete-instance", "instance": home + "/SaveMe"})
    raw.wait_event(lambda e: e.get("event") == "instance-removed"
                   and e.get("instance") == home + "/SaveMe")
    raw.send({"cmd": "load-flow", "file": "rawtwin"})
    raw.wait_event(lambda e: e.get("event") == "flow-loaded", timeout=6)
    v = raw.value_of(home + "/SaveMe", "Value")
    r.expect("load: the member-made edit replays as the original's fact",
             "%s/SaveMe is back with Value=88 (recorded resolved, not by its doorway)" % home,
             "Value=%s" % v, v == "88")


def test_load_then_clone_binding(raw, r, home):
    """The reported bug: a self-contained view (a cloned slider + an alias
    of its Value), saved, loaded, then cloned - the copies' aliases must
    bind to their OWN slider, not to the one they were saved from.

    Load merges (it never clears the canvas), so loading a flow whose
    names are still live names the copy fresh - and every recorded
    back-reference (the alias's `of`, naming a slider two lines up) has
    to follow, or the loaded alias binds to the original's slider. That
    remap is what this proves, on the loaded copy AND on a later clone."""
    raw.send({"cmd": "create-instance", "class": "View", "as": home + "/LrView", "container": home})
    raw.wait_event(lambda e: e.get("event") == "instance-created"
                   and e.get("instance") == home + "/LrView")
    raw.send({"cmd": "list-instances", "container": home + "/LrView"})   # view it, so member events arrive
    raw.wait_event(lambda e: e.get("event") == "instances-done", timeout=4)

    raw.send({"cmd": "clone-instance", "of": "/Root/Palette/Slider",
              "container": home + "/LrView", "x": "20", "y": "20"})
    ev = raw.wait_event(lambda e: e.get("event") == "instance-created"
                        and e.get("class") == "Slider"
                        and e.get("container") == home + "/LrView")
    sl = ev.get("instance") if ev else None
    raw.send({"cmd": "create-alias", "of": sl, "prop": "Value",
              "container": home + "/LrView", "x": "20", "y": "80"})
    raw.wait_event(lambda e: e.get("event") == "instance-created"
                   and e.get("class") == "Alias" and e.get("container") == home + "/LrView")

    raw.send({"cmd": "save-flow", "file": "lrtwin"})
    raw.wait_event(lambda e: e.get("event") == "flow-saved")

    def members(view):
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

    def bound_to_own_slider(view):
        """view's Alias must name view's own Slider, and a write through
        it must actually land there - Target is metadata, the link is the
        binding."""
        mem = members(view)
        sl = next((m for m, c in mem if c == "Slider"), None)
        al = next((m for m, c in mem if c == "Alias"), None)
        if not sl or not al:
            return mem, sl, None, False
        tgt = raw.value_of(al, "Target")
        raw.value_of(sl, "Value")          # arm the subscription
        raw.send({"cmd": "set-property", "instance": al, "prop": "Value", "value": "55"})
        live = raw.wait_event(lambda e: e.get("event") == "property-changed"
                              and e.get("instance") == sl and e.get("port") == "Value"
                              and e.get("value") == "55", timeout=4)
        return mem, sl, tgt, bool(live)

    # load into the LIVE session: the names are still taken, so the copy
    # is named fresh - and its alias must follow to the fresh slider.
    # A save records the WHOLE session, so the replay rebuilds every
    # earlier test's objects too; the LrView copy is the one view that
    # comes back holding exactly one Slider and one Alias.
    raw.events = []
    raw.send({"cmd": "load-flow", "file": "lrtwin"})
    raw.wait_event(lambda e: e.get("event") == "flow-loaded", timeout=6)
    fresh = [e.get("instance") for e in raw.events
             if e.get("event") == "instance-created" and e.get("class") == "View"]

    # the replay rebuilds this suite's own view too, so the LrView copy is
    # one level inside that copy - and a container's members are only
    # announced once we list (start viewing) it
    for v in list(fresh):
        fresh += [m for m, c in members(v) if c == "View"]

    copy, mem, sl, tgt, live = None, [], None, None, False
    for v in fresh:
        shape = sorted(c for _, c in members(v))
        if shape == ["Alias", "Slider"]:
            copy = v
            mem, sl, tgt, live = bound_to_own_slider(v)
            break
    r.expect("load: the loaded copy's alias binds to the loaded copy's slider",
             "the copy's Alias targets ITS Slider and a write through it lands there",
             "copy=%s members=%s Target=%s ownSlider=%s liveWrite=%s"
             % (copy, [c for _, c in mem], tgt, sl, live),
             bool(copy) and tgt == sl and live)

    orig = members(home + "/LrView")
    r.expect("load: the merge left the original view alone",
             "%s/LrView still holds exactly its own Slider + Alias" % home,
             "%s" % [c for _, c in orig],
             len(orig) == 2 and sorted(c for _, c in orig) == ["Alias", "Slider"])

    # and the reported gesture: clone the view, the clone binds to itself
    raw.events = []
    raw.send({"cmd": "clone-instance", "of": home + "/LrView", "container": home, "x": "500", "y": "500"})
    ev = raw.wait_event(lambda e: e.get("event") == "instance-created"
                        and e.get("class") == "View"
                        and str(e.get("instance", "")).startswith(home + "/View_"))
    v4 = ev.get("instance") if ev else None
    mem, sl, tgt, live = bound_to_own_slider(v4) if v4 else ([], None, None, False)
    r.expect("clone after load: the clone's alias drives the clone's own slider",
             "the cloned Alias targets the cloned Slider and the write lands there",
             "clone=%s Target=%s ownSlider=%s liveWrite=%s" % (v4, tgt, sl, live),
             bool(v4) and tgt == sl and live)


def test_move(raw, r, home):
    """Twin of the GUI's drag-drop: ONE move-instance re-containers,
    renames, and repositions; the engine refuses a containment cycle."""
    raw.send({"cmd": "create-instance", "class": "View", "as": home + "/RawMoveView", "container": home})
    raw.wait_event(lambda e: e.get("event") == "instance-created"
                   and e.get("instance") == home + "/RawMoveView")
    raw.send({"cmd": "create-instance", "class": "Slider",
              "container": home, "x": "700", "y": "40"})
    ev = raw.wait_event(lambda e: e.get("event") == "instance-created"
                        and e.get("class") == "Slider")
    name = ev.get("instance") if ev else None
    if not name:
        r.expect("move: staging", "a Slider to move", "no event", False)
        return

    raw.send({"cmd": "move-instance", "of": name,
              "container": home + "/RawMoveView", "x": "25", "y": "35"})
    ev2 = raw.wait_event(lambda e: e.get("event") == "instance-renamed"
                         and e.get("from") == name)
    moved = ev2.get("to") if ev2 else None
    x = raw.value_of(moved, "X") if moved else None
    r.expect("move: one verb re-containers, renames, repositions",
             "instance-renamed into %s/RawMoveView/..., X=25" % home,
             "renamed to %s, X=%s" % (moved, x),
             bool(moved) and moved.startswith(home + "/RawMoveView/") and x == "25")

    raw.send({"cmd": "move-instance", "of": home + "/RawMoveView",
              "container": home + "/RawMoveView", "x": "5", "y": "5"})
    err = raw.wait_event(lambda e: e.get("event") == "error"
                         and e.get("cmd") == "move-instance")
    cont = raw.value_of(home + "/RawMoveView", "Container")
    r.expect("move: the engine refuses a containment cycle",
             "error event; the view's Container unchanged (%s)" % home,
             "error=%s Container=%r" % (bool(err), cont),
             bool(err) and cont == home)


def test_delete(raw, r, home):
    """Twin of Delete mode: the verb goes out, instance-removed is the
    only truth; an undeletable thing is refused and stays."""
    raw.send({"cmd": "create-instance", "class": "Slider", "as": home + "/RawDoomed", "container": home})
    raw.wait_event(lambda e: e.get("event") == "instance-created"
                   and e.get("instance") == home + "/RawDoomed")
    raw.send({"cmd": "delete-instance", "instance": home + "/RawDoomed"})
    ev = raw.wait_event(lambda e: e.get("event") == "instance-removed"
                        and e.get("instance") == home + "/RawDoomed")
    r.expect("delete: instance-removed is the confirmation",
             "an instance-removed event for /Root/RawDoomed",
             "event=%s" % bool(ev), bool(ev))

    raw.send({"cmd": "create-instance", "class": "Slider", "as": home + "/RawKeeper", "container": home})
    raw.wait_event(lambda e: e.get("event") == "instance-created"
                   and e.get("instance") == home + "/RawKeeper")
    raw.send({"cmd": "set-property", "instance": home + "/RawKeeper",
              "prop": "Deletable", "value": "0"})
    raw.send({"cmd": "delete-instance", "instance": home + "/RawKeeper"})
    err = raw.wait_event(lambda e: e.get("event") == "error"
                         and e.get("cmd") == "delete-instance")
    still = raw.value_of(home + "/RawKeeper", "Name")
    r.expect("delete: Deletable=0 is refused, nothing removed",
             "error event; the instance still answers",
             "error=%s Name=%s" % (bool(err), still),
             bool(err) and still == "RawKeeper")


# --------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8091)
    ap.add_argument("--webport", type=int, default=8083)
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="print every check, not just the failures")
    args = ap.parse_args()

    ensure_raw_bridge(args.host, args.port, args.webport)

    r = Report("raw-protocol", args.verbose)
    raw = Raw(args.host, args.port)

    # the documented contract: you hear about containers you listed -
    # same first command the GUI sends on connect
    raw.send({"cmd": "list-instances"})
    ev = raw.wait_event(lambda e: e.get("event") == "instances-done", timeout=8)
    r.expect("session opens: list-instances answers",
             "instances-done after listing the root",
             "got instances-done: %s" % bool(ev), bool(ev))

    def guarded(fn, *a):
        try:
            return fn(*a)
        except Exception as e:
            r.expect(fn.__name__, "no exception", "%s: %s" % (type(e).__name__, e), False)
            return None

    home = suite_view(raw, "RawTests")   # everything this suite builds lives in here

    source = guarded(test_atomic_birth, raw, r, home)
    if source:
        guarded(test_widget_stamp, raw, r, home, source)
        guarded(test_internals_members_stamped, raw, r, home, source)
        guarded(test_clone_panel, raw, r, home, source)
    guarded(test_save_load, raw, r, home)
    guarded(test_load_then_clone_binding, raw, r, home)
    guarded(test_move, raw, r, home)
    guarded(test_delete, raw, r, home)

    raw.close()
    sys.exit(1 if r.summary() else 0)


if __name__ == "__main__":
    main()
