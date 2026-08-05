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
    protocol itself: a TCP and a Bridge, wired together, activated. The
    transport the raw tests run on is the first thing the protocol proves
    it can make. They live in /Main with the web flow's own plumbing, NOT
    on the canvas - load-flow destroys /Root's contents by design, and a
    transport that sits there gets wiped mid-command."""
    try:
        s = socket.create_connection((host, rawport), timeout=0.5)
        s.close()
        return
    except Exception:
        pass

    from cdp import WS
    ws = WS("ws://%s:%d/" % (host, webport))
    for cmd in [
        {"cmd": "create-instance", "class": "TCP", "as": "/Main/RawTcp", "container": "/Main", "hidden": "1"},
        {"cmd": "set-property", "instance": "/Main/RawTcp", "prop": "LocalPort", "value": str(rawport)},
        {"cmd": "create-instance", "class": "Bridge", "as": "/Main/RawBridge", "container": "/Main", "hidden": "1"},
        {"cmd": "connect", "from": "/Main/RawTcp", "fromPort": "Out", "to": "/Main/RawBridge", "toPort": "In"},
        {"cmd": "connect", "from": "/Main/RawBridge", "fromPort": "Out", "to": "/Main/RawTcp", "toPort": "In"},
        {"cmd": "activate", "instance": "/Main/RawBridge"},
        {"cmd": "activate", "instance": "/Main/RawTcp"},
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


def group_view(raw, home, name):
    """A dedicated, uniquely-named child view for ONE group of tests inside
    a suite - so whatever it builds (including clone-instance's own
    auto-minted names; clone-instance never takes an "as") can't be
    confused with another group's instances or events. Returns the view's
    alias, already being viewed (see suite_view)."""
    path = home + "/" + name
    raw.send({"cmd": "create-instance", "class": "View", "as": path, "container": home})
    raw.wait_event(lambda e: e.get("event") == "instance-created" and e.get("instance") == path)
    raw.send({"cmd": "list-instances", "container": path})
    raw.wait_event(lambda e: e.get("event") == "instances-done", timeout=4)
    return path


def close_group(raw, view):
    """Delete a group's view once its checks are done - it stops occupying
    a name and can't leave anything behind for a later group to collide
    with or a browser suite sharing the session to trip over."""
    raw.send({"cmd": "delete-instance", "instance": view})
    raw.wait_event(lambda e: e.get("event") == "instance-removed" and e.get("instance") == view, timeout=4)


def container_children(raw, container):
    """The current children of `container`, freshly listed (this both views
    it and answers "what's there right now").

    list-instances({"container": ""}) (root) announces its children with
    container "/Root", not "" - a fresh create-instance echoes the
    command's own literal "" instead, so listing EXISTING children and
    announcing a JUST-CREATED one take different paths to the same field.
    Confirmed directly. Accept both spellings for root."""
    wanted = ("", "/Root") if container == "" else (container,)
    raw.send({"cmd": "list-instances", "container": container})
    out = []
    while True:
        e = raw.wait_event(lambda e: (e.get("event") == "instance-created"
                                      and e.get("container") in wanted
                                      and e.get("instance") not in [m[0] for m in out])
                           or e.get("event") == "instances-done", timeout=4)
        if not e or e.get("event") == "instances-done":
            break
        out.append((e.get("instance"), e.get("class")))
    return out


def close_new_children(raw, container, before):
    """Delete every child of `container` that appeared after `before` (a
    prior container_children() snapshot) - whatever a test left behind,
    its own named group view and any incidental siblings alike, without
    the caller having to know every path a test might have touched."""
    for m, c in container_children(raw, container):
        if m not in before:
            raw.send({"cmd": "delete-instance", "instance": m})
            raw.wait_event(lambda e: e.get("event") == "instance-removed" and e.get("instance") == m, timeout=4)


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
            print(".", end="", flush=True)
            return
        print("TEST     %s" % name)
        print("  expected: %s" % expected)
        print("  observed: %s" % observed)
        print("  result:   %s" % ("PASS" if ok else "FAIL"))
        print()

    def summary(self):
        failed = [r for r in self.results if not r[3]]
        if any(r[3] for r in self.results) and not self.verbose:
            print()  # end the dot line before the summary
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
        self.host, self.port = host, port
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(0.2)
        self.buf = b""
        self.events = []

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass

    def reconnect(self):
        """A load destroys every container this connection had open, so its
        view state names things that are gone - the socket is live but
        blind, and nothing inside the new content is announced to it.
        Drop it so the bridge forgets the session, come back fresh, and
        take the boot replay as the new picture. The browser does exactly
        this by reloading the page."""
        self.close()
        time.sleep(0.4)
        self.sock = socket.create_connection((self.host, self.port), timeout=5)
        self.sock.settimeout(0.2)
        self.buf = b""
        self.events = []
        deadline = time.time() + 2.0      # let the boot replay land
        while time.time() < deadline:
            self.pump()
            time.sleep(0.1)

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
    """Twin of the GUI's alias rendering: the ENGINE stamps Widget on an
    alias at birth from what the target's class published.
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
    r.expect("widget stamp: create-alias stamps the published type",
             "Widget=5 (PROP_SLIDER, Slider.Value's published type)",
             "Widget=%s" % w,
             w == "5")

    raw.send({"cmd": "create-alias", "of": source, "prop": "ReservedViewOpen",
              "container": home, "x": "600", "y": "90"})
    ev2 = raw.wait_event(lambda e: e.get("event") == "instance-created"
                         and e.get("class") == "Alias"
                         and e.get("instance") != name)
    name2 = ev2.get("instance") if ev2 else None
    w2 = raw.value_of(name2, "Widget") if name2 else None
    r.expect("widget stamp: an alias of ReservedViewOpen is a doorway",
             "Widget=12 (PROP_ICON - ReservedViewOpen's published type)",
             "Widget=%s" % w2,
             w2 == "12")


