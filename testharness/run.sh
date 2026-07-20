#!/bin/bash
#
# GUI test harness runner: the whole ritual, from scratch, on the real
# default port - kill any running server, make clean, make, start a fresh
# server, drive it with a headless chromium, clean up.
#
#   ./testharness/run.sh            # default port 8083
#   ./testharness/run.sh -v         # verbose: every check prints, pass or fail
#   PORT=9090 ./testharness/run.sh  # somewhere else if you must
#
cd "$(dirname "$0")/.." || exit 1

# -v as an argument or VERBOSE=1 in the environment - either turns on
# every suite's full expected/observed output for passing tests too
for arg in "$@"; do
    [ "$arg" = "-v" ] && VERBOSE=1
done

PORT="${PORT:-8083}"
CDP_PORT="${CDP_PORT:-9223}"
LOGDIR="testharness/logs"
mkdir -p "$LOGDIR"

export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:."

CHROME="$(command -v chromium || command -v chromium-browser || command -v google-chrome)"
if [ -z "$CHROME" ]; then
    echo "no chromium/google-chrome found - the harness drives a real browser" >&2
    exit 1
fi

# the tests run against a freshly built server, always - and a fresh
# browser: a stale debug-port chromium would silently hijack the session
echo "stopping any running framework ..."
pkill -x framework 2>/dev/null
pkill -f "remote-debugging-port=$CDP_PORT" 2>/dev/null
sleep 1

echo "make clean && make ..."
make clean > "$LOGDIR/build.log" 2>&1
make >> "$LOGDIR/build.log" 2>&1
# the root Makefile does not propagate sub-make failures - a module that
# fails to compile just quietly never produces its .object; catch it here
if grep -qE '\*\*\*|Error [0-9]' "$LOGDIR/build.log"; then
    echo "build failed - see $LOGDIR/build.log" >&2
    grep -B4 -E '\*\*\*|Error [0-9]' "$LOGDIR/build.log" | tail -20 >&2
    exit 1
fi

# warnings are kept at zero (core and modules alike) - a new one is a
# regression, and the silent sub-makes would otherwise swallow it
if grep -q 'warning:' "$LOGDIR/build.log"; then
    echo "build has compiler warnings - the tree builds warning-free, keep it that way" >&2
    grep 'warning:' "$LOGDIR/build.log" | head -10 >&2
    exit 1
fi

# the server runs on its own defaults - 0.0.0.0:8083 - reachable from the
# whole LAN, so you can point YOUR browser at it and watch the tests drive
# the shared session live
./framework -ip 0.0.0.0 -port "$PORT" > "$LOGDIR/server.log" 2>&1 &
SERVER_PID=$!

"$CHROME" --headless=new --remote-debugging-port="$CDP_PORT" --window-size=1400,950 \
          --no-sandbox --disable-gpu about:blank > "$LOGDIR/chrome.log" 2>&1 &
CHROME_PID=$!

# only the browser is ours to clean up - the freshly built, freshly
# tested server is LEFT RUNNING so you can point a browser at it and
# dissect what every test staged (each test's leftovers live in its own
# labelled view: CloneTest, AliasTest, OptionsTest, ...)
cleanup() { kill "$CHROME_PID" 2>/dev/null; }
trap cleanup EXIT

# wait until the server actually answers a WebSocket handshake - and
# refuse to run against a server that never does
UP=0
echo "waiting for the server on port $PORT ..."
for i in $(seq 1 60); do
    if python3 -c "
import socket, base64, os, sys
try:
    s = socket.create_connection(('127.0.0.1', $PORT), timeout=2)
    key = base64.b64encode(os.urandom(16)).decode()
    s.sendall(('GET / HTTP/1.1\r\nHost: h\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n' % key).encode())
    s.settimeout(2); d = s.recv(200); s.close()
    sys.exit(0 if b'101' in d else 1)
except Exception:
    sys.exit(1)
" 2>/dev/null; then
        UP=1
        break
    fi
    sleep 1
done
if [ "$UP" != 1 ]; then
    echo "the server never answered on port $PORT - see $LOGDIR/server.log" >&2
    tail -20 "$LOGDIR/server.log" >&2
    exit 1
fi

