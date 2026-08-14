#!/bin/sh
#
# The whole suite, run against several builds of the core AT THE SAME TIME.
#
# Each variant gets its own core build, its own framework on its own ports, its
# own chromium, and its own log directory - so nothing steps on anything else,
# and your desktop framework on the production port keeps running throughout.
#
#   testharness/tests/<stamp>/<variant>/build/      that variant's .o/.so/exes
#   testharness/tests/<stamp>/<variant>/<suite>.log one log per suite
#   testharness/tests/<stamp>/report.txt            rc per suite per variant
#
#   ./testharness/run.sh              # every variant, every suite
#   ./testharness/run.sh -v           # verbose suites
#   VARIANTS=debug ./testharness/run.sh
#   SUITES="unit_test rawtest" ./testharness/run.sh
#
cd "$(dirname "$0")/.." || exit 1

# ONE RUN AT A TIME. The first thing this script does, before it reads an
# option or touches a file - because everything after this point steps on a run
# already going: make rewrites the .object files the running variants have
# loaded, and a second set of frameworks cannot bind the ports so its suites
# silently measure the first run's engines.
if [ -f testharness/run.pid ] && kill -0 "$(cat testharness/run.pid 2>/dev/null)" 2>/dev/null; then
	echo "already running (PID $(cat testharness/run.pid))" >&2
	exit 1
fi
echo $$ > testharness/run.pid

for arg in "$@"; do [ "$arg" = "-v" ] && VERBOSE=1; done
VERBOSE="${VERBOSE:+-v}"

# Cores, please. kernel.core_pattern is a bare filename (no leading /), so a
# crash dumps into the crashing process's CWD - the variant directory - right
# beside that variant's log/ and saved/. The soft limit is 0 by default and the
# hard limit is infinity, so this needs no privilege; without it the kernel
# refuses to write anything and a segfault leaves no evidence at all.
ulimit -c unlimited 2>/dev/null || true

STAMP=$(date +%Y%m%d_%H%M%S)
ROOT=testharness/tests/$STAMP
INC="-Isrc -Isrc/dyn -Wall -Wextra"

# name|flags|port offset. Ports are base+offset, so variants never collide with
# each other. The bases below keep the whole run clear of the desktop
# instance, which owns more than one port now (8083 web, 8283 REST).
ALL_VARIANTS="debug|-O0 -g3 -fno-omit-frame-pointer -fno-inline|1
release|-O3 -march=native -flto=auto|2
asan|-O1 -g -fno-omit-frame-pointer -fsanitize=address|3
ubsan|-O1 -g -fsanitize=undefined -fno-sanitize-recover=all|4
gcov|-O0 -g --coverage|5"

# simplest first, browser last - a failure in an early suite makes the later
# ones unreliable signal for the same root cause
ALL_SUITES="unit_test connectiontest flowtest viewclonetest jstest scriptboxtest scriptedwidgettest widgettest tcpporttest guitest rawtest leaktest"
SUITES=${SUITES:-$ALL_SUITES}

# overridable so a second run can be pointed somewhere else - two runs on the
# same bases silently measure each other's engines (seen: 2026-08-05)
#
# HIGH AND FAR APART, deliberately. A framework opens more than the web port -
# it also opens its REST port at web+200 - so the bases have to leave room for
# every socket a variant opens, not just the one it is named after. Sharing a
# base with the desktop instance cost a whole run on 2026-08-14, when a
# framework started before a port change sat on a variant's web port and every
# suite reported "connection refused" as a crash.
#
# EVERY port a run touches, not just these three - the suites derive more
# from the bases, and so does the framework itself. Check the whole map
# before moving anything:
#
#   web   8501..8505     the GUI/HTTP port, WEB_BASE+offset
#   raw   8601..8605     the raw bridge, RAW_BASE+offset
#   echo  8701..8705     flowtest's TCP echo server, raw+100
#   REST  8901..8905     the framework's own REST port, web+REST_PORT_OFFSET
#   cdp   9501..9505     chromium, CDP_BASE+offset
#   panel 13510..13550   tcpporttest, 8400+(raw-8091)*10
WEB_BASE=${WEB_BASE:-8500}; RAW_BASE=${RAW_BASE:-8600}; CDP_BASE=${CDP_BASE:-9500}

