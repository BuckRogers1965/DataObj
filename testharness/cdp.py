#!/usr/bin/env python3
"""
Minimal Chrome DevTools Protocol client - no external dependencies.

The test harness drives a REAL browser (headless chromium) pointed at the
framework's web GUI, and talks to it over CDP: evaluating JavaScript in the
page and dispatching genuine mouse/keyboard input. Nothing is simulated
inside the page - a drag here is the same event stream a human drag makes.
"""
import socket, base64, os, json, struct, time, urllib.request


class WS:
    """A bare-bones WebSocket client (client->server frames masked, as the
    RFC requires; server frames parsed unmasked)."""

    def __init__(self, url):
        rest = url[5:]  # strip ws://
        hostport, _, path = rest.partition('/')
        host, _, port = hostport.partition(':')
        self.s = socket.create_connection((host, int(port or 80)), timeout=15)
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall((
            "GET /%s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\n"
            "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n" % (path, hostport, key)).encode())
        r = b""
        while b"\r\n\r\n" not in r:
            r += self.s.recv(4096)
        self.buf = r.split(b"\r\n\r\n", 1)[1]

    def send(self, text):
        p = text.encode()
        m = os.urandom(4)
        if len(p) < 126:
            h = bytes([0x81, 0x80 | len(p)])
        elif len(p) < 65536:
            h = bytes([0x81, 0x80 | 126]) + struct.pack(">H", len(p))
        else:
            h = bytes([0x81, 0x80 | 127]) + struct.pack(">Q", len(p))
        self.s.sendall(h + m + bytes(b ^ m[i % 4] for i, b in enumerate(p)))

    def recv_frame(self, deadline):
        while True:
            while len(self.buf) >= 2:
                ln, off = self.buf[1] & 0x7f, 2
                if ln == 126:
                    if len(self.buf) < 4:
                        break
                    ln, off = struct.unpack(">H", self.buf[2:4])[0], 4
                elif ln == 127:
                    if len(self.buf) < 10:
                        break
                    ln, off = struct.unpack(">Q", self.buf[2:10])[0], 10
                if len(self.buf) < off + ln:
                    break
                payload, self.buf = self.buf[off:off + ln], self.buf[off + ln:]
                return payload
            if time.time() > deadline:
                return None
            self.s.settimeout(max(0.05, deadline - time.time()))
            try:
                d = self.s.recv(1 << 20)
            except socket.timeout:
                return None
            if not d:
                return None
            self.buf += d


class CDP:
    """One page's DevTools session."""

    def __init__(self, ws_url):
        self.ws = WS(ws_url)
        self.next_id = 1

    def call(self, method, params=None, timeout=15):
        mid = self.next_id
        self.next_id += 1
        self.ws.send(json.dumps({"id": mid, "method": method, "params": params or {}}))
        deadline = time.time() + timeout
        while True:
            f = self.ws.recv_frame(deadline)
            if f is None:
                raise RuntimeError("CDP timeout on " + method)
            msg = json.loads(f)
            if msg.get("id") == mid:
                if "error" in msg:
                    raise RuntimeError(method + ": " + str(msg["error"]))
                return msg.get("result", {})

    def js(self, expr, timeout=15):
        """Evaluate an expression in the page, return its JSON value."""
        r = self.call("Runtime.evaluate", {"expression": expr, "returnByValue": True}, timeout)
        return r.get("result", {}).get("value")

    def wait_js(self, expr, desc, timeout=20):
        """Poll an expression until truthy; the value is returned."""
        end = time.time() + timeout
        while time.time() < end:
            v = self.js(expr)
            if v:
                return v
            time.sleep(0.3)
        raise RuntimeError("timed out waiting for " + desc)

    # ---- real input ----------------------------------------------------

    def mouse(self, kind, x, y):
        self.call("Input.dispatchMouseEvent",
                  {"type": kind, "x": x, "y": y, "button": "left",
                   "buttons": 1 if kind != "mouseReleased" else 0,
                   "clickCount": 1 if kind in ("mousePressed", "mouseReleased") else 0})

    def click(self, x, y):
        self.mouse("mousePressed", x, y)
        self.mouse("mouseReleased", x, y)

    def key(self, name, code):
        self.call("Input.dispatchKeyEvent", {"type": "keyDown", "key": name, "code": name, "windowsVirtualKeyCode": code})
        self.call("Input.dispatchKeyEvent", {"type": "keyUp", "key": name, "code": name, "windowsVirtualKeyCode": code})

    def pick_place(self, x1, y1, x2, y2):
        """Clone/Alias gesture: click picks up the ghost, click again places."""
        self.click(x1, y1)
        for i in range(1, 6):
            self.mouse("mouseMoved", x1 + (x2 - x1) * i / 5, y1 + (y2 - y1) * i / 5)
            time.sleep(0.02)
        self.click(x2, y2)

    def press_drag(self, x1, y1, x2, y2):
        """Move-mode gesture: hold, drag, release."""
        self.mouse("mousePressed", x1, y1)
        for i in range(1, 8):
            self.mouse("mouseMoved", x1 + (x2 - x1) * i / 7, y1 + (y2 - y1) * i / 7)
            time.sleep(0.02)
        self.mouse("mouseReleased", x2, y2)

    # ---- GUI conveniences ----------------------------------------------

    def center_of(self, inst_expr):
        """Scroll an instance's element into view and return its screen center."""
        return self.js(
            "(()=>{const i=%s;if(!i)return null;i.el.scrollIntoView({block:'center'});"
            "const r=i.el.getBoundingClientRect();"
            "return {x:r.left+r.width/2,y:r.top+r.height/2};})()" % inst_expr)

    def set_mode(self, mode):
        """Switch the session mode through the real ModeMenu property."""
        self.js("send({cmd:'set-property',instance:'ModeMenu',prop:'Selected',value:'%s'})" % mode)
        self.wait_js("currentMode === '%s'" % mode, mode + " mode")

    def hook_events(self):
        """Record every bridge event the page receives into window.__evts."""
        self.js("(()=>{if(window.__hooked)return;window.__hooked=1;"
                "const oh=handleEvent; window.__evts=[];"
                "handleEvent=(m)=>{window.__evts.push(m); oh(m);};})()")

    def clear_events(self):
        self.js("window.__evts=[]")

    def events(self, js_predicate):
        return self.js("window.__evts.filter(m=>%s)" % js_predicate)


def attach(cdp_port, target_id=None):
    """Attach to the first page (or a specific target) of a chromium
    started with --remote-debugging-port."""
    targets = json.load(urllib.request.urlopen("http://127.0.0.1:%d/json" % cdp_port))
    if target_id:
        page = next(t for t in targets if t["type"] == "page" and t["id"] == target_id)
    else:
        page = next(t for t in targets if t["type"] == "page")
    return CDP(page["webSocketDebuggerUrl"])
