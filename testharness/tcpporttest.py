#!/usr/bin/env python3
"""
TCPPort, raw-protocol: the TCP instrument panel ported from VNOS. It is
a FRONT END - it holds no socket, it drives a contained TCP engine
instance - so what is proven here is that the panel's controls really
operate the network underneath, over a real socket.

 1. Listen brings the engine up on Port; a real client can connect.
 2. Bytes a client sends land in RxData (accumulating), light RxReady,
    count into BytesReady, and go out the widget's own Out.
 3. Send transmits TxData to the connected client; ClearOnSend empties
    the box; In + AutoSend does the same thing from a flow.
 4. The Standard Ports menu writes Port; the Auto options stay mutually
    exclusive; Close and Enable=0 are full stops.

    python3 testharness/tcpporttest.py --host 127.0.0.1 --port 8091
"""
import argparse, os, socket, ssl, sys, time
from rawtest import Raw, Report, ensure_raw_bridge, suite_view

PANEL_PORT = 8477      # a port of our own, clear of the framework's


def make(raw, cls, alias, home, x, y):
    raw.send({"cmd": "create-instance", "class": cls, "as": alias,
              "container": home, "x": str(x), "y": str(y)})
    return raw.wait_event(lambda e: e.get("event") == "instance-created"
                          and e.get("instance") == alias, timeout=4)


def press(raw, panel, command):
    """A command is an ordinary in port taking a 1 - exactly what the
    panel's MoButton writes, and what a Pulse or a script would."""
    raw.send({"cmd": "set-property", "instance": panel, "prop": command, "value": "1"})
    time.sleep(0.4)


def fresh(raw, panel, prop):
    """Read a CONTINUOUSLY-published property's present value. value_of
    consumes the OLDEST matching event, so with a backlog it returns
    stale history - purge first, and the value consumed is the fresh
    push the subscribe itself triggers (leaktest.py's read_counters
    learned this the same way)."""
    raw.pump()
    raw.events = [e for e in raw.events if e.get("event") != "property-changed"]
    return raw.value_of(panel, prop)


def port_is_open(port=PANEL_PORT):
    try:
        s = socket.create_connection(("127.0.0.1", port), timeout=2)
        s.close()
        return True
    except Exception:
        return False


def test_inert_until_activated(raw, r, home):
    """The reference sets up the widget when it is PLACED (VNOS
    inctActivatedTask), so it comes up live and its buttons work at once -
    gated only by Enable, never a separate "activate" step. With Auto Close
    the default it opens NO socket on its own. (Reported both ways: it must
    not self-listen, and Listen must work.)"""
    panel = home + "/Inert"
    make(raw, "TCPPort", panel, home, 20, 140)
    raw.send({"cmd": "set-property", "instance": panel, "prop": "Port", "value": str(PANEL_PORT + 1)})
    time.sleep(0.6)     # let the deferred build + auto-activate settle

    # it came up live, but on its OWN it did not open a socket
    state = fresh(raw, panel, "StreamState")
    r.expect("tcpport: comes up live but does not self-listen",
             "the freshly placed panel is IDLING with the port shut",
             "state=%s port open=%s" % (state, port_is_open(PANEL_PORT + 1)),
             state == "IDLING" and not port_is_open(PANEL_PORT + 1))

    # and because it is live, pressing Listen listens - no separate Activate
    press(raw, panel, "Listen")
    state = fresh(raw, panel, "StreamState")
    r.expect("tcpport: a live panel listens when Listen is pressed",
             "pressing Listen brings it to LISTEN and opens the port",
             "state=%s port open=%s" % (state, port_is_open(PANEL_PORT + 1)),
             state == "LISTEN" and port_is_open(PANEL_PORT + 1))


