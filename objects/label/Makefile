CC=gcc

SOURCE= label.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=label.object

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

label.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
label.o: /usr/include/features.h /usr/include/features-time64.h
label.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
label.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
label.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
label.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
label.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
label.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
label.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
label.o: /usr/include/bits/types/__mbstate_t.h
label.o: /usr/include/bits/types/__fpos64_t.h
label.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
label.o: /usr/include/bits/types/struct_FILE.h /usr/include/bits/stdio_lim.h
label.o: /usr/include/bits/floatn.h /usr/include/bits/floatn-common.h
label.o: /usr/include/stdlib.h /usr/include/bits/waitflags.h
label.o: /usr/include/bits/waitstatus.h /usr/include/sys/types.h
label.o: /usr/include/bits/types/clock_t.h
label.o: /usr/include/bits/types/clockid_t.h /usr/include/bits/types/time_t.h
label.o: /usr/include/bits/types/timer_t.h /usr/include/bits/stdint-intn.h
label.o: /usr/include/endian.h /usr/include/bits/endian.h
label.o: /usr/include/bits/endianness.h /usr/include/bits/byteswap.h
label.o: /usr/include/bits/uintn-identity.h /usr/include/sys/select.h
label.o: /usr/include/bits/select.h /usr/include/bits/types/sigset_t.h
label.o: /usr/include/bits/types/__sigset_t.h
label.o: /usr/include/bits/types/struct_timeval.h
label.o: /usr/include/bits/types/struct_timespec.h
label.o: /usr/include/bits/pthreadtypes.h
label.o: /usr/include/bits/thread-shared-types.h
label.o: /usr/include/bits/pthreadtypes-arch.h
label.o: /usr/include/bits/atomic_wide_counter.h
label.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
label.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
label.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
label.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
label.o: ../../node.h ../../data.h ../../object.h ../../sched.h
label.o: ../../callback.h ../../DebugPrint.h
