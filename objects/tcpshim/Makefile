CC=gcc

SOURCE= tcpshim.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=tcpshim.object

INCLUDES=-iquote../../src

OPT=-Wall -g

%.o : %.c
	$(CC) -fPIC $(OPT) $(INCLUDES) -c $< -o $@

all: $(OBJECTS) $(GOAL)

$(GOAL): $(OBJECTS)
	ld  -shared $(OBJECTS) -o $(GOAL)

clean:
	rm -f $(OBJECTS) $(GOAL) > /dev/null 2>&1

DEPINC=-I../../src -I/usr/include/x86_64-linux-gnu \
       -I$(shell $(CC) -print-file-name=include)

depend:
	makedepend $(DEPINC) $(SOURCE)
# DO NOT DELETE

tcpshim.o: /usr/include/stdio.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/libc-header-start.h
tcpshim.o: /usr/include/features.h /usr/include/features-time64.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/wordsize.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/timesize.h
tcpshim.o: /usr/include/stdc-predef.h
tcpshim.o: /usr/include/x86_64-linux-gnu/sys/cdefs.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/long-double.h
tcpshim.o: /usr/include/x86_64-linux-gnu/gnu/stubs.h
tcpshim.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
tcpshim.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/types.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/typesizes.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/time64.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/types/__fpos_t.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/types/__mbstate_t.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/types/__fpos64_t.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/types/__FILE.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/types/FILE.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/types/struct_FILE.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/stdio_lim.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/floatn.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/floatn-common.h
tcpshim.o: /usr/include/stdlib.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/waitflags.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/waitstatus.h
tcpshim.o: /usr/include/x86_64-linux-gnu/sys/types.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/types/clock_t.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/types/clockid_t.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/types/time_t.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/types/timer_t.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/stdint-intn.h
tcpshim.o: /usr/include/endian.h /usr/include/x86_64-linux-gnu/bits/endian.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/endianness.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/byteswap.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/uintn-identity.h
tcpshim.o: /usr/include/x86_64-linux-gnu/sys/select.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/select.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/types/sigset_t.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/types/__sigset_t.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/types/struct_timeval.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/types/struct_timespec.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/thread-shared-types.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes-arch.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/atomic_wide_counter.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/struct_mutex.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/struct_rwlock.h
tcpshim.o: /usr/include/alloca.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/stdlib-float.h
tcpshim.o: /usr/include/string.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/types/locale_t.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/types/__locale_t.h
tcpshim.o: /usr/include/strings.h /usr/include/sched.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/sched.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/types/struct_sched_param.h
tcpshim.o: /usr/include/x86_64-linux-gnu/bits/cpu-set.h
tcpshim.o: ../../objects/network/tcp.h
