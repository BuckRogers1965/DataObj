CC=gcc

SOURCE= tplink.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=tplink.object

INCLUDES=-I../../src

OPT=-Wall -g

%.o : %.c
	$(CC) -fPIC $(OPT) $(INCLUDES) -c $< -o $@

all: $(OBJECTS) $(GOAL)

$(GOAL): $(OBJECTS)
	ld  -shared $(OBJECTS) -o $(GOAL)

clean:
	rm -f $(OBJECTS) $(GOAL) > /dev/null 2>&1

# makedepend understands neither -iquote nor gcc's own header dir,
# so depend uses a plain -I and is told where stddef.h lives
DEPINC=-I../../src -I$(shell $(CC) -print-file-name=include)

depend:
	makedepend $(DEPINC) $(SOURCE)
# DO NOT DELETE

tplink.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
tplink.o: /usr/include/features.h /usr/include/features-time64.h
tplink.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
tplink.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
tplink.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
tplink.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
tplink.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
tplink.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
tplink.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
tplink.o: /usr/include/bits/types/__mbstate_t.h
tplink.o: /usr/include/bits/types/__fpos64_t.h
tplink.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
tplink.o: /usr/include/bits/types/struct_FILE.h /usr/include/bits/stdio_lim.h
tplink.o: /usr/include/bits/floatn.h /usr/include/bits/floatn-common.h
tplink.o: /usr/include/stdlib.h /usr/include/bits/waitflags.h
tplink.o: /usr/include/bits/waitstatus.h /usr/include/sys/types.h
tplink.o: /usr/include/bits/types/clock_t.h
tplink.o: /usr/include/bits/types/clockid_t.h
tplink.o: /usr/include/bits/types/time_t.h /usr/include/bits/types/timer_t.h
tplink.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
tplink.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
tplink.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
tplink.o: /usr/include/sys/select.h /usr/include/bits/select.h
tplink.o: /usr/include/bits/types/sigset_t.h
tplink.o: /usr/include/bits/types/__sigset_t.h
tplink.o: /usr/include/bits/types/struct_timeval.h
tplink.o: /usr/include/bits/types/struct_timespec.h
tplink.o: /usr/include/bits/pthreadtypes.h
tplink.o: /usr/include/bits/thread-shared-types.h
tplink.o: /usr/include/bits/pthreadtypes-arch.h
tplink.o: /usr/include/bits/atomic_wide_counter.h
tplink.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
tplink.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
tplink.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
tplink.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
tplink.o: ../../src/node.h ../../src/data.h ../../src/object.h
tplink.o: ../../src/sched.h ../../src/callback.h ../../src/DebugPrint.h
tplink.o: ../../src/widget.h ../../src/control.h
