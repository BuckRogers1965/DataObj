CC=gcc

SOURCE= stablediffusion.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=stablediffusion.object

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

stablediffusion.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
stablediffusion.o: /usr/include/features.h /usr/include/features-time64.h
stablediffusion.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
stablediffusion.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
stablediffusion.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
stablediffusion.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
stablediffusion.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
stablediffusion.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
stablediffusion.o: /usr/include/bits/time64.h
stablediffusion.o: /usr/include/bits/types/__fpos_t.h
stablediffusion.o: /usr/include/bits/types/__mbstate_t.h
stablediffusion.o: /usr/include/bits/types/__fpos64_t.h
stablediffusion.o: /usr/include/bits/types/__FILE.h
stablediffusion.o: /usr/include/bits/types/FILE.h
stablediffusion.o: /usr/include/bits/types/struct_FILE.h
stablediffusion.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
stablediffusion.o: /usr/include/bits/floatn-common.h /usr/include/stdlib.h
stablediffusion.o: /usr/include/bits/waitflags.h
stablediffusion.o: /usr/include/bits/waitstatus.h /usr/include/sys/types.h
stablediffusion.o: /usr/include/bits/types/clock_t.h
stablediffusion.o: /usr/include/bits/types/clockid_t.h
stablediffusion.o: /usr/include/bits/types/time_t.h
stablediffusion.o: /usr/include/bits/types/timer_t.h
stablediffusion.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
stablediffusion.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
stablediffusion.o: /usr/include/bits/byteswap.h
stablediffusion.o: /usr/include/bits/uintn-identity.h
stablediffusion.o: /usr/include/sys/select.h /usr/include/bits/select.h
stablediffusion.o: /usr/include/bits/types/sigset_t.h
stablediffusion.o: /usr/include/bits/types/__sigset_t.h
stablediffusion.o: /usr/include/bits/types/struct_timeval.h
stablediffusion.o: /usr/include/bits/types/struct_timespec.h
stablediffusion.o: /usr/include/bits/pthreadtypes.h
stablediffusion.o: /usr/include/bits/thread-shared-types.h
stablediffusion.o: /usr/include/bits/pthreadtypes-arch.h
stablediffusion.o: /usr/include/bits/atomic_wide_counter.h
stablediffusion.o: /usr/include/bits/struct_mutex.h
stablediffusion.o: /usr/include/bits/struct_rwlock.h /usr/include/alloca.h
stablediffusion.o: /usr/include/bits/stdlib-float.h /usr/include/string.h
stablediffusion.o: /usr/include/bits/types/locale_t.h
stablediffusion.o: /usr/include/bits/types/__locale_t.h
stablediffusion.o: /usr/include/strings.h /usr/include/unistd.h
stablediffusion.o: /usr/include/bits/posix_opt.h
stablediffusion.o: /usr/include/bits/environments.h
stablediffusion.o: /usr/include/bits/confname.h
stablediffusion.o: /usr/include/bits/getopt_posix.h
stablediffusion.o: /usr/include/bits/getopt_core.h
stablediffusion.o: /usr/include/bits/unistd_ext.h /usr/include/errno.h
stablediffusion.o: /usr/include/bits/errno.h /usr/include/linux/errno.h
stablediffusion.o: /usr/include/asm/errno.h /usr/include/asm-generic/errno.h
stablediffusion.o: /usr/include/asm-generic/errno-base.h /usr/include/fcntl.h
stablediffusion.o: /usr/include/bits/fcntl.h /usr/include/bits/fcntl-linux.h
stablediffusion.o: /usr/include/bits/stat.h /usr/include/bits/struct_stat.h
stablediffusion.o: /usr/include/time.h /usr/include/bits/time.h
stablediffusion.o: /usr/include/bits/types/struct_tm.h
stablediffusion.o: /usr/include/bits/types/struct_itimerspec.h
stablediffusion.o: /usr/include/sys/socket.h
stablediffusion.o: /usr/include/bits/types/struct_iovec.h
stablediffusion.o: /usr/include/bits/socket.h /usr/include/bits/socket_type.h
stablediffusion.o: /usr/include/bits/sockaddr.h /usr/include/asm/socket.h
stablediffusion.o: /usr/include/asm-generic/socket.h
stablediffusion.o: /usr/include/linux/posix_types.h
stablediffusion.o: /usr/include/linux/stddef.h /usr/include/asm/posix_types.h
stablediffusion.o: /usr/include/asm/posix_types_64.h
stablediffusion.o: /usr/include/asm-generic/posix_types.h
stablediffusion.o: /usr/include/asm/bitsperlong.h
stablediffusion.o: /usr/include/asm-generic/bitsperlong.h
stablediffusion.o: /usr/include/asm/sockios.h
stablediffusion.o: /usr/include/asm-generic/sockios.h
stablediffusion.o: /usr/include/bits/types/struct_osockaddr.h
stablediffusion.o: /usr/include/netinet/in.h /usr/include/bits/stdint-uintn.h
stablediffusion.o: /usr/include/bits/in.h /usr/include/arpa/inet.h
stablediffusion.o: /usr/include/netdb.h /usr/include/rpc/netdb.h
stablediffusion.o: /usr/include/bits/netdb.h /usr/include/sched.h
stablediffusion.o: /usr/include/bits/sched.h
stablediffusion.o: /usr/include/bits/types/struct_sched_param.h
stablediffusion.o: /usr/include/bits/cpu-set.h