def test_auto_listen_unchecks(raw, r, home):
    """A port's write is DELIVERED to its handler, not stored behind it -
    a handler that only acts on 1 leaves the box stuck checked and the
    panel auto-listens forever. (Reported: auto-listened when Auto Listen
    was not set.)"""
    panel = home + "/Unset"
    make(raw, "TCPPort", panel, home, 140, 140)
    raw.send({"cmd": "set-property", "instance": panel, "prop": "AutoListen", "value": "1"})
    time.sleep(0.3)
    on = fresh(raw, panel, "AutoListen")
    raw.send({"cmd": "set-property", "instance": panel, "prop": "AutoListen", "value": "0"})
    time.sleep(0.3)
    off = fresh(raw, panel, "AutoListen")
    r.expect("tcpport: Auto Listen can be turned back off",
             "AutoListen reads 1 after checking and 0 after unchecking",
             "checked=%s unchecked=%s" % (on, off),
             on == "1" and off == "0")

    # and with it off, activating must NOT open a socket
    raw.send({"cmd": "set-property", "instance": panel, "prop": "Port", "value": str(PANEL_PORT + 2)})
    time.sleep(0.2)
    raw.send({"cmd": "activate", "instance": panel})
    time.sleep(0.6)
    state = fresh(raw, panel, "StreamState")
    r.expect("tcpport: Activate with Auto Listen off does not listen",
             "the panel comes up IDLING and its port stays shut",
             "state=%s port open=%s" % (state, port_is_open(PANEL_PORT + 2)),
             state == "IDLING" and not port_is_open(PANEL_PORT + 2))


def test_listen_and_receive(raw, r, home):
    panel = home + "/Panel"
    make(raw, "TCPPort", panel, home, 20, 20)
    raw.send({"cmd": "set-property", "instance": panel, "prop": "Port", "value": str(PANEL_PORT)})
    raw.send({"cmd": "subscribe", "instance": panel, "port": "Out"})
    time.sleep(0.3)

    # activation is what makes it live - only then does Listen do anything
    raw.send({"cmd": "activate", "instance": panel})
    time.sleep(0.4)
    press(raw, panel, "Listen")
    state = fresh(raw, panel, "StreamState")
    lit = fresh(raw, panel, "Listening")
    r.expect("tcpport: Listen brings the engine up",
             "StreamState is LISTEN and the Listening LED is lit",
             "state=%s listening=%s" % (state, lit),
             state == "LISTEN" and lit == "1")

    sock = None
    try:
        sock = socket.create_connection(("127.0.0.1", PANEL_PORT), timeout=4)
    except Exception as e:
        r.expect("tcpport: a real client can connect to the panel's port",
                 "a TCP connection to port %d is accepted" % PANEL_PORT,
                 "connect failed: %s" % e, False)
        return panel, None

    sock.sendall(b"hello panel")
    time.sleep(0.8)

    rx = fresh(raw, panel, "RxData")
    ready = fresh(raw, panel, "RxReady")
    count = fresh(raw, panel, "BytesReady")
    r.expect("tcpport: received bytes land in the Receive box",
             "RxData is 'hello panel', RxReady lit, BytesReady 11",
             "rx=%r ready=%s bytes=%s" % (rx, ready, count),
             rx == "hello panel" and ready == "1" and count == "11")

    state = fresh(raw, panel, "StreamState")
    conn = fresh(raw, panel, "Connected")
    r.expect("tcpport: traffic means connected",
             "StreamState is CONNECTED and the Connected LED is lit",
             "state=%s connected=%s" % (state, conn),
             state == "CONNECTED" and conn == "1")

    flowed = raw.wait_event(lambda e: e.get("event") == "message-flowed"
                            and e.get("instance") == panel
                            and e.get("port") == "Out", timeout=3)
    r.expect("tcpport: what arrives also goes out the widget's Out",
             "a message-flowed on Out carrying the received bytes "
             "(default output connection is Receive Data)",
             "event=%s" % (flowed and flowed.get("value")),
             bool(flowed) and flowed.get("value") == "hello panel")

    return panel, sock


