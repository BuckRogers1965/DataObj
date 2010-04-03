# Generated automatically from Makefile.in by configure.
CC=gcc

SOURCE= data.c deamon.c DebugPrint.c dirscan.c libload.c list.c namespace.c node.c object.c sched.c timer.c dyn/buff.c dyn/queue.c dyn/bufftest.c


OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=framework

#OPT=-w -m486 -malign-loops=2 -malign-jumps=2 -malign-functions=2 -fstrength-reduce -fomit-frame-pointer -O6 -Idyn

#OPT=-w -malign-loops=2 -malign-jumps=2 -malign-functions=2 -fstrength-reduce -fomit-frame-pointer -Os -Idyn
OPT= -g -O -Wall
 
%.o : %.c
	$(CC) -fPIC $(OPT) -c $< -o $@
#	$(CC) $OPT -shared -DCUSTOM -g -w -c $< -o $@
#	$(CC) -m486 -malign-loops=2 -malign-jumps=2 -malign-functions=2 -fstrength-reduce -fomit-frame-pointer -DCUSTOM  -w -c $< -o $@

all: $(OBJECTS) libframework.so $(GOAL) 

# Libraries
# m is for math
# dl is for dynamic loading our objects using dirscan.c

$(GOAL):
	$(CC) $(OPT) -L. -lframework main.c -o $(GOAL)

