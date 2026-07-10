CC=gcc

SOURCE= textbox.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=textbox.object

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

textbox.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
textbox.o: /usr/include/features.h /usr/include/features-time64.h
textbox.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
textbox.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
textbox.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
textbox.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
textbox.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
textbox.o: /usr/include/bits/types/__mbstate_t.h
textbox.o: /usr/include/bits/types/__fpos64_t.h
textbox.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
textbox.o: /usr/include/bits/types/struct_FILE.h
textbox.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
textbox.o: /usr/include/bits/floatn-common.h /usr/include/stdlib.h
textbox.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
textbox.o: /usr/include/sys/types.h /usr/include/bits/types/clock_t.h
textbox.o: /usr/include/bits/types/clockid_t.h
textbox.o: /usr/include/bits/types/time_t.h /usr/include/bits/types/timer_t.h
textbox.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
textbox.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
textbox.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
textbox.o: /usr/include/sys/select.h /usr/include/bits/select.h
textbox.o: /usr/include/bits/types/sigset_t.h
textbox.o: /usr/include/bits/types/__sigset_t.h
textbox.o: /usr/include/bits/types/struct_timeval.h
textbox.o: /usr/include/bits/types/struct_timespec.h
textbox.o: /usr/include/bits/pthreadtypes.h
textbox.o: /usr/include/bits/thread-shared-types.h
textbox.o: /usr/include/bits/pthreadtypes-arch.h
textbox.o: /usr/include/bits/atomic_wide_counter.h
textbox.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
textbox.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
textbox.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
textbox.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
textbox.o: ../../node.h ../../data.h ../../object.h ../../sched.h
textbox.o: ../../callback.h ../../DebugPrint.h
