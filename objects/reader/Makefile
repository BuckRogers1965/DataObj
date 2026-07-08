# Generated automatically from Makefile.in by configure.
CC=gcc 

SOURCE= $(wildcard *.c) 

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=reader.object

INCLUDES=-I../..

OPT=-Wall -ggdb

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

reader.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
reader.o: /usr/include/features.h /usr/include/features-time64.h
reader.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
reader.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
reader.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
reader.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
reader.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
reader.o: /usr/include/bits/types/__mbstate_t.h
reader.o: /usr/include/bits/types/__fpos64_t.h
reader.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
reader.o: /usr/include/bits/types/struct_FILE.h /usr/include/bits/stdio_lim.h
reader.o: /usr/include/bits/floatn.h /usr/include/bits/floatn-common.h
reader.o: /usr/include/fcntl.h /usr/include/bits/fcntl.h
reader.o: /usr/include/bits/fcntl-linux.h
reader.o: /usr/include/bits/types/struct_timespec.h
reader.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
reader.o: /usr/include/bits/types/time_t.h /usr/include/bits/stat.h
reader.o: /usr/include/bits/struct_stat.h /usr/include/string.h
reader.o: /usr/include/bits/types/locale_t.h
reader.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
reader.o: /usr/include/stdlib.h /usr/include/bits/waitflags.h
reader.o: /usr/include/bits/waitstatus.h /usr/include/sys/types.h
reader.o: /usr/include/bits/types/clock_t.h
reader.o: /usr/include/bits/types/clockid_t.h
reader.o: /usr/include/bits/types/timer_t.h /usr/include/bits/stdint-intn.h
reader.o: /usr/include/endian.h /usr/include/bits/byteswap.h
reader.o: /usr/include/bits/uintn-identity.h /usr/include/sys/select.h
reader.o: /usr/include/bits/select.h /usr/include/bits/types/sigset_t.h
reader.o: /usr/include/bits/types/__sigset_t.h
reader.o: /usr/include/bits/types/struct_timeval.h
reader.o: /usr/include/bits/pthreadtypes.h
reader.o: /usr/include/bits/thread-shared-types.h
reader.o: /usr/include/bits/pthreadtypes-arch.h
reader.o: /usr/include/bits/atomic_wide_counter.h
reader.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
reader.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
reader.o: ../../framework.h ../../data.h ../../deamon.h ../../DebugPrint.h
reader.o: ../../dirscan.h ../../dyn/buff.h ../../dyn/bufftest.h
reader.o: ../../dyn/queue.h ../../libload.h ../../list.h ../../node.h
reader.o: ../../object.h ../../sched.h ../../callback.h ../../timer.h
reader.o: /usr/include/time.h /usr/include/bits/time.h
reader.o: /usr/include/bits/types/struct_tm.h
reader.o: /usr/include/bits/types/struct_itimerspec.h
