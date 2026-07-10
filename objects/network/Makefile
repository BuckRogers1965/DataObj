CC=gcc

# tcp.c is the live module, TCPObject.c stays as the VNOS reference
SOURCE= tcp.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=tcp.object

INCLUDES=-I../..

OPT=-Wall -g

%.o : %.c
	$(CC) -fPIC $(OPT) $(INCLUDES) -c $< -o $@

all: $(OBJECTS) $(GOAL)

$(GOAL): $(OBJECTS)
	ld  -shared -L../../ -lframework $(OBJECTS) -o $(GOAL)

clean:
	rm $(OBJECTS) $(GOAL) > /dev/null 2>&1

depend:
	makedepend $(INCLUDES) $(SOURCE) > /dev/null 2>&1
# DO NOT DELETE

tcp.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
tcp.o: /usr/include/features.h /usr/include/features-time64.h
tcp.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
tcp.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
tcp.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
tcp.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
tcp.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
tcp.o: /usr/include/bits/types/__mbstate_t.h
tcp.o: /usr/include/bits/types/__fpos64_t.h /usr/include/bits/types/__FILE.h
tcp.o: /usr/include/bits/types/FILE.h /usr/include/bits/types/struct_FILE.h
tcp.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
tcp.o: /usr/include/bits/floatn-common.h /usr/include/stdlib.h
tcp.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
tcp.o: /usr/include/sys/types.h /usr/include/bits/types/clock_t.h
tcp.o: /usr/include/bits/types/clockid_t.h /usr/include/bits/types/time_t.h
tcp.o: /usr/include/bits/types/timer_t.h /usr/include/bits/stdint-intn.h
tcp.o: /usr/include/endian.h /usr/include/bits/endian.h
tcp.o: /usr/include/bits/endianness.h /usr/include/bits/byteswap.h
tcp.o: /usr/include/bits/uintn-identity.h /usr/include/sys/select.h
tcp.o: /usr/include/bits/select.h /usr/include/bits/types/sigset_t.h
tcp.o: /usr/include/bits/types/__sigset_t.h
tcp.o: /usr/include/bits/types/struct_timeval.h
tcp.o: /usr/include/bits/types/struct_timespec.h
tcp.o: /usr/include/bits/pthreadtypes.h
tcp.o: /usr/include/bits/thread-shared-types.h
tcp.o: /usr/include/bits/pthreadtypes-arch.h
tcp.o: /usr/include/bits/atomic_wide_counter.h
tcp.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
tcp.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
tcp.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
tcp.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
tcp.o: /usr/include/unistd.h /usr/include/bits/posix_opt.h
tcp.o: /usr/include/bits/environments.h /usr/include/bits/confname.h
tcp.o: /usr/include/bits/getopt_posix.h /usr/include/bits/getopt_core.h
tcp.o: /usr/include/bits/unistd_ext.h /usr/include/fcntl.h
tcp.o: /usr/include/bits/fcntl.h /usr/include/bits/fcntl-linux.h
tcp.o: /usr/include/bits/stat.h /usr/include/bits/struct_stat.h
tcp.o: /usr/include/errno.h /usr/include/bits/errno.h
tcp.o: /usr/include/linux/errno.h /usr/include/asm/errno.h
tcp.o: /usr/include/asm-generic/errno.h /usr/include/asm-generic/errno-base.h
tcp.o: /usr/include/signal.h /usr/include/bits/signum-generic.h
tcp.o: /usr/include/bits/signum-arch.h /usr/include/bits/types/sig_atomic_t.h
tcp.o: /usr/include/bits/types/siginfo_t.h
tcp.o: /usr/include/bits/types/__sigval_t.h /usr/include/bits/siginfo-arch.h
tcp.o: /usr/include/bits/siginfo-consts.h /usr/include/bits/types/sigval_t.h
tcp.o: /usr/include/bits/types/sigevent_t.h
tcp.o: /usr/include/bits/sigevent-consts.h /usr/include/bits/sigaction.h
tcp.o: /usr/include/bits/sigcontext.h /usr/include/bits/types/stack_t.h
tcp.o: /usr/include/sys/ucontext.h /usr/include/bits/sigstack.h
tcp.o: /usr/include/bits/sigstksz.h /usr/include/bits/ss_flags.h
tcp.o: /usr/include/bits/types/struct_sigstack.h
tcp.o: /usr/include/bits/sigthread.h /usr/include/bits/signal_ext.h
tcp.o: /usr/include/sys/socket.h /usr/include/bits/types/struct_iovec.h
tcp.o: /usr/include/bits/socket.h /usr/include/bits/socket_type.h
tcp.o: /usr/include/bits/sockaddr.h /usr/include/asm/socket.h
tcp.o: /usr/include/asm-generic/socket.h /usr/include/linux/posix_types.h
tcp.o: /usr/include/linux/stddef.h /usr/include/asm/posix_types.h
tcp.o: /usr/include/asm/posix_types_64.h
tcp.o: /usr/include/asm-generic/posix_types.h /usr/include/asm/bitsperlong.h
tcp.o: /usr/include/asm-generic/bitsperlong.h /usr/include/asm/sockios.h
tcp.o: /usr/include/asm-generic/sockios.h
tcp.o: /usr/include/bits/types/struct_osockaddr.h /usr/include/netinet/in.h
tcp.o: /usr/include/bits/stdint-uintn.h /usr/include/bits/in.h
tcp.o: /usr/include/arpa/inet.h ../../node.h ../../data.h ../../object.h
tcp.o: ../../sched.h ../../callback.h ../../DebugPrint.h ../../dyn/buff.h