def test_send(raw, r, panel, sock):
    if not sock:
        return
    # isolate the manual Send button: with Auto Send on, filling the box
    # would itself transmit (see test_autosend_from_flow), doubling this.
    raw.send({"cmd": "set-property", "instance": panel, "prop": "AutoSend", "value": "0"})
    time.sleep(0.2)
    raw.send({"cmd": "set-property", "instance": panel, "prop": "TxData", "value": "from the panel"})
    time.sleep(0.3)
    press(raw, panel, "Send")

    sock.settimeout(3)
    try:
        got = sock.recv(200)
    except Exception as e:
        got = b"<nothing: %s>" % str(e).encode()
    r.expect("tcpport: Send transmits the Transmit box",
             "the connected client receives 'from the panel'",
             "client got: %r" % got,
             got == b"from the panel")

    # ClearOnSend empties the box as it sends
    raw.send({"cmd": "set-property", "instance": panel, "prop": "ClearOnSend", "value": "1"})
    raw.send({"cmd": "set-property", "instance": panel, "prop": "TxData", "value": "second"})
    time.sleep(0.3)
    press(raw, panel, "Send")
    try:
        sock.recv(200)
    except Exception:
        pass
    left = fresh(raw, panel, "TxData")
    r.expect("tcpport: Clear On Send empties the box",
             "TxData is empty after a send with ClearOnSend set",
             "TxData=%r" % left, left == "")


def test_autosend_from_flow(raw, r, panel, sock):
    if not sock:
        return
    # "Default input connection is to Transmit Data" - and AutoSend
    # transmits it the moment the box changes (test_send turned it off)
    raw.send({"cmd": "set-property", "instance": panel, "prop": "AutoSend", "value": "1"})
    raw.send({"cmd": "set-property", "instance": panel, "prop": "ClearOnSend", "value": "0"})
    time.sleep(0.2)
    raw.send({"cmd": "set-property", "instance": panel, "prop": "In", "value": "through the flow"})
    time.sleep(0.6)

    sock.settimeout(3)
    try:
        got = sock.recv(200)
    except Exception as e:
        got = b"<nothing: %s>" % str(e).encode()
    tx = fresh(raw, panel, "TxData")
    r.expect("tcpport: In feeds Transmit Data and Auto Send sends it",
             "data arriving on In lands in TxData and reaches the client untouched",
             "TxData=%r client got=%r" % (tx, got),
             tx == "through the flow" and got == b"through the flow")


def test_accumulate_and_clear(raw, r, panel, sock):
    if not sock:
        return
    sock.sendall(b"AAA")
    time.sleep(0.5)
    sock.sendall(b"BBB")
    time.sleep(0.6)
    rx = fresh(raw, panel, "RxData")
    r.expect("tcpport: Accumulate Data appends rather than replacing",
             "two arrivals leave both in RxData, in order",
             "RxData=%r" % rx,
             rx is not None and rx.endswith("AAABBB"))

    press(raw, panel, "ClearRx")
    rx = fresh(raw, panel, "RxData")
    count = fresh(raw, panel, "BytesReady")
    r.expect("tcpport: Clear Rx empties the Receive box",
             "RxData empty and BytesReady 0",
             "RxData=%r bytes=%s" % (rx, count),
             rx == "" and count == "0")


def test_standard_ports(raw, r, home):
    panel = home + "/Menu"
    make(raw, "TCPPort", panel, home, 140, 20)
    items = fresh(raw, panel, "StandardPortList")
    raw.send({"cmd": "set-property", "instance": panel, "prop": "StandardPort", "value": "HTTPS"})
    time.sleep(0.4)
    port = fresh(raw, panel, "Port")
    r.expect("tcpport: the Standard Ports menu writes the Port box",
             "the six services are offered and picking HTTPS sets Port to 443",
             "list=%s port=%s" % (items, port),
             items == "FTP,TELNET,SMTP,HTTP,POP,HTTPS" and port == "443")


