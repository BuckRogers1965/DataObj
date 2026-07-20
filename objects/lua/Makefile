CC=gcc

# script.c plus the vendored Lua 5.4.7 core (lua/, MIT license) - one
# self-contained .object, no system Lua needed
SOURCE= script.c
LUASRC= $(wildcard lua/*.c)

OBJECTS= $(subst .c,.o,$(SOURCE)) $(subst .c,.o,$(LUASRC))

GOAL=script.object

INCLUDES=-I../..

OPT=-Wall -g

%.o : %.c
	$(CC) -fPIC $(OPT) $(INCLUDES) -c $< -o $@

all: $(OBJECTS) $(GOAL)

$(GOAL): $(OBJECTS)
	ld  -shared -L../../ -lframework $(OBJECTS) -lm -o $(GOAL)

clean:
	rm $(OBJECTS) $(GOAL) > /dev/null 2>&1

# makedepend understands neither -iquote nor gcc's own header dir,
# so depend uses a plain -I and is told where stddef.h lives
DEPINC=-I../.. -I$(shell $(CC) -print-file-name=include)

depend:
	makedepend $(DEPINC) $(SOURCE)
# DO NOT DELETE

script.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
script.o: /usr/include/features.h /usr/include/features-time64.h
script.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
script.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
script.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
script.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
script.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
script.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
script.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
script.o: /usr/include/bits/types/__mbstate_t.h
script.o: /usr/include/bits/types/__fpos64_t.h
script.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
script.o: /usr/include/bits/types/struct_FILE.h /usr/include/bits/stdio_lim.h
script.o: /usr/include/bits/floatn.h /usr/include/bits/floatn-common.h
script.o: /usr/include/stdlib.h /usr/include/bits/waitflags.h
script.o: /usr/include/bits/waitstatus.h /usr/include/sys/types.h
script.o: /usr/include/bits/types/clock_t.h
script.o: /usr/include/bits/types/clockid_t.h
script.o: /usr/include/bits/types/time_t.h /usr/include/bits/types/timer_t.h
script.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
script.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
script.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
script.o: /usr/include/sys/select.h /usr/include/bits/select.h
script.o: /usr/include/bits/types/sigset_t.h
script.o: /usr/include/bits/types/__sigset_t.h
script.o: /usr/include/bits/types/struct_timeval.h
script.o: /usr/include/bits/types/struct_timespec.h
script.o: /usr/include/bits/pthreadtypes.h
script.o: /usr/include/bits/thread-shared-types.h
script.o: /usr/include/bits/pthreadtypes-arch.h
script.o: /usr/include/bits/atomic_wide_counter.h
script.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
script.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
script.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
script.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
script.o: ../../node.h ../../data.h ../../object.h ../../sched.h
script.o: ../../callback.h ../../DebugPrint.h lua/lua.h lua/luaconf.h
script.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/limits.h
script.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/syslimits.h
script.o: /usr/include/limits.h /usr/include/bits/posix1_lim.h
script.o: /usr/include/bits/local_lim.h /usr/include/linux/limits.h
script.o: /usr/include/bits/pthread_stack_min-dynamic.h
script.o: /usr/include/bits/pthread_stack_min.h
script.o: /usr/include/bits/posix2_lim.h lua/lauxlib.h lua/lua.h lua/lualib.h