# if the port was already taken (a live dev server, say) OUR framework
# failed to bind and the browser would silently test whatever is running
# there instead - refuse, loudly
if grep -q "could not bind its port" "$LOGDIR/server.log"; then
    echo "port $PORT is already in use - the harness must run its own server." >&2
    echo "use PORT=<free port> ./testharness/run.sh" >&2
    exit 1
fi

sleep 2   # let chromium's debug endpoint come up too

# Each suite prints ONLY its failures plus one summary line - a green run
# is four lines total, on purpose (there will be hundreds of tests; a
# passing report nobody reads is pure cost). Pass -v to any of them by
# hand to watch every check.
#
# Raw-protocol suites first - the readme's harness rule: every mechanism
# is proven through the raw JSON protocol (port 8091, no browser) before
# the browser tests prove its presentation. Separate bridge, same engine.
VERBOSE="${VERBOSE:+-v}"

# the verbs themselves: birth, naming, stamping, internals, save/load,
# move, delete
python3 testharness/rawtest.py --host 127.0.0.1 --port 8091 $VERBOSE
RAW_RC=$?

# the dataflow flows that used to boot inside main.c (cat, filter/gate,
# queue/stack, tcp echo) - same wiring, built over the raw protocol,
# asserting on subscribed events instead of printing probes
python3 testharness/flowtest.py --host 127.0.0.1 --port 8091 $VERBOSE
FLOW_RC=$?

# deep-cloning a view: members, aliases AND the wires between them, over
# clone, clone-of-clone, and save/load - proven by driving the copies,
# never by reading structure back
python3 testharness/viewclonetest.py --host 127.0.0.1 --port 8091 $VERBOSE
VC_RC=$?

# connections: every wire is listed, announced, disconnectable, scrubbed
# on sink delete, and chains - what Connect mode draws and the "x" undoes
python3 testharness/connectiontest.py --host 127.0.0.1 --port 8091 $VERBOSE
CONN_RC=$?

# allocation accounting: create/destroy and message-burst cycles must
# come back to rest - a counter that grows and never shrinks is a leak
python3 testharness/leaktest.py --host 127.0.0.1 --port 8091 $VERBOSE
LEAK_RC=$?

# the JS language host (QuickJS): a script as a dataflow object AND as a
# bridge client speaking the JSON protocol - the second language proving
# the "language host is a bridge" pattern
python3 testharness/jstest.py --host 127.0.0.1 --port 8091 $VERBOSE
JS_RC=$?

# the ScriptBox shell: discovers script hosts, runs code, collects output,
# swaps languages - the script widget's engine behavior
python3 testharness/scriptboxtest.py --host 127.0.0.1 --port 8091 $VERBOSE
SB_RC=$?

# the scripted composite widget: a View with container In/Out ports, inner
# controls, and a script that puppets them - a coded widget coded in script
python3 testharness/widgettest.py --host 127.0.0.1 --port 8091 $VERBOSE
WIDGET_RC=$?

# the TCP instrument panel (ported from VNOS): a front end driving a
# contained TCP engine, proven over a real socket
python3 testharness/tcpporttest.py --host 127.0.0.1 --port 8091 $VERBOSE
TCPP_RC=$?

# and the browser, proving presentation: gestures emit the right verb,
# events paint the right pixels
python3 testharness/guitest.py --app "http://127.0.0.1:$PORT" --cdp "$CDP_PORT" $VERBOSE
RC=$?

echo "logs: $LOGDIR/server.log, $LOGDIR/chrome.log   server up on http://localhost:$PORT (pid $SERVER_PID)"
[ "$RAW_RC" != 0 ] && exit "$RAW_RC"
[ "$FLOW_RC" != 0 ] && exit "$FLOW_RC"
[ "$VC_RC" != 0 ] && exit "$VC_RC"
[ "$CONN_RC" != 0 ] && exit "$CONN_RC"
[ "$LEAK_RC" != 0 ] && exit "$LEAK_RC"
[ "$JS_RC" != 0 ] && exit "$JS_RC"
[ "$SB_RC" != 0 ] && exit "$SB_RC"
[ "$WIDGET_RC" != 0 ] && exit "$WIDGET_RC"
[ "$TCPP_RC" != 0 ] && exit "$TCPP_RC"
exit $RC
