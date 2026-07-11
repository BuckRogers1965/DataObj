
CC=gcc

SOURCE= data.c deamon.c DebugPrint.c dirscan.c libload.c list.c namespace.c node.c object.c sched.c timer.c dyn/buff.c dyn/queue.c dyn/bufftest.c

# Directories that need to be built
SUBDIRS := $(wildcard objects/*)

# Objects to be built
OBJECTS= $(subst .c,.o,$(SOURCE))

# Final executable and library names
GOAL=framework

# Compiler options
OPT=-w -falign-loops -falign-jumps=2 -falign-functions=2 -fstrength-reduce -fomit-frame-pointer -O6 -Idyn 

# Rule to compile .c files to .o files
%.o : %.c
	$(CC) -fPIC $(OPT) -c $< -o $@ 

# Build all targets
all: $(OBJECTS) libframework.so $(GOAL) subdirs

# Rule to handle subdirectories
subdirs:
	@for dir in $(SUBDIRS); do \
		if [ -f $$dir/Makefile ]; then \
			echo "Making object: $$dir"; \
			$(MAKE) -s -C $$dir; \
		else \
			echo "No Makefile in $$dir. Skipping..."; \
		fi \
	done

# Goal executable build rule
$(GOAL): $(OBJECTS)
	$(CC) $(OPT) -L. main.c -o $(GOAL) -lframework

# Shared library build rule
libframework.so: $(OBJECTS)
	$(CC) -dynamic-lib -shared -o libframework.so $(OPT) *.o dyn/*.o -lc -ldl -lm

# Clean up build artifacts
clean:
	rm -f $(GOAL) $(OBJECTS) *~ libframework.so
	@for dir in $(SUBDIRS); do \
			echo "Cleaning object: $$dir"; \
		if [ -f $$dir/Makefile ]; then \
			$(MAKE) -s -C $$dir clean; \
		else \
			echo "No Makefile in $$dir. Skipping..."; \
		fi \
	done

# Handle dependencies
depend:
	makedepend $(SOURCE) main.c  
	@for dir in $(SUBDIRS); do \
		if [ -f $$dir/Makefile ]; then \
			echo "Depend for object: $$dir"; \
			$(MAKE) -s -C $$dir depend; \
		else \
			echo "No Makefile in $$dir. Skipping..."; \
		fi \
	done

.PHONY: all subdirs clean depend

# DO NOT DELETE

data.o: /usr/include/stdlib.h /usr/include/bits/libc-header-start.h
data.o: /usr/include/features.h /usr/include/features-time64.h
data.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
data.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
data.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
data.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
data.o: /usr/include/bits/floatn.h /usr/include/bits/floatn-common.h
data.o: /usr/include/sys/types.h /usr/include/bits/types.h
data.o: /usr/include/bits/typesizes.h /usr/include/bits/time64.h
data.o: /usr/include/bits/types/clock_t.h /usr/include/bits/types/clockid_t.h
data.o: /usr/include/bits/types/time_t.h /usr/include/bits/types/timer_t.h
data.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
data.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
data.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
data.o: /usr/include/sys/select.h /usr/include/bits/select.h
data.o: /usr/include/bits/types/sigset_t.h
data.o: /usr/include/bits/types/__sigset_t.h
data.o: /usr/include/bits/types/struct_timeval.h
data.o: /usr/include/bits/types/struct_timespec.h
data.o: /usr/include/bits/pthreadtypes.h
data.o: /usr/include/bits/thread-shared-types.h
data.o: /usr/include/bits/pthreadtypes-arch.h
data.o: /usr/include/bits/atomic_wide_counter.h
data.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
data.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
data.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
data.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
data.o: /usr/include/stdio.h /usr/include/bits/types/__fpos_t.h
data.o: /usr/include/bits/types/__mbstate_t.h
data.o: /usr/include/bits/types/__fpos64_t.h /usr/include/bits/types/__FILE.h
data.o: /usr/include/bits/types/FILE.h /usr/include/bits/types/struct_FILE.h
data.o: /usr/include/bits/stdio_lim.h /usr/include/ctype.h
data.o: /usr/include/stdint.h /usr/include/bits/wchar.h
data.o: /usr/include/bits/stdint-uintn.h
deamon.o: /usr/include/arpa/inet.h /usr/include/features.h
deamon.o: /usr/include/features-time64.h /usr/include/bits/wordsize.h
deamon.o: /usr/include/bits/timesize.h /usr/include/stdc-predef.h
deamon.o: /usr/include/sys/cdefs.h /usr/include/bits/long-double.h
deamon.o: /usr/include/gnu/stubs.h /usr/include/netinet/in.h
deamon.o: /usr/include/bits/stdint-uintn.h /usr/include/bits/types.h
deamon.o: /usr/include/bits/typesizes.h /usr/include/bits/time64.h
deamon.o: /usr/include/sys/socket.h /usr/include/bits/types/struct_iovec.h
deamon.o: /usr/include/bits/socket.h /usr/include/sys/types.h
deamon.o: /usr/include/bits/types/clock_t.h
deamon.o: /usr/include/bits/types/clockid_t.h
deamon.o: /usr/include/bits/types/time_t.h /usr/include/bits/types/timer_t.h
deamon.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
deamon.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
deamon.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
deamon.o: /usr/include/sys/select.h /usr/include/bits/select.h
deamon.o: /usr/include/bits/types/sigset_t.h
deamon.o: /usr/include/bits/types/__sigset_t.h
deamon.o: /usr/include/bits/types/struct_timeval.h
deamon.o: /usr/include/bits/types/struct_timespec.h
deamon.o: /usr/include/bits/pthreadtypes.h
deamon.o: /usr/include/bits/thread-shared-types.h
deamon.o: /usr/include/bits/pthreadtypes-arch.h
deamon.o: /usr/include/bits/atomic_wide_counter.h
deamon.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
deamon.o: /usr/include/bits/socket_type.h /usr/include/bits/sockaddr.h
deamon.o: /usr/include/asm/socket.h /usr/include/asm-generic/socket.h
deamon.o: /usr/include/linux/posix_types.h /usr/include/linux/stddef.h
deamon.o: /usr/include/asm/posix_types.h /usr/include/asm/posix_types_64.h
deamon.o: /usr/include/asm-generic/posix_types.h
deamon.o: /usr/include/asm/bitsperlong.h
deamon.o: /usr/include/asm-generic/bitsperlong.h /usr/include/asm/sockios.h
deamon.o: /usr/include/asm-generic/sockios.h
deamon.o: /usr/include/bits/types/struct_osockaddr.h /usr/include/bits/in.h
deamon.o: /usr/include/errno.h /usr/include/bits/errno.h
deamon.o: /usr/include/linux/errno.h /usr/include/asm/errno.h
deamon.o: /usr/include/asm-generic/errno.h
deamon.o: /usr/include/asm-generic/errno-base.h /usr/include/netdb.h
deamon.o: /usr/include/rpc/netdb.h /usr/include/bits/netdb.h
deamon.o: /usr/include/signal.h /usr/include/bits/signum-generic.h
deamon.o: /usr/include/bits/signum-arch.h
deamon.o: /usr/include/bits/types/sig_atomic_t.h
deamon.o: /usr/include/bits/types/siginfo_t.h
deamon.o: /usr/include/bits/types/__sigval_t.h
deamon.o: /usr/include/bits/siginfo-arch.h /usr/include/bits/siginfo-consts.h
deamon.o: /usr/include/bits/types/sigval_t.h
deamon.o: /usr/include/bits/types/sigevent_t.h
deamon.o: /usr/include/bits/sigevent-consts.h /usr/include/bits/sigaction.h
deamon.o: /usr/include/bits/sigcontext.h /usr/include/bits/types/stack_t.h
deamon.o: /usr/include/sys/ucontext.h /usr/include/bits/sigstack.h
deamon.o: /usr/include/bits/sigstksz.h /usr/include/bits/ss_flags.h
deamon.o: /usr/include/bits/types/struct_sigstack.h
deamon.o: /usr/include/bits/sigthread.h /usr/include/bits/signal_ext.h
deamon.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
deamon.o: /usr/include/bits/types/__fpos_t.h
deamon.o: /usr/include/bits/types/__mbstate_t.h
deamon.o: /usr/include/bits/types/__fpos64_t.h
deamon.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
deamon.o: /usr/include/bits/types/struct_FILE.h /usr/include/bits/stdio_lim.h
deamon.o: /usr/include/bits/floatn.h /usr/include/bits/floatn-common.h
deamon.o: /usr/include/stdlib.h /usr/include/bits/waitflags.h
deamon.o: /usr/include/bits/waitstatus.h /usr/include/alloca.h
deamon.o: /usr/include/bits/stdlib-float.h /usr/include/strings.h
deamon.o: /usr/include/bits/types/locale_t.h
deamon.o: /usr/include/bits/types/__locale_t.h /usr/include/sys/file.h
deamon.o: /usr/include/fcntl.h /usr/include/bits/fcntl.h
deamon.o: /usr/include/bits/fcntl-linux.h /usr/include/bits/stat.h
deamon.o: /usr/include/bits/struct_stat.h /usr/include/sys/ioctl.h
deamon.o: /usr/include/bits/ioctls.h /usr/include/asm/ioctls.h
deamon.o: /usr/include/asm-generic/ioctls.h /usr/include/linux/ioctl.h
deamon.o: /usr/include/asm/ioctl.h /usr/include/asm-generic/ioctl.h
deamon.o: /usr/include/bits/ioctl-types.h /usr/include/sys/ttydefaults.h
deamon.o: /usr/include/sys/param.h /usr/include/limits.h
deamon.o: /usr/include/bits/posix1_lim.h /usr/include/bits/local_lim.h
deamon.o: /usr/include/linux/limits.h
deamon.o: /usr/include/bits/pthread_stack_min-dynamic.h
deamon.o: /usr/include/bits/pthread_stack_min.h
deamon.o: /usr/include/bits/posix2_lim.h /usr/include/bits/param.h
deamon.o: /usr/include/linux/param.h /usr/include/asm/param.h
deamon.o: /usr/include/asm-generic/param.h /usr/include/sys/signal.h
deamon.o: /usr/include/sys/stat.h /usr/include/sys/time.h
deamon.o: /usr/include/sys/wait.h /usr/include/bits/types/idtype_t.h
deamon.o: /usr/include/unistd.h /usr/include/bits/posix_opt.h
deamon.o: /usr/include/bits/environments.h /usr/include/bits/confname.h
deamon.o: /usr/include/bits/getopt_posix.h /usr/include/bits/getopt_core.h
deamon.o: /usr/include/bits/unistd_ext.h
DebugPrint.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
DebugPrint.o: /usr/include/features.h /usr/include/features-time64.h
DebugPrint.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
DebugPrint.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
DebugPrint.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
DebugPrint.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
DebugPrint.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
DebugPrint.o: /usr/include/bits/types/__mbstate_t.h
DebugPrint.o: /usr/include/bits/types/__fpos64_t.h
DebugPrint.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
DebugPrint.o: /usr/include/bits/types/struct_FILE.h
DebugPrint.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
DebugPrint.o: /usr/include/bits/floatn-common.h /usr/include/string.h
DebugPrint.o: /usr/include/bits/types/locale_t.h
DebugPrint.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
DebugPrint.o: /usr/include/stdlib.h /usr/include/bits/waitflags.h
DebugPrint.o: /usr/include/bits/waitstatus.h /usr/include/sys/types.h
DebugPrint.o: /usr/include/bits/types/clock_t.h
DebugPrint.o: /usr/include/bits/types/clockid_t.h
DebugPrint.o: /usr/include/bits/types/time_t.h
DebugPrint.o: /usr/include/bits/types/timer_t.h
DebugPrint.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
DebugPrint.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
DebugPrint.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
DebugPrint.o: /usr/include/sys/select.h /usr/include/bits/select.h
DebugPrint.o: /usr/include/bits/types/sigset_t.h
DebugPrint.o: /usr/include/bits/types/__sigset_t.h
DebugPrint.o: /usr/include/bits/types/struct_timeval.h
DebugPrint.o: /usr/include/bits/types/struct_timespec.h
DebugPrint.o: /usr/include/bits/pthreadtypes.h
DebugPrint.o: /usr/include/bits/thread-shared-types.h
DebugPrint.o: /usr/include/bits/pthreadtypes-arch.h
DebugPrint.o: /usr/include/bits/atomic_wide_counter.h
DebugPrint.o: /usr/include/bits/struct_mutex.h
DebugPrint.o: /usr/include/bits/struct_rwlock.h /usr/include/alloca.h
DebugPrint.o: /usr/include/bits/stdlib-float.h DebugPrint.h timer.h
DebugPrint.o: /usr/include/time.h /usr/include/bits/time.h
DebugPrint.o: /usr/include/bits/types/struct_tm.h
DebugPrint.o: /usr/include/bits/types/struct_itimerspec.h
dirscan.o: /usr/include/dirent.h /usr/include/features.h
dirscan.o: /usr/include/features-time64.h /usr/include/bits/wordsize.h
dirscan.o: /usr/include/bits/timesize.h /usr/include/stdc-predef.h
dirscan.o: /usr/include/sys/cdefs.h /usr/include/bits/long-double.h
dirscan.o: /usr/include/gnu/stubs.h /usr/include/bits/types.h
dirscan.o: /usr/include/bits/typesizes.h /usr/include/bits/time64.h
dirscan.o: /usr/include/bits/dirent.h /usr/include/bits/posix1_lim.h
dirscan.o: /usr/include/bits/local_lim.h /usr/include/linux/limits.h
dirscan.o: /usr/include/bits/pthread_stack_min-dynamic.h
dirscan.o: /usr/include/bits/pthread_stack_min.h
dirscan.o: /usr/include/bits/dirent_ext.h /usr/include/stdio.h
dirscan.o: /usr/include/bits/libc-header-start.h
dirscan.o: /usr/include/bits/types/__fpos_t.h
dirscan.o: /usr/include/bits/types/__mbstate_t.h
dirscan.o: /usr/include/bits/types/__fpos64_t.h
dirscan.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
dirscan.o: /usr/include/bits/types/struct_FILE.h
dirscan.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
dirscan.o: /usr/include/bits/floatn-common.h /usr/include/unistd.h
dirscan.o: /usr/include/bits/posix_opt.h /usr/include/bits/environments.h
dirscan.o: /usr/include/bits/confname.h /usr/include/bits/getopt_posix.h
dirscan.o: /usr/include/bits/getopt_core.h /usr/include/bits/unistd_ext.h
dirscan.o: /usr/include/sys/types.h /usr/include/bits/types/clock_t.h
dirscan.o: /usr/include/bits/types/clockid_t.h
dirscan.o: /usr/include/bits/types/time_t.h /usr/include/bits/types/timer_t.h
dirscan.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
dirscan.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
dirscan.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
dirscan.o: /usr/include/sys/select.h /usr/include/bits/select.h
dirscan.o: /usr/include/bits/types/sigset_t.h
dirscan.o: /usr/include/bits/types/__sigset_t.h
dirscan.o: /usr/include/bits/types/struct_timeval.h
dirscan.o: /usr/include/bits/types/struct_timespec.h
dirscan.o: /usr/include/bits/pthreadtypes.h
dirscan.o: /usr/include/bits/thread-shared-types.h
dirscan.o: /usr/include/bits/pthreadtypes-arch.h
dirscan.o: /usr/include/bits/atomic_wide_counter.h
dirscan.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
dirscan.o: /usr/include/sys/stat.h /usr/include/bits/stat.h
dirscan.o: /usr/include/bits/struct_stat.h /usr/include/string.h
dirscan.o: /usr/include/bits/types/locale_t.h
dirscan.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
dirscan.o: /usr/include/stdlib.h /usr/include/bits/waitflags.h
dirscan.o: /usr/include/bits/waitstatus.h /usr/include/alloca.h
dirscan.o: /usr/include/bits/stdlib-float.h
libload.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
libload.o: /usr/include/features.h /usr/include/features-time64.h
libload.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
libload.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
libload.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
libload.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
libload.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
libload.o: /usr/include/bits/types/__mbstate_t.h
libload.o: /usr/include/bits/types/__fpos64_t.h
libload.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
libload.o: /usr/include/bits/types/struct_FILE.h
libload.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
libload.o: /usr/include/bits/floatn-common.h /usr/include/string.h
libload.o: /usr/include/bits/types/locale_t.h
libload.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
libload.o: /usr/include/stdlib.h /usr/include/bits/waitflags.h
libload.o: /usr/include/bits/waitstatus.h /usr/include/sys/types.h
libload.o: /usr/include/bits/types/clock_t.h
libload.o: /usr/include/bits/types/clockid_t.h
libload.o: /usr/include/bits/types/time_t.h /usr/include/bits/types/timer_t.h
libload.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
libload.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
libload.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
libload.o: /usr/include/sys/select.h /usr/include/bits/select.h
libload.o: /usr/include/bits/types/sigset_t.h
libload.o: /usr/include/bits/types/__sigset_t.h
libload.o: /usr/include/bits/types/struct_timeval.h
libload.o: /usr/include/bits/types/struct_timespec.h
libload.o: /usr/include/bits/pthreadtypes.h
libload.o: /usr/include/bits/thread-shared-types.h
libload.o: /usr/include/bits/pthreadtypes-arch.h
libload.o: /usr/include/bits/atomic_wide_counter.h
libload.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
libload.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
libload.o: /usr/include/dlfcn.h /usr/include/bits/dlfcn.h
libload.o: /usr/include/dirent.h /usr/include/bits/dirent.h
libload.o: /usr/include/bits/posix1_lim.h /usr/include/bits/local_lim.h
libload.o: /usr/include/linux/limits.h
libload.o: /usr/include/bits/pthread_stack_min-dynamic.h
libload.o: /usr/include/bits/pthread_stack_min.h
libload.o: /usr/include/bits/dirent_ext.h /usr/include/unistd.h
libload.o: /usr/include/bits/posix_opt.h /usr/include/bits/environments.h
libload.o: /usr/include/bits/confname.h /usr/include/bits/getopt_posix.h
libload.o: /usr/include/bits/getopt_core.h /usr/include/bits/unistd_ext.h
libload.o: DebugPrint.h
list.o: node.h data.h
namespace.o: /usr/include/stdlib.h /usr/include/bits/libc-header-start.h
namespace.o: /usr/include/features.h /usr/include/features-time64.h
namespace.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
namespace.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
namespace.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
namespace.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
namespace.o: /usr/include/bits/floatn.h /usr/include/bits/floatn-common.h
namespace.o: /usr/include/sys/types.h /usr/include/bits/types.h
namespace.o: /usr/include/bits/typesizes.h /usr/include/bits/time64.h
namespace.o: /usr/include/bits/types/clock_t.h
namespace.o: /usr/include/bits/types/clockid_t.h
namespace.o: /usr/include/bits/types/time_t.h
namespace.o: /usr/include/bits/types/timer_t.h
namespace.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
namespace.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
namespace.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
namespace.o: /usr/include/sys/select.h /usr/include/bits/select.h
namespace.o: /usr/include/bits/types/sigset_t.h
namespace.o: /usr/include/bits/types/__sigset_t.h
namespace.o: /usr/include/bits/types/struct_timeval.h
namespace.o: /usr/include/bits/types/struct_timespec.h
namespace.o: /usr/include/bits/pthreadtypes.h
namespace.o: /usr/include/bits/thread-shared-types.h
namespace.o: /usr/include/bits/pthreadtypes-arch.h
namespace.o: /usr/include/bits/atomic_wide_counter.h
namespace.o: /usr/include/bits/struct_mutex.h
namespace.o: /usr/include/bits/struct_rwlock.h /usr/include/alloca.h
namespace.o: /usr/include/bits/stdlib-float.h /usr/include/stdio.h
namespace.o: /usr/include/bits/types/__fpos_t.h
namespace.o: /usr/include/bits/types/__mbstate_t.h
namespace.o: /usr/include/bits/types/__fpos64_t.h
namespace.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
namespace.o: /usr/include/bits/types/struct_FILE.h
namespace.o: /usr/include/bits/stdio_lim.h /usr/include/unistd.h
namespace.o: /usr/include/bits/posix_opt.h /usr/include/bits/environments.h
namespace.o: /usr/include/bits/confname.h /usr/include/bits/getopt_posix.h
namespace.o: /usr/include/bits/getopt_core.h /usr/include/bits/unistd_ext.h
namespace.o: namespace.h
node.o: /usr/include/stdlib.h /usr/include/bits/libc-header-start.h
node.o: /usr/include/features.h /usr/include/features-time64.h
node.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
node.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
node.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
node.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
node.o: /usr/include/bits/floatn.h /usr/include/bits/floatn-common.h
node.o: /usr/include/sys/types.h /usr/include/bits/types.h
node.o: /usr/include/bits/typesizes.h /usr/include/bits/time64.h
node.o: /usr/include/bits/types/clock_t.h /usr/include/bits/types/clockid_t.h
node.o: /usr/include/bits/types/time_t.h /usr/include/bits/types/timer_t.h
node.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
node.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
node.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
node.o: /usr/include/sys/select.h /usr/include/bits/select.h
node.o: /usr/include/bits/types/sigset_t.h
node.o: /usr/include/bits/types/__sigset_t.h
node.o: /usr/include/bits/types/struct_timeval.h
node.o: /usr/include/bits/types/struct_timespec.h
node.o: /usr/include/bits/pthreadtypes.h
node.o: /usr/include/bits/thread-shared-types.h
node.o: /usr/include/bits/pthreadtypes-arch.h
node.o: /usr/include/bits/atomic_wide_counter.h
node.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
node.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
node.o: /usr/include/stdio.h /usr/include/bits/types/__fpos_t.h
node.o: /usr/include/bits/types/__mbstate_t.h
node.o: /usr/include/bits/types/__fpos64_t.h /usr/include/bits/types/__FILE.h
node.o: /usr/include/bits/types/FILE.h /usr/include/bits/types/struct_FILE.h
node.o: /usr/include/bits/stdio_lim.h /usr/include/string.h
node.o: /usr/include/bits/types/locale_t.h
node.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h data.h
node.o: callback.h
object.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
object.o: /usr/include/features.h /usr/include/features-time64.h
object.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
object.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
object.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
object.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
object.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
object.o: /usr/include/bits/types/__mbstate_t.h
object.o: /usr/include/bits/types/__fpos64_t.h
object.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
object.o: /usr/include/bits/types/struct_FILE.h /usr/include/bits/stdio_lim.h
object.o: /usr/include/bits/floatn.h /usr/include/bits/floatn-common.h
object.o: /usr/include/stdlib.h /usr/include/bits/waitflags.h
object.o: /usr/include/bits/waitstatus.h /usr/include/sys/types.h
object.o: /usr/include/bits/types/clock_t.h
object.o: /usr/include/bits/types/clockid_t.h
object.o: /usr/include/bits/types/time_t.h /usr/include/bits/types/timer_t.h
object.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
object.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
object.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
object.o: /usr/include/sys/select.h /usr/include/bits/select.h
object.o: /usr/include/bits/types/sigset_t.h
object.o: /usr/include/bits/types/__sigset_t.h
object.o: /usr/include/bits/types/struct_timeval.h
object.o: /usr/include/bits/types/struct_timespec.h
object.o: /usr/include/bits/pthreadtypes.h
object.o: /usr/include/bits/thread-shared-types.h
object.o: /usr/include/bits/pthreadtypes-arch.h
object.o: /usr/include/bits/atomic_wide_counter.h
object.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
object.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
object.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
object.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h node.h
object.o: data.h object.h DebugPrint.h callback.h
sched.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
sched.o: /usr/include/features.h /usr/include/features-time64.h
sched.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
sched.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
sched.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
sched.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
sched.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
sched.o: /usr/include/bits/types/__mbstate_t.h
sched.o: /usr/include/bits/types/__fpos64_t.h
sched.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
sched.o: /usr/include/bits/types/struct_FILE.h /usr/include/bits/stdio_lim.h
sched.o: /usr/include/bits/floatn.h /usr/include/bits/floatn-common.h
sched.o: /usr/include/stdlib.h /usr/include/bits/waitflags.h
sched.o: /usr/include/bits/waitstatus.h /usr/include/sys/types.h
sched.o: /usr/include/bits/types/clock_t.h
sched.o: /usr/include/bits/types/clockid_t.h /usr/include/bits/types/time_t.h
sched.o: /usr/include/bits/types/timer_t.h /usr/include/bits/stdint-intn.h
sched.o: /usr/include/endian.h /usr/include/bits/endian.h
sched.o: /usr/include/bits/endianness.h /usr/include/bits/byteswap.h
sched.o: /usr/include/bits/uintn-identity.h /usr/include/sys/select.h
sched.o: /usr/include/bits/select.h /usr/include/bits/types/sigset_t.h
sched.o: /usr/include/bits/types/__sigset_t.h
sched.o: /usr/include/bits/types/struct_timeval.h
sched.o: /usr/include/bits/types/struct_timespec.h
sched.o: /usr/include/bits/pthreadtypes.h
sched.o: /usr/include/bits/thread-shared-types.h
sched.o: /usr/include/bits/pthreadtypes-arch.h
sched.o: /usr/include/bits/atomic_wide_counter.h
sched.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
sched.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
sched.o: /usr/include/unistd.h /usr/include/bits/posix_opt.h
sched.o: /usr/include/bits/environments.h /usr/include/bits/confname.h
sched.o: /usr/include/bits/getopt_posix.h /usr/include/bits/getopt_core.h
sched.o: /usr/include/bits/unistd_ext.h node.h data.h list.h timer.h
sched.o: /usr/include/time.h /usr/include/bits/time.h
sched.o: /usr/include/bits/types/struct_tm.h
sched.o: /usr/include/bits/types/struct_itimerspec.h
sched.o: /usr/include/bits/types/locale_t.h
sched.o: /usr/include/bits/types/__locale_t.h callback.h
timer.o: /usr/include/time.h /usr/include/features.h
timer.o: /usr/include/features-time64.h /usr/include/bits/wordsize.h
timer.o: /usr/include/bits/timesize.h /usr/include/stdc-predef.h
timer.o: /usr/include/sys/cdefs.h /usr/include/bits/long-double.h
timer.o: /usr/include/gnu/stubs.h /usr/include/bits/time.h
timer.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
timer.o: /usr/include/bits/time64.h /usr/include/bits/types/clock_t.h
timer.o: /usr/include/bits/types/time_t.h /usr/include/bits/types/struct_tm.h
timer.o: /usr/include/bits/types/struct_timespec.h /usr/include/bits/endian.h
timer.o: /usr/include/bits/endianness.h /usr/include/bits/types/clockid_t.h
timer.o: /usr/include/bits/types/timer_t.h
timer.o: /usr/include/bits/types/struct_itimerspec.h
timer.o: /usr/include/bits/types/locale_t.h
timer.o: /usr/include/bits/types/__locale_t.h /usr/include/sys/time.h
timer.o: /usr/include/bits/types/struct_timeval.h /usr/include/sys/select.h
timer.o: /usr/include/bits/select.h /usr/include/bits/types/sigset_t.h
timer.o: /usr/include/bits/types/__sigset_t.h /usr/include/stdio.h
timer.o: /usr/include/bits/libc-header-start.h
timer.o: /usr/include/bits/types/__fpos_t.h
timer.o: /usr/include/bits/types/__mbstate_t.h
timer.o: /usr/include/bits/types/__fpos64_t.h
timer.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
timer.o: /usr/include/bits/types/struct_FILE.h /usr/include/bits/stdio_lim.h
timer.o: /usr/include/bits/floatn.h /usr/include/bits/floatn-common.h
timer.o: /usr/include/stdlib.h /usr/include/bits/waitflags.h
timer.o: /usr/include/bits/waitstatus.h /usr/include/sys/types.h
timer.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
timer.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
timer.o: /usr/include/bits/pthreadtypes.h
timer.o: /usr/include/bits/thread-shared-types.h
timer.o: /usr/include/bits/pthreadtypes-arch.h
timer.o: /usr/include/bits/atomic_wide_counter.h
timer.o: /usr/include/bits/struct_mutex.h /usr/include/bits/struct_rwlock.h
timer.o: /usr/include/alloca.h /usr/include/bits/stdlib-float.h
timer.o: /usr/include/string.h /usr/include/strings.h
dyn/buff.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
dyn/buff.o: /usr/include/features.h /usr/include/features-time64.h
dyn/buff.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
dyn/buff.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
dyn/buff.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
dyn/buff.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
dyn/buff.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
dyn/buff.o: /usr/include/bits/types/__mbstate_t.h
dyn/buff.o: /usr/include/bits/types/__fpos64_t.h
dyn/buff.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
dyn/buff.o: /usr/include/bits/types/struct_FILE.h
dyn/buff.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
dyn/buff.o: /usr/include/bits/floatn-common.h /usr/include/stdlib.h
dyn/buff.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
dyn/buff.o: /usr/include/sys/types.h /usr/include/bits/types/clock_t.h
dyn/buff.o: /usr/include/bits/types/clockid_t.h
dyn/buff.o: /usr/include/bits/types/time_t.h
dyn/buff.o: /usr/include/bits/types/timer_t.h /usr/include/bits/stdint-intn.h
dyn/buff.o: /usr/include/endian.h /usr/include/bits/endian.h
dyn/buff.o: /usr/include/bits/endianness.h /usr/include/bits/byteswap.h
dyn/buff.o: /usr/include/bits/uintn-identity.h /usr/include/sys/select.h
dyn/buff.o: /usr/include/bits/select.h /usr/include/bits/types/sigset_t.h
dyn/buff.o: /usr/include/bits/types/__sigset_t.h
dyn/buff.o: /usr/include/bits/types/struct_timeval.h
dyn/buff.o: /usr/include/bits/types/struct_timespec.h
dyn/buff.o: /usr/include/bits/pthreadtypes.h
dyn/buff.o: /usr/include/bits/thread-shared-types.h
dyn/buff.o: /usr/include/bits/pthreadtypes-arch.h
dyn/buff.o: /usr/include/bits/atomic_wide_counter.h
dyn/buff.o: /usr/include/bits/struct_mutex.h
dyn/buff.o: /usr/include/bits/struct_rwlock.h /usr/include/alloca.h
dyn/buff.o: /usr/include/bits/stdlib-float.h /usr/include/string.h
dyn/buff.o: /usr/include/bits/types/locale_t.h
dyn/buff.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
dyn/buff.o: dyn/buff.h
dyn/queue.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
dyn/queue.o: /usr/include/features.h /usr/include/features-time64.h
dyn/queue.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
dyn/queue.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
dyn/queue.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
dyn/queue.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
dyn/queue.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
dyn/queue.o: /usr/include/bits/types/__mbstate_t.h
dyn/queue.o: /usr/include/bits/types/__fpos64_t.h
dyn/queue.o: /usr/include/bits/types/__FILE.h /usr/include/bits/types/FILE.h
dyn/queue.o: /usr/include/bits/types/struct_FILE.h
dyn/queue.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
dyn/queue.o: /usr/include/bits/floatn-common.h /usr/include/stdlib.h
dyn/queue.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
dyn/queue.o: /usr/include/sys/types.h /usr/include/bits/types/clock_t.h
dyn/queue.o: /usr/include/bits/types/clockid_t.h
dyn/queue.o: /usr/include/bits/types/time_t.h
dyn/queue.o: /usr/include/bits/types/timer_t.h
dyn/queue.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
dyn/queue.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
dyn/queue.o: /usr/include/bits/byteswap.h /usr/include/bits/uintn-identity.h
dyn/queue.o: /usr/include/sys/select.h /usr/include/bits/select.h
dyn/queue.o: /usr/include/bits/types/sigset_t.h
dyn/queue.o: /usr/include/bits/types/__sigset_t.h
dyn/queue.o: /usr/include/bits/types/struct_timeval.h
dyn/queue.o: /usr/include/bits/types/struct_timespec.h
dyn/queue.o: /usr/include/bits/pthreadtypes.h
dyn/queue.o: /usr/include/bits/thread-shared-types.h
dyn/queue.o: /usr/include/bits/pthreadtypes-arch.h
dyn/queue.o: /usr/include/bits/atomic_wide_counter.h
dyn/queue.o: /usr/include/bits/struct_mutex.h
dyn/queue.o: /usr/include/bits/struct_rwlock.h /usr/include/alloca.h
dyn/queue.o: /usr/include/bits/stdlib-float.h /usr/include/string.h
dyn/queue.o: /usr/include/bits/types/locale_t.h
dyn/queue.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
dyn/queue.o: dyn/buff.h
dyn/bufftest.o: dyn/buff.h /usr/include/stdio.h
dyn/bufftest.o: /usr/include/bits/libc-header-start.h /usr/include/features.h
dyn/bufftest.o: /usr/include/features-time64.h /usr/include/bits/wordsize.h
dyn/bufftest.o: /usr/include/bits/timesize.h /usr/include/stdc-predef.h
dyn/bufftest.o: /usr/include/sys/cdefs.h /usr/include/bits/long-double.h
dyn/bufftest.o: /usr/include/gnu/stubs.h /usr/include/bits/types.h
dyn/bufftest.o: /usr/include/bits/typesizes.h /usr/include/bits/time64.h
dyn/bufftest.o: /usr/include/bits/types/__fpos_t.h
dyn/bufftest.o: /usr/include/bits/types/__mbstate_t.h
dyn/bufftest.o: /usr/include/bits/types/__fpos64_t.h
dyn/bufftest.o: /usr/include/bits/types/__FILE.h
dyn/bufftest.o: /usr/include/bits/types/FILE.h
dyn/bufftest.o: /usr/include/bits/types/struct_FILE.h
dyn/bufftest.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
dyn/bufftest.o: /usr/include/bits/floatn-common.h /usr/include/stdlib.h
dyn/bufftest.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
dyn/bufftest.o: /usr/include/sys/types.h /usr/include/bits/types/clock_t.h
dyn/bufftest.o: /usr/include/bits/types/clockid_t.h
dyn/bufftest.o: /usr/include/bits/types/time_t.h
dyn/bufftest.o: /usr/include/bits/types/timer_t.h
dyn/bufftest.o: /usr/include/bits/stdint-intn.h /usr/include/endian.h
dyn/bufftest.o: /usr/include/bits/endian.h /usr/include/bits/endianness.h
dyn/bufftest.o: /usr/include/bits/byteswap.h
dyn/bufftest.o: /usr/include/bits/uintn-identity.h /usr/include/sys/select.h
dyn/bufftest.o: /usr/include/bits/select.h /usr/include/bits/types/sigset_t.h
dyn/bufftest.o: /usr/include/bits/types/__sigset_t.h
dyn/bufftest.o: /usr/include/bits/types/struct_timeval.h
dyn/bufftest.o: /usr/include/bits/types/struct_timespec.h
dyn/bufftest.o: /usr/include/bits/pthreadtypes.h
dyn/bufftest.o: /usr/include/bits/thread-shared-types.h
dyn/bufftest.o: /usr/include/bits/pthreadtypes-arch.h
dyn/bufftest.o: /usr/include/bits/atomic_wide_counter.h
dyn/bufftest.o: /usr/include/bits/struct_mutex.h
dyn/bufftest.o: /usr/include/bits/struct_rwlock.h /usr/include/alloca.h
dyn/bufftest.o: /usr/include/bits/stdlib-float.h /usr/include/string.h
dyn/bufftest.o: /usr/include/bits/types/locale_t.h
dyn/bufftest.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h
main.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
main.o: /usr/include/features.h /usr/include/features-time64.h
main.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
main.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
main.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
main.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
main.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
main.o: /usr/include/bits/types/__mbstate_t.h
main.o: /usr/include/bits/types/__fpos64_t.h /usr/include/bits/types/__FILE.h
main.o: /usr/include/bits/types/FILE.h /usr/include/bits/types/struct_FILE.h
main.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
main.o: /usr/include/bits/floatn-common.h /usr/include/unistd.h
main.o: /usr/include/bits/posix_opt.h /usr/include/bits/environments.h
main.o: /usr/include/bits/confname.h /usr/include/bits/getopt_posix.h
main.o: /usr/include/bits/getopt_core.h /usr/include/bits/unistd_ext.h
main.o: /usr/include/string.h /usr/include/bits/types/locale_t.h
main.o: /usr/include/bits/types/__locale_t.h /usr/include/strings.h node.h
main.o: data.h list.h sched.h callback.h deamon.h dirscan.h object.h timer.h
main.o: /usr/include/time.h /usr/include/bits/time.h
main.o: /usr/include/bits/types/clock_t.h /usr/include/bits/types/time_t.h
main.o: /usr/include/bits/types/struct_tm.h
main.o: /usr/include/bits/types/struct_timespec.h /usr/include/bits/endian.h
main.o: /usr/include/bits/endianness.h /usr/include/bits/types/clockid_t.h
main.o: /usr/include/bits/types/timer_t.h
main.o: /usr/include/bits/types/struct_itimerspec.h libload.h dyn/bufftest.h
main.o: DebugPrint.h namespace.h version.h
