CC=gcc

SOURCE= led.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=led.object

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

led.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
led.o: /usr/include/features.h /usr/include/features-time64.h
led.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
led.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
led.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
led.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
led.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
led.o: /usr/include/bits/types/__mbstate_t.h
led.o: /usr/include/bits/types/__fpos64_t.h /usr/include/bits/types/__FILE.h
led.o: /usr/include/bits/types/FILE.h /usr/include/bits/types/struct_FILE.h
led.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
led.o: /usr/include/bits/floatn-common.h /usr/include/stdlib.h
led.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
led.o: /usr/include/sys/types.h /usr/include/bits/types/clock_t.h
led.o: /usr/include/bits/types/clockid_t.h /usr/include/bits/types/time_t.h
led.o: /usr/include/bits/types/timer_t.h /usr/include/bits/stdint-intn.h
led.o: /usr/include/endian.h /usr/include/bits/endian.h
led.o: /usr/include/bits/endianness.h /usr/include/bits/byteswap.h
led.o: /usr/include/bits/uintn-identity.h /usr/include/sys/select.h
led.o: /usr/include/bits/select.h /usr/include/bits/types/sigset_t.h
led.o: /usr/include/bits/types/__sigset_t.h
led.o: /usr/include/bits/types/struct_timeval.h
led.o: /usr/include/bits/types/struct_timespec.h
led.o: /usr/include/bits/pthreadtypes.h
led.o: /usr/include/bits/thread-shared-types.h
led.o: /usr/include/bits/pthreadtypes-arch.h
led.o: /usr/include/bits/atomic_wide_counter.h
led.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
led.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
led.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
led.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
led.o: ../../node.h ../../data.h ../../object.h ../../sched.h
led.o: ../../callback.h ../../DebugPrint.h
