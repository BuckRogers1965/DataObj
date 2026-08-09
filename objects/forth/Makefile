CC=gcc

# forth.c plus vendored Atlast 1.2 (atlast/, public domain) - one
# self-contained .object, no system Forth needed
SOURCE= forth.c
ATLSRC= atlast/atlast.c

OBJECTS= $(subst .c,.o,$(SOURCE)) $(subst .c,.o,$(ATLSRC))

GOAL=forth.object

INCLUDES=-I../../src

# EXPORT   - atlast's stack and heap symbols become extern, which is what
#            lets forth.c define primitives in its own translation unit
# NOMANGLE - keep those symbols readable rather than atl__sp and friends
COMMON=-DEXPORT -DNOMANGLE

# Keybreak - atlast's inner loop polls this once per word (it defines BREAK
#            for itself), so pointing it at our budget check is the whole
#            runaway guard and the vendored source stays untouched.
# -include - the only reason for it: give that call a prototype without
#            editing a public-domain file.
# CUSTOM   - make atlast read atlcfig.h for its subpackage list, which is how
#            WORDSUSED gets left out. It writes a flag into the first byte of
#            a word's NAME, and a primitive's name is a string literal, so it
#            faults on the first lookup in atl_init(). See atlcfig.h.
ATLOPT=$(COMMON) -DCUSTOM -DKeybreak=Forth_PollBudget -I. -include forth_hook.h

OPT=-Wall -g

all: $(OBJECTS) $(GOAL)

forth.o: forth.c
	$(CC) -fPIC $(OPT) $(COMMON) $(INCLUDES) -c forth.c -o forth.o

atlast/atlast.o: atlast/atlast.c
	$(CC) -fPIC $(OPT) $(ATLOPT) $(INCLUDES) -c atlast/atlast.c -o atlast/atlast.o

$(GOAL): $(OBJECTS)
	ld  -shared $(OBJECTS) -lm -o $(GOAL)

clean:
	rm -f $(OBJECTS) $(GOAL) > /dev/null 2>&1

DEPINC=-I../../src -I$(shell $(CC) -print-file-name=include)

depend:
	makedepend $(DEPINC) $(SOURCE)
# DO NOT DELETE

forth.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
forth.o: /usr/include/features.h /usr/include/features-time64.h
forth.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
forth.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
forth.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
forth.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
forth.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
forth.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
forth.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
forth.o: /usr/include/bits/types/__mbstate_t.h
forth.o: /usr/include/bits/types/__fpos64_t.h
forth.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
forth.o: /usr/include/bits/types/struct_FILE.h /usr/include/bits/stdio_lim.h
forth.o: /usr/include/bits/floatn.h /usr/include/bits/floatn-common.h
forth.o: /usr/include/stdlib.h /usr/include/bits/waitflags.h
forth.o: /usr/include/bits/waitstatus.h /usr/include/sys/types.h
forth.o: /usr/include/bits/types/clock_t.h
forth.o: /usr/include/bits/types/clockid_t.h /usr/include/bits/types/time_t.h
forth.o: /usr/include/bits/types/timer_t.h /usr/include/bits/stdint-intn.h
forth.o: /usr/include/endian.h /usr/include/bits/endian.h
forth.o: /usr/include/bits/endianness.h /usr/include/bits/byteswap.h
forth.o: /usr/include/bits/uintn-identity.h /usr/include/sys/select.h
forth.o: /usr/include/bits/select.h /usr/include/bits/types/sigset_t.h
forth.o: /usr/include/bits/types/__sigset_t.h
forth.o: /usr/include/bits/types/struct_timeval.h
forth.o: /usr/include/bits/types/struct_timespec.h
forth.o: /usr/include/bits/pthreadtypes.h
forth.o: /usr/include/bits/thread-shared-types.h
forth.o: /usr/include/bits/pthreadtypes-arch.h
forth.o: /usr/include/bits/atomic_wide_counter.h
forth.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
forth.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
forth.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
forth.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
forth.o: ../../src/node.h ../../src/data.h ../../src/object.h
forth.o: ../../src/sched.h ../../src/callback.h ../../src/DebugPrint.h
forth.o: ../../src/script.h atlast/atldef.h atlast/atlast.h atlast/atlast.h
