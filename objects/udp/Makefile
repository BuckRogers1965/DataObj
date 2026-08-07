CC=gcc

SOURCE= udp.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=udp.object

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

udp.o: /usr/include/stdio.h
udp.o: /usr/include/x86_64-linux-gnu/bits/libc-header-start.h
udp.o: /usr/include/features.h /usr/include/features-time64.h
udp.o: /usr/include/x86_64-linux-gnu/bits/wordsize.h
udp.o: /usr/include/x86_64-linux-gnu/bits/timesize.h
udp.o: /usr/include/stdc-predef.h /usr/include/x86_64-linux-gnu/sys/cdefs.h
udp.o: /usr/include/x86_64-linux-gnu/bits/long-double.h
udp.o: /usr/include/x86_64-linux-gnu/gnu/stubs.h
udp.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
udp.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
udp.o: /usr/include/x86_64-linux-gnu/bits/types.h
udp.o: /usr/include/x86_64-linux-gnu/bits/typesizes.h
udp.o: /usr/include/x86_64-linux-gnu/bits/time64.h
udp.o: /usr/include/x86_64-linux-gnu/bits/types/__fpos_t.h
udp.o: /usr/include/x86_64-linux-gnu/bits/types/__mbstate_t.h
udp.o: /usr/include/x86_64-linux-gnu/bits/types/__fpos64_t.h
udp.o: /usr/include/x86_64-linux-gnu/bits/types/__FILE.h
udp.o: /usr/include/x86_64-linux-gnu/bits/types/FILE.h
udp.o: /usr/include/x86_64-linux-gnu/bits/types/struct_FILE.h
udp.o: /usr/include/x86_64-linux-gnu/bits/stdio_lim.h
udp.o: /usr/include/x86_64-linux-gnu/bits/floatn.h
udp.o: /usr/include/x86_64-linux-gnu/bits/floatn-common.h
udp.o: /usr/include/stdlib.h /usr/include/x86_64-linux-gnu/bits/waitflags.h
udp.o: /usr/include/x86_64-linux-gnu/bits/waitstatus.h
udp.o: /usr/include/x86_64-linux-gnu/sys/types.h
udp.o: /usr/include/x86_64-linux-gnu/bits/types/clock_t.h
udp.o: /usr/include/x86_64-linux-gnu/bits/types/clockid_t.h
udp.o: /usr/include/x86_64-linux-gnu/bits/types/time_t.h
udp.o: /usr/include/x86_64-linux-gnu/bits/types/timer_t.h
udp.o: /usr/include/x86_64-linux-gnu/bits/stdint-intn.h /usr/include/endian.h
udp.o: /usr/include/x86_64-linux-gnu/bits/endian.h
udp.o: /usr/include/x86_64-linux-gnu/bits/endianness.h
udp.o: /usr/include/x86_64-linux-gnu/bits/byteswap.h
udp.o: /usr/include/x86_64-linux-gnu/bits/uintn-identity.h
udp.o: /usr/include/x86_64-linux-gnu/sys/select.h
udp.o: /usr/include/x86_64-linux-gnu/bits/select.h
udp.o: /usr/include/x86_64-linux-gnu/bits/types/sigset_t.h
udp.o: /usr/include/x86_64-linux-gnu/bits/types/__sigset_t.h
udp.o: /usr/include/x86_64-linux-gnu/bits/types/struct_timeval.h
udp.o: /usr/include/x86_64-linux-gnu/bits/types/struct_timespec.h
udp.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes.h
udp.o: /usr/include/x86_64-linux-gnu/bits/thread-shared-types.h
udp.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes-arch.h
udp.o: /usr/include/x86_64-linux-gnu/bits/atomic_wide_counter.h
udp.o: /usr/include/x86_64-linux-gnu/bits/struct_mutex.h
udp.o: /usr/include/x86_64-linux-gnu/bits/struct_rwlock.h
udp.o: /usr/include/alloca.h
udp.o: /usr/include/x86_64-linux-gnu/bits/stdlib-float.h
udp.o: /usr/include/string.h
udp.o: /usr/include/x86_64-linux-gnu/bits/types/locale_t.h
udp.o: /usr/include/x86_64-linux-gnu/bits/types/__locale_t.h
udp.o: /usr/include/strings.h /usr/include/ctype.h /usr/include/unistd.h
udp.o: /usr/include/x86_64-linux-gnu/bits/posix_opt.h
udp.o: /usr/include/x86_64-linux-gnu/bits/environments.h
udp.o: /usr/include/x86_64-linux-gnu/bits/confname.h
udp.o: /usr/include/x86_64-linux-gnu/bits/getopt_posix.h
udp.o: /usr/include/x86_64-linux-gnu/bits/getopt_core.h
udp.o: /usr/include/x86_64-linux-gnu/bits/unistd_ext.h /usr/include/fcntl.h
udp.o: /usr/include/x86_64-linux-gnu/bits/fcntl.h
udp.o: /usr/include/x86_64-linux-gnu/bits/fcntl-linux.h
udp.o: /usr/include/x86_64-linux-gnu/bits/stat.h
udp.o: /usr/include/x86_64-linux-gnu/bits/struct_stat.h /usr/include/errno.h
udp.o: /usr/include/x86_64-linux-gnu/bits/errno.h /usr/include/linux/errno.h
udp.o: /usr/include/x86_64-linux-gnu/asm/errno.h
udp.o: /usr/include/asm-generic/errno.h /usr/include/asm-generic/errno-base.h
udp.o: /usr/include/x86_64-linux-gnu/sys/socket.h
udp.o: /usr/include/x86_64-linux-gnu/bits/types/struct_iovec.h
udp.o: /usr/include/x86_64-linux-gnu/bits/socket.h
udp.o: /usr/include/x86_64-linux-gnu/bits/socket_type.h
udp.o: /usr/include/x86_64-linux-gnu/bits/sockaddr.h
udp.o: /usr/include/x86_64-linux-gnu/asm/socket.h
udp.o: /usr/include/asm-generic/socket.h /usr/include/linux/posix_types.h
udp.o: /usr/include/linux/stddef.h
udp.o: /usr/include/x86_64-linux-gnu/asm/posix_types.h
udp.o: /usr/include/x86_64-linux-gnu/asm/posix_types_64.h
udp.o: /usr/include/asm-generic/posix_types.h
udp.o: /usr/include/x86_64-linux-gnu/asm/bitsperlong.h
udp.o: /usr/include/asm-generic/bitsperlong.h
udp.o: /usr/include/x86_64-linux-gnu/asm/sockios.h
udp.o: /usr/include/asm-generic/sockios.h
udp.o: /usr/include/x86_64-linux-gnu/bits/types/struct_osockaddr.h
udp.o: /usr/include/netinet/in.h
udp.o: /usr/include/x86_64-linux-gnu/bits/stdint-uintn.h
udp.o: /usr/include/x86_64-linux-gnu/bits/in.h /usr/include/arpa/inet.h
udp.o: /usr/include/netdb.h /usr/include/rpc/netdb.h
udp.o: /usr/include/x86_64-linux-gnu/bits/netdb.h ../../src/node.h
udp.o: ../../src/data.h ../../src/object.h ../../src/sched.h
udp.o: ../../src/callback.h ../../src/DebugPrint.h udp.h
