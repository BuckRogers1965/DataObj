CC=gcc

# lua.c plus the vendored Lua 5.4.7 core (lua/, MIT license) - one
# self-contained .object, no system Lua needed
SOURCE= lua.c
LUASRC= $(wildcard lua/*.c)

OBJECTS= $(subst .c,.o,$(SOURCE)) $(subst .c,.o,$(LUASRC))

GOAL=lua.object

INCLUDES=-I../../src

OPT=-Wall -g

%.o : %.c
	$(CC) -fPIC $(OPT) $(INCLUDES) -c $< -o $@

all: $(OBJECTS) $(GOAL)

$(GOAL): $(OBJECTS)
	ld  -shared $(OBJECTS) -lm -o $(GOAL)

clean:
	rm -f $(OBJECTS) $(GOAL) > /dev/null 2>&1

# makedepend understands neither -iquote nor gcc's own header dir,
# so depend uses a plain -I and is told where stddef.h lives
DEPINC=-I../../src -I$(shell $(CC) -print-file-name=include)

depend:
	makedepend $(DEPINC) $(SOURCE)
# DO NOT DELETE

lua.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
lua.o: /usr/include/features.h /usr/include/features-time64.h
lua.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
lua.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
lua.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
lua.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
lua.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
lua.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
lua.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
lua.o: /usr/include/bits/types/__mbstate_t.h
lua.o: /usr/include/bits/types/__fpos64_t.h /usr/include/bits/types/__FILE.h
lua.o: /usr/include/bits/types/FILE.h /usr/include/bits/types/struct_FILE.h
lua.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
lua.o: /usr/include/bits/floatn-common.h /usr/include/stdlib.h
lua.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
lua.o: /usr/include/sys/types.h /usr/include/bits/types/clock_t.h
lua.o: /usr/include/bits/types/clockid_t.h /usr/include/bits/types/time_t.h
lua.o: /usr/include/bits/types/timer_t.h /usr/include/bits/stdint-intn.h
lua.o: /usr/include/endian.h /usr/include/bits/endian.h
lua.o: /usr/include/bits/endianness.h /usr/include/bits/byteswap.h
lua.o: /usr/include/bits/uintn-identity.h /usr/include/sys/select.h
lua.o: /usr/include/bits/select.h /usr/include/bits/types/sigset_t.h
lua.o: /usr/include/bits/types/__sigset_t.h
lua.o: /usr/include/bits/types/struct_timeval.h
lua.o: /usr/include/bits/types/struct_timespec.h
lua.o: /usr/include/bits/pthreadtypes.h
lua.o: /usr/include/bits/thread-shared-types.h
lua.o: /usr/include/bits/pthreadtypes-arch.h
lua.o: /usr/include/bits/atomic_wide_counter.h
lua.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
lua.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
lua.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
lua.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
lua.o: ../../src/node.h ../../src/data.h ../../src/object.h ../../src/sched.h
lua.o: ../../src/callback.h ../../src/DebugPrint.h ../../src/script.h
lua.o: lua/lua.h lua/luaconf.h
lua.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/limits.h
lua.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/syslimits.h
lua.o: /usr/include/limits.h /usr/include/bits/posix1_lim.h
lua.o: /usr/include/bits/local_lim.h /usr/include/linux/limits.h
lua.o: /usr/include/bits/pthread_stack_min-dynamic.h
lua.o: /usr/include/bits/pthread_stack_min.h /usr/include/bits/posix2_lim.h
lua.o: lua/lauxlib.h lua/lua.h lua/lualib.h ../../src/control.h
