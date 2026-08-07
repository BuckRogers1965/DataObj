CC=gcc

SOURCE= widget.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=widget.object

INCLUDES=-iquote../../src

OPT=-Wall -g

%.o : %.c
	$(CC) -fPIC $(OPT) $(INCLUDES) -c $< -o $@

all: $(OBJECTS) $(GOAL)

$(GOAL): $(OBJECTS)
	ld  -shared $(OBJECTS) -o $(GOAL)

clean:
	rm -f $(OBJECTS) $(GOAL) > /dev/null 2>&1

DEPINC=-I../../src -I/usr/include/x86_64-linux-gnu \
       -I$(shell $(CC) -print-file-name=include)

depend:
	makedepend $(DEPINC) $(SOURCE)
# DO NOT DELETE

widget.o: /usr/include/stdio.h
widget.o: /usr/include/x86_64-linux-gnu/bits/libc-header-start.h
widget.o: /usr/include/features.h /usr/include/features-time64.h
widget.o: /usr/include/x86_64-linux-gnu/bits/wordsize.h
widget.o: /usr/include/x86_64-linux-gnu/bits/timesize.h
widget.o: /usr/include/stdc-predef.h
widget.o: /usr/include/x86_64-linux-gnu/sys/cdefs.h
widget.o: /usr/include/x86_64-linux-gnu/bits/long-double.h
widget.o: /usr/include/x86_64-linux-gnu/gnu/stubs.h
widget.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
widget.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
widget.o: /usr/include/x86_64-linux-gnu/bits/types.h
widget.o: /usr/include/x86_64-linux-gnu/bits/typesizes.h
widget.o: /usr/include/x86_64-linux-gnu/bits/time64.h
widget.o: /usr/include/x86_64-linux-gnu/bits/types/__fpos_t.h
widget.o: /usr/include/x86_64-linux-gnu/bits/types/__mbstate_t.h
widget.o: /usr/include/x86_64-linux-gnu/bits/types/__fpos64_t.h
widget.o: /usr/include/x86_64-linux-gnu/bits/types/__FILE.h
widget.o: /usr/include/x86_64-linux-gnu/bits/types/FILE.h
widget.o: /usr/include/x86_64-linux-gnu/bits/types/struct_FILE.h
widget.o: /usr/include/x86_64-linux-gnu/bits/stdio_lim.h
widget.o: /usr/include/x86_64-linux-gnu/bits/floatn.h
widget.o: /usr/include/x86_64-linux-gnu/bits/floatn-common.h
widget.o: /usr/include/stdlib.h
widget.o: /usr/include/x86_64-linux-gnu/bits/waitflags.h
widget.o: /usr/include/x86_64-linux-gnu/bits/waitstatus.h
widget.o: /usr/include/x86_64-linux-gnu/sys/types.h
widget.o: /usr/include/x86_64-linux-gnu/bits/types/clock_t.h
widget.o: /usr/include/x86_64-linux-gnu/bits/types/clockid_t.h
widget.o: /usr/include/x86_64-linux-gnu/bits/types/time_t.h
widget.o: /usr/include/x86_64-linux-gnu/bits/types/timer_t.h
widget.o: /usr/include/x86_64-linux-gnu/bits/stdint-intn.h
widget.o: /usr/include/endian.h /usr/include/x86_64-linux-gnu/bits/endian.h
widget.o: /usr/include/x86_64-linux-gnu/bits/endianness.h
widget.o: /usr/include/x86_64-linux-gnu/bits/byteswap.h
widget.o: /usr/include/x86_64-linux-gnu/bits/uintn-identity.h
widget.o: /usr/include/x86_64-linux-gnu/sys/select.h
widget.o: /usr/include/x86_64-linux-gnu/bits/select.h
widget.o: /usr/include/x86_64-linux-gnu/bits/types/sigset_t.h
widget.o: /usr/include/x86_64-linux-gnu/bits/types/__sigset_t.h
widget.o: /usr/include/x86_64-linux-gnu/bits/types/struct_timeval.h
widget.o: /usr/include/x86_64-linux-gnu/bits/types/struct_timespec.h
widget.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes.h
widget.o: /usr/include/x86_64-linux-gnu/bits/thread-shared-types.h
widget.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes-arch.h
widget.o: /usr/include/x86_64-linux-gnu/bits/atomic_wide_counter.h
widget.o: /usr/include/x86_64-linux-gnu/bits/struct_mutex.h
widget.o: /usr/include/x86_64-linux-gnu/bits/struct_rwlock.h
widget.o: /usr/include/alloca.h
widget.o: /usr/include/x86_64-linux-gnu/bits/stdlib-float.h
widget.o: /usr/include/string.h
widget.o: /usr/include/x86_64-linux-gnu/bits/types/locale_t.h
widget.o: /usr/include/x86_64-linux-gnu/bits/types/__locale_t.h
widget.o: /usr/include/strings.h ../../src/node.h ../../src/data.h
widget.o: ../../src/object.h ../../src/callback.h ../../src/sched.h
widget.o: ../../src/DebugPrint.h ../../src/widget.h ../../src/control.h
