CC=gcc

SOURCE= dropdown.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=dropdown.object

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

dropdown.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
dropdown.o: /usr/include/features.h /usr/include/features-time64.h
dropdown.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
dropdown.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
dropdown.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
dropdown.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
dropdown.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
dropdown.o: /usr/include/bits/types/__mbstate_t.h
dropdown.o: /usr/include/bits/types/__fpos64_t.h
dropdown.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
dropdown.o: /usr/include/bits/types/struct_FILE.h
dropdown.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
dropdown.o: /usr/include/bits/floatn-common.h /usr/include/stdlib.h
dropdown.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
dropdown.o: /usr/include/sys/types.h /usr/include/bits/types/clock_t.h
dropdown.o: /usr/include/bits/types/clockid_t.h
dropdown.o: /usr/include/bits/types/time_t.h /usr/include/bits/types/timer_t.h
dropdown.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
dropdown.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
dropdown.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
dropdown.o: /usr/include/sys/select.h /usr/include/bits/select.h
dropdown.o: /usr/include/bits/types/sigset_t.h
dropdown.o: /usr/include/bits/types/__sigset_t.h
dropdown.o: /usr/include/bits/types/struct_timeval.h
dropdown.o: /usr/include/bits/types/struct_timespec.h
dropdown.o: /usr/include/bits/pthreadtypes.h
dropdown.o: /usr/include/bits/thread-shared-types.h
dropdown.o: /usr/include/bits/pthreadtypes-arch.h
dropdown.o: /usr/include/bits/atomic_wide_counter.h
dropdown.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
dropdown.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
dropdown.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
dropdown.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
dropdown.o: ../../node.h ../../data.h ../../object.h ../../sched.h
dropdown.o: ../../callback.h ../../DebugPrint.h
