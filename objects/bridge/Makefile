CC=gcc

SOURCE= bridge.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=bridge.object

INCLUDES=-I../..

OPT=-Wall -g

%.o : %.c
	$(CC) -fPIC $(OPT) $(INCLUDES) -c $< -o $@

all: $(OBJECTS) $(GOAL)

$(GOAL): $(OBJECTS)
	ld  -shared -L../../ -lframework $(OBJECTS) -o $(GOAL)

clean:
	rm $(OBJECTS) $(GOAL) > /dev/null 2>&1

# makedepend understands neither -iquote nor gcc's own header dir,
# so depend uses a plain -I and is told where stddef.h lives
DEPINC=-I../.. -I$(shell $(CC) -print-file-name=include)

depend:
	makedepend $(DEPINC) $(SOURCE)
# DO NOT DELETE

bridge.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
bridge.o: /usr/include/features.h /usr/include/features-time64.h
bridge.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
bridge.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
bridge.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
bridge.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
bridge.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
bridge.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
bridge.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
bridge.o: /usr/include/bits/types/__mbstate_t.h
bridge.o: /usr/include/bits/types/__fpos64_t.h
bridge.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
bridge.o: /usr/include/bits/types/struct_FILE.h /usr/include/bits/stdio_lim.h
bridge.o: /usr/include/bits/floatn.h /usr/include/bits/floatn-common.h
bridge.o: /usr/include/stdlib.h /usr/include/bits/waitflags.h
bridge.o: /usr/include/bits/waitstatus.h /usr/include/sys/types.h
bridge.o: /usr/include/bits/types/clock_t.h
bridge.o: /usr/include/bits/types/clockid_t.h
bridge.o: /usr/include/bits/types/time_t.h /usr/include/bits/types/timer_t.h
bridge.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
bridge.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
bridge.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
bridge.o: /usr/include/sys/select.h /usr/include/bits/select.h
bridge.o: /usr/include/bits/types/sigset_t.h
bridge.o: /usr/include/bits/types/__sigset_t.h
bridge.o: /usr/include/bits/types/struct_timeval.h
bridge.o: /usr/include/bits/types/struct_timespec.h
bridge.o: /usr/include/bits/pthreadtypes.h
bridge.o: /usr/include/bits/thread-shared-types.h
bridge.o: /usr/include/bits/pthreadtypes-arch.h
bridge.o: /usr/include/bits/atomic_wide_counter.h
bridge.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
bridge.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
bridge.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
bridge.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
bridge.o: /usr/include/dirent.h /usr/include/bits/dirent.h
bridge.o: /usr/include/bits/posix1_lim.h /usr/include/bits/local_lim.h
bridge.o: /usr/include/linux/limits.h
bridge.o: /usr/include/bits/pthread_stack_min-dynamic.h
bridge.o: /usr/include/bits/pthread_stack_min.h
bridge.o: /usr/include/bits/dirent_ext.h /usr/include/sys/stat.h
bridge.o: /usr/include/bits/stat.h /usr/include/bits/struct_stat.h
bridge.o: ../../node.h ../../data.h ../../object.h ../../sched.h
bridge.o: ../../callback.h ../../DebugPrint.h
