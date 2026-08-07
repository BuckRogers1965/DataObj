CC=gcc

SOURCE= flow.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=flow.object

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

flow.o: /usr/include/stdio.h
flow.o: /usr/include/x86_64-linux-gnu/bits/libc-header-start.h
flow.o: /usr/include/features.h /usr/include/features-time64.h
flow.o: /usr/include/x86_64-linux-gnu/bits/wordsize.h
flow.o: /usr/include/x86_64-linux-gnu/bits/timesize.h
flow.o: /usr/include/stdc-predef.h /usr/include/x86_64-linux-gnu/sys/cdefs.h
flow.o: /usr/include/x86_64-linux-gnu/bits/long-double.h
flow.o: /usr/include/x86_64-linux-gnu/gnu/stubs.h
flow.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
flow.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
flow.o: /usr/include/x86_64-linux-gnu/bits/types.h
flow.o: /usr/include/x86_64-linux-gnu/bits/typesizes.h
flow.o: /usr/include/x86_64-linux-gnu/bits/time64.h
flow.o: /usr/include/x86_64-linux-gnu/bits/types/__fpos_t.h
flow.o: /usr/include/x86_64-linux-gnu/bits/types/__mbstate_t.h
flow.o: /usr/include/x86_64-linux-gnu/bits/types/__fpos64_t.h
flow.o: /usr/include/x86_64-linux-gnu/bits/types/__FILE.h
flow.o: /usr/include/x86_64-linux-gnu/bits/types/FILE.h
flow.o: /usr/include/x86_64-linux-gnu/bits/types/struct_FILE.h
flow.o: /usr/include/x86_64-linux-gnu/bits/stdio_lim.h
flow.o: /usr/include/x86_64-linux-gnu/bits/floatn.h
flow.o: /usr/include/x86_64-linux-gnu/bits/floatn-common.h
flow.o: /usr/include/stdlib.h /usr/include/x86_64-linux-gnu/bits/waitflags.h
flow.o: /usr/include/x86_64-linux-gnu/bits/waitstatus.h
flow.o: /usr/include/x86_64-linux-gnu/sys/types.h
flow.o: /usr/include/x86_64-linux-gnu/bits/types/clock_t.h
flow.o: /usr/include/x86_64-linux-gnu/bits/types/clockid_t.h
flow.o: /usr/include/x86_64-linux-gnu/bits/types/time_t.h
flow.o: /usr/include/x86_64-linux-gnu/bits/types/timer_t.h
flow.o: /usr/include/x86_64-linux-gnu/bits/stdint-intn.h
flow.o: /usr/include/endian.h /usr/include/x86_64-linux-gnu/bits/endian.h
flow.o: /usr/include/x86_64-linux-gnu/bits/endianness.h
flow.o: /usr/include/x86_64-linux-gnu/bits/byteswap.h
flow.o: /usr/include/x86_64-linux-gnu/bits/uintn-identity.h
flow.o: /usr/include/x86_64-linux-gnu/sys/select.h
flow.o: /usr/include/x86_64-linux-gnu/bits/select.h
flow.o: /usr/include/x86_64-linux-gnu/bits/types/sigset_t.h
flow.o: /usr/include/x86_64-linux-gnu/bits/types/__sigset_t.h
flow.o: /usr/include/x86_64-linux-gnu/bits/types/struct_timeval.h
flow.o: /usr/include/x86_64-linux-gnu/bits/types/struct_timespec.h
flow.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes.h
flow.o: /usr/include/x86_64-linux-gnu/bits/thread-shared-types.h
flow.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes-arch.h
flow.o: /usr/include/x86_64-linux-gnu/bits/atomic_wide_counter.h
flow.o: /usr/include/x86_64-linux-gnu/bits/struct_mutex.h
flow.o: /usr/include/x86_64-linux-gnu/bits/struct_rwlock.h
flow.o: /usr/include/alloca.h
flow.o: /usr/include/x86_64-linux-gnu/bits/stdlib-float.h
flow.o: /usr/include/string.h
flow.o: /usr/include/x86_64-linux-gnu/bits/types/locale_t.h
flow.o: /usr/include/x86_64-linux-gnu/bits/types/__locale_t.h
flow.o: /usr/include/strings.h ../../src/node.h ../../src/data.h
flow.o: ../../src/object.h ../../src/callback.h ../../src/DebugPrint.h
flow.o: ../../src/flow.h
