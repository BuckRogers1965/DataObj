#!/usr/bin/env python3
"""
Scripted composite widgets (a View holding InBox, OutBox, and a ScriptBox
carrying the logic) through every variation a real widget goes through:
clone, alias, export/import, save/load. This is the MCPSource agent-widget
pattern's own twin, built self-contained (no external MCP service) so it
runs anywhere run.sh does.

The shape, every test: InBox (Textbox) -> ScriptBox.In, and ScriptBox.Out
-> OutBox (Textbox). The script appends "_done" to whatever arrives and
send()s it back out. The ScriptBox is an ordinary member of the view, so
it joins the flow by WIRE - nothing in the Source names an address, which
is exactly why the widget still works after being cloned or imported
under a different name.

The logic used to live in a bare Lua instance reaching its siblings with
sibget/sibset. Language hosts are opaque now - they publish nothing and
cannot be addressed from outside - so the ScriptBox is the thing that
carries a script, and the script talks to the widget through its own
ports.

Triggering is a plain set-property on InBox (a real message through a real
Connect, not a client-side simulation) and the proof is reading OutBox
back - a widget that lost its wiring or its script looks structurally
identical to one that didn't.

Run through run.sh, or standalone against a running server:

    python3 testharness/scriptedwidgettest.py --host 127.0.0.1 --port 8091
"""
import argparse, sys, time
from rawtest import Raw, Report, ensure_raw_bridge, suite_view, container_children, close_new_children

RUNNER_SOURCE = (
    "oninput(function(value, kind)\n"
    "  if kind == 'eof' then return end\n"
    "  send(tostring(value) .. '_done')\n"
    "end)\n"
)


def members(raw, view):
    # list-instances({"container": ""}) (root) announces its children with
    # container "/Root", not "" - a fresh create-instance echoes the
    # command's own literal "" instead, which is why suite_view()'s single
    # event match never hit this: listing EXISTING children and announcing
    # a JUST-CREATED one take different paths to the same field. Confirmed
    # directly (a raw list-instances with container:"" reports every root
    # child as container:"/Root"). Accept both spellings for root.
    wanted = ("", "/Root") if view == "" else (view,)
    raw.send({"cmd": "list-instances", "container": view})
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


def press_run(raw, runner):
    """The Run button, over the protocol. Exactly ONE activate: setting
    Language builds the inner host, so the first Run is the one that
    executes. Activating twice re-runs a box that is already live and
    wedges the engine in an unbounded message loop, so a restored copy
    gets one press here too."""
    raw.send({"cmd": "set-property", "instance": runner, "prop": "Language",
              "value": "Lua"})
    time.sleep(0.3)
    raw.send({"cmd": "activate", "instance": runner})
    time.sleep(0.8)


def run_widget(raw, runner, in_value, timeout=4.0):
    """Functional proof of the whole widget: write InBox and read OutBox
    back. Writing InBox IS the trigger - it is wired into the ScriptBox's
    In - so there is nothing to poke by hand. Returns OutBox's value, or
    None on timeout."""
    view = "/".join(runner.split("/")[:-1])
    inbox = view + "/InBox"
    outbox = view + "/OutBox"
    raw.value_of(outbox, "Value")  # arm the subscription before triggering
    raw.send({"cmd": "set-property", "instance": inbox, "prop": "Value", "value": in_value})
    ev = raw.wait_event(lambda e: e.get("event") == "property-changed"
                        and e.get("instance") == outbox and e.get("port") == "Value",
                        timeout=timeout)
    return ev.get("value") if ev else None


def find_widget_parts(raw, view):
    """(InBox, OutBox, Runner) instance paths inside `view`, or (None,None,None)."""
    mem = members(raw, view)
    inbox = next((m for m, c in mem if c == "Textbox" and m.endswith("/InBox")), None)
    outbox = next((m for m, c in mem if c == "Textbox" and m.endswith("/OutBox")), None)
    runner = next((m for m, c in mem if c == "ScriptBox"), None)
    return inbox, outbox, runner


