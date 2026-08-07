# Generated automatically from Makefile.in by configure.
CC=gcc 

SOURCE= $(wildcard *.c) 

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=serializer.object

INCLUDES=-I../../src

OPT=-Wall -ggdb

%.o : %.c
	$(CC) -fPIC $(OPT) $(INCLUDES) -c $< -o $@

all: $(OBJECTS) $(GOAL)

$(GOAL): $(OBJECTS)
	ld  -shared $(OBJECTS) -o $(GOAL)

clean:
	rm -f $(OBJECTS) $(GOAL) > /dev/null 2>&1

# makedepend understands neither -iquote nor gcc's own header dir,
# so depend uses a plain -I and is told where stddef.h lives
DEPINC=-I../../src -I$(shell $(CC) -print-file-name=include)

depend:
	makedepend $(DEPINC) $(SOURCE)

# DO NOT DELETE

serializer.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
serializer.o: /usr/include/features.h /usr/include/features-time64.h
serializer.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
serializer.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
serializer.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
serializer.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
serializer.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
serializer.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
serializer.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
serializer.o: /usr/include/bits/types/__mbstate_t.h
serializer.o: /usr/include/bits/types/__fpos64_t.h
serializer.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
serializer.o: /usr/include/bits/types/struct_FILE.h
serializer.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
serializer.o: /usr/include/bits/floatn-common.h /usr/include/stdlib.h
serializer.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
serializer.o: /usr/include/sys/types.h /usr/include/bits/types/clock_t.h
serializer.o: /usr/include/bits/types/clockid_t.h
serializer.o: /usr/include/bits/types/time_t.h
serializer.o: /usr/include/bits/types/timer_t.h
serializer.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
serializer.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
serializer.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
serializer.o: /usr/include/sys/select.h /usr/include/bits/select.h
serializer.o: /usr/include/bits/types/sigset_t.h
serializer.o: /usr/include/bits/types/__sigset_t.h
serializer.o: /usr/include/bits/types/struct_timeval.h
serializer.o: /usr/include/bits/types/struct_timespec.h
serializer.o: /usr/include/bits/pthreadtypes.h
serializer.o: /usr/include/bits/thread-shared-types.h
serializer.o: /usr/include/bits/pthreadtypes-arch.h
serializer.o: /usr/include/bits/atomic_wide_counter.h
serializer.o: /usr/include/bits/struct_mutex.h
serializer.o: /usr/include/bits/struct_rwlock.h /usr/include/alloca.h
serializer.o: /usr/include/bits/stdlib-float.h /usr/include/string.h
serializer.o: /usr/include/bits/types/locale_t.h
serializer.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
serializer.o: ../../src/node.h ../../src/data.h ../../src/object.h
serializer.o: ../../src/sched.h ../../src/callback.h ../../src/DebugPrint.h
serializer.o: ../../src/widget.h ../../src/control.h ../../src/serializer.h