def test_internals_members_stamped(raw, r, home, source):
    """Twin of the Options panel: the internals view's member aliases carry
    the engine-stamped Widget, and asking twice reuses the ONE
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
    r.expect("internals: members carry the stamped presentation",
             "the Value member has Widget=5 (%d members total)" % len(members),
             "members=%d valueMember=%s Widget=%s"
             % (len(members), value_member, w),
             len(members) > 5 and w == "5")

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

    # clone-instance always auto-mints its new name (no "as") - land it in
    # a dest view that belongs to ONLY this test, not the shared suite
    # home, so its auto-minted name can never be confused with another
    # test's own clone-instance call landing in the same container.
    raw.send({"cmd": "create-instance", "class": "View", "as": home + "/CPDest", "container": home})
    raw.wait_event(lambda e: e.get("event") == "instance-created"
                   and e.get("instance") == home + "/CPDest")
    raw.send({"cmd": "list-instances", "container": home + "/CPDest"})  # view it, or its own clone never announces
    raw.wait_event(lambda e: e.get("event") == "instances-done", timeout=4)

    raw.send({"cmd": "clone-instance", "of": view, "container": home + "/CPDest", "x": "40", "y": "700"})
    ev2 = raw.wait_event(lambda e: e.get("event") == "instance-created"
                         and e.get("class") == "View"
                         and e.get("container") == home + "/CPDest")
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
    load - internals views are lazily rebuilt, never saved), so the
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
             "flow-saved for a stamped saved/rawtwin_<when>.flow",
             "%s" % (ev.get("file") if ev else None),
             bool(ev) and (ev.get("file") or "").startswith("saved/rawtwin")
             and (ev.get("file") or "").endswith(".flow"))

    raw.send({"cmd": "list-flows"})
    files = []
    while True:
        e = raw.wait_event(lambda e: e.get("event") in ("flow-file", "flows-done"), timeout=4)
        if not e or e.get("event") == "flows-done":
            break
        files.append(e.get("file"))
    r.expect("list-flows: the dialog's list is engine fact",
             "a rawtwin version among the listed flows",
             "%s" % files,
             any(f.startswith("rawtwin") and f.endswith(".flow") for f in files))

    # destroy, then a named load resurrects it - value and all
    raw.send({"cmd": "delete-instance", "instance": home + "/SaveMe"})
    raw.wait_event(lambda e: e.get("event") == "instance-removed"
                   and e.get("instance") == home + "/SaveMe")

    # load restores IN PLACE: the container's contents are destroyed and
    # rebuilt from the file under their own recorded names, so SaveMe comes
    # back at exactly the path it was saved from.
    raw.send({"cmd": "load-flow", "file": "rawtwin"})
    raw.wait_event(lambda e: e.get("event") == "flow-loaded", timeout=6)
    # the load destroyed everything this connection had open - come back
    # fresh or it cannot see what was just loaded
    raw.reconnect()

    v, found = None, None
    deadline = time.time() + 8.0
    while time.time() < deadline and v != "88":
        for m, c in container_children(raw, home):
            if c == "Slider" and m == home + "/SaveMe":
                found, v = m, raw.value_of(m, "Value")
                break
        if v != "88":
            time.sleep(0.4)

    r.expect("load: the member-made edit is restored as the original's fact",
             "%s/SaveMe is back at its own path with Value=88 - the edit went "
             "through a panel member, but the fact belongs to the slider" % home,
             "found=%s Value=%s" % (found, v), v == "88")


def test_load_then_clone_binding(raw, r, home):
    """The reported bug: a self-contained view (a cloned slider + an alias
    of its Value), saved, loaded, then cloned - the copies' aliases must
    bind to their OWN slider, not to the one they were saved from.

    Load restores in place - the container's contents are destroyed and
    rebuilt from the file under their recorded names - so the view comes
    back at its own path, and its alias must resolve to its OWN slider
    rather than the one it was saved from. That binding is what this
    proves, on the restored view AND on a later clone."""
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

    def bound_to_own_slider(view, value="55"):
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
        raw.send({"cmd": "set-property", "instance": al, "prop": "Value", "value": value})
        live = raw.wait_event(lambda e: e.get("event") == "property-changed"
                              and e.get("instance") == sl and e.get("port") == "Value"
                              and e.get("value") == value, timeout=4)
        return mem, sl, tgt, bool(live)

    # load restores IN PLACE: /Root's contents are destroyed and rebuilt
    # from the file under their recorded names, so LrView comes back at
    # its own path, with its own Slider and its Alias bound to that one.
    raw.events = []
    raw.send({"cmd": "load-flow", "file": "lrtwin"})
    raw.wait_event(lambda e: e.get("event") == "flow-loaded", timeout=6)
    # the load destroyed everything this connection had open - come back
    # fresh or it cannot see what was just loaded
    raw.reconnect()

    copy = home + "/LrView"
    mem, sl, tgt, live = bound_to_own_slider(copy)
    r.expect("load: the loaded copy's alias binds to the loaded copy's slider",
             "the restored Alias targets the restored Slider and a write "
             "through it lands there",
             "copy=%s members=%s Target=%s ownSlider=%s liveWrite=%s"
             % (copy, [c for _, c in mem], tgt, sl, live),
             bool(sl) and tgt == sl and live)

    # and the reported gesture: clone the view, the clone binds to itself.
    # Land it in a dest view owned by ONLY this test - clone-instance always
    # auto-mints its new name (no "as"), and matching a bare "View_" prefix
    # in the shared suite home can pick up ANOTHER test's own clone-instance
    # call (test_clone_panel does one too). A private, freshly-listed dest
    # also sidesteps load-flow having just rebuilt `home` as a fresh node
    # whose own viewed mark did not carry over.
    raw.send({"cmd": "create-instance", "class": "View", "as": home + "/LrCloneDest", "container": home})
    raw.wait_event(lambda e: e.get("event") == "instance-created"
                   and e.get("instance") == home + "/LrCloneDest")
    raw.send({"cmd": "list-instances", "container": home + "/LrCloneDest"})
    raw.wait_event(lambda e: e.get("event") == "instances-done", timeout=4)

    raw.events = []
    raw.send({"cmd": "clone-instance", "of": home + "/LrView", "container": home + "/LrCloneDest", "x": "500", "y": "500"})
    ev = raw.wait_event(lambda e: e.get("event") == "instance-created"
                        and e.get("class") == "View"
                        and e.get("container") == home + "/LrCloneDest")
    v4 = ev.get("instance") if ev else None
    # the clone was taken from the view checked above, so it already holds
    # that value - a repeat write is not a change and fans out to nobody
    mem, sl, tgt, live = bound_to_own_slider(v4, "77") if v4 else ([], None, None, False)
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


# Regression guard (2026-07-30): IsPaletteExcluded (object.c) sat as an
# unimplemented `(void) className; return 0;` stub - despite ALREADY having
# a comment above it describing Bridge/TCP as classes that were supposed to
# be excluded - for long enough that it got "fixed" and silently reverted
# back to the no-op stub something like six times before this test existed.
# Each regression showed up as "every widget has a tiny panel with no
# controls": a bare palette-bootstrap instance of a class that's only ever
# meant to be built programmatically (MCPAgent by MCPSource_BuildAgentView;
# Bridge/TCP are this very session's own transport) has none of the real
# construction that gives it controls. If this test starts failing, the fix
# is almost always that IsPaletteExcluded's body got emptied out again -
# see the huge warning comment sitting directly above its definition.
def test_palette_excludes_internals(raw, r):
    """The palette (session-global, always at /Root/Palette - not something
    a test builds or tears down) must never offer a bootstrap icon for a
    class that only makes sense built programmatically by something else."""
    children = container_children(raw, "/Root/Palette")
    found_bad = [cls for name, cls in children
                 if cls in ("Bridge", "TCP", "MCPAgent")]
    r.expect("palette excludes internal-only classes",
             "no Bridge/TCP/MCPAgent bootstrap icon under /Root/Palette",
             "found: %s (of %d palette entries)" % (found_bad or "none", len(children)),
             not found_bad)


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

    guarded(test_palette_excludes_internals, raw, r)

    home = suite_view(raw, "RawTests")   # the suite's own top-level view

    # atomic-birth/widget-stamp/internals/clone-panel all operate on the ONE
    # instance born in the first - one dependency chain, one group view
    birth = group_view(raw, home, "AtomicBirth")
    source = guarded(test_atomic_birth, raw, r, birth)
    if source:
        guarded(test_widget_stamp, raw, r, birth, source)
        guarded(test_internals_members_stamped, raw, r, birth, source)
        guarded(test_clone_panel, raw, r, birth, source)
    close_group(raw, birth)

    g = group_view(raw, home, "SaveLoad")
    guarded(test_save_load, raw, r, g)
    close_group(raw, g)

    g = group_view(raw, home, "LoadThenClone")
    guarded(test_load_then_clone_binding, raw, r, g)
    close_group(raw, g)

    g = group_view(raw, home, "Move")
    guarded(test_move, raw, r, g)
    close_group(raw, g)

    g = group_view(raw, home, "Delete")
    guarded(test_delete, raw, r, g)
    close_group(raw, g)

    raw.close()
    sys.exit(1 if r.summary() else 0)


if __name__ == "__main__":
    main()
