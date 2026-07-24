CC=gcc

SOURCE= regexp.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=regexp.object

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

regexp.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
regexp.o: /usr/include/features.h /usr/include/features-time64.h
regexp.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
regexp.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
regexp.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
regexp.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
regexp.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
regexp.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
regexp.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
regexp.o: /usr/include/bits/types/__mbstate_t.h
regexp.o: /usr/include/bits/types/__fpos64_t.h
regexp.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
regexp.o: /usr/include/bits/types/struct_FILE.h /usr/include/bits/stdio_lim.h
regexp.o: /usr/include/bits/floatn.h /usr/include/bits/floatn-common.h
regexp.o: /usr/include/stdlib.h /usr/include/bits/waitflags.h
regexp.o: /usr/include/bits/waitstatus.h /usr/include/sys/types.h
regexp.o: /usr/include/bits/types/clock_t.h
regexp.o: /usr/include/bits/types/clockid_t.h
regexp.o: /usr/include/bits/types/time_t.h /usr/include/bits/types/timer_t.h
regexp.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
regexp.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
regexp.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
regexp.o: /usr/include/sys/select.h /usr/include/bits/select.h
regexp.o: /usr/include/bits/types/sigset_t.h
regexp.o: /usr/include/bits/types/__sigset_t.h
regexp.o: /usr/include/bits/types/struct_timeval.h
regexp.o: /usr/include/bits/types/struct_timespec.h
regexp.o: /usr/include/bits/pthreadtypes.h
regexp.o: /usr/include/bits/thread-shared-types.h
regexp.o: /usr/include/bits/pthreadtypes-arch.h
regexp.o: /usr/include/bits/atomic_wide_counter.h
regexp.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
regexp.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
regexp.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
regexp.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
regexp.o: /usr/include/regex.h ../../node.h ../../data.h ../../object.h
regexp.o: ../../sched.h ../../callback.h ../../DebugPrint.h