# every step says what it is about to do, with a timestamp - a long silent
# phase is indistinguishable from a hang
say() { printf '%s  %s\n' "$(date +%H:%M:%S)" "$*"; }

# Start a child that the KERNEL kills when this script dies - PR_SET_PDEATHSIG.
# A trap only covers clean exits and Ctrl-C; this covers the script being
# kill -9'd or crashing, which is what left eight orphaned frameworks behind.
#   pdeath <cwd> <logfile> <command...>   -> sets PDEATH_PID
# No subshell: the wrapper's parent must be THIS script, or the death signal is
# anchored to something that exits immediately.
pdeath() {
	pd_cwd=$1; pd_log=$2; shift 2
	python3 - "$pd_cwd" "$pd_log" "$@" <<'PY' &
import ctypes, os, signal, subprocess, sys
cwd, log, cmd = sys.argv[1], sys.argv[2], sys.argv[3:]
os.chdir(cwd)
def die_with_parent():
    ctypes.CDLL("libc.so.6").prctl(1, signal.SIGTERM)   # 1 = PR_SET_PDEATHSIG
with open(log, "wb") as f:
    p = subprocess.Popen(cmd, stdout=f, stderr=subprocess.STDOUT,
                         preexec_fn=die_with_parent)
    sys.exit(p.wait())
PY
	PDEATH_PID=$!
}

CHROME="$(command -v chromium || command -v chromium-browser || command -v google-chrome)"
[ -z "$CHROME" ] && echo "no chromium found - guitest needs a real browser" >&2

# ---- the objects are shared: build them once, the normal way ----------------
mkdir -p "$ROOT/log"
say "run $STAMP -> $ROOT   (logs in log/, summary in report.txt)"
say "suites: $SUITES"
say "chromium: ${CHROME:-none found}"
say "building the shared objects (make, log: $ROOT/objects-build.log) - this is the slow one ..."
make > "$ROOT/log/objects-build.log" 2>&1
say "objects built: $(ls objects/*/*.object 2>/dev/null | wc -l) modules, $(wc -l < "$ROOT/log/objects-build.log") lines of output"
if grep -qE '\*\*\*|Error [0-9]' "$ROOT/log/objects-build.log"; then
	echo "build failed - see $ROOT/objects-build.log" >&2; exit 1
fi
if grep -q 'warning:' "$ROOT/log/objects-build.log"; then
	echo "build has compiler warnings - the tree builds warning-free, keep it that way" >&2
	grep 'warning:' "$ROOT/log/objects-build.log" | head -10 >&2; exit 1
fi

# ---- one core build per variant, in parallel -------------------------------
pick_variants() {
	echo "$ALL_VARIANTS" | while IFS='|' read -r v flags off; do
		[ -n "$v" ] || continue
		case " ${VARIANTS:-$( echo "$ALL_VARIANTS" | cut -d'|' -f1 | tr '\n' ' ')} " in
			*" $v "*) printf '%s|%s|%s\n' "$v" "$flags" "$off" ;;
		esac
	done
}

pick_variants > "$ROOT/log/variants"
say "variants: $(cut -d'|' -f1 "$ROOT/log/variants" | tr '\n' ' ')"
while IFS='|' read -r v flags off; do
	say "[$v] building core: $flags"
	mkdir -p "$ROOT/$v/build" "$ROOT/$v/log"
	( make --no-print-directory BUILD="$ROOT/$v/build" OPT="$flags $INC" \
	       "$ROOT/$v/build/framework" "$ROOT/$v/build/unit_test" \
	       > "$ROOT/$v/log/build.log" 2>&1; echo $? > "$ROOT/$v/log/build.rc" ) &
