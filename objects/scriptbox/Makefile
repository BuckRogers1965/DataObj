CC=gcc

SOURCE= scriptbox.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=scriptbox.object

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

scriptbox.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
scriptbox.o: /usr/include/features.h /usr/include/features-time64.h
scriptbox.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
scriptbox.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
scriptbox.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
scriptbox.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
scriptbox.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
scriptbox.o: /usr/include/bits/types/__mbstate_t.h
scriptbox.o: /usr/include/bits/types/__fpos64_t.h
scriptbox.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
scriptbox.o: /usr/include/bits/types/struct_FILE.h
scriptbox.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
scriptbox.o: /usr/include/bits/floatn-common.h /usr/include/stdlib.h
scriptbox.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
scriptbox.o: /usr/include/sys/types.h /usr/include/bits/types/clock_t.h
scriptbox.o: /usr/include/bits/types/clockid_t.h
scriptbox.o: /usr/include/bits/types/time_t.h /usr/include/bits/types/timer_t.h
scriptbox.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
scriptbox.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
scriptbox.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
scriptbox.o: /usr/include/sys/select.h /usr/include/bits/select.h
scriptbox.o: /usr/include/bits/types/sigset_t.h
scriptbox.o: /usr/include/bits/types/__sigset_t.h
scriptbox.o: /usr/include/bits/types/struct_timeval.h
scriptbox.o: /usr/include/bits/types/struct_timespec.h
scriptbox.o: /usr/include/bits/pthreadtypes.h
scriptbox.o: /usr/include/bits/thread-shared-types.h
scriptbox.o: /usr/include/bits/pthreadtypes-arch.h
scriptbox.o: /usr/include/bits/atomic_wide_counter.h
scriptbox.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
scriptbox.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
scriptbox.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
scriptbox.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
scriptbox.o: ../../node.h ../../data.h ../../object.h ../../sched.h
scriptbox.o: ../../callback.h ../../DebugPrint.h
