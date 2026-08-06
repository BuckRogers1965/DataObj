CC=gcc

SOURCE= mcpsource.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=mcpsource.object

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

mcpsource.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
mcpsource.o: /usr/include/features.h /usr/include/features-time64.h
mcpsource.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
mcpsource.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
mcpsource.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
mcpsource.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
mcpsource.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
mcpsource.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
mcpsource.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
mcpsource.o: /usr/include/bits/types/__mbstate_t.h
mcpsource.o: /usr/include/bits/types/__fpos64_t.h
mcpsource.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
mcpsource.o: /usr/include/bits/types/struct_FILE.h
mcpsource.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
mcpsource.o: /usr/include/bits/floatn-common.h /usr/include/stdlib.h
mcpsource.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
mcpsource.o: /usr/include/sys/types.h /usr/include/bits/types/clock_t.h
mcpsource.o: /usr/include/bits/types/clockid_t.h
mcpsource.o: /usr/include/bits/types/time_t.h
mcpsource.o: /usr/include/bits/types/timer_t.h
mcpsource.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
mcpsource.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
mcpsource.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
mcpsource.o: /usr/include/sys/select.h /usr/include/bits/select.h
mcpsource.o: /usr/include/bits/types/sigset_t.h
mcpsource.o: /usr/include/bits/types/__sigset_t.h
mcpsource.o: /usr/include/bits/types/struct_timeval.h
mcpsource.o: /usr/include/bits/types/struct_timespec.h
mcpsource.o: /usr/include/bits/pthreadtypes.h
mcpsource.o: /usr/include/bits/thread-shared-types.h
mcpsource.o: /usr/include/bits/pthreadtypes-arch.h
mcpsource.o: /usr/include/bits/atomic_wide_counter.h
mcpsource.o: /usr/include/bits/struct_mutex.h
mcpsource.o: /usr/include/bits/struct_rwlock.h /usr/include/alloca.h
mcpsource.o: /usr/include/bits/stdlib-float.h /usr/include/string.h
mcpsource.o: /usr/include/bits/types/locale_t.h
mcpsource.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
mcpsource.o: /usr/include/ctype.h /usr/include/sched.h
mcpsource.o: /usr/include/bits/sched.h
mcpsource.o: /usr/include/bits/types/struct_sched_param.h
mcpsource.o: /usr/include/bits/cpu-set.h