done < "$ROOT/log/variants"
wait
while IFS='|' read -r v f o; do
	say "[$v] core build return code=$(cat "$ROOT/$v/log/build.rc" 2>/dev/null)"
done < "$ROOT/log/variants"

# ---- run every suite against every variant, variants in parallel -----------
# ask a variant's own bridge to write the live root into <variant>/saved/
save_root() {   # $1 = raw port, $2 = file name
	python3 - "$1" "$2" <<'PY' 2>/dev/null
import json, socket, sys, time
port, name = int(sys.argv[1]), sys.argv[2]
try:
    s = socket.create_connection(("127.0.0.1", port), timeout=3)
    s.sendall(json.dumps({"cmd": "save-flow", "of": "/Root", "file": name}).encode())
    time.sleep(1.5)
    s.close()
except Exception:
    sys.exit(1)
PY
}

# ask a variant's framework to shut ITSELF down: Main's State is an ordinary
# property, /Main an ordinary path, set-property an existing command - so the
# quit verb already exists (IsRunning reads State, Stopping=0, main.c:338).
# It matters for the sanitizer builds: TERM is a signal death, and LeakSanitizer
# only runs its exit-time check when the process exits normally.
ask_to_quit() {  # $1 = raw port
	python3 - "$1" <<'PY' 2>/dev/null
import json, socket, sys, time
try:
    s = socket.create_connection(("127.0.0.1", int(sys.argv[1])), timeout=3)
    s.sendall((json.dumps({"cmd": "set-property", "instance": "/Main",
                           "prop": "State", "value": "0"}) + "\n").encode())
    time.sleep(0.5)
    s.close()
except Exception:
    sys.exit(1)
PY
}

# One place that turns a suite's exit status into a cell: CRASH, LEAK or a
# count.  $5 is the pid of the engine the suite needed, or "" when it needed
# none - unit_test links libframework directly and talks to nobody, so an
# absent engine is not evidence of anything for it.  Sets LAST_CRASH.
record() {   # $1 = variant  $2 = variant dir  $3 = suite  $4 = rc  $5 = server pid
	local v=$1 d=$2 s=$3 rc=$4 srv=$5 crash=0 leak=0

	# crash, in the order the evidence is trustworthy: the engine is gone;
	# the sanitizer bailed out (ASAN _exit(1)s by default on Linux, so its
	# abort is NOT a signal death and rc alone cannot see it); the suite
	# could not reach the engine; the suite itself died by signal.
	# LeakSanitizer is deliberately NOT in that list - a leak is reported
	# at exit, AFTER the suite ran every test, so it is a finding on a
	# suite that worked, not a suite that died.
	if [ -n "$srv" ] && ! kill -0 "$srv" 2>/dev/null; then
		crash=1
	elif grep -qE "ERROR: (Address|Thread)Sanitizer|==[0-9]+==ABORTING" \
	            "$d/log/$s.log" 2>/dev/null; then
		crash=1
	elif grep -qE "ConnectionRefusedError|BrokenPipeError" \
	            "$d/log/$s.log" 2>/dev/null; then
		crash=1
	elif [ "$rc" -ge 128 ]; then
		crash=1
	fi

	rm -f "$d/log/$s.leak"
	if grep -qE "ERROR: LeakSanitizer|byte\(s\) leaked" "$d/log/$s.log" 2>/dev/null
	then
		leak=1
		grep -E "SUMMARY: AddressSanitizer: .*leaked" "$d/log/$s.log" \
			| tail -1 > "$d/log/$s.leak"
	fi

	if [ "$crash" = 1 ]; then
		echo -1 > "$d/log/$s.rc"
		say "[$v] $s CRASHED (exit $rc) - see $d/log/$s.log"
	else
		echo $rc > "$d/log/$s.rc"
		if [ "$leak" = 1 ]; then
			say "[$v] $s LEAKED - $(cat "$d/log/$s.leak")"
		elif [ "$rc" = 0 ]; then
			say "[$v] $s success"
		else
			say "[$v] $s $rc failures"
		fi
	fi
	LAST_CRASH=$crash
}