def test_auto_exclusive(raw, r, home):
    panel = home + "/Auto"
    make(raw, "TCPPort", panel, home, 260, 20)
    raw.send({"cmd": "set-property", "instance": panel, "prop": "AutoListen", "value": "1"})
    time.sleep(0.4)
    # The three are plain properties (a port can neither be read back nor
    # announce itself), so exclusivity is resolved where it is USED and
    # normalized on Activate: Listen beats Open beats Close.
    raw.send({"cmd": "activate", "instance": panel})
    time.sleep(0.6)
    vals = (fresh(raw, panel, "AutoOpen"), fresh(raw, panel, "AutoListen"),
            fresh(raw, panel, "AutoClose"))
    r.expect("tcpport: the three Auto options settle mutually exclusive",
             "with Auto Listen checked, activating normalizes the trio to (0,1,0)",
             "open/listen/close = %s" % (vals,),
             vals == ("0", "1", "0"))


def test_close_is_a_full_stop(raw, r, panel, sock):
    if not panel:
        return
    press(raw, panel, "Close")
    state = fresh(raw, panel, "StreamState")
    r.expect("tcpport: Close is a full stop",
             "StreamState returns to IDLING",
             "state=%s" % state, state == "IDLING")

    refused = False
    try:
        s2 = socket.create_connection(("127.0.0.1", PANEL_PORT), timeout=2)
        s2.close()
    except Exception:
        refused = True
    r.expect("tcpport: a closed panel is really off the port",
             "a new connection to port %d is refused" % PANEL_PORT,
             "refused=%s" % refused, refused)

    raw.send({"cmd": "activate", "instance": panel})
    time.sleep(0.4)
    raw.send({"cmd": "set-property", "instance": panel, "prop": "Enable", "value": "0"})
    time.sleep(0.5)
    state = fresh(raw, panel, "StreamState")
    dis = fresh(raw, panel, "Disabled")
    live = fresh(raw, panel, "State")
    r.expect("tcpport: unenabling DEACTIVATES the panel",
             "StreamState DISABLED, the Disabled LED lit, and the object's own "
             "State no longer Running (2) - it must be Activated again to listen",
             "stream=%s disabled=%s State=%s" % (state, dis, live),
             state == "DISABLED" and dis == "1" and live != "2")

    # and while disabled it stays deaf, however its buttons are pressed
    press(raw, panel, "Listen")
    r.expect("tcpport: a disabled panel cannot be made to listen",
             "pressing Listen while disabled opens nothing",
             "state=%s port open=%s" % (fresh(raw, panel, "StreamState"), port_is_open()),
             fresh(raw, panel, "StreamState") == "DISABLED" and not port_is_open())


