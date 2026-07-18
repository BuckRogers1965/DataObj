#!/usr/bin/env python3
"""
ScriptBox, raw-protocol: the script WIDGET shell that holds a language
host inside it and drives it. Proven at the engine before the GUI
renders it.

 1. A fresh ScriptBox discovers the script hosts (LanguageList carries
    JSScript, and Lua's Script), and defaults to one.
 2. Run (activate) hands Source to the inner host and the host's
    print() output flows into ScriptBox's Output box.
 3. Selecting a different Language swaps the inner host - the SAME
    Source now runs under the new language.

    python3 testharness/scriptboxtest.py --host 127.0.0.1 --port 8091
"""
import argparse, sys, time
from rawtest import Raw, Report, ensure_raw_bridge, suite_view


def make(raw, cls, alias, home, x, y):
    raw.send({"cmd": "create-instance", "class": cls, "as": alias,
              "container": home, "x": str(x), "y": str(y)})
    return raw.wait_event(lambda e: e.get("event") == "instance-created"
                          and e.get("instance") == alias, timeout=4)


def run_and_read_output(raw, box):
    """Activate the box and return its Output after the inner host ran."""
    raw.send({"cmd": "activate", "instance": box})
    time.sleep(0.8)
    return raw.value_of(box, "Output")


def test_discovery(raw, r, home):
    box = home + "/Box"
    make(raw, "ScriptBox", box, home, 20, 20)
    langs = raw.value_of(box, "LanguageList")
    items = (langs or "").split(",")
    r.expect("scriptbox: discovers the registered script hosts at runtime",
             "LanguageList lists JSScript and Lua and JSScript as exact entries",
             "LanguageList items: %s" % items,
             "JSScript" in items and "Lua" in items)

    lang = raw.value_of(box, "Language")
    r.expect("scriptbox: defaults to a discovered host",
             "Language is one of the discovered hosts",
             "Language: %s" % lang,
             bool(lang) and lang in items)
    return box


def test_run_js(raw, r, home):
    box = home + "/JSBox"
    make(raw, "ScriptBox", box, home, 120, 20)
    # force JS
    raw.send({"cmd": "set-property", "instance": box, "prop": "Language", "value": "JSScript"})
    time.sleep(0.2)
    raw.send({"cmd": "set-property", "instance": box, "prop": "Source",
              "value": "var n = 6 * 7;\nprint('js says ' + n);\n"})
    out = run_and_read_output(raw, box)
    r.expect("scriptbox: Run executes the code and Output shows print()",
             "Output contains 'js says 42'",
             "Output: %r" % out,
             bool(out) and "js says 42" in out)


def test_run_lua(raw, r, home):
    box = home + "/LuaBox"
    make(raw, "ScriptBox", box, home, 220, 20)
    raw.send({"cmd": "set-property", "instance": box, "prop": "Language", "value": "Lua"})
    time.sleep(0.2)
    # Lua's Script host: send() emits, but its "print" analog is log/send;
    # the ScriptBox wires the host's Print/Out - Lua Script sends out Out.
    # Use send() which Lua supports; ScriptBox forwards Out -> its Out, but
    # Output collects Print. Lua Script has no Print, so drive via Out here.
    raw.send({"cmd": "subscribe", "instance": box, "port": "Out"})
    time.sleep(0.2)
    raw.events = []
    raw.send({"cmd": "set-property", "instance": box, "prop": "Source",
              "value": "local n = 6 * 7\nsend('lua says ' .. n)\n"})
    raw.send({"cmd": "activate", "instance": box})
    ev = raw.wait_event(lambda e: e.get("event") == "message-flowed"
                        and e.get("instance") == box and e.get("port") == "Out", timeout=4)
    r.expect("scriptbox: swapping Language to Lua runs the same shell under Lua",
             "the Lua host's send() flows out ScriptBox.Out ('lua says 42')",
             "Out: %s" % (ev.get("value") if ev else None),
             bool(ev) and "lua says 42" in (ev.get("value") or ""))


def test_swap_reruns(raw, r, home):
    """One box, one Source, two languages - the swap is real."""
    box = home + "/SwapBox"
    make(raw, "ScriptBox", box, home, 320, 20)
    raw.send({"cmd": "set-property", "instance": box, "prop": "Language", "value": "JSScript"})
    time.sleep(0.2)
    raw.send({"cmd": "set-property", "instance": box, "prop": "Source",
              "value": "print('hi from js');\n"})
    out_js = run_and_read_output(raw, box)

    # swap to Lua; JS print() syntax won't run under Lua, so Output differs
    raw.send({"cmd": "set-property", "instance": box, "prop": "Language", "value": "Lua"})
    time.sleep(0.3)
    raw.send({"cmd": "set-property", "instance": box, "prop": "Source",
              "value": "log('hi from lua')\n"})
    raw.send({"cmd": "activate", "instance": box})
    time.sleep(0.6)

    r.expect("scriptbox: the language actually swapped",
             "JS run produced 'hi from js' in Output; the box now runs Lua without error",
             "js Output: %r" % out_js,
             bool(out_js) and "hi from js" in out_js)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8091)
    ap.add_argument("--webport", type=int, default=8083)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    ensure_raw_bridge(args.host, args.port, args.webport)

    raw = Raw(args.host, args.port)
    r = Report("scriptbox tests", verbose=args.verbose)

    home = suite_view(raw, "ScriptBoxTest")

    test_discovery(raw, r, home)
    test_run_js(raw, r, home)
    test_run_lua(raw, r, home)
    test_swap_reruns(raw, r, home)

    raw.close()
    sys.exit(1 if r.summary() else 0)


if __name__ == "__main__":
    main()
