#!/bin/sh
#
# killrun.sh - stop a harness run and everything it started.
#
# run.sh anchors its children with PR_SET_PDEATHSIG, so killing run.sh is
# normally enough - the kernel takes the frameworks and the chromiums down
# with it. This exists for when that is not enough: a run killed with -9 in
# the wrong order, a suite wedged on a socket, or orphans left over from a
# crash days ago that are still holding the harness ports.
#
# THE ONE RULE: your own framework keeps running. run.sh is built so that a
# desktop framework on the production port survives a whole run, and a kill
# script that breaks that promise is worse than no kill script. So nothing is
# matched by name here - a process is harness-owned if and only if it is
# WORKING IN a run directory (its cwd, or a --user-data-dir, under
# testharness/tests). Your framework's cwd is the repo, so it can never match,
# whatever it happens to be called or whichever port it holds.
#
#   testharness/killrun.sh          stop the run, wait for things to go
#   testharness/killrun.sh -n       say what it would kill, kill nothing
#
here=$(cd "$(dirname "$0")" && pwd)
runs="$here/tests"
dry=""
[ "$1" = "-n" ] && dry=1

say() { printf '%s\n' "$*"; }

# every pid whose working directory is inside a run directory. /proc is the
# only place that answers "where is this process actually working", which is
# the whole discriminator - a name or a port would catch the wrong framework.
harness_pids() {
	for d in /proc/[0-9]*; do
		pid=${d#/proc/}
		[ "$pid" = "$$" ] && continue

		cwd=$(readlink "$d/cwd" 2>/dev/null) || continue
		case "$cwd" in
			"$runs"/*) echo "$pid"; continue ;;
		esac

		# chromium runs with the repo as its cwd but is told to keep its
		# profile in the run directory, so it is named by its arguments
		args=$(tr '\0' ' ' < "$d/cmdline" 2>/dev/null) || continue
		case "$args" in
			*"$runs"/*) echo "$pid" ;;
		esac
	done
}

# run.sh itself, so it stops launching the next suite while we clear the
# current one. Its own cwd is the repo, so the walk above never finds it.
runner_pids() {
	for d in /proc/[0-9]*; do
		pid=${d#/proc/}
		[ "$pid" = "$$" ] && continue
		args=$(tr '\0' ' ' < "$d/cmdline" 2>/dev/null) || continue
		case "$args" in
			*run.sh*)
				case "$args" in
					*killrun.sh*) ;;			# never ourselves
					*) echo "$pid" ;;
				esac
				;;
		esac
	done
}

describe() {
	for pid in $1; do
		args=$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null)
		[ -z "$args" ] && continue
		say "  $pid  $(printf '%.100s' "$args")"
	done
}

runners=$(runner_pids)
workers=$(harness_pids)
all=$(printf '%s\n%s\n' "$runners" "$workers" | sort -un | tr '\n' ' ')

if [ -z "$(printf '%s' "$all" | tr -d ' ')" ]; then
	say "no harness run is going"
	exit 0
fi

say "harness processes:"
describe "$all"

if [ -n "$dry" ]; then
	say "(-n given, nothing killed)"
	exit 0
fi

# ask first: run.sh traps and tears its own children down in order, and a
# framework asked to stop writes its exit-time leak check. Only then insist.
kill -TERM $all 2>/dev/null

n=0
while [ $n -lt 20 ]; do
	left=$(printf '%s\n%s\n' "$(runner_pids)" "$(harness_pids)" | sort -un | tr -d '\n ')
	[ -z "$left" ] && break
	sleep 0.5
	n=$((n + 1))
done

still=$(printf '%s\n%s\n' "$(runner_pids)" "$(harness_pids)" | sort -un | tr '\n' ' ')
if [ -n "$(printf '%s' "$still" | tr -d ' ')" ]; then
	say "still up after 10s, forcing:"
	describe "$still"
	kill -KILL $still 2>/dev/null
	sleep 1
fi

say "harness stopped"
