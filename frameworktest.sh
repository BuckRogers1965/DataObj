#!/bin/sh

# one framework at a time: kill any other instance (a harness leftover,
# a forgotten manual run) so this one always gets its port
pkill -x framework 2>/dev/null && sleep 1

# -v 3 turns on the CLONE trace category (DebugPrint.c) so the step-by-step
# of every clone prints; pass your own -v after to override, e.g.
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:./build/
./build/unit_test $@
