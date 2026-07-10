CC=gcc

SOURCE= vumeter.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=vumeter.object

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

vumeter.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
vumeter.o: /usr/include/features.h /usr/include/features-time64.h
vumeter.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
vumeter.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
vumeter.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
vumeter.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
vumeter.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
vumeter.o: /usr/include/bits/types/__mbstate_t.h
vumeter.o: /usr/include/bits/types/__fpos64_t.h
vumeter.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
vumeter.o: /usr/include/bits/types/struct_FILE.h
vumeter.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
vumeter.o: /usr/include/bits/floatn-common.h /usr/include/stdlib.h
vumeter.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
vumeter.o: /usr/include/sys/types.h /usr/include/bits/types/clock_t.h
vumeter.o: /usr/include/bits/types/clockid_t.h
vumeter.o: /usr/include/bits/types/time_t.h /usr/include/bits/types/timer_t.h
vumeter.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
vumeter.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
vumeter.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
vumeter.o: /usr/include/sys/select.h /usr/include/bits/select.h
vumeter.o: /usr/include/bits/types/sigset_t.h
vumeter.o: /usr/include/bits/types/__sigset_t.h
vumeter.o: /usr/include/bits/types/struct_timeval.h
vumeter.o: /usr/include/bits/types/struct_timespec.h
vumeter.o: /usr/include/bits/pthreadtypes.h
vumeter.o: /usr/include/bits/thread-shared-types.h
vumeter.o: /usr/include/bits/pthreadtypes-arch.h
vumeter.o: /usr/include/bits/atomic_wide_counter.h
vumeter.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
vumeter.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
vumeter.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
vumeter.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
vumeter.o: ../../node.h ../../data.h ../../object.h ../../sched.h
vumeter.o: ../../callback.h ../../DebugPrint.h
