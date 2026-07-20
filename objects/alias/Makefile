CC=gcc

SOURCE= alias.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=alias.object

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

alias.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
alias.o: /usr/include/features.h /usr/include/features-time64.h
alias.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
alias.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
alias.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
alias.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
alias.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
alias.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
alias.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
alias.o: /usr/include/bits/types/__mbstate_t.h
alias.o: /usr/include/bits/types/__fpos64_t.h
alias.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
alias.o: /usr/include/bits/types/struct_FILE.h /usr/include/bits/stdio_lim.h
alias.o: /usr/include/bits/floatn.h /usr/include/bits/floatn-common.h
alias.o: /usr/include/stdlib.h /usr/include/bits/waitflags.h
alias.o: /usr/include/bits/waitstatus.h /usr/include/sys/types.h
alias.o: /usr/include/bits/types/clock_t.h
alias.o: /usr/include/bits/types/clockid_t.h /usr/include/bits/types/time_t.h
alias.o: /usr/include/bits/types/timer_t.h /usr/include/bits/stdint-intn.h
alias.o: /usr/include/endian.h /usr/include/bits/endian.h
alias.o: /usr/include/bits/endianness.h /usr/include/bits/byteswap.h
alias.o: /usr/include/bits/uintn-identity.h /usr/include/sys/select.h
alias.o: /usr/include/bits/select.h /usr/include/bits/types/sigset_t.h
alias.o: /usr/include/bits/types/__sigset_t.h
alias.o: /usr/include/bits/types/struct_timeval.h
alias.o: /usr/include/bits/types/struct_timespec.h
alias.o: /usr/include/bits/pthreadtypes.h
alias.o: /usr/include/bits/thread-shared-types.h
alias.o: /usr/include/bits/pthreadtypes-arch.h
alias.o: /usr/include/bits/atomic_wide_counter.h
alias.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
alias.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
alias.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
alias.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
alias.o: ../../node.h ../../data.h ../../object.h ../../sched.h
alias.o: ../../callback.h ../../DebugPrint.h
