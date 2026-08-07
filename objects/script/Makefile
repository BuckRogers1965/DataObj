CC=gcc

SOURCE= script.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=script.object

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

script.o: /usr/include/stdio.h
script.o: /usr/include/x86_64-linux-gnu/bits/libc-header-start.h
script.o: /usr/include/features.h /usr/include/features-time64.h
script.o: /usr/include/x86_64-linux-gnu/bits/wordsize.h
script.o: /usr/include/x86_64-linux-gnu/bits/timesize.h
script.o: /usr/include/stdc-predef.h
script.o: /usr/include/x86_64-linux-gnu/sys/cdefs.h
script.o: /usr/include/x86_64-linux-gnu/bits/long-double.h
script.o: /usr/include/x86_64-linux-gnu/gnu/stubs.h
script.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
script.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
script.o: /usr/include/x86_64-linux-gnu/bits/types.h
script.o: /usr/include/x86_64-linux-gnu/bits/typesizes.h
script.o: /usr/include/x86_64-linux-gnu/bits/time64.h
script.o: /usr/include/x86_64-linux-gnu/bits/types/__fpos_t.h
script.o: /usr/include/x86_64-linux-gnu/bits/types/__mbstate_t.h
script.o: /usr/include/x86_64-linux-gnu/bits/types/__fpos64_t.h
script.o: /usr/include/x86_64-linux-gnu/bits/types/__FILE.h
script.o: /usr/include/x86_64-linux-gnu/bits/types/FILE.h
script.o: /usr/include/x86_64-linux-gnu/bits/types/struct_FILE.h
script.o: /usr/include/x86_64-linux-gnu/bits/stdio_lim.h
script.o: /usr/include/x86_64-linux-gnu/bits/floatn.h
script.o: /usr/include/x86_64-linux-gnu/bits/floatn-common.h ../../src/node.h
script.o: ../../src/data.h ../../src/object.h ../../src/callback.h
script.o: /usr/include/stdlib.h
script.o: /usr/include/x86_64-linux-gnu/bits/waitflags.h
script.o: /usr/include/x86_64-linux-gnu/bits/waitstatus.h
script.o: /usr/include/x86_64-linux-gnu/sys/types.h
script.o: /usr/include/x86_64-linux-gnu/bits/types/clock_t.h
script.o: /usr/include/x86_64-linux-gnu/bits/types/clockid_t.h
script.o: /usr/include/x86_64-linux-gnu/bits/types/time_t.h
script.o: /usr/include/x86_64-linux-gnu/bits/types/timer_t.h
script.o: /usr/include/x86_64-linux-gnu/bits/stdint-intn.h
script.o: /usr/include/endian.h /usr/include/x86_64-linux-gnu/bits/endian.h
script.o: /usr/include/x86_64-linux-gnu/bits/endianness.h
script.o: /usr/include/x86_64-linux-gnu/bits/byteswap.h
script.o: /usr/include/x86_64-linux-gnu/bits/uintn-identity.h
script.o: /usr/include/x86_64-linux-gnu/sys/select.h
script.o: /usr/include/x86_64-linux-gnu/bits/select.h
script.o: /usr/include/x86_64-linux-gnu/bits/types/sigset_t.h
script.o: /usr/include/x86_64-linux-gnu/bits/types/__sigset_t.h
script.o: /usr/include/x86_64-linux-gnu/bits/types/struct_timeval.h
script.o: /usr/include/x86_64-linux-gnu/bits/types/struct_timespec.h
script.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes.h
script.o: /usr/include/x86_64-linux-gnu/bits/thread-shared-types.h
script.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes-arch.h
script.o: /usr/include/x86_64-linux-gnu/bits/atomic_wide_counter.h
script.o: /usr/include/x86_64-linux-gnu/bits/struct_mutex.h
script.o: /usr/include/x86_64-linux-gnu/bits/struct_rwlock.h
script.o: /usr/include/alloca.h
script.o: /usr/include/x86_64-linux-gnu/bits/stdlib-float.h
script.o: /usr/include/string.h
script.o: /usr/include/x86_64-linux-gnu/bits/types/locale_t.h
script.o: /usr/include/x86_64-linux-gnu/bits/types/__locale_t.h
script.o: /usr/include/strings.h /usr/include/x86_64-linux-gnu/sys/time.h
script.o: ../../src/sched.h ../../src/DebugPrint.h ../../src/script.h
