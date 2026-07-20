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

# makedepend understands neither -iquote nor gcc's own header dir,
# so depend uses a plain -I and is told where stddef.h lives
DEPINC=-I../.. -I$(shell $(CC) -print-file-name=include)

depend:
	makedepend $(DEPINC) $(SOURCE)
# DO NOT DELETE

dropdown.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
dropdown.o: /usr/include/features.h /usr/include/features-time64.h
dropdown.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
dropdown.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
dropdown.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
dropdown.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
dropdown.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
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
dropdown.o: /usr/include/bits/types/time_t.h
dropdown.o: /usr/include/bits/types/timer_t.h /usr/include/bits/stdint-intn.h
dropdown.o: /usr/include/endian.h /usr/include/bits/endian.h
dropdown.o: /usr/include/bits/endianness.h /usr/include/bits/byteswap.h
dropdown.o: /usr/include/bits/uintn-identity.h /usr/include/sys/select.h
dropdown.o: /usr/include/bits/select.h /usr/include/bits/types/sigset_t.h
dropdown.o: /usr/include/bits/types/__sigset_t.h
dropdown.o: /usr/include/bits/types/struct_timeval.h
dropdown.o: /usr/include/bits/types/struct_timespec.h
dropdown.o: /usr/include/bits/pthreadtypes.h
dropdown.o: /usr/include/bits/thread-shared-types.h
dropdown.o: /usr/include/bits/pthreadtypes-arch.h
dropdown.o: /usr/include/bits/atomic_wide_counter.h
dropdown.o: /usr/include/bits/struct_mutex.h
dropdown.o: /usr/include/bits/struct_rwlock.h /usr/include/alloca.h
dropdown.o: /usr/include/bits/stdlib-float.h /usr/include/string.h
dropdown.o: /usr/include/bits/types/locale_t.h
dropdown.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
dropdown.o: ../../node.h ../../data.h ../../object.h ../../sched.h
dropdown.o: ../../callback.h ../../DebugPrint.h