def test_tls(raw, r, home):
    """SECURE mode, end to end with a real TLS client. The VNOS TCPObject
    was a secure cross-platform object (SSL_CTX from a PEM cert/key, an
    SSL session per peer, SSL_read/SSL_write); tcp.c carries that now and
    the panel drives it. Also proves the refusal: secure-on with no
    usable cert must NOT fall back to serving in the clear."""
    here = os.path.dirname(os.path.abspath(__file__))
    crt = os.path.join(here, "certs", "test.crt")
    key = os.path.join(here, "certs", "test.key")
    if not (os.path.exists(crt) and os.path.exists(key)):
        r.expect("tcpport: TLS", "a test certificate exists in testharness/certs",
                 "missing %s / %s - run the openssl req in the README" % (crt, key), False)
        return

    # 1) secure ON but no cert: must refuse to listen, not serve plaintext
    bad = home + "/NoCert"
    make(raw, "TCPPort", bad, home, 260, 140)
    raw.send({"cmd": "set-property", "instance": bad, "prop": "Port", "value": str(PANEL_PORT + 3)})
    raw.send({"cmd": "set-property", "instance": bad, "prop": "SslEnable", "value": "1"})
    time.sleep(0.3)
    raw.send({"cmd": "activate", "instance": bad})
    time.sleep(0.3)
    press(raw, bad, "Listen")
    r.expect("tcpport: secure with no certificate refuses to listen",
             "the port stays shut rather than quietly serving in the clear",
             "state=%s port open=%s" % (fresh(raw, bad, "StreamState"),
                                        port_is_open(PANEL_PORT + 3)),
             not port_is_open(PANEL_PORT + 3))

    # 2) a real TLS server, spoken to by a real TLS client
    panel = home + "/Secure"
    tls_port = PANEL_PORT + 4
    make(raw, "TCPPort", panel, home, 380, 140)
    raw.send({"cmd": "set-property", "instance": panel, "prop": "Port", "value": str(tls_port)})
    raw.send({"cmd": "set-property", "instance": panel, "prop": "SslCert", "value": crt})
    raw.send({"cmd": "set-property", "instance": panel, "prop": "SslKey", "value": key})
    raw.send({"cmd": "set-property", "instance": panel, "prop": "SslEnable", "value": "1"})
    raw.send({"cmd": "subscribe", "instance": panel, "port": "Out"})
    time.sleep(0.4)
    raw.send({"cmd": "activate", "instance": panel})
    time.sleep(0.4)
    press(raw, panel, "Listen")

    r.expect("tcpport: a secure panel reports TLS actually running",
             "StreamState LISTEN and the sslStatus LED lit (the engine's own readback)",
             "state=%s sslStatus=%s" % (fresh(raw, panel, "StreamState"),
                                        fresh(raw, panel, "SslStatus")),
             fresh(raw, panel, "StreamState") == "LISTEN"
             and fresh(raw, panel, "SslStatus") == "1")

    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    got, cipher, err = None, None, None
    try:
        raw_sock = socket.create_connection(("127.0.0.1", tls_port), timeout=5)
        tls = ctx.wrap_socket(raw_sock, server_hostname="localhost")
        cipher = tls.cipher()
        tls.sendall(b"secure hello")
        time.sleep(1.0)
        got = fresh(raw, panel, "RxData")
        tls.close()
    except Exception as e:
        err = e

    r.expect("tcpport: a real TLS client completes a handshake and is heard",
             "the client negotiates a cipher and 'secure hello' arrives in RxData",
             "cipher=%s RxData=%r err=%s" % (cipher, got, err),
             err is None and cipher is not None and got == "secure hello")

    # 3) plaintext to a TLS port must NOT be accepted as data
    plain = None
    try:
        p = socket.create_connection(("127.0.0.1", tls_port), timeout=3)
        p.sendall(b"plaintext")
        time.sleep(0.8)
        plain = fresh(raw, panel, "RxData")
        p.close()
    except Exception as e:
        plain = "connect failed: %s" % e
    r.expect("tcpport: plaintext is not accepted on a secure port",
             "a raw non-TLS client never gets its bytes into RxData",
             "RxData=%r" % plain,
             plain != "plaintext" and (plain is None or "plaintext" not in str(plain)))

    press(raw, panel, "Close")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8091)
    ap.add_argument("--webport", type=int, default=8083)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    ensure_raw_bridge(args.host, args.port, args.webport)

    raw = Raw(args.host, args.port)
    r = Report("tcpport tests", verbose=args.verbose)

    home = suite_view(raw, "TCPPortTest")

    test_inert_until_activated(raw, r, home)
    test_auto_listen_unchecks(raw, r, home)
    panel, sock = test_listen_and_receive(raw, r, home)
    test_send(raw, r, panel, sock)
    test_autosend_from_flow(raw, r, panel, sock)
    test_accumulate_and_clear(raw, r, panel, sock)
    test_standard_ports(raw, r, home)
    test_auto_exclusive(raw, r, home)
    if sock:
        sock.close()
        time.sleep(0.3)
    test_tls(raw, r, home)
    test_close_is_a_full_stop(raw, r, panel, sock)

    raw.close()
    sys.exit(1 if r.summary() else 0)


if __name__ == "__main__":
    main()