# every suite from here on is marked not-run, with the reason
skip_rest() {   # $1 = variant  $2 = variant dir  $3 = reason  $4... = suites to skip
	local v=$1 d=$2 why=$3 s; shift 3   # s LOCAL: the caller's loop uses it too
	for s in "$@"; do
		echo -1 > "$d/log/$s.rc"
		echo "not run: $why" > "$d/log/$s.log"
		say "[$v] $s NOT RUN - $why"
	done
}

run_variant() {
	v=$1; off=$2
	web=$((WEB_BASE + off)); raw=$((RAW_BASE + off)); cdp=$((CDP_BASE + off))
	d=$ROOT/$v; b=$PWD/$d/build

	[ "$(cat "$d/log/build.rc" 2>/dev/null)" = 0 ] || { echo "build failed" > "$d/log/SKIPPED"; return; }

	mkdir -p "$d/saved" "$d/objects" "$d/log" "$d/web"
	# HARDLINKS, not a symlink: dirscan uses lstat on purpose (dirscan.c:89), so
	# a symlinked dir or file is not seen as a directory or a regular file and
	# the scan skips it. Made after the shared build, since make replaces a
	# .object with a new inode and would leave an old link stale.
	ln -f objects/*/*.object "$d/objects/" 2>/dev/null
	# and the same for the client: Http serves Root="web" RELATIVE TO CWD
	# (main.c:211), and the framework runs with cwd=$d - without these the
	# page 404s, the browser boots empty, and every guitest cascades off boot.
	ln -f web/* "$d/web/" 2>/dev/null
	# ...but NOT the generated ones. Each variant's own Bridge writes its
	# controls' web halves at start; hardlinked, all five variants and the
	# repo would be truncating and rewriting ONE inode at once, so a variant
	# would be testing whichever build won the race rather than its own.
	rm -f "$d/web/widgets.js" "$d/web/widgets.css"
	say "[$v] $(ls "$d/objects" | wc -l) objects, $(ls "$d/web" | wc -l) web files hardlinked into $d"

	# unit_test links libframework directly and speaks to no server, so it goes
	# FIRST - before anything is started. If the library itself is broken there
	# is nothing to learn from standing up a framework on top of it, so a crash
	# here ends the variant without ever starting one.
	say "[$v] unit_test ..."
	( cd "$d" && LD_LIBRARY_PATH=build ./build/unit_test -v 0 ) \
		> "$d/log/unit_test.log" 2>&1
	record "$v" "$d" unit_test "$?" ""
	if [ "$LAST_CRASH" = 1 ]; then
		skip_rest "$v" "$d" "unit_test crashed - no framework was started" \
			$(echo "$SUITES" | tr ' ' '\n' | grep -v '^unit_test$')
		return
	fi

	# -v 3 ALWAYS, not just when something looks wrong. A forensic log is only
	# useful if it was already running when the thing happened, and a failure
	# you have to reproduce at a higher verbosity is a failure you may not be
	# able to reproduce at all (several of today's were timing-sensitive). This
	# turns on WIRE, PLACE, CLONE, REGISTER and the rest - every wire made and
	# removed, every placement, every rename - so a failed assertion has the
	# engine's own account of what led up to it sitting next to it.
	#
	# It costs disk and it costs speed. The speed matters: DebugPrint at this
	# level is thousands of lines per run, which changes timing, which can hide
	# a race the way the asan build does. A test must never depend on it.
	say "[$v] starting framework on web=$web raw=$raw cdp=$cdp (cwd=$d)"
	pdeath "$PWD/$d" log/server.log env LD_LIBRARY_PATH=build ./build/framework \
	       -ip 127.0.0.1 -port "$web" -v 3
	server=$PDEATH_PID
	for i in $(seq 1 60); do
		sleep 1
		python3 - "$web" <<'PY' && break
import socket, sys
try:
    s = socket.create_connection(("127.0.0.1", int(sys.argv[1])), timeout=2); s.close()
except Exception: sys.exit(1)
PY
	done

	say "[$v] framework answering on $web - composing its raw bridge on $raw"
	python3 - "$raw" "$web" <<'PY' > "$d/log/bridge.log" 2>&1
import sys
sys.path.insert(0, "testharness")
from rawtest import ensure_raw_bridge
ensure_raw_bridge("127.0.0.1", int(sys.argv[1]), int(sys.argv[2]))
PY

	if [ -n "$CHROME" ]; then
		pdeath "$PWD" "$d/log/chrome.log" "$CHROME" --headless=new \
		       --remote-debugging-port="$cdp" --window-size=1400,950 \
		       --no-sandbox --disable-gpu --user-data-dir="$PWD/$d/chrome" about:blank
		browser=$PDEATH_PID
		sleep 2
	fi

	# a suite that never got to measure anything is a CRASH, not a count of
	# failures - the two used to print the same "1" and read as one failed
	# test. -1 goes in the .rc and the report prints CRASH. It can only ever
	# be a FILE value: a process exit status is truncated to 0-255 by the
	# kernel, so an exiting -1 would come back as 255 and read as 255 failures.
	crashed_by=""
	# every check writes itself to passed.log / failed.log in here (Report.record),
	# so a measurement survives the run whether or not it failed
	export HARNESS_LOGDIR="$PWD/$d/log"
	for s in $SUITES; do
		[ "$s" = unit_test ] && continue          # already run, before the server
		say "[$v] $s ..."
		case $s in
		guitest)
			[ -n "$CHROME" ] && python3 testharness/guitest.py \
				--app "http://127.0.0.1:$web" --cdp "$cdp" $VERBOSE > "$d/log/guitest.log" 2>&1 ;;
		*)
			python3 "testharness/$s.py" --host 127.0.0.1 --port "$raw" \
				--webport "$web" $VERBOSE > "$d/log/$s.log" 2>&1 ;;
		esac
		rc=$?

		record "$v" "$d" "$s" "$rc" "$server"

		# a crash ends this variant. Everything after it would be measuring an
		# engine that is not there, and every one of those cells reads as a
		# separate failure - one death, printed eleven times, is what made the
		# report unreadable. The other variants are their own engines and keep
		# going.
		if [ "$LAST_CRASH" = 1 ]; then
			crashed_by=$s
			rest=$(echo "$SUITES" | tr ' ' '\n' | sed -n "/^$s\$/,\$p" | tail -n +2)
			[ -n "$rest" ] && skip_rest "$v" "$d" "framework crashed during $s" $rest
			say "[$v] STOPPED - the framework crashed during $crashed_by"
			break
		fi

		if [ "$rc" != 0 ]; then
			say "[$v] $s not clean - saving the session for forensics"
			save_root "$raw" "fail_$s"
		fi
	done

	# "is the pid gone" is true both for a framework that quit when asked and
	# for one that died an hour ago, so ask BEFORE assuming either. A dead one
	# never reached its exit-time leak check, and the log must not imply it did.
	if ! kill -0 "$server" 2>/dev/null; then
		say "[$v] framework was already gone (crashed during ${crashed_by:-an earlier suite}) - no final save, no exit-time leak check"
	else
		say "[$v] saving the final root"
		save_root "$raw" "final"

		say "[$v] done, asking its framework to quit"
		ask_to_quit "$raw"
		for i in $(seq 1 10); do
			kill -0 "$server" 2>/dev/null || break
			sleep 1
		done
		if kill -0 "$server" 2>/dev/null; then
			say "[$v] framework did not quit when asked, signalling it"
		else
			say "[$v] framework quit when asked - exit-time leak check ran"
		fi
	fi

	for pid in $browser $server; do
		kill "$pid" 2>/dev/null
	done
	sleep 1
	for pid in $browser $server; do
		if kill -0 "$pid" 2>/dev/null; then
			say "[$v] pid $pid ignored TERM, sending KILL"
			kill -9 "$pid" 2>/dev/null
		fi
	done
	wait $browser $server 2>/dev/null
}

while IFS='|' read -r v flags off; do
	run_variant "$v" "$off" &
done < "$ROOT/log/variants"
wait
say "all variants finished, writing the report"

VARFAIL=0; VARCRASH=0; VARLEAK=0
# ---- one report: builds across the top, suites down the side, ERROR COUNTS ---
# a suite's own summary line ("... 23 tests, 23 passed, 0 failed") is the count;
# unit_test reports leftover tasks; anything with no summary falls back to its
# exit code (0 = no errors, non-zero = 1).
# the return code IS the failure count now: suites exit with it, unit_test exits
# with its leftover-task count, so the cell is just that number
errors_for() {   # $1 = variant dir, $2 = suite - a count, CRASH, LEAK, or -
	local rc
	[ -f "$1/log/$2.rc" ] || { echo "-"; return; }
	rc=$(cat "$1/log/$2.rc")
	if [ "$rc" = -1 ]; then echo "CRASH"; return; fi
	if [ -f "$1/log/$2.leak" ]; then
		[ "$rc" = 0 ] && echo "LEAK" || echo "$rc+LEAK"
		return
	fi
	echo "$rc"
}

# the raw failure count, whatever decoration the cell carries (-1 crash = 0)
count_for() {   # $1 = variant dir, $2 = suite
	local rc
	rc=$(cat "$1/log/$2.rc" 2>/dev/null)
	case $rc in ''|*[!0-9]*) echo 0 ;; *) echo "$rc" ;; esac
}

{
	printf "%-20s" "suite"
	while IFS='|' read -r v f o; do printf "%-9s" "$v"; done < "$ROOT/log/variants"
	echo
	printf "%-20s" "--------------------"
	while IFS='|' read -r v f o; do printf "%-9s" "--------"; done < "$ROOT/log/variants"
	echo
	for s in $SUITES; do
		printf "%-20s" "$s"
		while IFS='|' read -r v f o; do printf "%-9s" "$(errors_for "$ROOT/$v" "$s")"; done < "$ROOT/log/variants"
		echo
	done
	printf "%-20s" "--------------------"
	while IFS='|' read -r v f o; do printf "%-9s" "--------"; done < "$ROOT/log/variants"
	echo
	printf "%-20s" "failures"
	while IFS='|' read -r v f o; do
		t=0
		for s in $SUITES; do
			t=$((t + $(count_for "$ROOT/$v" "$s")))
		done
		VARFAIL=$((VARFAIL + t))
		printf "%-9s" "$t"
	done < "$ROOT/log/variants"
	echo
	printf "%-20s" "crashed"
	while IFS='|' read -r v f o; do
		c=0
		for s in $SUITES; do
			[ "$(errors_for "$ROOT/$v" "$s")" = CRASH ] && c=$((c + 1))
		done
		VARCRASH=$((VARCRASH + c))
		printf "%-9s" "$c"
	done < "$ROOT/log/variants"
	echo
	# Checks that describe what the engine SHOULD do and name the work that
	# will make it true (Report.expect(..., roadmap=...)). Measured every run,
	# listed below, and NOT a failure - the run can be clean with these
	# standing. They are only honest while two things hold, both enforced in
	# Report.expect: one that starts failing differently says a different
	# sentence, and one that PASSES turns into a failure so the declaration
	# gets deleted rather than quietly outliving the work.
	printf "%-20s" "not yet"
	while IFS='|' read -r v f o; do
		n=0
		for s in $SUITES; do
			# grep -c prints 0 AND exits 1 when it matches nothing, so a
			# `|| echo 0` appends a SECOND zero and the arithmetic sees "0\n0".
			# Same guard count_for uses: take the output, insist it is a number.
			c=$(grep -c "^  result:   NOT YET" "$ROOT/$v/log/$s.log" 2>/dev/null)
			case $c in ''|*[!0-9]*) c=0 ;; esac
			n=$((n + c))
		done
		printf "%-9s" "$n"
	done < "$ROOT/log/variants"
	echo
	printf "%-20s" "leaked"
	while IFS='|' read -r v f o; do
		l=0
		for s in $SUITES; do
			[ -f "$ROOT/$v/log/$s.leak" ] && l=$((l + 1))
		done
		# the ENGINE's own exit-time leak belongs in this column too. It is not
		# attributable to any one suite, so it used to show up only in the
		# sanitizer row - a real 11.6 KB leak read as "leaked 0" in a column of
		# zeros, which is the wrong way round for the thing this row exists for.
		grep -q "ERROR: LeakSanitizer" "$ROOT/$v/log/server.log" 2>/dev/null && l=$((l + 1))
		VARLEAK=$((VARLEAK + l))
		printf "%-9s" "$l"
	done < "$ROOT/log/variants"
	echo
	printf "%-20s" "sanitizer"
	while IFS='|' read -r v f o; do
		h=$(cat "$ROOT/$v"/log/*.log 2>/dev/null | grep -cE "runtime error|ERROR: (AddressSanitizer|LeakSanitizer)")
		printf "%-9s" "$h"
	done < "$ROOT/log/variants"
	echo

	# ---- the same failure count on two runs can be two different sets of bugs.
	# A count that does not move reads as "nothing changed", which is how four
	# distinct failures hid behind a steady 6 for a day. So say WHAT failed, and
	# say it once: identical text across builds is one entry listing the builds
	# it came from. Text that DIFFERS between builds lists separately on purpose
	# - that difference is the signal that it is contention rather than a bug.
	echo "FAILURES"
	python3 - "$ROOT" "$SUITES" <<'PYFAIL'
import os, re, sys

root, suites = sys.argv[1], sys.argv[2].split()
variants = [l.split('|')[0] for l in open(os.path.join(root, 'log', 'variants')) if l.strip()]

def failures(path):
    """every failed check in one suite log, as (test, expected, observed)"""
    out = []
    try:
        lines = open(path, errors='replace').read().split('\n')
    except OSError:
        return out
    for i, line in enumerate(lines):
        m = re.search(r'TEST\s+(\S.*)', line)
        if not m:
            continue
        exp = obs = ''
        verdict = ''
        for j in range(i + 1, min(i + 8, len(lines))):
            t = lines[j].strip()
            if t.startswith('expected:'):   exp = t[len('expected:'):].strip()
            elif t.startswith('observed:'): obs = t[len('observed:'):].strip()
            elif t.startswith('result:'):   verdict = t; break
        if 'FAIL' in verdict:
            out.append((m.group(1).strip(), exp, obs))
    # unit_test and anything else that just prints its own failure line
    for line in lines:
        t = line.strip()
        if re.match(r'^[A-Za-z][A-Za-z0-9_]*: Failed', t) or t.startswith('FAIL:'):
            out.append((t, '', ''))
    return out

groups = {}
for s in suites:
    for v in variants:
        for f in failures(os.path.join(root, v, 'log', s + '.log')):
            got = groups.setdefault((s,) + f, [])
            if v not in got:
                got.append(v)

if not groups:
    print("  none")
for (s, name, exp, obs), vs in sorted(groups.items()):
    same = len(vs) == len(variants)
    print("  %s: %s" % (s, name))
    print("    builds:   %s" % ("all" if same else " ".join(vs)))
    if exp: print("    expected: %s" % exp)
    if obs: print("    observed: %s" % obs)
    print()
PYFAIL

	# What the engine is expected to do and does not do YET, each naming the
	# work. This list is the gap between the design and the code, measured
	# rather than remembered - the tests are written against what SHOULD be
	# true, so a design decision that has not been built yet shows up here
	# instead of being quietly absent from the harness.
	echo "NOT YET (roadmap work - measured, not failures)"
	python3 - "$ROOT" "$SUITES" <<'PYNOTYET'
import os, re, sys

root, suites = sys.argv[1], sys.argv[2].split()
variants = [l.split('|')[0] for l in open(os.path.join(root, 'log', 'variants')) if l.strip()]

def notyet(path):
    """every not-yet check in one suite log, as (test, observed, roadmap)"""
    out = []
    try:
        lines = open(path, errors='replace').read().split('\n')
    except OSError:
        return out
    for i, line in enumerate(lines):
        m = re.search(r'TEST\s+(\S.*)', line)
        if not m:
            continue
        obs = ref = ''
        hit = False
        for j in range(i + 1, min(i + 8, len(lines))):
            t = lines[j].strip()
            if t.startswith('observed:'):  obs = t[len('observed:'):].strip()
            elif t.startswith('result:'):  hit = 'NOT YET' in t
            elif t.startswith('roadmap:'): ref = t[len('roadmap:'):].strip(); break
        if hit:
            out.append((m.group(1).strip(), obs, ref))
    return out

groups = {}
for s in suites:
    for v in variants:
        for f in notyet(os.path.join(root, v, 'log', s + '.log')):
            got = groups.setdefault((s,) + f, [])
            if v not in got:
                got.append(v)

if not groups:
    print("  none - everything the tests describe, the engine does")

# grouped by the work, because one item is usually holding several checks
by_ref = {}
for (s, name, obs, ref), vs in groups.items():
    by_ref.setdefault(ref, []).append((s, name, obs, vs))

for ref in sorted(by_ref):
    print("  %s" % (ref or "(no roadmap reference given)"))
    for s, name, obs, vs in sorted(by_ref[ref]):
        same = len(vs) == len(variants)
        print("    %s: %s  [%s]" % (s, name, "all" if same else " ".join(vs)))
        if obs: print("      observed: %s" % obs)
    print()
PYNOTYET

	# a suite whose count DIFFERS between builds is almost never the engine -
	# same code in every build, so only the environment can explain it. Look at
	# the test for a shared resource (a port, a file, a listener) before
	# reading a line of engine code.
	for s in $SUITES; do
		first=; differs=0
		while IFS='|' read -r v f o; do
			c=$(count_for "$ROOT/$v" "$s")
			[ -z "$first" ] && first=$c
			[ "$c" != "$first" ] && differs=1
		done < "$ROOT/log/variants"
		[ "$differs" = 1 ] && echo "NOTE: $s failed a different number of times per build - suspect a shared resource in the test, not a bug in the engine"
	done

	echo
	while IFS='|' read -r v f o; do
		echo "$v: build return code=$(cat "$ROOT/$v/log/build.rc" 2>/dev/null)  web=$((WEB_BASE+o)) raw=$((RAW_BASE+o)) cdp=$((CDP_BASE+o))  logs=$ROOT/$v/log/"
	done < "$ROOT/log/variants"
	for f in "$ROOT"/*/log/*.leak; do
		[ -f "$f" ] || continue
		v=$(basename "$(dirname "$(dirname "$f")")")
		echo "leak: $v $(basename "$f" .leak) - $(cat "$f" | sed 's/^SUMMARY: //')"
	done
	if [ -d "$ROOT/gcov/build" ]; then
		( cd "$ROOT/gcov" && gcov -b -o build ../../../../src/*.c > coverage.txt 2>&1 )
		echo "coverage: $ROOT/gcov/coverage.txt"
	fi
} > "$ROOT/report.txt"
cat "$ROOT/report.txt"
echo "logs: $ROOT/log/ and $ROOT/<variant>/log/   summary: $ROOT/report.txt"

# the return code counts everything that was not a pass: failures plus crashes
# (a crash cannot be -1 here - the kernel truncates an exit status to 0-255)
BAD=$((VARFAIL + VARCRASH + VARLEAK))
if [ "$BAD" = 0 ]; then
	say "success - no failures, no crashes, no leaks, in any variant"
else
	say "$VARFAIL failures, $VARCRASH crashes and $VARLEAK leaks across all variants"
fi
exit $((BAD > 254 ? 254 : BAD))
