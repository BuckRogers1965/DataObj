CC=gcc

SOURCE= view.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=view.object

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

view.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
view.o: /usr/include/features.h /usr/include/features-time64.h
view.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
view.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
view.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
view.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
view.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
view.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
view.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
view.o: /usr/include/bits/types/__mbstate_t.h
view.o: /usr/include/bits/types/__fpos64_t.h /usr/include/bits/types/__FILE.h
view.o: /usr/include/bits/types/FILE.h /usr/include/bits/types/struct_FILE.h
view.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
view.o: /usr/include/bits/floatn-common.h /usr/include/stdlib.h
view.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
view.o: /usr/include/sys/types.h /usr/include/bits/types/clock_t.h
view.o: /usr/include/bits/types/clockid_t.h /usr/include/bits/types/time_t.h
view.o: /usr/include/bits/types/timer_t.h /usr/include/bits/stdint-intn.h
view.o: /usr/include/endian.h /usr/include/bits/endian.h
view.o: /usr/include/bits/endianness.h /usr/include/bits/byteswap.h
view.o: /usr/include/bits/uintn-identity.h /usr/include/sys/select.h
view.o: /usr/include/bits/select.h /usr/include/bits/types/sigset_t.h
view.o: /usr/include/bits/types/__sigset_t.h
view.o: /usr/include/bits/types/struct_timeval.h
view.o: /usr/include/bits/types/struct_timespec.h
view.o: /usr/include/bits/pthreadtypes.h
view.o: /usr/include/bits/thread-shared-types.h
view.o: /usr/include/bits/pthreadtypes-arch.h
view.o: /usr/include/bits/atomic_wide_counter.h
view.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
view.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
view.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
view.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
view.o: ../../node.h ../../data.h ../../object.h ../../sched.h
view.o: ../../callback.h ../../DebugPrint.h
