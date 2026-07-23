#!/usr/bin/env python3
"""
Base64, transport layering: the encode/decode instrument panel ported from VNOS.

The reported failure: the web GUI DISCONNECTS the moment you decode. Decoding
turns Base64 text back into arbitrary bytes (e.g. decode("aaaaaa") = 69 A6 9A 69),
and those bytes are almost never valid UTF-8. A WebSocket TEXT frame carrying an
invalid byte MUST fail the connection (RFC 6455), so a stray high byte in any
property value knocked the browser off.

The fix is at the GUI transport only (objects/websocket/websocket.c): the frame
sent to the browser is made valid UTF-8, invalid bytes shown as U+FFFD. The DATA
is untouched - the point of the widget is to produce real binary. So this suite
proves BOTH halves:

  * over the WebSocket (what the browser uses): a decode keeps the socket alive
    and every frame is valid UTF-8, with the bad bytes shown as U+FFFD.
  * over the raw TCP bridge (the data transport): the SAME decode still carries
    the exact original bytes - the fix changed the display, not the data.

    python3 testharness/base64test.py --host 127.0.0.1 --port 8091 --webport 8083
"""
import argparse, json, socket, sys, time
from rawtest import ensure_raw_bridge, Report
from cdp import WS

DECODE_IN = "aaaaaa"           # all-alphabet -> decodes to 69 A6 9A 69
RAW_MARK = b"\xa6"             # a byte from that decode that is invalid UTF-8
FFFD = b"\xef\xbf\xbd"         # U+FFFD, what the display sanitizer emits


def frames_until_quiet(ws, seconds=1.2):
    out, deadline = [], time.time() + seconds
    while True:
        f = ws.recv_frame(min(deadline, time.time() + 0.3))
        if f is None:
            if time.time() >= deadline:
                break
            continue
        out.append(f)
    return out


def find_value(frames, port):
    """Last property-changed value for a port, out of raw frame bytes
    (lenient decode - the strict UTF-8 check is done on the bytes themselves)."""
    val = None
    for f in frames:
        try:
            o = json.loads(f.decode(errors="replace"))
        except ValueError:
            continue
        if o.get("event") == "property-changed" and o.get("port") == port:
            val = o.get("value")
    return val


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8091)
    ap.add_argument("--webport", type=int, default=8083)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    ensure_raw_bridge(args.host, args.port, args.webport)
    r = Report("base64", args.verbose)

    # ---- the browser transport: decode must NOT drop the socket -----------
    ws = WS("ws://%s:%d/" % (args.host, args.webport))
    view = "/Root/Base64WsTests"
    panel = view + "/B64"
    for cmd in [
        {"cmd": "create-instance", "class": "View", "as": view, "container": "", "x": "40", "y": "8"},
        {"cmd": "list-instances", "container": view},
        {"cmd": "create-instance", "class": "Base64", "as": panel, "container": view, "x": "20", "y": "20"},
    ]:
        ws.send(json.dumps(cmd)); time.sleep(0.2)
    time.sleep(0.6)
    frames_until_quiet(ws, 0.5)                       # drain the build chatter

    ws.send(json.dumps({"cmd": "subscribe", "instance": panel, "prop": "Output"}))
    ws.send(json.dumps({"cmd": "set-property", "instance": panel, "prop": "Decode", "value": "1"}))
    ws.send(json.dumps({"cmd": "set-property", "instance": panel, "prop": "Input", "value": DECODE_IN}))
    frames = frames_until_quiet(ws, 1.2)

    bad = next((f for f in frames if _invalid_utf8(f)), None)
    r.expect("base64: every websocket frame after a decode is valid UTF-8",
             "no frame carries an invalid byte (the browser keeps the socket)",
             "clean" if bad is None else "invalid frame: %r" % bad[:80], bad is None)

    # socket still alive: a fresh command still gets answered
    ws.send(json.dumps({"cmd": "subscribe", "instance": panel, "prop": "Decode"}))
    alive = find_value(frames_until_quiet(ws, 1.0), "Decode") is not None
    r.expect("base64: the websocket survives the decode",
             "a command sent after the decode is still answered",
             "answered=%s" % alive, alive)

    disp = find_value(frames, "Output")
    shown = disp.encode(errors="replace") if disp is not None else b""
    r.expect("base64: the display shows the replacement char, not the raw byte",
             "Output on the wire holds U+FFFD and no invalid 0xA6",
             "value=%r" % shown, FFFD in shown and RAW_MARK not in shown)

    # ---- the data transport: the raw bridge keeps the exact bytes ---------
    s = socket.create_connection((args.host, args.port), timeout=5)
    s.settimeout(0.2)

    def rsend(obj):
        s.sendall(json.dumps(obj).encode()); time.sleep(0.15)

    def rdrain(win=1.0):
        buf, deadline = b"", time.time() + win
        while time.time() < deadline:
            try:
                d = s.recv(65536)
            except socket.timeout:
                continue
            if not d:
                break
            buf += d
        return buf

    rview = "/Root/Base64RawTests"
    rpanel = rview + "/B64"
    rsend({"cmd": "create-instance", "class": "View", "as": rview, "container": "", "x": "40", "y": "8"})
    rsend({"cmd": "list-instances", "container": rview})
    rsend({"cmd": "create-instance", "class": "Base64", "as": rpanel, "container": rview, "x": "20", "y": "20"})
    time.sleep(0.6); rdrain(0.4)
    rsend({"cmd": "subscribe", "instance": rpanel, "prop": "Output"})
    rsend({"cmd": "set-property", "instance": rpanel, "prop": "Decode", "value": "1"})
    rsend({"cmd": "set-property", "instance": rpanel, "prop": "Input", "value": DECODE_IN})
    raw = rdrain(1.2)
    s.close()

    r.expect("base64: the raw bridge still carries the exact decoded bytes",
             "the untouched data path delivers the real 0xA6 byte",
             "0xA6 present=%s" % (RAW_MARK in raw), RAW_MARK in raw)

    sys.exit(1 if r.summary() else 0)


def _invalid_utf8(b):
    try:
        b.decode("utf-8")
        return False
    except UnicodeDecodeError:
        return True


if __name__ == "__main__":
    main()
