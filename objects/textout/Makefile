CC=gcc

SOURCE= textout.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=textout.object

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

textout.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
textout.o: /usr/include/features.h /usr/include/features-time64.h
textout.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
textout.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
textout.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
textout.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
textout.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
textout.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
textout.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
textout.o: /usr/include/bits/types/__mbstate_t.h
textout.o: /usr/include/bits/types/__fpos64_t.h
textout.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
textout.o: /usr/include/bits/types/struct_FILE.h
textout.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
textout.o: /usr/include/bits/floatn-common.h /usr/include/stdlib.h
textout.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
textout.o: /usr/include/sys/types.h /usr/include/bits/types/clock_t.h
textout.o: /usr/include/bits/types/clockid_t.h
textout.o: /usr/include/bits/types/time_t.h /usr/include/bits/types/timer_t.h
textout.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
textout.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
textout.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
textout.o: /usr/include/sys/select.h /usr/include/bits/select.h
textout.o: /usr/include/bits/types/sigset_t.h
textout.o: /usr/include/bits/types/__sigset_t.h
textout.o: /usr/include/bits/types/struct_timeval.h
textout.o: /usr/include/bits/types/struct_timespec.h
textout.o: /usr/include/bits/pthreadtypes.h
textout.o: /usr/include/bits/thread-shared-types.h
textout.o: /usr/include/bits/pthreadtypes-arch.h
textout.o: /usr/include/bits/atomic_wide_counter.h
textout.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
textout.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
textout.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
textout.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
textout.o: ../../node.h ../../data.h ../../object.h ../../sched.h
textout.o: ../../callback.h ../../DebugPrint.h
