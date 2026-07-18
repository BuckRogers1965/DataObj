#!/usr/bin/env python3
"""
The scripted composite widget: a View that IS a widget - standard
Enable/Activate/In/Out ports, controls laid out inside it, and a script
that PUPPETS the controls. It behaves like a compiled widget, but its
logic is editable script.

    external --> [View.In]==>[Slider] --(script watches)--> sets [Output]
                                                              |
                              [View.Out]<==[Output.Value] <---+

 - View.In is bound (container port) to an inner Input control's In.
 - View.Out is bound to an inner Output control's Value.
 - A JSScript (wired to a Bridge, so it speaks the protocol) subscribes
   to the Input control's Value and drives the Output control's Value.
   THAT is the widget's behavior - edit the script, change the widget.

This proves Phase 5 (composite) + Phase 7 (script behavior) together,
and it only works because the script is a bridge client that can address
its sibling controls by path (Phase 1.5 addressing).

    python3 testharness/widgettest.py --host 127.0.0.1 --port 8091
"""
import argparse, sys, time
from rawtest import Raw, Report, ensure_raw_bridge, suite_view


def make(raw, cls, alias, container, x, y, hidden=False):
    cmd = {"cmd": "create-instance", "class": cls, "as": alias,
           "container": container, "x": str(x), "y": str(y)}
    if hidden:
        cmd["hidden"] = "1"
    raw.send(cmd)
    return raw.wait_event(lambda e: e.get("event") == "instance-created"
                          and e.get("instance") == alias, timeout=4)


def build_widget(raw, home):
    """Assemble the scripted widget entirely over the protocol - exactly
    what a GUI composition or a saved flow would replay."""
    view = home + "/Doubler"
    make(raw, "View", view, home, 40, 40)
    raw.send({"cmd": "list-instances", "container": view})
    raw.wait_event(lambda e: e.get("event") == "instances-done", timeout=4)

    slider = view + "/Input"
    out = view + "/Output"
    js = view + "/Logic"
    br = view + "/Wire"
    make(raw, "Slider", slider, view, 20, 20)
    make(raw, "Textbox", out, view, 20, 80)
    make(raw, "JSScript", js, view, 20, 140)
    make(raw, "Bridge", br, view, 20, 200, hidden=True)

    # the script is a protocol client: Cmd -> Bridge.In, Bridge.Out -> Evt
    raw.send({"cmd": "connect", "from": js, "fromPort": "Cmd", "to": br, "toPort": "In"})
    raw.send({"cmd": "connect", "from": br, "fromPort": "Out", "to": js, "toPort": "Evt"})
    raw.send({"cmd": "activate", "instance": br})

    # CONTAINER PORTS: the View's own In/Out become the widget's interface
    raw.send({"cmd": "bind-port", "container": view, "port": "In",
              "target": slider, "targetProp": "In"})
    raw.send({"cmd": "bind-port", "container": view, "port": "Out",
              "target": out, "targetProp": "Value"})

    # the widget's LOGIC, in editable script: watch the input, double it,
    # drive the output. The script addresses its siblings by path.
    src = (
        "var IN = '%s';\n"
        "var OUT = '%s';\n"
        "onevent(function(txt) {\n"
        "  try {\n"
        "    var e = JSON.parse(txt);\n"
        "    if (e.event === 'property-changed'\n"
        "        && e.instance === IN && e.port === 'Value') {\n"
        "      var v = parseInt(e.value, 10) || 0;\n"
        "      cmd({cmd: 'set-property', instance: OUT,\n"
        "           prop: 'Value', value: String(v * 2)});\n"
        "    }\n"
        "  } catch (x) {}\n"
        "});\n"
        "cmd({cmd: 'subscribe', instance: IN, port: 'Value'});\n"
    ) % (slider, out)
    raw.send({"cmd": "set-property", "instance": js, "prop": "Source", "value": src})
    raw.send({"cmd": "activate", "instance": js})
    time.sleep(0.6)
    return view, slider, out, js


def test_puppet(raw, r, home):
    view, slider, out, js = build_widget(raw, home)

    # drive the INPUT control directly and watch the script puppet the OUTPUT
    raw.send({"cmd": "set-property", "instance": slider, "prop": "Value", "value": "21"})
    time.sleep(0.6)
    got = raw.value_of(out, "Value")
    r.expect("scripted widget: the script puppets the output from the input",
             "setting Input=21 makes the script drive Output=42 (doubled)",
             "Output=%s" % got,
             got == "42")

    # do it again with a different value - it's live logic, not a one-shot
    raw.send({"cmd": "set-property", "instance": slider, "prop": "Value", "value": "5"})
    time.sleep(0.6)
    got2 = raw.value_of(out, "Value")
    r.expect("scripted widget: the puppet logic runs live on every change",
             "Input=5 -> Output=10",
             "Output=%s" % got2,
             got2 == "10")
    return view, slider, out


def test_container_ports(raw, r, home, view, slider, out):
    """The View's own In/Out are the widget's interface: driving View.In
    reaches the inner Input, and the inner Output reaches View.Out - the
    whole thing wires up as a black box from outside."""
    # an external source into View.In, an external sink off View.Out
    src = home + "/Feed"
    sink = home + "/Catch"
    make(raw, "Slider", src, home, 300, 40)
    make(raw, "Textbox", sink, home, 300, 100)
    raw.send({"cmd": "connect", "from": src, "fromPort": "Value", "to": view, "toPort": "In"})
    raw.send({"cmd": "connect", "from": view, "fromPort": "Out", "to": sink, "toPort": "In"})
    time.sleep(0.4)

    # drive the EXTERNAL feed; it should flow In -> Input -> script -> Output -> Out -> sink
    raw.send({"cmd": "set-property", "instance": src, "prop": "Value", "value": "8"})
    time.sleep(0.8)
    in_val = raw.value_of(slider, "Value")
    sink_val = raw.value_of(sink, "Value")
    r.expect("scripted widget: View.In forwards to the inner Input (container port)",
             "feeding View.In=8 drives the inner Input control to 8",
             "Input=%s" % in_val,
             in_val == "8")
    r.expect("scripted widget: the inner Output reaches View.Out to the outside",
             "the doubled result (16) flows out View.Out to the external sink",
             "sink=%s" % sink_val,
             sink_val == "16")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8091)
    ap.add_argument("--webport", type=int, default=8083)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    ensure_raw_bridge(args.host, args.port, args.webport)

    raw = Raw(args.host, args.port)
    r = Report("scripted widget tests", verbose=args.verbose)

    home = suite_view(raw, "WidgetTest")

    view, slider, out = test_puppet(raw, r, home)
    test_container_ports(raw, r, home, view, slider, out)

    raw.close()
    sys.exit(1 if r.summary() else 0)


if __name__ == "__main__":
    main()
