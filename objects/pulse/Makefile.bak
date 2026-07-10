CC=gcc

SOURCE= pulseobj.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=pulse.object

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

pulseobj.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
pulseobj.o: /usr/include/features.h /usr/include/features-time64.h
pulseobj.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
pulseobj.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
pulseobj.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
pulseobj.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
pulseobj.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
pulseobj.o: /usr/include/bits/types/__mbstate_t.h
pulseobj.o: /usr/include/bits/types/__fpos64_t.h
pulseobj.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
pulseobj.o: /usr/include/bits/types/struct_FILE.h
pulseobj.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
pulseobj.o: /usr/include/bits/floatn-common.h /usr/include/stdlib.h
pulseobj.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
pulseobj.o: /usr/include/sys/types.h /usr/include/bits/types/clock_t.h
pulseobj.o: /usr/include/bits/types/clockid_t.h
pulseobj.o: /usr/include/bits/types/time_t.h
pulseobj.o: /usr/include/bits/types/timer_t.h /usr/include/bits/stdint-intn.h
pulseobj.o: /usr/include/endian.h /usr/include/bits/endian.h
pulseobj.o: /usr/include/bits/endianness.h /usr/include/bits/byteswap.h
pulseobj.o: /usr/include/bits/uintn-identity.h /usr/include/sys/select.h
pulseobj.o: /usr/include/bits/select.h /usr/include/bits/types/sigset_t.h
pulseobj.o: /usr/include/bits/types/__sigset_t.h
pulseobj.o: /usr/include/bits/types/struct_timeval.h
pulseobj.o: /usr/include/bits/types/struct_timespec.h
pulseobj.o: /usr/include/bits/pthreadtypes.h
pulseobj.o: /usr/include/bits/thread-shared-types.h
pulseobj.o: /usr/include/bits/pthreadtypes-arch.h
pulseobj.o: /usr/include/bits/atomic_wide_counter.h
pulseobj.o: /usr/include/bits/struct_mutex.h
pulseobj.o: /usr/include/bits/struct_rwlock.h /usr/include/alloca.h
pulseobj.o: /usr/include/bits/stdlib-float.h /usr/include/string.h
pulseobj.o: /usr/include/bits/types/locale_t.h
pulseobj.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
pulseobj.o: ../../node.h ../../data.h ../../object.h ../../sched.h
pulseobj.o: ../../callback.h ../../DebugPrint.h
