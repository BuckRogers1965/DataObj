#!/bin/sh

# One framework at a time ON THIS PORT.
#
# NOT "pkill -x framework": that killed every instance by name, including the
# five a test run has going on their own ports. The only thing in the way of
# this start is whoever already holds the port this start wants.
#
# The port is -port <n> (default 8083). Note -p is a different option
# entirely - print the node tree on exit - and takes no argument.
port=8083
prev=
for arg in "$@"; do
	[ "$prev" = "-port" ] && port=$arg
	prev=$arg
done

holder=$(ss -ltnp 2>/dev/null | awk -v p=":$port\$" '$4 ~ p {print $NF}' |
         grep -o 'pid=[0-9]*' | cut -d= -f2 | head -1)

if [ -n "$holder" ] && [ "$(cat /proc/$holder/comm 2>/dev/null)" = framework ]; then
	echo "framework.sh: port $port is held by framework pid $holder - stopping it"
	kill "$holder" 2>/dev/null
	sleep 1
fi

# -v 3 turns on the CLONE trace category (DebugPrint.c) so the step-by-step
# of every clone prints; pass your own -v after to override, e.g.
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:./build/
#./build/framework -v 3 $@
./build/framework $@
