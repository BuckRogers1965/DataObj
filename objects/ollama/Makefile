CC=gcc

SOURCE= ollama.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=ollama.object

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

ollama.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
ollama.o: /usr/include/features.h /usr/include/features-time64.h
ollama.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
ollama.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
ollama.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
ollama.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
ollama.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
ollama.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
ollama.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
ollama.o: /usr/include/bits/types/__mbstate_t.h
ollama.o: /usr/include/bits/types/__fpos64_t.h
ollama.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
ollama.o: /usr/include/bits/types/struct_FILE.h /usr/include/bits/stdio_lim.h
ollama.o: /usr/include/bits/floatn.h /usr/include/bits/floatn-common.h
ollama.o: /usr/include/stdlib.h /usr/include/bits/waitflags.h
ollama.o: /usr/include/bits/waitstatus.h /usr/include/sys/types.h
ollama.o: /usr/include/bits/types/clock_t.h
ollama.o: /usr/include/bits/types/clockid_t.h
ollama.o: /usr/include/bits/types/time_t.h /usr/include/bits/types/timer_t.h
ollama.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
ollama.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
ollama.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
ollama.o: /usr/include/sys/select.h /usr/include/bits/select.h
ollama.o: /usr/include/bits/types/sigset_t.h
ollama.o: /usr/include/bits/types/__sigset_t.h
ollama.o: /usr/include/bits/types/struct_timeval.h
ollama.o: /usr/include/bits/types/struct_timespec.h
ollama.o: /usr/include/bits/pthreadtypes.h
ollama.o: /usr/include/bits/thread-shared-types.h
ollama.o: /usr/include/bits/pthreadtypes-arch.h
ollama.o: /usr/include/bits/atomic_wide_counter.h
ollama.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
ollama.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
ollama.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
ollama.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
ollama.o: /usr/include/unistd.h /usr/include/bits/posix_opt.h
ollama.o: /usr/include/bits/environments.h /usr/include/bits/confname.h
ollama.o: /usr/include/bits/getopt_posix.h /usr/include/bits/getopt_core.h
ollama.o: /usr/include/bits/unistd_ext.h /usr/include/errno.h
ollama.o: /usr/include/bits/errno.h /usr/include/linux/errno.h
ollama.o: /usr/include/asm/errno.h /usr/include/asm-generic/errno.h
ollama.o: /usr/include/asm-generic/errno-base.h /usr/include/fcntl.h
ollama.o: /usr/include/bits/fcntl.h /usr/include/bits/fcntl-linux.h
ollama.o: /usr/include/bits/stat.h /usr/include/bits/struct_stat.h
ollama.o: /usr/include/time.h /usr/include/bits/time.h
ollama.o: /usr/include/bits/types/struct_tm.h
ollama.o: /usr/include/bits/types/struct_itimerspec.h
ollama.o: /usr/include/sys/socket.h /usr/include/bits/types/struct_iovec.h
ollama.o: /usr/include/bits/socket.h /usr/include/bits/socket_type.h
ollama.o: /usr/include/bits/sockaddr.h /usr/include/asm/socket.h
ollama.o: /usr/include/asm-generic/socket.h /usr/include/linux/posix_types.h
ollama.o: /usr/include/linux/stddef.h /usr/include/asm/posix_types.h
ollama.o: /usr/include/asm/posix_types_64.h
ollama.o: /usr/include/asm-generic/posix_types.h
ollama.o: /usr/include/asm/bitsperlong.h
ollama.o: /usr/include/asm-generic/bitsperlong.h /usr/include/asm/sockios.h
ollama.o: /usr/include/asm-generic/sockios.h
ollama.o: /usr/include/bits/types/struct_osockaddr.h
ollama.o: /usr/include/netinet/in.h /usr/include/bits/stdint-uintn.h
ollama.o: /usr/include/bits/in.h /usr/include/arpa/inet.h
ollama.o: /usr/include/netdb.h /usr/include/rpc/netdb.h
ollama.o: /usr/include/bits/netdb.h ../../node.h ../../data.h ../../object.h
ollama.o: ../../sched.h ../../callback.h ../../DebugPrint.h
