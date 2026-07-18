#!/usr/bin/env python3
"""
GUI unit tests: drive the framework's web canvas through a real browser
and check clone / alias / move / open / close behavior.

A passing check prints NOTHING - only failures speak, saying what was
EXPECTED and what was actually OBSERVED, plus one summary line. Add -v
to watch every check go by.

Each test stages its artifacts in its own clearly-labelled View
(CloneTest, AliasTest, ...) with the panels laid out in a grid, never
stacked - and the server is left running afterwards, so after a run you
can open a browser on the same port and dissect exactly what every test
left behind.

Run through run.sh (kill server, make clean, make, fresh server, test),
or standalone against an already-running pair:

    python3 testharness/guitest.py --app http://127.0.0.1:8083 --cdp 9223
"""
import argparse, json, sys, time
from cdp import attach

APP = "http://127.0.0.1:8083"
CDP_PORT = 9223


# --------------------------------------------------------------------------
# expected-vs-observed reporting
# --------------------------------------------------------------------------

class Report:
    """A PASSING TEST PRINTS NOTHING. Only failures speak, and they say
    everything (what was expected, what was actually observed) - plus one
    summary line. There will eventually be hundreds of these: a green run
    must cost a line to read, not a screenful. Pass -v when a human wants
    to watch every check go by. (Same contract as rawtest.py's Report.)"""

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
# staging: one labelled view per test, panels on a grid, icons in a row
# --------------------------------------------------------------------------

_slot = [0]

