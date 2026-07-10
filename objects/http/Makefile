CC=gcc

SOURCE= http.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=http.object

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

http.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
http.o: /usr/include/features.h /usr/include/features-time64.h
http.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
http.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
http.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
http.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
http.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
http.o: /usr/include/bits/types/__mbstate_t.h
http.o: /usr/include/bits/types/__fpos64_t.h /usr/include/bits/types/__FILE.h
http.o: /usr/include/bits/types/FILE.h /usr/include/bits/types/struct_FILE.h
http.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
http.o: /usr/include/bits/floatn-common.h /usr/include/stdlib.h
http.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
http.o: /usr/include/sys/types.h /usr/include/bits/types/clock_t.h
http.o: /usr/include/bits/types/clockid_t.h /usr/include/bits/types/time_t.h
http.o: /usr/include/bits/types/timer_t.h /usr/include/bits/stdint-intn.h
http.o: /usr/include/endian.h /usr/include/bits/endian.h
http.o: /usr/include/bits/endianness.h /usr/include/bits/byteswap.h
http.o: /usr/include/bits/uintn-identity.h /usr/include/sys/select.h
http.o: /usr/include/bits/select.h /usr/include/bits/types/sigset_t.h
http.o: /usr/include/bits/types/__sigset_t.h
http.o: /usr/include/bits/types/struct_timeval.h
http.o: /usr/include/bits/types/struct_timespec.h
http.o: /usr/include/bits/pthreadtypes.h
http.o: /usr/include/bits/thread-shared-types.h
http.o: /usr/include/bits/pthreadtypes-arch.h
http.o: /usr/include/bits/atomic_wide_counter.h
http.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
http.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
http.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
http.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
http.o: ../../node.h ../../data.h ../../object.h ../../sched.h
http.o: ../../callback.h ../../DebugPrint.h
