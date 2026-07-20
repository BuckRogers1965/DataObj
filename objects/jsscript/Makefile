CC=gcc

SOURCE= jsscript.c

# vendored QuickJS core (no qjs shell, no std/os libs - the fabric is
# the runtime); compiled with -w: vendor warnings are not ours to fix
QJS_SRC= quickjs/quickjs.c quickjs/libregexp.c quickjs/libunicode.c quickjs/cutils.c quickjs/libbf.c

OBJECTS= $(subst .c,.o,$(SOURCE))
QJS_OBJ= $(subst .c,.o,$(QJS_SRC))

GOAL=jsscript.object

INCLUDES=-I../..

QJS_DEFS=-DCONFIG_VERSION=\"2024-01-13\" -D_GNU_SOURCE

OPT=-Wall -g

%.o : %.c
	$(CC) -fPIC $(OPT) $(INCLUDES) -c $< -o $@

quickjs/%.o : quickjs/%.c
	$(CC) -fPIC -w -O2 $(QJS_DEFS) -c $< -o $@

all: $(OBJECTS) $(QJS_OBJ) $(GOAL)

$(GOAL): $(OBJECTS) $(QJS_OBJ)
	ld  -shared -L../../ -lframework $(OBJECTS) $(QJS_OBJ) -lm -lpthread -o $(GOAL)

clean:
	rm $(OBJECTS) $(QJS_OBJ) $(GOAL) > /dev/null 2>&1

# makedepend understands neither -iquote nor gcc's own header dir,
# so depend uses a plain -I and is told where stddef.h lives
DEPINC=-I../.. -I$(shell $(CC) -print-file-name=include)

depend:
	makedepend $(DEPINC) $(SOURCE)
# DO NOT DELETE

jsscript.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
jsscript.o: /usr/include/features.h /usr/include/features-time64.h
jsscript.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
jsscript.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
jsscript.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
jsscript.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
jsscript.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
jsscript.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
jsscript.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
jsscript.o: /usr/include/bits/types/__mbstate_t.h
jsscript.o: /usr/include/bits/types/__fpos64_t.h
jsscript.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
jsscript.o: /usr/include/bits/types/struct_FILE.h
jsscript.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
jsscript.o: /usr/include/bits/floatn-common.h /usr/include/stdlib.h
jsscript.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
jsscript.o: /usr/include/sys/types.h /usr/include/bits/types/clock_t.h
jsscript.o: /usr/include/bits/types/clockid_t.h
jsscript.o: /usr/include/bits/types/time_t.h
jsscript.o: /usr/include/bits/types/timer_t.h /usr/include/bits/stdint-intn.h
jsscript.o: /usr/include/endian.h /usr/include/bits/endian.h
jsscript.o: /usr/include/bits/endianness.h /usr/include/bits/byteswap.h
jsscript.o: /usr/include/bits/uintn-identity.h /usr/include/sys/select.h
jsscript.o: /usr/include/bits/select.h /usr/include/bits/types/sigset_t.h
jsscript.o: /usr/include/bits/types/__sigset_t.h
jsscript.o: /usr/include/bits/types/struct_timeval.h
jsscript.o: /usr/include/bits/types/struct_timespec.h
jsscript.o: /usr/include/bits/pthreadtypes.h
jsscript.o: /usr/include/bits/thread-shared-types.h
jsscript.o: /usr/include/bits/pthreadtypes-arch.h
jsscript.o: /usr/include/bits/atomic_wide_counter.h
jsscript.o: /usr/include/bits/struct_mutex.h
jsscript.o: /usr/include/bits/struct_rwlock.h /usr/include/alloca.h
jsscript.o: /usr/include/bits/stdlib-float.h /usr/include/string.h
jsscript.o: /usr/include/bits/types/locale_t.h
jsscript.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
jsscript.o: /usr/include/sys/time.h ../../node.h ../../data.h ../../object.h
jsscript.o: ../../sched.h ../../callback.h ../../DebugPrint.h
jsscript.o: quickjs/quickjs.h
jsscript.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdint.h
jsscript.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdint-gcc.h