def build_scripted_view(raw, home, name):
    """A View holding InBox, OutBox, and a Runner whose Source is set and
    activated - the minimal self-contained scripted composite widget."""
    raw.send({"cmd": "create-instance", "class": "View", "as": home + "/" + name,
              "container": home, "x": "20", "y": "20"})
    ev = raw.wait_event(lambda e: e.get("event") == "instance-created"
                        and e.get("class") == "View" and e.get("container") == home)
    view = ev.get("instance") if ev else None
    members(raw, view)  # start viewing it, or its own members never announce

    raw.send({"cmd": "create-instance", "class": "Textbox", "as": view + "/InBox",
              "container": view, "x": "10", "y": "10"})
    raw.wait_event(lambda e: e.get("event") == "instance-created"
                  and e.get("instance") == view + "/InBox")

    raw.send({"cmd": "create-instance", "class": "Textbox", "as": view + "/OutBox",
              "container": view, "x": "10", "y": "70"})
    raw.wait_event(lambda e: e.get("event") == "instance-created"
                  and e.get("instance") == view + "/OutBox")

    raw.send({"cmd": "create-instance", "class": "ScriptBox", "as": view + "/Runner",
              "container": view, "x": "10", "y": "140"})
    ev = raw.wait_event(lambda e: e.get("event") == "instance-created"
                        and e.get("instance") == view + "/Runner")
    runner = ev.get("instance") if ev else (view + "/Runner")

    raw.send({"cmd": "set-property", "instance": runner, "prop": "Language", "value": "Lua"})
    raw.send({"cmd": "set-property", "instance": runner, "prop": "Source", "value": RUNNER_SOURCE})
    raw.send({"cmd": "connect", "from": view + "/InBox", "fromPort": "Value",
              "to": runner, "toPort": "In"})
    raw.send({"cmd": "connect", "from": runner, "fromPort": "Out",
              "to": view + "/OutBox", "toPort": "Value"})
    time.sleep(0.3)

    press_run(raw, runner)
    return view, view + "/InBox", view + "/OutBox", runner


def test_build_and_run(raw, r, home):
    """Baseline sanity - the widget works freshly built, before any clone/
    export/save touches it at all."""
    view, inbox, outbox, runner = build_scripted_view(raw, home, "SW_Base")
    out = run_widget(raw, runner, "hello")
    r.expect("scripted widget: builds and runs",
             "OutBox becomes InBox's value + '_done'",
             "OutBox=%r" % out,
             out == "hello_done")


def test_clone(raw, r, home):
    """Clone the whole widget - Runner must survive the clone-walk (not
    _Hidden - see design_alias_panel_model/project_first_scripted_widget),
    the copied wires must resolve to the CLONE's own InBox/OutBox, and
    the clone must be independent of the original."""
    view, inbox, outbox, runner = build_scripted_view(raw, home, "SW_Clone")

    raw.events = []
    raw.send({"cmd": "clone-instance", "of": view, "container": home, "x": "260", "y": "20"})
    ev = raw.wait_event(lambda e: e.get("event") == "instance-created"
                        and e.get("class") == "View" and e.get("container") == home
                        and e.get("instance") != view)
    clone = ev.get("instance") if ev else None
    time.sleep(0.4)

    cinbox, coutbox, crunner = find_widget_parts(raw, clone) if clone else (None, None, None)
    r.expect("scripted widget clone: Runner survived the clone",
             "the clone holds InBox, OutBox, and a ScriptBox Runner",
             "clone=%s inbox=%s outbox=%s runner=%s" % (clone, cinbox, coutbox, crunner),
             bool(clone) and bool(cinbox) and bool(coutbox) and bool(crunner))

    if crunner:
        press_run(raw, crunner)
    out = run_widget(raw, crunner, "cloneval") if crunner else None
    r.expect("scripted widget clone: the copy's wiring resolves to the CLONE's own controls",
             "the clone's OutBox becomes the clone's InBox value + '_done'",
             "OutBox=%r" % out,
             out == "cloneval_done")

    # independence: running the clone must not touch the original's OutBox
    orig_before = raw.value_of(outbox, "Value")
    if crunner:
        run_widget(raw, crunner, "again")
    orig_after = raw.value_of(outbox, "Value")
    r.expect("scripted widget clone: running the clone leaves the original untouched",
             "original OutBox unchanged (%r)" % orig_before,
             "before=%r after=%r" % (orig_before, orig_after),
             orig_before == orig_after)


def test_alias(raw, r, home):
    """Alias the widget's OutBox.Value - the alias must track whatever the
    script last wrote, same as aliasing any other computed property."""
    view, inbox, outbox, runner = build_scripted_view(raw, home, "SW_Alias")

    raw.send({"cmd": "create-alias", "of": outbox, "prop": "Value",
              "container": home, "x": "20", "y": "260"})
    ev = raw.wait_event(lambda e: e.get("event") == "instance-created"
                        and e.get("class") == "Alias" and e.get("container") == home)
    al = ev.get("instance") if ev else None

    run_widget(raw, runner, "aliasme")
    tgt = raw.value_of(al, "Target") if al else None

    # subscribing THROUGH the alias correctly resolves to the original -
    # reads/writes/subscriptions through an alias all land on the original
    # (same rule Connect() follows) - so the property-changed event reports
    # the ORIGINAL's own instance path, not the alias's. Match on that,
    # not on the alias's path (which never arrives, by design, not a bug).
    val = None
    if al:
        raw.send({"cmd": "subscribe", "instance": al, "port": "Value"})
        ev = raw.wait_event(lambda e: e.get("event") == "property-changed"
                            and e.get("port") == "Value"
                            and e.get("instance") in (al, outbox), timeout=5)
        val = ev.get("value") if ev else None
    r.expect("scripted widget alias: aliases the script's own output",
             "alias targets OutBox (%s) and reads its script-written value" % outbox,
             "alias=%s Target=%s Value=%r" % (al, tgt, val),
             bool(al) and tgt == outbox and val == "aliasme_done")


