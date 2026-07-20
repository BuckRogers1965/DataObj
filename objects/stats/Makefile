CC=gcc

SOURCE= stats.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=stats.object

INCLUDES=-I../.. -I../../dyn

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
DEPINC=-I../.. -I../../dyn -I$(shell $(CC) -print-file-name=include)

depend:
	makedepend $(DEPINC) $(SOURCE)
# DO NOT DELETE

stats.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
stats.o: /usr/include/features.h /usr/include/features-time64.h
stats.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
stats.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
stats.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
stats.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
stats.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
stats.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
stats.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
stats.o: /usr/include/bits/types/__mbstate_t.h
stats.o: /usr/include/bits/types/__fpos64_t.h
stats.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
stats.o: /usr/include/bits/types/struct_FILE.h /usr/include/bits/stdio_lim.h
stats.o: /usr/include/bits/floatn.h /usr/include/bits/floatn-common.h
stats.o: /usr/include/stdlib.h /usr/include/bits/waitflags.h
stats.o: /usr/include/bits/waitstatus.h /usr/include/sys/types.h
stats.o: /usr/include/bits/types/clock_t.h
stats.o: /usr/include/bits/types/clockid_t.h /usr/include/bits/types/time_t.h
stats.o: /usr/include/bits/types/timer_t.h /usr/include/bits/stdint-intn.h
stats.o: /usr/include/endian.h /usr/include/bits/endian.h
stats.o: /usr/include/bits/endianness.h /usr/include/bits/byteswap.h
stats.o: /usr/include/bits/uintn-identity.h /usr/include/sys/select.h
stats.o: /usr/include/bits/select.h /usr/include/bits/types/sigset_t.h
stats.o: /usr/include/bits/types/__sigset_t.h
stats.o: /usr/include/bits/types/struct_timeval.h
stats.o: /usr/include/bits/types/struct_timespec.h
stats.o: /usr/include/bits/pthreadtypes.h
stats.o: /usr/include/bits/thread-shared-types.h
stats.o: /usr/include/bits/pthreadtypes-arch.h
stats.o: /usr/include/bits/atomic_wide_counter.h
stats.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
stats.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
stats.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
stats.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
stats.o: ../../node.h ../../data.h ../../object.h ../../sched.h
stats.o: ../../callback.h ../../dyn/buff.h ../../dyn/queue.h
stats.o: ../../DebugPrint.h
