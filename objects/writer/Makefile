# Generated automatically from Makefile.in by configure.
CC=gcc 

SOURCE= $(wildcard *.c) 

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=writer.object

INCLUDES=-I../..

OPT=-Wall -g

%.o : %.c
	$(CC) -fPIC $(OPT) $(INCLUDES) -c $< -o $@

all: $(OBJECTS) $(GOAL)

$(GOAL): $(OBJECTS)
	ld  -shared -L../../ -lframework $(OBJECTS) -o $(GOAL)

clean:
	rm $(OBJECTS) $(GOAL) > /dev/null 2>&1

depend:
	makedepend $(INCLUDES) $(SOURCE) > /dev/null 2>&1

# DO NOT DELETE

writer.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
writer.o: /usr/include/features.h /usr/include/features-time64.h
writer.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
writer.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
writer.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
writer.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
writer.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
writer.o: /usr/include/bits/types/__mbstate_t.h
writer.o: /usr/include/bits/types/__fpos64_t.h
writer.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
writer.o: /usr/include/bits/types/struct_FILE.h /usr/include/bits/stdio_lim.h
writer.o: /usr/include/bits/floatn.h /usr/include/bits/floatn-common.h
writer.o: /usr/include/stdlib.h /usr/include/bits/waitflags.h
writer.o: /usr/include/bits/waitstatus.h /usr/include/sys/types.h
writer.o: /usr/include/bits/types/clock_t.h
writer.o: /usr/include/bits/types/clockid_t.h
writer.o: /usr/include/bits/types/time_t.h /usr/include/bits/types/timer_t.h
writer.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
writer.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
writer.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
writer.o: /usr/include/sys/select.h /usr/include/bits/select.h
writer.o: /usr/include/bits/types/sigset_t.h
writer.o: /usr/include/bits/types/__sigset_t.h
writer.o: /usr/include/bits/types/struct_timeval.h
writer.o: /usr/include/bits/types/struct_timespec.h
writer.o: /usr/include/bits/pthreadtypes.h
writer.o: /usr/include/bits/thread-shared-types.h
writer.o: /usr/include/bits/pthreadtypes-arch.h
writer.o: /usr/include/bits/atomic_wide_counter.h
writer.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
writer.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
writer.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
writer.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
writer.o: ../../node.h ../../data.h ../../object.h ../../sched.h
writer.o: ../../callback.h ../../DebugPrint.h ../../dyn/buff.h