libframework.so: $(OBJECTS)
	gcc -dynamic-lib -shared -o libframework.so $(OPT) -lc -ldl *.o dyn/*.o

clean:
	rm $(GOAL) $(OBJECTS) *~ libframework.so

depend:
	makedepend $(SOURCE) main/main.c test/testharness.c > /dev/null 2>&1

libframework.so: $(SOURCE) *.h  dyn/*.h
framework: libframework.so main.c Makefile
# DO NOT DELETE

data.o: /usr/include/stdlib.h /usr/include/features.h
data.o: /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h
data.o: /usr/include/gnu/stubs.h /usr/include/gnu/stubs-32.h
data.o: /usr/include/sys/types.h /usr/include/bits/types.h
data.o: /usr/include/bits/typesizes.h /usr/include/time.h
data.o: /usr/include/endian.h /usr/include/bits/endian.h
data.o: /usr/include/sys/select.h /usr/include/bits/select.h
data.o: /usr/include/bits/sigset.h /usr/include/bits/time.h
data.o: /usr/include/sys/sysmacros.h /usr/include/bits/pthreadtypes.h
data.o: /usr/include/alloca.h /usr/include/string.h /usr/include/stdio.h
data.o: /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h
data.o: /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h
data.o: /usr/include/ctype.h
deamon.o: /usr/include/arpa/inet.h /usr/include/features.h
deamon.o: /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h
deamon.o: /usr/include/gnu/stubs.h /usr/include/gnu/stubs-32.h
deamon.o: /usr/include/netinet/in.h /usr/include/stdint.h
deamon.o: /usr/include/bits/wchar.h /usr/include/sys/socket.h
deamon.o: /usr/include/sys/uio.h /usr/include/sys/types.h
deamon.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
deamon.o: /usr/include/time.h /usr/include/endian.h
deamon.o: /usr/include/bits/endian.h /usr/include/sys/select.h
deamon.o: /usr/include/bits/select.h /usr/include/bits/sigset.h
deamon.o: /usr/include/bits/time.h /usr/include/sys/sysmacros.h
deamon.o: /usr/include/bits/pthreadtypes.h /usr/include/bits/uio.h
deamon.o: /usr/include/bits/socket.h /usr/include/limits.h
deamon.o: /usr/include/bits/posix1_lim.h /usr/include/bits/local_lim.h
deamon.o: /usr/include/linux/limits.h /usr/include/bits/posix2_lim.h
deamon.o: /usr/include/bits/sockaddr.h /usr/include/asm/socket.h
deamon.o: /usr/include/asm/sockios.h /usr/include/bits/in.h
deamon.o: /usr/include/bits/byteswap.h /usr/include/errno.h
deamon.o: /usr/include/bits/errno.h /usr/include/linux/errno.h
deamon.o: /usr/include/asm/errno.h /usr/include/asm-generic/errno.h
deamon.o: /usr/include/asm-generic/errno-base.h /usr/include/netdb.h
deamon.o: /usr/include/rpc/netdb.h /usr/include/bits/netdb.h
deamon.o: /usr/include/signal.h /usr/include/bits/signum.h
deamon.o: /usr/include/bits/siginfo.h /usr/include/bits/sigaction.h
deamon.o: /usr/include/bits/sigcontext.h /usr/include/bits/sigstack.h
deamon.o: /usr/include/bits/sigthread.h /usr/include/stdio.h
deamon.o: /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h
deamon.o: /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h
deamon.o: /usr/include/stdlib.h /usr/include/alloca.h /usr/include/strings.h
deamon.o: /usr/include/sys/file.h /usr/include/fcntl.h
deamon.o: /usr/include/bits/fcntl.h /usr/include/sys/ioctl.h
deamon.o: /usr/include/bits/ioctls.h /usr/include/asm/ioctls.h
deamon.o: /usr/include/asm/ioctl.h /usr/include/asm-generic/ioctl.h
deamon.o: /usr/include/bits/ioctl-types.h /usr/include/sys/ttydefaults.h
deamon.o: /usr/include/sys/param.h /usr/include/linux/param.h
deamon.o: /usr/include/asm/param.h /usr/include/sys/signal.h
deamon.o: /usr/include/sys/stat.h /usr/include/bits/stat.h
deamon.o: /usr/include/sys/time.h /usr/include/sys/wait.h
deamon.o: /usr/include/sys/resource.h /usr/include/bits/resource.h
deamon.o: /usr/include/bits/waitflags.h /usr/include/bits/waitstatus.h
deamon.o: /usr/include/unistd.h /usr/include/bits/posix_opt.h
deamon.o: /usr/include/bits/confname.h /usr/include/getopt.h
DebugPrint.o: /usr/include/stdio.h /usr/include/features.h
DebugPrint.o: /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h
DebugPrint.o: /usr/include/gnu/stubs.h /usr/include/gnu/stubs-32.h
DebugPrint.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
DebugPrint.o: /usr/include/libio.h /usr/include/_G_config.h
DebugPrint.o: /usr/include/wchar.h /usr/include/bits/stdio_lim.h
DebugPrint.o: /usr/include/bits/sys_errlist.h /usr/include/string.h
DebugPrint.o: /usr/include/stdlib.h /usr/include/sys/types.h
DebugPrint.o: /usr/include/time.h /usr/include/endian.h
DebugPrint.o: /usr/include/bits/endian.h /usr/include/sys/select.h
DebugPrint.o: /usr/include/bits/select.h /usr/include/bits/sigset.h
DebugPrint.o: /usr/include/bits/time.h /usr/include/sys/sysmacros.h
DebugPrint.o: /usr/include/bits/pthreadtypes.h /usr/include/alloca.h
DebugPrint.o: DebugPrint.h
dirscan.o: /usr/include/dirent.h /usr/include/features.h
dirscan.o: /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h
dirscan.o: /usr/include/gnu/stubs.h /usr/include/gnu/stubs-32.h
dirscan.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
dirscan.o: /usr/include/bits/dirent.h /usr/include/bits/posix1_lim.h
dirscan.o: /usr/include/bits/local_lim.h /usr/include/linux/limits.h
dirscan.o: /usr/include/stdio.h /usr/include/libio.h /usr/include/_G_config.h
dirscan.o: /usr/include/wchar.h /usr/include/bits/stdio_lim.h
dirscan.o: /usr/include/bits/sys_errlist.h /usr/include/unistd.h
dirscan.o: /usr/include/bits/posix_opt.h /usr/include/bits/confname.h
dirscan.o: /usr/include/getopt.h /usr/include/sys/types.h /usr/include/time.h
dirscan.o: /usr/include/endian.h /usr/include/bits/endian.h
dirscan.o: /usr/include/sys/select.h /usr/include/bits/select.h
dirscan.o: /usr/include/bits/sigset.h /usr/include/bits/time.h
dirscan.o: /usr/include/sys/sysmacros.h /usr/include/bits/pthreadtypes.h
dirscan.o: /usr/include/sys/stat.h /usr/include/bits/stat.h
dirscan.o: /usr/include/string.h /usr/include/stdlib.h /usr/include/alloca.h
libload.o: /usr/include/stdio.h /usr/include/features.h
libload.o: /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h
libload.o: /usr/include/gnu/stubs.h /usr/include/gnu/stubs-32.h
libload.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
libload.o: /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h
libload.o: /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h
libload.o: /usr/include/string.h /usr/include/stdlib.h
libload.o: /usr/include/sys/types.h /usr/include/time.h /usr/include/endian.h
libload.o: /usr/include/bits/endian.h /usr/include/sys/select.h
libload.o: /usr/include/bits/select.h /usr/include/bits/sigset.h
libload.o: /usr/include/bits/time.h /usr/include/sys/sysmacros.h
libload.o: /usr/include/bits/pthreadtypes.h /usr/include/alloca.h
libload.o: /usr/include/dlfcn.h /usr/include/bits/dlfcn.h
libload.o: /usr/include/dirent.h /usr/include/bits/dirent.h
libload.o: /usr/include/bits/posix1_lim.h /usr/include/bits/local_lim.h
libload.o: /usr/include/linux/limits.h /usr/include/unistd.h
libload.o: /usr/include/bits/posix_opt.h /usr/include/bits/confname.h
libload.o: /usr/include/getopt.h DebugPrint.h
list.o: node.h data.h
node.o: /usr/include/stdlib.h /usr/include/features.h
node.o: /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h
node.o: /usr/include/gnu/stubs.h /usr/include/gnu/stubs-32.h
node.o: /usr/include/sys/types.h /usr/include/bits/types.h
node.o: /usr/include/bits/typesizes.h /usr/include/time.h
node.o: /usr/include/endian.h /usr/include/bits/endian.h
node.o: /usr/include/sys/select.h /usr/include/bits/select.h
node.o: /usr/include/bits/sigset.h /usr/include/bits/time.h
node.o: /usr/include/sys/sysmacros.h /usr/include/bits/pthreadtypes.h
node.o: /usr/include/alloca.h /usr/include/stdio.h /usr/include/libio.h
node.o: /usr/include/_G_config.h /usr/include/wchar.h
node.o: /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h
node.o: /usr/include/string.h data.h
object.o: /usr/include/stdio.h /usr/include/features.h
object.o: /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h
object.o: /usr/include/gnu/stubs.h /usr/include/gnu/stubs-32.h
object.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
object.o: /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h
object.o: /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h
object.o: node.h data.h object.h DebugPrint.h
sched.o: /usr/include/stdio.h /usr/include/features.h
sched.o: /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h
sched.o: /usr/include/gnu/stubs.h /usr/include/gnu/stubs-32.h
sched.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
sched.o: /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h
sched.o: /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h
sched.o: /usr/include/stdlib.h /usr/include/sys/types.h /usr/include/time.h
sched.o: /usr/include/endian.h /usr/include/bits/endian.h
sched.o: /usr/include/sys/select.h /usr/include/bits/select.h
sched.o: /usr/include/bits/sigset.h /usr/include/bits/time.h
sched.o: /usr/include/sys/sysmacros.h /usr/include/bits/pthreadtypes.h
sched.o: /usr/include/alloca.h node.h data.h list.h timer.h
timer.o: /usr/include/time.h /usr/include/bits/types.h
timer.o: /usr/include/features.h /usr/include/sys/cdefs.h
timer.o: /usr/include/bits/wordsize.h /usr/include/gnu/stubs.h
timer.o: /usr/include/gnu/stubs-32.h /usr/include/bits/typesizes.h
timer.o: /usr/include/sys/time.h /usr/include/bits/time.h
timer.o: /usr/include/sys/select.h /usr/include/bits/select.h
timer.o: /usr/include/bits/sigset.h /usr/include/stdio.h /usr/include/libio.h
timer.o: /usr/include/_G_config.h /usr/include/wchar.h
timer.o: /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h
timer.o: /usr/include/stdlib.h /usr/include/sys/types.h /usr/include/endian.h
timer.o: /usr/include/bits/endian.h /usr/include/sys/sysmacros.h
timer.o: /usr/include/bits/pthreadtypes.h /usr/include/alloca.h
timer.o: /usr/include/string.h
dyn/buff.o: /usr/include/stdio.h /usr/include/features.h
dyn/buff.o: /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h
dyn/buff.o: /usr/include/gnu/stubs.h /usr/include/gnu/stubs-32.h
dyn/buff.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
dyn/buff.o: /usr/include/libio.h /usr/include/_G_config.h
dyn/buff.o: /usr/include/wchar.h /usr/include/bits/stdio_lim.h
dyn/buff.o: /usr/include/bits/sys_errlist.h /usr/include/stdlib.h
dyn/buff.o: /usr/include/sys/types.h /usr/include/time.h
dyn/buff.o: /usr/include/endian.h /usr/include/bits/endian.h
dyn/buff.o: /usr/include/sys/select.h /usr/include/bits/select.h
dyn/buff.o: /usr/include/bits/sigset.h /usr/include/bits/time.h
dyn/buff.o: /usr/include/sys/sysmacros.h /usr/include/bits/pthreadtypes.h
dyn/buff.o: /usr/include/alloca.h /usr/include/string.h dyn/buff.h
dyn/queue.o: /usr/include/stdio.h /usr/include/features.h
dyn/queue.o: /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h
dyn/queue.o: /usr/include/gnu/stubs.h /usr/include/gnu/stubs-32.h
dyn/queue.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
dyn/queue.o: /usr/include/libio.h /usr/include/_G_config.h
dyn/queue.o: /usr/include/wchar.h /usr/include/bits/stdio_lim.h
dyn/queue.o: /usr/include/bits/sys_errlist.h /usr/include/stdlib.h
dyn/queue.o: /usr/include/sys/types.h /usr/include/time.h
dyn/queue.o: /usr/include/endian.h /usr/include/bits/endian.h
dyn/queue.o: /usr/include/sys/select.h /usr/include/bits/select.h
dyn/queue.o: /usr/include/bits/sigset.h /usr/include/bits/time.h
dyn/queue.o: /usr/include/sys/sysmacros.h /usr/include/bits/pthreadtypes.h
dyn/queue.o: /usr/include/alloca.h /usr/include/string.h dyn/buff.h
dyn/bufftest.o: dyn/buff.h /usr/include/stdio.h /usr/include/features.h
dyn/bufftest.o: /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h
dyn/bufftest.o: /usr/include/gnu/stubs.h /usr/include/gnu/stubs-32.h
dyn/bufftest.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
dyn/bufftest.o: /usr/include/libio.h /usr/include/_G_config.h
dyn/bufftest.o: /usr/include/wchar.h /usr/include/bits/stdio_lim.h
dyn/bufftest.o: /usr/include/bits/sys_errlist.h /usr/include/stdlib.h
dyn/bufftest.o: /usr/include/sys/types.h /usr/include/time.h
dyn/bufftest.o: /usr/include/endian.h /usr/include/bits/endian.h
dyn/bufftest.o: /usr/include/sys/select.h /usr/include/bits/select.h
dyn/bufftest.o: /usr/include/bits/sigset.h /usr/include/bits/time.h
dyn/bufftest.o: /usr/include/sys/sysmacros.h /usr/include/bits/pthreadtypes.h
dyn/bufftest.o: /usr/include/alloca.h /usr/include/string.h
