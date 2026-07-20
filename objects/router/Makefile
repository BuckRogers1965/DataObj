CC=gcc

SOURCE= router.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=router.object

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

router.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
router.o: /usr/include/features.h /usr/include/features-time64.h
router.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
router.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
router.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
router.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
router.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
router.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
router.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
router.o: /usr/include/bits/types/__mbstate_t.h
router.o: /usr/include/bits/types/__fpos64_t.h
router.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
router.o: /usr/include/bits/types/struct_FILE.h /usr/include/bits/stdio_lim.h
router.o: /usr/include/bits/floatn.h /usr/include/bits/floatn-common.h
router.o: /usr/include/stdlib.h /usr/include/bits/waitflags.h
router.o: /usr/include/bits/waitstatus.h /usr/include/sys/types.h
router.o: /usr/include/bits/types/clock_t.h
router.o: /usr/include/bits/types/clockid_t.h
router.o: /usr/include/bits/types/time_t.h /usr/include/bits/types/timer_t.h
router.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
router.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
router.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
router.o: /usr/include/sys/select.h /usr/include/bits/select.h
router.o: /usr/include/bits/types/sigset_t.h
router.o: /usr/include/bits/types/__sigset_t.h
router.o: /usr/include/bits/types/struct_timeval.h
router.o: /usr/include/bits/types/struct_timespec.h
router.o: /usr/include/bits/pthreadtypes.h
router.o: /usr/include/bits/thread-shared-types.h
router.o: /usr/include/bits/pthreadtypes-arch.h
router.o: /usr/include/bits/atomic_wide_counter.h
router.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
router.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
router.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
router.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
router.o: ../../node.h ../../data.h ../../object.h ../../sched.h
router.o: ../../callback.h ../../DebugPrint.h