def next_slot():
    """Non-overlapping panel positions: 3 columns of 320, rows of 265."""
    i = _slot[0]
    _slot[0] += 1
    return 240 + (i % 3) * 320, 60 + (i // 3) * 265


def make_test_view(t, label):
    """A View named for the test, icon in the top row, panel in its own
    grid slot, opened in this window so the test can drop things into it."""
    alias = '/Root/' + label
    i = _slot[0]
    px, py = next_slot()
    t.js("send({cmd:'create-instance',class:'View',as:'%s'})" % alias)
    t.js("send({cmd:'set-property',instance:'%s',prop:'X',value:'%d'})" % (alias, 250 + i * 115))
    t.js("send({cmd:'set-property',instance:'%s',prop:'Y',value:'8'})" % alias)
    t.js("send({cmd:'set-property',instance:'%s',prop:'PanelX',value:'%d'})" % (alias, px))
    t.js("send({cmd:'set-property',instance:'%s',prop:'PanelY',value:'%d'})" % (alias, py))
    t.wait_js("!!views['%s']" % alias, label + " view")
    t.js("panels['%s'].setOpen(true)" % alias)
    time.sleep(0.6)
    return alias


def inner_center(t, view, dx=0, dy=0):
    return t.js("(()=>{const v=views['%s'];const r=v.innerEl.getBoundingClientRect();"
                "return {x:r.left+r.width/2+(%d),y:r.top+r.height/2+(%d)};})()" % (view, dx, dy))


def members(t, view, pattern):
    return t.js("(Object.keys(instances).find(k=>k.startsWith('%s/%s')) || false)" % (view, pattern))


# --------------------------------------------------------------------------
# the tests
# --------------------------------------------------------------------------

def install_error_trap(t):
    t.js("window.__errs=window.__errs||[];"
         "window.addEventListener('error',(e)=>window.__errs.push(String(e.message)))")


def test_boot(t, r):
    t.call("Page.enable")
    t.call("Page.navigate", {"url": APP})
    time.sleep(2)
    t.wait_js("typeof instances !== 'undefined' && !!views['/Root/Palette']", "boot replay")
    time.sleep(1.5)
    install_error_trap(t)
    t.hook_events()

    palette_open = t.js("panels['/Root/Palette'].el.style.display !== 'none'")
    seeds = t.js("Object.keys(instances).filter(k=>k.startsWith('/Root/Palette/')).length")
    r.expect("boot: palette view opens with its seeds",
             "palette panel open (initial Open=1) and >5 class seeds fetched on open",
             "open=%s, seeds=%s" % (palette_open, seeds),
             palette_open and seeds and seeds > 5)

    root_only = t.js("Object.keys(instances).every(k=>{const c=propertyValues[k+'.Container'];"
                     "return c===undefined || c==='' || c==='/Root/Palette';})")
    r.expect("boot: only visible containers were loaded",
             "no instance from any container this window has not opened",
             "all known instances are root-level or palette members: %s" % root_only,
             root_only)


def test_clone(t, r):
    """Clone = engine snapshot, placed by pick-then-place - staged in CloneTest."""
    view = make_test_view(t, 'CloneTest')
    t.set_mode('Clone')
    src = t.center_of("instances['/Root/Palette/Slider']")
    tgt = inner_center(t, view, -25, -30)
    t.pick_place(src["x"], src["y"], tgt["x"], tgt["y"])
    clone = t.wait_js("(Object.keys(instances).find(k=>k.startsWith('%s/Slider_')) || false)" % view, "slider clone")
    time.sleep(0.8)
    inside = t.js("(()=>{const i=instances['%s'];const inn=i&&i.el.closest('.view-inner');"
                  "return inn&&inn.dataset.viewAlias;})()" % clone)
    r.expect("clone: palette Slider -> the CloneTest view",
             "a new independent Slider instance created where the second click landed, inside CloneTest",
             "created %s, rendered inside %s" % (clone, inside),
             clone and inside == view)

    t.js("send({cmd:'set-property',instance:'%s',prop:'Value',value:'42'})" % clone)
    time.sleep(0.6)
    t.set_mode('Clone')
    src = t.center_of("instances['%s']" % clone)
    tgt = inner_center(t, view, 25, 40)
    t.pick_place(src["x"], src["y"], tgt["x"], tgt["y"])
    clone2 = t.wait_js("(Object.keys(instances).find(k=>k.startsWith('%s/Slider_') && k!=='%s') || false)" % (view, clone),
                       "second clone")
    time.sleep(0.8)
    snap = t.js("propertyValues['%s.Value']" % clone2)
    t.js("send({cmd:'set-property',instance:'%s',prop:'Value',value:'99'})" % clone)
    time.sleep(0.8)
    vals = t.js("[propertyValues['%s.Value'], propertyValues['%s.Value']]" % (clone, clone2))
    r.expect("clone: data is a snapshot, then independent",
             "clone starts at the source's current value (42); changing the source (99) does not move the clone",
             "clone started at %s; after source->99 values are %s" % (snap, vals),
             snap == "42" and vals == ["99", "42"])
    return clone, clone2


def test_esc_cancels(t, r):
    t.set_mode('Clone')
    src = t.center_of("instances['/Root/Palette/LED']")
    t.click(src["x"], src["y"])
    carrying = t.js("!!gestureDrag")
    before = t.js("Object.keys(instances).length")
    t.key("Escape", 27)
    time.sleep(0.5)
    after = t.js("Object.keys(instances).length")
    dropped = t.js("!gestureDrag")
    r.expect("esc: cancels a carry",
             "first click picks up a ghost; Esc drops it and nothing is created",
             "carrying=%s, dropped=%s, instances %s -> %s" % (carrying, dropped, before, after),
             carrying and dropped and before == after)


def test_alias(t, r, slider):
    """Alias = a doorway - staged in AliasTest, pointing at CloneTest's slider."""
    view = make_test_view(t, 'AliasTest')
    t.set_mode('Alias')
    src = t.center_of("instances['%s']" % slider)
    tgt = inner_center(t, view)
    t.pick_place(src["x"], src["y"], tgt["x"], tgt["y"])
    alias = t.wait_js("(Object.keys(aliasAtoms).find(k=>aliasAtoms[k].target==='%s') || false)" % slider, "alias atom")
    time.sleep(0.8)
    rec = t.js("(()=>{const a=aliasAtoms['%s'];return a&&{target:a.target,prop:a.targetProp,live:!!a.control};})()" % alias)
    r.expect("alias: slider -> alias atom in AliasTest",
             "a real Alias instance bound to the slider's Value with a live control, living in AliasTest",
             "%s -> %s" % (alias, rec),
             rec and rec.get("target") == slider and rec.get("prop") == "Value" and rec.get("live")
             and alias.startswith(view + "/"))

    t.clear_events()
    t.js("send({cmd:'set-property',instance:'%s',prop:'Value',value:'63'})" % alias)
    time.sleep(0.8)
    evs = t.events("m.event==='property-changed' && m.value==='63'")
    r.expect("alias: writing through the alias writes the original",
             "one property-changed event, speaking the ORIGINAL's name (%s)" % slider,
             "%s" % evs,
             evs and len(evs) == 1 and evs[0].get("instance") == slider)
    return alias


def test_clone_of_alias(t, r, slider, alias):
    """Cloning an alias clones the THING - staged in CloneAliasTest."""
    view = make_test_view(t, 'CloneAliasTest')
    t.set_mode('Clone')
    src = t.center_of("instances['%s']" % alias)
    tgt = inner_center(t, view)
    t.pick_place(src["x"], src["y"], tgt["x"], tgt["y"])
    clone = t.wait_js("(Object.keys(instances).find(k=>k.startsWith('%s/Slider_')) || false)" % view, "clone via alias")
    time.sleep(0.8)
    cls = t.js("instances['%s'] && instances['%s'].className" % (clone, clone))
    snap = t.js("propertyValues['%s.Value']" % clone)
    r.expect("clone an alias: you get the THING, snapshotted",
             "a new Slider instance (not an Alias) in CloneAliasTest, carrying the original's current value (63)",
             "created %s, class=%s, Value=%s" % (clone, cls, snap),
             clone and cls == "Slider" and snap == "63")

    t.js("send({cmd:'set-property',instance:'%s',prop:'Value',value:'20'})" % slider)
    time.sleep(0.8)
    vals = t.js("[propertyValues['%s.Value'], propertyValues['%s.Value']]" % (slider, clone))
    r.expect("clone an alias: the snapshot is independent of the original",
             "changing the original (20) leaves the clone at its snapshot (63)",
             "original,clone = %s" % vals,
             vals == ["20", "63"])


def test_alias_of_clone(t, r, clone2, other_slider):
    """Aliasing a clone binds to the CLONE - staged in AliasCloneTest."""
    view = make_test_view(t, 'AliasCloneTest')
    t.set_mode('Alias')
    src = t.center_of("instances['%s']" % clone2)
    tgt = inner_center(t, view)
    t.pick_place(src["x"], src["y"], tgt["x"], tgt["y"])
    alias = t.wait_js("(Object.keys(aliasAtoms).find(k=>aliasAtoms[k].target==='%s') || false)" % clone2, "alias of clone")
    time.sleep(0.8)
    rec = t.js("(()=>{const a=aliasAtoms['%s'];return a&&{target:a.target,live:!!a.control};})()" % alias)
    r.expect("alias a clone: the alias binds to the clone itself",
             "an Alias instance in AliasCloneTest targeting %s (the clone), with a live control" % clone2,
             "%s -> %s" % (alias, rec),
             rec and rec.get("target") == clone2 and rec.get("live") and alias.startswith(view + "/"))

    before = t.js("propertyValues['%s.Value']" % other_slider)
    t.clear_events()
    t.js("send({cmd:'set-property',instance:'%s',prop:'Value',value:'71'})" % alias)
    time.sleep(0.8)
    evs = t.events("m.event==='property-changed' && m.value==='71'")
    after = t.js("propertyValues['%s.Value']" % other_slider)
    r.expect("alias a clone: writes reach the clone, never its source",
             "one event in the CLONE's name (%s); the instance it was cloned from stays at %s" % (clone2, before),
             "events=%s; clone-source value %s -> %s" % (evs, before, after),
             evs and len(evs) == 1 and evs[0].get("instance") == clone2 and after == before)


def test_options_internals(t, r):
    """Options: the whole dissection table - staged around OptionsTest's own slider."""
    view = make_test_view(t, 'OptionsTest')
    t.set_mode('Clone')
    src = t.center_of("instances['/Root/Palette/Slider']")
    tgt = inner_center(t, view)
    t.pick_place(src["x"], src["y"], tgt["x"], tgt["y"])
    thing = t.wait_js("(Object.keys(instances).find(k=>k.startsWith('%s/Slider_')) || false)" % view, "options subject")
    t.js("send({cmd:'set-property',instance:'%s',prop:'Value',value:'71'})" % thing)
    time.sleep(0.6)

    t.set_mode('Options')
    src = t.center_of("instances['%s']" % thing)
    t.click(src["x"], src["y"])
    panel_view = t.wait_js(
        "(Object.keys(panels).find(k=>/Panel_/.test(k) && panels[k].el.style.display!=='none') || false)",
        "internals panel")
    # the dissection table is TALL (one row per published property) -
    # park it in its own column on the far right, clear of the grid
    t.js("send({cmd:'set-property',instance:'%s',prop:'PanelX',value:'1080'})" % panel_view)
    t.js("send({cmd:'set-property',instance:'%s',prop:'PanelY',value:'60'})" % panel_view)
    time.sleep(1.5)  # members stream in on open

    raw = t.js("JSON.stringify(Object.keys(aliasAtoms).filter(k=>k.startsWith('%s/'))"
               ".map(k=>({alias:k,target:aliasAtoms[k].target,prop:aliasAtoms[k].targetProp})))" % panel_view)
    mlist = json.loads(raw) if raw else []
    all_bound = mlist and all(m["target"] == thing for m in mlist)
    props = sorted(m["prop"] for m in mlist)
    whole_frog = all(p in props for p in ("Value", "State", "X", "Y", "Container", "Deletable", "Enable", "In", "Name"))
    r.expect("options: click a thing, its ENTIRE internal state lays out",
             "a View opens with one Alias per published property - data, position, container, ports, "
             "everything, all bound to %s" % thing,
             "view %s opened with members %s (all bound: %s)" % (panel_view, props, all_bound),
             panel_view and whole_frog and all_bound)

    val_member = next((m["alias"] for m in mlist if m["prop"] == "Value"), None)
    t.clear_events()
    t.js("send({cmd:'set-property',instance:'%s',prop:'Value',value:'35'})" % val_member)
    time.sleep(0.8)
    evs = t.events("m.event==='property-changed' && m.value==='35'")
    now = t.js("propertyValues['%s.Value']" % thing)
    r.expect("options: the panel's controls connect to the object's data",
             "writing through the panel's Value control changes %s itself (one event, its name)" % thing,
             "events=%s, %s.Value=%s" % (evs, thing, now),
             evs and len(evs) == 1 and evs[0].get("instance") == thing and now == "35")

    before = t.js("Object.keys(panels).filter(k=>/Panel_/.test(k)).length")
    t.set_mode('Options')
    src = t.center_of("instances['%s']" % thing)
    t.click(src["x"], src["y"])
    time.sleep(1.0)
    after = t.js("Object.keys(panels).filter(k=>/Panel_/.test(k)).length")
    r.expect("options: every ask opens the same one view",
             "a second Options click reuses the existing internals view (no new panel)",
             "panel count %s -> %s" % (before, after),
             before == after)

    # the name is just one of the properties on the table - write it, the thing renames
    # (writes go through the member's doorway slot, "Value")
    name_member = next((m["alias"] for m in mlist if m["prop"] == "Name"), None)
    t.js("send({cmd:'set-property',instance:'%s',prop:'Value',value:'Fred'})" % name_member)
    time.sleep(1.0)
    fred = view + "/Fred"
    renamed = t.js("!!instances['%s']" % fred)
    old_gone = t.js("!instances['%s']" % thing)
    label = t.js("(()=>{const i=instances['%s'];const l=i&&i.el.querySelector('.widget-atom-label');"
                 "return l&&l.textContent;})()" % fred)
    val = t.js("propertyValues['%s.Value']" % fred)
    r.expect("options: the Name on the table renames the thing",
             "writing 'Fred' through the panel's Name control re-keys %s to %s, "
             "label follows, state (Value=35) rides along" % (thing, fred),
             "renamed=%s, old gone=%s, label=%s, Value=%s" % (renamed, old_gone, label, val),
             renamed and old_gone and label == "Fred" and val == "35")


def test_move(t, r):
    """Move: X/Y are ordinary writes; crossing into a view re-containers."""
    view = make_test_view(t, 'MoveTest')
    t.set_mode('Clone')
    src = t.center_of("instances['/Root/Palette/Slider']")
    t.pick_place(src["x"], src["y"], 100, 380)  # the free column under the palette
    # anchored pattern: the OptionsTest dissection view is named
    # /Root/Slider_1Panel_1 and must not match
    slider = t.wait_js("(Object.keys(instances).find(k=>/^\\/Root\\/Slider_\\d+$/.test(k)) || false)", "move subject")
    # let the birth X/Y land before dragging from wherever it painted
    t.wait_js("propertyValues['%s.X'] === '100'" % slider, "move subject placed")
    time.sleep(0.4)

    t.set_mode('Move')
    src = t.center_of("instances['%s']" % slider)
    t.press_drag(src["x"], src["y"], src["x"] + 80, src["y"] - 40)
    time.sleep(0.8)
    pos = t.js("[propertyValues['%s.X'], propertyValues['%s.Y']]" % (slider, slider))
    moved = pos and pos[0] and int(pos[0]) > 100
    r.expect("move: drag writes X/Y back as shared properties",
             "the slider's X/Y properties reflect the drop point (moved right of 100)",
             "X,Y = %s" % pos,
             bool(moved))

    src = t.center_of("instances['%s']" % slider)
    tgt = inner_center(t, view)
    t.press_drag(src["x"], src["y"], tgt["x"], tgt["y"])
    time.sleep(1.2)
    renamed = t.js("(Object.keys(instances).find(k=>k.startsWith('%s/Slider_')) || false)" % view)
    inside = renamed and t.js("(()=>{const i=instances['%s'];const inn=i&&i.el.closest('.view-inner');"
                              "return inn&&inn.dataset.viewAlias;})()" % renamed)
    r.expect("move: dropping into a view re-containers (and renames) the thing",
             "the slider now lives in MoveTest (Container change + rename), rendered inside its panel",
             "renamed to %s, rendered inside %s" % (renamed, inside),
             renamed and inside == view)


def test_open_close(t, r):
    """Open/close: the icon is permanent; the panel is a separate life at
    the root. The test view is cloned from the palette and RENAMED to
    OpenCloseTest - the label is just the Name property doing its job."""
    t.set_mode('Clone')
    # ours is the NEW View_N - the shared engine namespace means the raw
    # suites' leftovers are visible here too, so "any View_N" grabs theirs
    t.js("window.__preViews = Object.keys(views).join('|')")
    src = t.center_of("instances['/Root/Palette/View']")
    t.pick_place(src["x"], src["y"], 100, 500)  # the free column under the palette
    raw = t.wait_js("(Object.keys(views).find(k=>/^\\/Root\\/View_\\d+$/.test(k)"
                    " && window.__preViews.split('|').indexOf(k) < 0) || false)", "view clone")
    time.sleep(0.6)
    t.js("send({cmd:'set-property',instance:'%s',prop:'Name',value:'OpenCloseTest'})" % raw)
    view = "/Root/OpenCloseTest"
    t.wait_js("!!views['%s']" % view, "renamed view")
    px, py = next_slot()
    t.js("send({cmd:'set-property',instance:'%s',prop:'PanelX',value:'%d'})" % (view, px))
    t.js("send({cmd:'set-property',instance:'%s',prop:'PanelY',value:'%d'})" % (view, py))
    time.sleep(0.6)

    closed = t.js("panels['%s'].el.style.display === 'none'" % view)
    r.expect("open/close: a fresh view arrives closed (an icon)",
             "the cloned view renders as an icon only (Open defaults to 0)",
             "panel hidden: %s" % closed, closed)

    t.set_mode('Operate')
    icon_before = t.js("instances['%s'].el.getBoundingClientRect().left" % view)
    src = t.center_of("instances['%s']" % view)
    t.click(src["x"], src["y"])
    time.sleep(1.0)
    st = t.js("(()=>{const i=instances['%s'];const p=panels['%s'];"
              "return {open:p.el.style.display!=='none', parent:p.el.parentElement.id,"
              "iconLeft:i.el.getBoundingClientRect().left,"
              "iconShown:getComputedStyle(i.el.querySelector('.instance-icon')).display!=='none'};})()" % (view, view))
    r.expect("open: click the icon, the panel opens at the ROOT",
             "panel visible as a root-level peer; the icon did not move, change, or disappear",
             "%s (icon was at %s)" % (st, icon_before),
             st and st.get("open") and st.get("parent") == "canvas"
             and st.get("iconShown") and st.get("iconLeft") == icon_before)

    t.js("send({cmd:'set-property',instance:'%s',prop:'PanelX',value:'%d'})" % (view, px + 40))
    time.sleep(0.8)
    st = t.js("(()=>{const i=instances['%s'];const p=panels['%s'];"
              "return {panelLeft:p.el.style.left, iconLeft:i.el.getBoundingClientRect().left};})()" % (view, view))
    r.expect("open: the panel's position is its own, independent of the icon",
             "PanelX=%d moves the panel only; the icon stays where it was" % (px + 40),
             "%s (icon was at %s)" % (st, icon_before),
             st and st.get("panelLeft") == "%dpx" % (px + 40) and st.get("iconLeft") == icon_before)

    t.js("(()=>{const p=panels['%s'];p.el.querySelector('.node-collapse').click();})()" % view)
    time.sleep(0.6)
    st = t.js("(()=>{const i=instances['%s'];const p=panels['%s'];"
              "return {closed:p.el.style.display==='none',"
              "iconShown:getComputedStyle(i.el.querySelector('.instance-icon')).display!=='none'};})()" % (view, view))
    r.expect("close: the corner symbol closes the panel, the icon remains",
             "panel hidden again, icon still present in its place",
             "%s" % st,
             st and st.get("closed") and st.get("iconShown"))


def test_rename_then_manipulate(t, r):
    """A renamed thing is the same thing: its gestures keep working.
    Every gesture resolves the CURRENT name at the moment you make it -
    a handler that remembered the name its view was born with silently
    stopped writing anything the moment the view was renamed."""
    # its own view, in the clear column right of the grid and high enough
    # that the panel's BOTTOM-right resize handle is inside the window
    # (the real viewport is ~1400x811 - a panel at y=560 has its corner
    # off the bottom edge, where synthetic clicks hit nothing). Taking a
    # grid slot would also push every later test's panel down a row.
    view = '/Root/RenameTest'
    t.js("send({cmd:'create-instance',class:'View',as:'%s',container:'',x:'660',y:'8'})" % view)
    t.wait_js("!!views['%s']" % view, "rename test view")
    t.js("send({cmd:'set-property',instance:'%s',prop:'PanelX',value:'1130'})" % view)
    t.js("send({cmd:'set-property',instance:'%s',prop:'PanelY',value:'300'})" % view)
    t.js("panels['%s'].setOpen(true)" % view)
    time.sleep(0.8)

    t.js("send({cmd:'set-property',instance:'%s',prop:'Name',value:'RenamedLive'})" % view)
    renamed = '/Root/RenamedLive'
    t.wait_js("!!views['%s']" % renamed, "renamed view")
    time.sleep(0.4)

    def drag(el_js, dx, dy):
        p = t.js("(()=>{const e=%s;const r=e.getBoundingClientRect();"
                 "return {x:r.left+r.width/2,y:r.top+r.height/2,w:r.width};})()" % el_js)
        if not p or not p.get("w"):
            return False   # not rendered/visible - the drag would hit nothing
        t.mouse("mousePressed", p["x"], p["y"])
        for i in range(1, 6):
            t.mouse("mouseMoved", p["x"] + dx * i / 5, p["y"] + dy * i / 5)
            time.sleep(0.02)
        t.mouse("mouseReleased", p["x"] + dx, p["y"] + dy)
        time.sleep(0.6)
        return True

    t.clear_events()
    drag("views['%s'].resizeHandle" % renamed, 60, 40)
    sized = t.js("JSON.stringify((window.__evts||[]).filter(m=>m.event==='property-changed'"
                 " && m.instance==='%s' && (m.port==='W'||m.port==='H'))"
                 ".map(m=>m.port+'='+m.value))" % renamed)
    r.expect("rename: a renamed view still resizes",
             "dragging the corner writes W and H back under the NEW name",
             "%s" % sized,
             sized and len(json.loads(sized)) == 2)

    t.clear_events()
    drag("views['%s'].header" % renamed, 40, 25)
    moved = t.js("JSON.stringify((window.__evts||[]).filter(m=>m.event==='property-changed'"
                 " && m.instance==='%s' && (m.port==='PanelX'||m.port==='PanelY'))"
                 ".map(m=>m.port+'='+m.value))" % renamed)
    r.expect("rename: a renamed view's panel still drags",
             "dragging the titlebar writes PanelX and PanelY back under the NEW name",
             "%s" % moved,
             moved and len(json.loads(moved)) == 2)


def test_lazy_contents(t, r):
    """A closed view's contents are never sent - they stream in on open."""
    t.set_mode('Clone')
    src = t.center_of("instances['/Root/Palette/View']")
    # the free spot under the palette - computed from the palette panel's
    # real bounds (the engine sizes it to fit its seeds, so it grows as
    # classes join; a hardcoded y went stale the moment it did)
    free = t.js("(()=>{const r=panels['/Root/Palette'].el.getBoundingClientRect();"
                "return {x:100,y:Math.round(r.bottom)+40};})()")
    t.js("window.__preViews = Object.keys(views).join('|')")
    t.pick_place(src["x"], src["y"], free["x"], free["y"])
    raw = t.wait_js("(Object.keys(views).find(k=>/^\\/Root\\/View_\\d+$/.test(k)"
                    " && window.__preViews.split('|').indexOf(k) < 0) || false)", "lazy view clone")
    time.sleep(0.6)
    t.js("send({cmd:'set-property',instance:'%s',prop:'Name',value:'LazyTest'})" % raw)
    view = "/Root/LazyTest"
    t.wait_js("!!views['%s']" % view, "renamed lazy view")
    px, py = next_slot()
    t.js("send({cmd:'set-property',instance:'%s',prop:'PanelX',value:'%d'})" % (view, px))
    t.js("send({cmd:'set-property',instance:'%s',prop:'PanelY',value:'%d'})" % (view, py))
    t.js("panels['%s'].setOpen(true)" % view)
    time.sleep(0.8)

    # source first, target second: center_of may scroll the canvas to
    # reach the seed, which would leave earlier-computed coordinates stale
    src = t.center_of("instances['/Root/Palette/LED']")
    tgt = inner_center(t, view)
    t.pick_place(src["x"], src["y"], tgt["x"], tgt["y"])
    member = t.wait_js("(Object.keys(instances).find(k=>k.startsWith('%s/LED')) || false)" % view, "member")

    # a FRESH page must not know the closed view's contents until it opens it
    t.call("Page.navigate", {"url": APP})
    time.sleep(2.5)
    t.wait_js("typeof instances !== 'undefined' && !!views['%s']" % view, "reboot")
    time.sleep(1.5)
    install_error_trap(t)
    t.hook_events()   # the reload rebuilt handleEvent - re-hook, later tests count events
    known_before = t.js("!!instances['%s']" % member)
    t.set_mode('Operate')
    src = t.center_of("instances['%s']" % view)
    t.click(src["x"], src["y"])
    time.sleep(1.2)
    known_after = t.js("!!instances['%s']" % member)
    r.expect("lazy: the GUI only holds what it can see",
             "after a fresh page load the member of the closed view is unknown; opening the view streams it in",
             "known before open: %s, after open: %s" % (known_before, known_after),
             (not known_before) and known_after)


# --------------------------------------------------------------------------


def test_script_pulse(t, r, lang, snippet):
    """The script pulse generator as a real WIDGET: a ScriptBox (the
    script widget - dropdown, source box, output box) powered by `lang`,
    counting pulse edges and speaking out its Out like any coded widget.
    The raw language host never appears in any view - it lives INSIDE
    the ScriptBox as plumbing."""
    view = make_test_view(t, lang + 'PulseTest')
    box = view + '/Counter'
    pulse = view + '/P'
    sink = view + '/Shown'
    t.js("send({cmd:'create-instance',class:'ScriptBox',as:'%s',container:'%s',x:'20',y:'20'})" % (box, view))
    t.js("send({cmd:'create-instance',class:'Pulse',as:'%s',container:'%s',x:'20',y:'90'})" % (pulse, view))
    t.js("send({cmd:'create-instance',class:'Textbox',as:'%s',container:'%s',x:'20',y:'160'})" % (sink, view))
    t.wait_js("!!instances['%s']" % sink, "widget parts")
    time.sleep(0.5)

    t.js("send({cmd:'set-property',instance:'%s',prop:'Language',value:'%s'})" % (box, lang))
    time.sleep(0.3)
    t.js("send({cmd:'set-property',instance:'%s',prop:'Source',value:%s})" % (box, json.dumps(snippet)))
    t.js("send({cmd:'set-property',instance:'%s',prop:'Interval',value:'150'})" % pulse)
    t.js("send({cmd:'set-property',instance:'%s',prop:'Count',value:'3'})" % pulse)
    t.js("send({cmd:'connect',from:'%s',fromPort:'Out',to:'%s',toPort:'In'})" % (pulse, box))
    t.js("send({cmd:'connect',from:'%s',fromPort:'Out',to:'%s',toPort:'In'})" % (box, sink))
    t.js("send({cmd:'subscribe',instance:'%s',port:'Value'})" % sink)
    time.sleep(0.4)
    t.js("send({cmd:'activate',instance:'%s'})" % box)
    t.js("send({cmd:'activate',instance:'%s'})" % pulse)
    time.sleep(3.0)

    final = t.js("propertyValues['%s.Value']" % sink)
    r.expect("%s pulse widget: a ScriptBox counts pulses like a coded widget" % lang.lower(),
             "the %s-powered ScriptBox counts 3 rising edges; the wired Textbox shows 3" % lang,
             "Textbox shows: %s" % final,
             final == "3")


def test_lua_pulse(t, r):
    test_script_pulse(t, r, 'Lua',
        "local c = 0\n"
        "oninput(function(v, k)\n"
        "  if v == '1' then\n"
        "    c = c + 1\n"
        "    send(tostring(c))\n"
        "  end\n"
        "end)\n")


def test_js_pulse(t, r):
    test_script_pulse(t, r, 'JSScript',
        "var c = 0;\n"
        "oninput(function(v, k) {\n"
        "  if (v === '1') {\n"
        "    c = c + 1;\n"
        "    send(String(c));\n"
        "  }\n"
        "});\n")


def test_textbox_any_size(t, r):
    """The ONE Textbox control holds text of any size at a FIXED size: a
    ScriptBox's Source box opens at its declared 12x48 (Rows/Cols on the
    published entry), renders a multi-line script with its newlines
    (overflow scrolls inside the box - it never resizes by content), and
    commits an edited multi-line value intact - while an undeclared row
    (Name) stays one line. Raw twin: scriptboxtest.py already runs
    multi-line Source through the protocol; this proves presentation."""
    view = make_test_view(t, 'TextSizeTest')
    box = view + '/Box'
    t.js("send({cmd:'create-instance',class:'ScriptBox',as:'%s',container:'%s',x:'20',y:'20'})" % (box, view))
    t.wait_js("!!instances['%s']" % box, "scriptbox instance")

    # open the card's panel the way the Operate click does - rows are the
    # internals view's members, streamed in on open
    t.js("internalsAskMode['%s']='card'; send({cmd:'internals',instance:'%s'})" % (box, box))
    t.wait_js("!!(liveControls['%s.Source']||[]).length" % box, "Source control")
    time.sleep(1.0)  # let the member rows finish streaming

    # the box is big BEFORE any code is typed - the size the OBJECT declared
    empty = t.js("(()=>{const l=liveControls['%s.Source'];const el=l[l.length-1].el;"
                 "return {h:el.clientHeight,w:el.clientWidth};})()" % box)
    r.expect("textbox: the ScriptBox Source box opens at its declared size",
             "an empty Source control is already a code-sized area (>=100px tall, >=200px wide)",
             "empty Source control: %s" % empty,
             empty and empty["h"] >= 100 and empty["w"] >= 200)

    source = ("-- six lines, one long\n"
              "local total = 0\n"
              "for i = 1, 10 do\n"
              "    total = total + i * i  -- a deliberately long line that needs real width\n"
              "end\n"
              "print(total)")
    t.js("send({cmd:'set-property',instance:'%s',prop:'Source',value:%s})" % (box, json.dumps(source)))
    time.sleep(0.8)

    shown = t.js("(()=>{const l=liveControls['%s.Source'];const el=l[l.length-1].el;"
                 "return {val:el.value,h:el.clientHeight};})()" % box)
    r.expect("textbox: a multi-line value renders with its newlines",
             "the Source control shows the six-line script verbatim, newlines intact",
             "control value: %r" % (shown and shown["val"]),
             shown and shown["val"] == source)

    # the user edits multi-line code in the box; the commit carries it intact
    edited = "print('one')\nprint('two')\nprint('three')"
    t.js("(()=>{const l=liveControls['%s.Source'];const el=l[l.length-1].el;"
         "el.value=%s;el.dispatchEvent(new Event('change'));})()" % (box, json.dumps(edited)))
    time.sleep(0.8)
    stored = t.js("propertyValues['%s.Source']" % box)
    r.expect("textbox: an edited multi-line value commits intact",
             "typing three lines into the box stores all three lines on the instance",
             "stored Source: %r" % stored,
             stored == edited)

    # uniformity guard: the same widget on a one-line property stays small
    name_h = t.js("(()=>{const l=liveControls['%s.Name']||[];if(!l.length)return false;"
                  "return l[l.length-1].el.clientHeight;})()" % box)
    r.expect("textbox: a one-line row stays one line",
             "the Name control on the same panel is compact (<40px tall)",
             "Name control height: %s" % name_h,
             name_h and name_h < 40)


def test_options_on_panel_control(t, r):
    """A control INSIDE a panel is an ordinary instance - Options-clicking
    it opens the CONTROL'S OWN dissection panel (its Target/Label/X/Y laid
    out), which is how a panel's controls get repositioned to fit a
    graphics background. Regression guard: clone/alias/options work on
    controls anywhere, panels included - nothing is blocked."""
    view = make_test_view(t, 'CtlOptTest')
    thing = view + '/S'
    t.js("send({cmd:'create-instance',class:'Slider',as:'%s',container:'%s',x:'20',y:'20'})" % (thing, view))
    t.wait_js("!!instances['%s']" % thing, "slider")

    # the slider's own dissection panel, parked clear of the grid (opened
    # last, so it sits topmost where we click)
    t.js("send({cmd:'internals',instance:'%s'})" % thing)
    panel = t.wait_js("(Object.keys(internalsOwner).find(k=>internalsOwner[k]==='%s') || false)" % thing,
                      "slider internals")
    t.js("send({cmd:'set-property',instance:'%s',prop:'PanelX',value:'880'})" % panel)
    t.js("send({cmd:'set-property',instance:'%s',prop:'PanelY',value:'430'})" % panel)
    time.sleep(1.5)  # members stream in

    member = t.js("(Object.keys(aliasAtoms).find(k=>k.startsWith('%s/')"
                  " && aliasAtoms[k].targetProp==='Value') || false)" % panel)
    r.expect("panel-control options: the panel's Value control is a real instance",
             "the dissection panel holds an addressable Alias member for Value",
             "member: %s" % member, bool(member))

    t.set_mode('Options')
    src = t.center_of("instances['%s']" % member)
    t.click(src["x"], src["y"])
    member_panel = t.wait_js(
        "(Object.keys(internalsOwner).find(k=>internalsOwner[k]==='%s') || false)" % member,
        "the control's own internals")
    time.sleep(1.2)  # its members stream in

    shown = t.js("panels['%s'] && panels['%s'].el.style.display!=='none'" % (member_panel, member_panel))
    props = t.js("JSON.stringify(Object.keys(aliasAtoms).filter(k=>k.startsWith('%s/'))"
                 ".map(k=>aliasAtoms[k].targetProp))" % member_panel)
    plist = json.loads(props) if props else []
    r.expect("panel-control options: a control opens like anything else",
             "Options-clicking the Value control opens ITS own panel, laid out with "
             "the control's own state (Target and X among the members)",
             "panel %s shown=%s members=%s" % (member_panel, shown, sorted(plist)),
             member_panel and shown and "Target" in plist and "X" in plist)

    # the SAME members projected as a CARD's rows must not lose their
    # instance-hood (the reported bug: rows filtered the member away and an
    # Options click fell through to the card itself)
    pulse = view + '/P'
    t.js("send({cmd:'create-instance',class:'Pulse',as:'%s',container:'%s',x:'20',y:'90'})" % (pulse, view))
    t.wait_js("!!instances['%s']" % pulse, "pulse")
    t.js("internalsAskMode['%s']='card'; send({cmd:'internals',instance:'%s'})" % (pulse, pulse))
    card_view = t.wait_js("(Object.keys(internalsOwner).find(k=>internalsOwner[k]==='%s') || false)" % pulse,
                          "pulse card internals")
    t.js("panels['%s'].setOpen(true)" % pulse)
    time.sleep(1.5)  # rows stream in

    row_member = t.js("(Object.keys(aliasAtoms).find(k=>k.startsWith('%s/')"
                      " && aliasAtoms[k].targetProp==='Interval') || false)" % card_view)
    pos = t.js("(()=>{const p=panels['%s'];if(!p)return null;"
               "const rows=[...p.el.querySelectorAll('.prop-row')];"
               "const r=rows.find(x=>{const l=x.querySelector('label');return l&&l.textContent==='Interval';});"
               "if(!r)return null;r.scrollIntoView({block:'center'});"
               "const b=r.getBoundingClientRect();return {x:b.left+b.width/2,y:b.top+b.height/2};})()" % pulse)
    t.click(pos["x"], pos["y"])
    row_panel = t.wait_js(
        "(Object.keys(internalsOwner).find(k=>internalsOwner[k]==='%s') || false)" % row_member,
        "the row member's own internals")
    row_shown = t.js("panels['%s'] && panels['%s'].el.style.display!=='none'" % (row_panel, row_panel))
    r.expect("panel-control options: a card ROW is the member, not chrome",
             "Options-clicking the Interval row on a Pulse card opens the Interval "
             "member's own panel, same as its atom form would",
             "row member %s -> panel %s shown=%s" % (row_member, row_panel, row_shown),
             row_member and row_panel and row_shown)


def test_gesture_checkbox_counts(t, r):
    """The whole flow by hand gestures, no dots anywhere: a Checkbox
    Connect-clicked onto the ScriptBox ICON (landing a wire on an icon
    means its first in port - the same one-click gesture as any control,
    panel never opened), the icon clicked again to start a wire (its
    first out port) landing on a Textbox, then Operate-mode clicks on
    the checkbox drive the script, which counts rising edges; the count
    lands in the Textbox."""
    view = make_test_view(t, 'GestureCount')
    # roomy enough that the ScriptBox icon's edge dots sit INSIDE the
    # view (a clipped dot is unclickable - found the hard way)
    t.js("send({cmd:'set-property',instance:'%s',prop:'W',value:'460'})" % view)
    t.js("send({cmd:'set-property',instance:'%s',prop:'H',value:'320'})" % view)
    time.sleep(0.4)
    cb, box, sink = view + '/C', view + '/B', view + '/T'
    t.js("send({cmd:'create-instance',class:'Checkbox',as:'%s',container:'%s',x:'30',y:'20'})" % (cb, view))
    t.js("send({cmd:'create-instance',class:'ScriptBox',as:'%s',container:'%s',x:'120',y:'100'})" % (box, view))
    t.js("send({cmd:'create-instance',class:'Textbox',as:'%s',container:'%s',x:'30',y:'210'})" % (sink, view))
    t.wait_js("!!(instances['%s']&&instances['%s']&&instances['%s'])" % (cb, box, sink), "the three parts")
    time.sleep(0.5)

    # program the script widget over the protocol - typing is the textbox
    # test's business; this test is about the wires and the clicks
    t.js("send({cmd:'set-property',instance:'%s',prop:'Language',value:'JSScript'})" % box)
    time.sleep(0.3)
    t.js("send({cmd:'set-property',instance:'%s',prop:'Source',value:%s})" % (box, json.dumps(
        "var c = 0;\n"
        "oninput(function(v, k) {\n"
        "  if (v === '1') {\n"
        "    c = c + 1;\n"
        "    send(String(c));\n"
        "  }\n"
        "});\n")))
    t.js("send({cmd:'activate',instance:'%s'})" % box)
    time.sleep(0.5)

    t.set_mode('Connect')
    src = t.center_of("instances['%s']" % cb)
    t.click(src["x"], src["y"])                      # arms Checkbox.Value
    ic = t.center_of("instances['%s']" % box)
    t.click(ic["x"], ic["y"])                        # icon completes as sink -> In
    t.wait_js("wires.length>=1", "checkbox->scriptbox wire")
    t.click(ic["x"], ic["y"])                        # icon starts as source -> Out
    snk = t.center_of("instances['%s']" % sink)
    t.click(snk["x"], snk["y"])                      # -> connect to Textbox
    t.wait_js("wires.length>=2", "scriptbox->textbox wire")

    # count: 5 toggles = on,off,on,off,on = three rising edges
    t.set_mode('Operate')
    cbc = t.center_of("instances['%s']" % cb)
    for _ in range(5):
        t.click(cbc["x"], cbc["y"])
        time.sleep(0.35)
    time.sleep(1.0)

    val = t.js("propertyValues['%s.Value']" % sink)
    r.expect("gesture count: clicking the checkbox counts pulses",
             "five checkbox clicks are three rising edges; the wired Textbox shows 3",
             "Textbox shows: %s" % val, val == "3")


def test_connect_wires(t, r):
    """Presentation of connections (raw twin: connectiontest.py). The
    gesture sends ONE connect verb and draws nothing itself - the wire
    appears when the connected event comes back, in the VIEW's own layer
    (the reported bug drew it into the root). Re-entering Connect mode
    redraws existing wires from list-connections (the other reported
    bug: they never came back). The mid-wire x sends disconnect, and the
    disconnected event is the only remover."""
    view = make_test_view(t, 'WireTest')

    t.js("send({cmd:'create-instance',class:'Slider',container:'%s',x:'30',y:'25'})" % view)
    a = t.wait_js("(Object.keys(instances).find(k=>k.startsWith('%s/Slider_')) || false)" % view, "first slider")
    t.js("send({cmd:'create-instance',class:'Slider',container:'%s',x:'30',y:'95'})" % view)
    b = t.wait_js("(Object.keys(instances).find(k=>k.startsWith('%s/Slider_') && k!=='%s') || false)"
                  % (view, a), "second slider")
    time.sleep(0.6)
    key = "%s.Value>%s.Value" % (a, b)

    t.set_mode('Connect')
    time.sleep(0.4)
    pa = t.center_of("instances['%s']" % a)
    pb = t.center_of("instances['%s']" % b)
    t.click(pa["x"], pa["y"])
    time.sleep(0.2)
    t.click(pb["x"], pb["y"])

    drawn = t.wait_js("wires.some(w=>w.key==='%s')" % key, "wire drawn from the connected event")
    in_view_layer = t.js("(()=>{const w=wires.find(w=>w.key==='%s');"
                         "return !!(w && w.svg.classList.contains('view-wires')"
                         " && w.svg.parentElement===views['%s'].innerEl);})()" % (key, view))
    r.expect("connect: two clicks, one verb, wire drawn in the view's own layer",
             "the wire for %s renders in WireTest's inner svg, not the root overlay" % key,
             "drawn=%s inViewLayer=%s" % (drawn, in_view_layer),
             drawn and in_view_layer)

    t.set_mode('Operate')
    time.sleep(0.4)
    cleared = t.js("!wires.some(w=>w.key==='%s')" % key)
    t.set_mode('Connect')
    redrawn = t.wait_js("wires.some(w=>w.key==='%s')" % key, "wire redrawn on re-entering Connect")
    r.expect("connect: re-entering the mode shows existing wires (the reported bug)",
             "wires clear on leaving Connect mode and come back from list-connections on re-entry",
             "cleared=%s redrawn=%s" % (cleared, redrawn),
             cleared and redrawn)

    px = t.js("(()=>{const w=wires.find(w=>w.key==='%s');if(!w)return null;"
              "const r=w.x.getBoundingClientRect();"
              "return {x:r.left+r.width/2,y:r.top+r.height/2};})()" % key)
    t.click(px["x"], px["y"])
    removed = t.wait_js("!wires.some(w=>w.key==='%s')" % key, "wire removed by the disconnected event")
    r.expect("disconnect: the mid-wire x removes the wire via the event",
             "clicking the x sends disconnect; the disconnected event erases the line everywhere",
             "removed=%s" % removed,
             removed)

    t.set_mode('Operate')
    # lower the panel so it can't sit over anything a later test clicks
    t.js("panels['%s'].setOpen(false)" % view)


def post_mortem(t):
    """A timed-out wait usually means the page stopped keeping up with the
    session - dump what it saw so the failure explains itself."""
    print("  --- post-mortem ---")
    try:
        print("  page errors :", t.js("JSON.stringify(window.__errs || [])"))
        print("  last events :", t.js("JSON.stringify((window.__evts||[]).slice(-6))"))
        print("  gestureDrag :", t.js("gestureDrag && gestureDrag.kind"))
        print("  mode        :", t.js("currentMode"))
    except Exception as e2:
        print("  (post-mortem itself failed: %s)" % e2)
    print()


def main():
    global APP
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", default=APP)
    ap.add_argument("--cdp", type=int, default=CDP_PORT)
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="print every check, not just the failures")
    args = ap.parse_args()
    APP = args.app

    t = attach(args.cdp)
    r = Report("browser GUI", args.verbose)

    def guarded(name, fn, *deps_named):
        """One test crashing (a timed-out wait, anything) is ONE failure in
        the report - never the death of the whole run. Missing dependencies
        from an earlier failed test are reported as such, not as tracebacks."""
        missing = [d for d, v in deps_named if not v]
        if missing:
            r.expect(name, "the test runs (needs %s from earlier tests)" % ", ".join(d for d, _ in deps_named),
                     "skipped - earlier failure left no %s" % ", ".join(missing), False)
            return None
        try:
            return fn()
        except Exception as e:
            r.expect(name, "the test runs to completion", "aborted: %s" % e, False)
            post_mortem(t)
            return None

    guarded("boot", lambda: test_boot(t, r))
    pair = guarded("clone", lambda: test_clone(t, r))
    slider, clone2 = pair if pair else (None, None)
    guarded("esc", lambda: test_esc_cancels(t, r))
    alias = guarded("alias", lambda: test_alias(t, r, slider), ("slider", slider))
    guarded("clone-of-alias", lambda: test_clone_of_alias(t, r, slider, alias), ("slider", slider), ("alias", alias))
    guarded("alias-of-clone", lambda: test_alias_of_clone(t, r, clone2, slider), ("clone2", clone2), ("slider", slider))
    guarded("options", lambda: test_options_internals(t, r))
    guarded("move", lambda: test_move(t, r))
    guarded("open-close", lambda: test_open_close(t, r))
    guarded("rename-manipulate", lambda: test_rename_then_manipulate(t, r))
    guarded("lazy", lambda: test_lazy_contents(t, r))
    guarded("lua-pulse", lambda: test_lua_pulse(t, r))
    guarded("js-pulse", lambda: test_js_pulse(t, r))
    # keep this one LAST of the view-staging tests: its view takes a grid
    # slot, and inserting a slot ahead of lua-script shifted that test's
    # panel onto the palette icons it clicks
    guarded("connect-wires", lambda: test_connect_wires(t, r))
    guarded("textbox-any-size", lambda: test_textbox_any_size(t, r))
    guarded("options-on-panel-control", lambda: test_options_on_panel_control(t, r))
    guarded("gesture-checkbox-count", lambda: test_gesture_checkbox_counts(t, r))

    # last step, every time, pass or fail: put the shared session back in
    # Operate mode so whoever opens a browser next gets a usable canvas
    try:
        t.set_mode('Operate')
    except Exception:
        pass

    try:
        errs = t.js("window.__errs || []")
        r.expect("no page errors",
                 "the browser console saw no uncaught errors during the whole run",
                 "%s" % (errs if errs else "none"), not errs)
    except Exception as e:
        r.expect("no page errors", "the page is still answering at the end", "aborted: %s" % e, False)

    sys.exit(1 if r.summary() else 0)


if __name__ == "__main__":
    main()
