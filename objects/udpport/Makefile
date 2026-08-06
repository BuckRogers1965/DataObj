CC=gcc

SOURCE= udpport.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=udpport.object

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

udpport.o: /usr/include/stdio.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/libc-header-start.h
udpport.o: /usr/include/features.h /usr/include/features-time64.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/wordsize.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/timesize.h
udpport.o: /usr/include/stdc-predef.h
udpport.o: /usr/include/x86_64-linux-gnu/sys/cdefs.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/long-double.h
udpport.o: /usr/include/x86_64-linux-gnu/gnu/stubs.h
udpport.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
udpport.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/types.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/typesizes.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/time64.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/types/__fpos_t.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/types/__mbstate_t.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/types/__fpos64_t.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/types/__FILE.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/types/FILE.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/types/struct_FILE.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/stdio_lim.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/floatn.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/floatn-common.h
udpport.o: /usr/include/stdlib.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/waitflags.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/waitstatus.h
udpport.o: /usr/include/x86_64-linux-gnu/sys/types.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/types/clock_t.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/types/clockid_t.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/types/time_t.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/types/timer_t.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/stdint-intn.h
udpport.o: /usr/include/endian.h /usr/include/x86_64-linux-gnu/bits/endian.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/endianness.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/byteswap.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/uintn-identity.h
udpport.o: /usr/include/x86_64-linux-gnu/sys/select.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/select.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/types/sigset_t.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/types/__sigset_t.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/types/struct_timeval.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/types/struct_timespec.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/thread-shared-types.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes-arch.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/atomic_wide_counter.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/struct_mutex.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/struct_rwlock.h
udpport.o: /usr/include/alloca.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/stdlib-float.h
udpport.o: /usr/include/string.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/types/locale_t.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/types/__locale_t.h
udpport.o: /usr/include/strings.h /usr/include/ctype.h /usr/include/sched.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/sched.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/types/struct_sched_param.h
udpport.o: /usr/include/x86_64-linux-gnu/bits/cpu-set.h
udpport.o: ../../objects/udp/udp.h
