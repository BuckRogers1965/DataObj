CC=gcc

SOURCE= comfyui.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=comfyui.object

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

comfyui.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
comfyui.o: /usr/include/features.h /usr/include/features-time64.h
comfyui.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
comfyui.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
comfyui.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
comfyui.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
comfyui.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
comfyui.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
comfyui.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
comfyui.o: /usr/include/bits/types/__mbstate_t.h
comfyui.o: /usr/include/bits/types/__fpos64_t.h
comfyui.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
comfyui.o: /usr/include/bits/types/struct_FILE.h
comfyui.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
comfyui.o: /usr/include/bits/floatn-common.h /usr/include/stdlib.h
comfyui.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
comfyui.o: /usr/include/sys/types.h /usr/include/bits/types/clock_t.h
comfyui.o: /usr/include/bits/types/clockid_t.h
comfyui.o: /usr/include/bits/types/time_t.h /usr/include/bits/types/timer_t.h
comfyui.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
comfyui.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
comfyui.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
comfyui.o: /usr/include/sys/select.h /usr/include/bits/select.h
comfyui.o: /usr/include/bits/types/sigset_t.h
comfyui.o: /usr/include/bits/types/__sigset_t.h
comfyui.o: /usr/include/bits/types/struct_timeval.h
comfyui.o: /usr/include/bits/types/struct_timespec.h
comfyui.o: /usr/include/bits/pthreadtypes.h
comfyui.o: /usr/include/bits/thread-shared-types.h
comfyui.o: /usr/include/bits/pthreadtypes-arch.h
comfyui.o: /usr/include/bits/atomic_wide_counter.h
comfyui.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
comfyui.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
comfyui.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
comfyui.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
comfyui.o: /usr/include/unistd.h /usr/include/bits/posix_opt.h
comfyui.o: /usr/include/bits/environments.h /usr/include/bits/confname.h
comfyui.o: /usr/include/bits/getopt_posix.h /usr/include/bits/getopt_core.h
comfyui.o: /usr/include/bits/unistd_ext.h /usr/include/errno.h
comfyui.o: /usr/include/bits/errno.h /usr/include/linux/errno.h
comfyui.o: /usr/include/asm/errno.h /usr/include/asm-generic/errno.h
comfyui.o: /usr/include/asm-generic/errno-base.h /usr/include/fcntl.h
comfyui.o: /usr/include/bits/fcntl.h /usr/include/bits/fcntl-linux.h
comfyui.o: /usr/include/bits/stat.h /usr/include/bits/struct_stat.h
comfyui.o: /usr/include/time.h /usr/include/bits/time.h
comfyui.o: /usr/include/bits/types/struct_tm.h
comfyui.o: /usr/include/bits/types/struct_itimerspec.h
comfyui.o: /usr/include/sys/socket.h /usr/include/bits/types/struct_iovec.h
comfyui.o: /usr/include/bits/socket.h /usr/include/bits/socket_type.h
comfyui.o: /usr/include/bits/sockaddr.h /usr/include/asm/socket.h
comfyui.o: /usr/include/asm-generic/socket.h /usr/include/linux/posix_types.h
comfyui.o: /usr/include/linux/stddef.h /usr/include/asm/posix_types.h
comfyui.o: /usr/include/asm/posix_types_64.h
comfyui.o: /usr/include/asm-generic/posix_types.h
comfyui.o: /usr/include/asm/bitsperlong.h
comfyui.o: /usr/include/asm-generic/bitsperlong.h /usr/include/asm/sockios.h
comfyui.o: /usr/include/asm-generic/sockios.h
comfyui.o: /usr/include/bits/types/struct_osockaddr.h
comfyui.o: /usr/include/netinet/in.h /usr/include/bits/stdint-uintn.h
comfyui.o: /usr/include/bits/in.h /usr/include/arpa/inet.h
comfyui.o: /usr/include/netdb.h /usr/include/rpc/netdb.h
comfyui.o: /usr/include/bits/netdb.h ../../node.h ../../data.h ../../object.h
comfyui.o: ../../sched.h ../../callback.h ../../DebugPrint.h ../../widget.h
