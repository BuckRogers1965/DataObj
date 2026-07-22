CC=gcc

SOURCE= pulsegenerator.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=pulsegenerator.object

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

pulsegenerator.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
pulsegenerator.o: /usr/include/features.h /usr/include/features-time64.h
pulsegenerator.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
pulsegenerator.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
pulsegenerator.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
pulsegenerator.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
pulsegenerator.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
pulsegenerator.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
pulsegenerator.o: /usr/include/bits/time64.h
pulsegenerator.o: /usr/include/bits/types/__fpos_t.h
pulsegenerator.o: /usr/include/bits/types/__mbstate_t.h
pulsegenerator.o: /usr/include/bits/types/__fpos64_t.h
pulsegenerator.o: /usr/include/bits/types/__FILE.h
pulsegenerator.o: /usr/include/bits/types/FILE.h
pulsegenerator.o: /usr/include/bits/types/struct_FILE.h
pulsegenerator.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
pulsegenerator.o: /usr/include/bits/floatn-common.h /usr/include/stdlib.h
pulsegenerator.o: /usr/include/bits/waitflags.h
pulsegenerator.o: /usr/include/bits/waitstatus.h /usr/include/sys/types.h
pulsegenerator.o: /usr/include/bits/types/clock_t.h
pulsegenerator.o: /usr/include/bits/types/clockid_t.h
pulsegenerator.o: /usr/include/bits/types/time_t.h
pulsegenerator.o: /usr/include/bits/types/timer_t.h
pulsegenerator.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
pulsegenerator.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
pulsegenerator.o: /usr/include/bits/byteswap.h
pulsegenerator.o: /usr/include/bits/uintn-identity.h
pulsegenerator.o: /usr/include/sys/select.h /usr/include/bits/select.h
pulsegenerator.o: /usr/include/bits/types/sigset_t.h
pulsegenerator.o: /usr/include/bits/types/__sigset_t.h
pulsegenerator.o: /usr/include/bits/types/struct_timeval.h
pulsegenerator.o: /usr/include/bits/types/struct_timespec.h
pulsegenerator.o: /usr/include/bits/pthreadtypes.h
pulsegenerator.o: /usr/include/bits/thread-shared-types.h
pulsegenerator.o: /usr/include/bits/pthreadtypes-arch.h
pulsegenerator.o: /usr/include/bits/atomic_wide_counter.h
pulsegenerator.o: /usr/include/bits/struct_mutex.h
pulsegenerator.o: /usr/include/bits/struct_rwlock.h /usr/include/alloca.h
pulsegenerator.o: /usr/include/bits/stdlib-float.h /usr/include/string.h
pulsegenerator.o: /usr/include/bits/types/locale_t.h
pulsegenerator.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
pulsegenerator.o: ../../node.h ../../data.h ../../object.h ../../sched.h
pulsegenerator.o: ../../callback.h ../../DebugPrint.h
