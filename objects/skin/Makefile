CC=gcc

SOURCE= skin.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=skin.object

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

skin.o: /usr/include/stdio.h
skin.o: /usr/include/x86_64-linux-gnu/bits/libc-header-start.h
skin.o: /usr/include/features.h /usr/include/features-time64.h
skin.o: /usr/include/x86_64-linux-gnu/bits/wordsize.h
skin.o: /usr/include/x86_64-linux-gnu/bits/timesize.h
skin.o: /usr/include/stdc-predef.h /usr/include/x86_64-linux-gnu/sys/cdefs.h
skin.o: /usr/include/x86_64-linux-gnu/bits/long-double.h
skin.o: /usr/include/x86_64-linux-gnu/gnu/stubs.h
skin.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
skin.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
skin.o: /usr/include/x86_64-linux-gnu/bits/types.h
skin.o: /usr/include/x86_64-linux-gnu/bits/typesizes.h
skin.o: /usr/include/x86_64-linux-gnu/bits/time64.h
skin.o: /usr/include/x86_64-linux-gnu/bits/types/__fpos_t.h
skin.o: /usr/include/x86_64-linux-gnu/bits/types/__mbstate_t.h
skin.o: /usr/include/x86_64-linux-gnu/bits/types/__fpos64_t.h
skin.o: /usr/include/x86_64-linux-gnu/bits/types/__FILE.h
skin.o: /usr/include/x86_64-linux-gnu/bits/types/FILE.h
skin.o: /usr/include/x86_64-linux-gnu/bits/types/struct_FILE.h
skin.o: /usr/include/x86_64-linux-gnu/bits/stdio_lim.h
skin.o: /usr/include/x86_64-linux-gnu/bits/floatn.h
skin.o: /usr/include/x86_64-linux-gnu/bits/floatn-common.h
skin.o: /usr/include/stdlib.h /usr/include/x86_64-linux-gnu/bits/waitflags.h
skin.o: /usr/include/x86_64-linux-gnu/bits/waitstatus.h
skin.o: /usr/include/x86_64-linux-gnu/sys/types.h
skin.o: /usr/include/x86_64-linux-gnu/bits/types/clock_t.h
skin.o: /usr/include/x86_64-linux-gnu/bits/types/clockid_t.h
skin.o: /usr/include/x86_64-linux-gnu/bits/types/time_t.h
skin.o: /usr/include/x86_64-linux-gnu/bits/types/timer_t.h
skin.o: /usr/include/x86_64-linux-gnu/bits/stdint-intn.h
skin.o: /usr/include/endian.h /usr/include/x86_64-linux-gnu/bits/endian.h
skin.o: /usr/include/x86_64-linux-gnu/bits/endianness.h
skin.o: /usr/include/x86_64-linux-gnu/bits/byteswap.h
skin.o: /usr/include/x86_64-linux-gnu/bits/uintn-identity.h
skin.o: /usr/include/x86_64-linux-gnu/sys/select.h
skin.o: /usr/include/x86_64-linux-gnu/bits/select.h
skin.o: /usr/include/x86_64-linux-gnu/bits/types/sigset_t.h
skin.o: /usr/include/x86_64-linux-gnu/bits/types/__sigset_t.h
skin.o: /usr/include/x86_64-linux-gnu/bits/types/struct_timeval.h
skin.o: /usr/include/x86_64-linux-gnu/bits/types/struct_timespec.h
skin.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes.h
skin.o: /usr/include/x86_64-linux-gnu/bits/thread-shared-types.h
skin.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes-arch.h
skin.o: /usr/include/x86_64-linux-gnu/bits/atomic_wide_counter.h
skin.o: /usr/include/x86_64-linux-gnu/bits/struct_mutex.h
skin.o: /usr/include/x86_64-linux-gnu/bits/struct_rwlock.h
skin.o: /usr/include/alloca.h
skin.o: /usr/include/x86_64-linux-gnu/bits/stdlib-float.h
skin.o: /usr/include/string.h
skin.o: /usr/include/x86_64-linux-gnu/bits/types/locale_t.h
skin.o: /usr/include/x86_64-linux-gnu/bits/types/__locale_t.h
skin.o: /usr/include/strings.h ../../src/node.h ../../src/data.h
skin.o: ../../src/object.h ../../src/callback.h ../../src/DebugPrint.h
skin.o: ../../src/skin.h
