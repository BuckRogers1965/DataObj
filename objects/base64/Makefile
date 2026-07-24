CC=gcc

SOURCE= base64.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=base64.object

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

base64.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
base64.o: /usr/include/features.h /usr/include/features-time64.h
base64.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
base64.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
base64.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
base64.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
base64.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
base64.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
base64.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
base64.o: /usr/include/bits/types/__mbstate_t.h
base64.o: /usr/include/bits/types/__fpos64_t.h
base64.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
base64.o: /usr/include/bits/types/struct_FILE.h /usr/include/bits/stdio_lim.h
base64.o: /usr/include/bits/floatn.h /usr/include/bits/floatn-common.h
base64.o: /usr/include/stdlib.h /usr/include/bits/waitflags.h
base64.o: /usr/include/bits/waitstatus.h /usr/include/sys/types.h
base64.o: /usr/include/bits/types/clock_t.h
base64.o: /usr/include/bits/types/clockid_t.h
base64.o: /usr/include/bits/types/time_t.h /usr/include/bits/types/timer_t.h
base64.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
base64.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
base64.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
base64.o: /usr/include/sys/select.h /usr/include/bits/select.h
base64.o: /usr/include/bits/types/sigset_t.h
base64.o: /usr/include/bits/types/__sigset_t.h
base64.o: /usr/include/bits/types/struct_timeval.h
base64.o: /usr/include/bits/types/struct_timespec.h
base64.o: /usr/include/bits/pthreadtypes.h
base64.o: /usr/include/bits/thread-shared-types.h
base64.o: /usr/include/bits/pthreadtypes-arch.h
base64.o: /usr/include/bits/atomic_wide_counter.h
base64.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
base64.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
base64.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
base64.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
base64.o: ../../node.h ../../data.h ../../object.h ../../sched.h
base64.o: ../../callback.h ../../DebugPrint.h