def test_export_import(raw, r, home):
    """Export the widget's View alone, import it back - Source and the
    wiring must still resolve after the round trip through disk and a
    fresh clone-drop name (design_export_relative_import_drop: relative links, clone-drop)."""
    view, inbox, outbox, runner = build_scripted_view(raw, home, "SW_EI")

    raw.send({"cmd": "export-flow", "file": "sw_eitwin", "of": view})
    raw.wait_event(lambda e: e.get("event") == "flow-saved", timeout=6)
    wait_file(newest_flow("saved/sw_eitwin"))

    raw.events = []
    raw.send({"cmd": "import-flow", "file": "sw_eitwin", "into": home})
    raw.wait_event(lambda e: e.get("event") == "flow-loaded", timeout=8)
    time.sleep(0.4)

    # ask what is in home now, rather than waiting to be told: the import
    # drops the copy straight in, so listing is what sees it (the same
    # thing the client does when flow-loaded lands)
    fresh = [m for m, c in members(raw, home) if c == "View" and m != view]
    copy, cinbox, coutbox, crunner = None, None, None, None
    for v in fresh:
        ib, ob, rn = find_widget_parts(raw, v)
        if ib and ob and rn:
            copy, cinbox, coutbox, crunner = v, ib, ob, rn
            break

    r.expect("scripted widget import: the widget's shape came along",
             "the imported view holds InBox, OutBox, and a Runner",
             "copy=%s inbox=%s outbox=%s runner=%s" % (copy, cinbox, coutbox, crunner),
             bool(copy) and bool(cinbox) and bool(coutbox) and bool(crunner))

    if crunner:
        press_run(raw, crunner)
    out = run_widget(raw, crunner, "importval") if crunner else None
    r.expect("scripted widget import: Source and wiring survive export/import",
             "the imported OutBox becomes the imported InBox value + '_done'",
             "OutBox=%r" % out,
             out == "importval_done")


def test_save_load(raw, r, home):
    """Save the whole session, load it back - Source must survive, and one
    press of Run must bring the loaded widget back to life wired to the
    LOADED copy's own controls. The load itself deliberately does NOT run
    the script (no action on restore); Run is the user's gesture."""
    view, inbox, outbox, runner = build_scripted_view(raw, home, "SW_SaveLoad")

    raw.send({"cmd": "save-flow", "file": "sw_savetwin"})
    raw.wait_event(lambda e: e.get("event") == "flow-saved", timeout=6)

    raw.events = []
    raw.send({"cmd": "load-flow", "file": "sw_savetwin"})
    raw.wait_event(lambda e: e.get("event") == "flow-loaded", timeout=10)
    # the load destroyed everything this connection had open - come back
    # fresh or it cannot see what was just loaded
    raw.reconnect()

    # load restores IN PLACE: the container's contents are destroyed and
    # rebuilt from the file under their recorded names, so the widget's
    # view comes back at exactly the path it was saved from.
    copy, cinbox, coutbox, crunner, out = None, None, None, None, None
    deadline = time.time() + 8.0
    while time.time() < deadline and out != "loadval_done":
        ib, ob, rn = find_widget_parts(raw, view)
        if ib and ob and rn:
            press_run(raw, rn)
            out = run_widget(raw, rn, "loadval")
            copy, cinbox, coutbox, crunner = view, ib, ob, rn
        if out != "loadval_done":
            time.sleep(0.4)

    r.expect("scripted widget save/load: Source survived and the widget still runs",
             "the loaded Runner, Run pressed once, produces 'loadval_done'",
             "copy=%s runner=%s OutBox=%r" % (copy, crunner, out),
             out == "loadval_done")


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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8091)
    ap.add_argument("--webport", type=int, default=8083)
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="print every check, not just the failures")
    args = ap.parse_args()

    ensure_raw_bridge(args.host, args.port, args.webport)

    r = Report("scripted widget", args.verbose)
    raw = Raw(args.host, args.port)
    raw.send({"cmd": "list-instances"})
    raw.wait_event(lambda e: e.get("event") == "instances-done", timeout=8)

    def guarded(fn, *a):
        try:
            return fn(*a)
        except Exception as e:
            r.expect(fn.__name__, "no exception", "%s: %s" % (type(e).__name__, e), False)

    home = suite_view(raw, "ScriptedWidgetTests")

    for fn in (test_build_and_run, test_clone, test_alias, test_export_import, test_save_load):
        before = [m for m, _ in container_children(raw, home)]
        guarded(fn, raw, r, home)
        close_new_children(raw, home, before)

    raw.close()
    sys.exit(min(r.summary(), 254))	# the return code IS the failure count


if __name__ == "__main__":
    main()
