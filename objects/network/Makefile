CC=gcc

# tcp.c is the live module, TCPObject.c stays as the VNOS reference
SOURCE= tcp.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=tcp.object

# -iquote, not -I: openssl pulls in pthread.h, whose "#include <sched.h>"
# would otherwise find THIS PROJECT's sched.h instead of the C library's.
# -iquote keeps "quoted" includes finding our headers and leaves angle-
# bracket includes to the system, which is exactly the distinction wanted.
INCLUDES=-iquote../..

OPT=-Wall -g

%.o : %.c
	$(CC) -fPIC $(OPT) $(INCLUDES) -c $< -o $@

all: $(OBJECTS) $(GOAL)

$(GOAL): $(OBJECTS)
	ld  -shared -L../../ -lframework -lssl -lcrypto $(OBJECTS) -o $(GOAL)

clean:
	rm $(OBJECTS) $(GOAL) > /dev/null 2>&1

# makedepend understands neither -iquote nor gcc's own header dir,
# so depend uses a plain -I and is told where stddef.h lives
# openssl's opensslconf.h is multiarch, and without it its macros.h
# raises an API-level error at makedepend (not at compile) time
DEPINC=-I../.. -I/usr/include/x86_64-linux-gnu \
       -I$(shell $(CC) -print-file-name=include)

depend:
	makedepend $(DEPINC) $(SOURCE)
# DO NOT DELETE

tcp.o: /usr/include/stdio.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/libc-header-start.h
tcp.o: /usr/include/features.h /usr/include/features-time64.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/wordsize.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/timesize.h
tcp.o: /usr/include/stdc-predef.h /usr/include/x86_64-linux-gnu/sys/cdefs.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/long-double.h
tcp.o: /usr/include/x86_64-linux-gnu/gnu/stubs.h
tcp.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
tcp.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/typesizes.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/time64.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/__fpos_t.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/__mbstate_t.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/__fpos64_t.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/__FILE.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/FILE.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/struct_FILE.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/stdio_lim.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/floatn.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/floatn-common.h
tcp.o: /usr/include/stdlib.h /usr/include/x86_64-linux-gnu/bits/waitflags.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/waitstatus.h
tcp.o: /usr/include/x86_64-linux-gnu/sys/types.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/clock_t.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/clockid_t.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/time_t.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/timer_t.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/stdint-intn.h /usr/include/endian.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/endian.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/endianness.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/byteswap.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/uintn-identity.h
tcp.o: /usr/include/x86_64-linux-gnu/sys/select.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/select.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/sigset_t.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/__sigset_t.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/struct_timeval.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/struct_timespec.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/thread-shared-types.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/pthreadtypes-arch.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/atomic_wide_counter.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/struct_mutex.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/struct_rwlock.h
tcp.o: /usr/include/alloca.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/stdlib-float.h
tcp.o: /usr/include/string.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/locale_t.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/__locale_t.h
tcp.o: /usr/include/strings.h /usr/include/unistd.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/posix_opt.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/environments.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/confname.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/getopt_posix.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/getopt_core.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/unistd_ext.h /usr/include/fcntl.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/fcntl.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/fcntl-linux.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/stat.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/struct_stat.h /usr/include/errno.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/errno.h /usr/include/linux/errno.h
tcp.o: /usr/include/x86_64-linux-gnu/asm/errno.h
tcp.o: /usr/include/asm-generic/errno.h /usr/include/asm-generic/errno-base.h
tcp.o: /usr/include/signal.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/signum-generic.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/signum-arch.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/sig_atomic_t.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/siginfo_t.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/__sigval_t.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/siginfo-arch.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/siginfo-consts.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/sigval_t.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/sigevent_t.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/sigevent-consts.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/sigaction.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/sigcontext.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/stack_t.h
tcp.o: /usr/include/x86_64-linux-gnu/sys/ucontext.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/sigstack.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/sigstksz.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/ss_flags.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/struct_sigstack.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/sigthread.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/signal_ext.h
tcp.o: /usr/include/x86_64-linux-gnu/sys/socket.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/struct_iovec.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/socket.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/socket_type.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/sockaddr.h
tcp.o: /usr/include/x86_64-linux-gnu/asm/socket.h
tcp.o: /usr/include/asm-generic/socket.h /usr/include/linux/posix_types.h
tcp.o: /usr/include/linux/stddef.h
tcp.o: /usr/include/x86_64-linux-gnu/asm/posix_types.h
tcp.o: /usr/include/x86_64-linux-gnu/asm/posix_types_64.h
tcp.o: /usr/include/asm-generic/posix_types.h
tcp.o: /usr/include/x86_64-linux-gnu/asm/bitsperlong.h
tcp.o: /usr/include/asm-generic/bitsperlong.h
tcp.o: /usr/include/x86_64-linux-gnu/asm/sockios.h
tcp.o: /usr/include/asm-generic/sockios.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/struct_osockaddr.h
tcp.o: /usr/include/netinet/in.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/stdint-uintn.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/in.h /usr/include/arpa/inet.h
tcp.o: /usr/include/netdb.h /usr/include/rpc/netdb.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/netdb.h /usr/include/openssl/ssl.h
tcp.o: /usr/include/openssl/macros.h
tcp.o: /usr/include/x86_64-linux-gnu/openssl/opensslconf.h
tcp.o: /usr/include/x86_64-linux-gnu/openssl/configuration.h
tcp.o: /usr/include/openssl/opensslv.h /usr/include/openssl/e_os2.h
tcp.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdint.h
tcp.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdint-gcc.h
tcp.o: /usr/include/openssl/comp.h /usr/include/openssl/crypto.h
tcp.o: /usr/include/time.h /usr/include/x86_64-linux-gnu/bits/time.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/struct_tm.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/struct_itimerspec.h
tcp.o: /usr/include/openssl/safestack.h /usr/include/openssl/stack.h
tcp.o: /usr/include/openssl/types.h
tcp.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/limits.h
tcp.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/syslimits.h
tcp.o: /usr/include/limits.h /usr/include/x86_64-linux-gnu/bits/posix1_lim.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/local_lim.h
tcp.o: /usr/include/linux/limits.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/pthread_stack_min-dynamic.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/pthread_stack_min.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/posix2_lim.h
tcp.o: /usr/include/openssl/cryptoerr.h /usr/include/openssl/symhacks.h
tcp.o: /usr/include/openssl/cryptoerr_legacy.h /usr/include/openssl/core.h
tcp.o: /usr/include/pthread.h ../../sched.h ../../callback.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/setjmp.h
tcp.o: /usr/include/x86_64-linux-gnu/bits/types/struct___jmp_buf_tag.h
tcp.o: /usr/include/openssl/comperr.h /usr/include/openssl/bio.h
tcp.o: /usr/include/openssl/bioerr.h /usr/include/openssl/x509.h
tcp.o: /usr/include/openssl/buffer.h /usr/include/openssl/buffererr.h
tcp.o: /usr/include/openssl/evp.h /usr/include/openssl/core_dispatch.h
tcp.o: /usr/include/openssl/evperr.h /usr/include/openssl/params.h
tcp.o: /usr/include/openssl/bn.h /usr/include/openssl/bnerr.h
tcp.o: /usr/include/openssl/objects.h /usr/include/openssl/obj_mac.h
tcp.o: /usr/include/openssl/asn1.h /usr/include/openssl/asn1err.h
tcp.o: /usr/include/openssl/objectserr.h /usr/include/openssl/ec.h
tcp.o: /usr/include/openssl/ecerr.h /usr/include/openssl/rsa.h
tcp.o: /usr/include/openssl/rsaerr.h /usr/include/openssl/dsa.h
tcp.o: /usr/include/openssl/dh.h /usr/include/openssl/dherr.h
tcp.o: /usr/include/openssl/dsaerr.h /usr/include/openssl/sha.h
tcp.o: /usr/include/openssl/x509err.h /usr/include/openssl/x509_vfy.h
tcp.o: /usr/include/openssl/lhash.h /usr/include/openssl/pkcs7.h
tcp.o: /usr/include/openssl/pkcs7err.h /usr/include/openssl/http.h
tcp.o: /usr/include/openssl/conf.h /usr/include/openssl/conferr.h
tcp.o: /usr/include/openssl/conftypes.h /usr/include/openssl/pem.h
tcp.o: /usr/include/openssl/pemerr.h /usr/include/openssl/hmac.h
tcp.o: /usr/include/openssl/async.h /usr/include/openssl/asyncerr.h
tcp.o: /usr/include/openssl/ct.h /usr/include/openssl/cterr.h
tcp.o: /usr/include/openssl/sslerr.h /usr/include/openssl/sslerr_legacy.h
tcp.o: /usr/include/openssl/prov_ssl.h /usr/include/openssl/ssl2.h
tcp.o: /usr/include/openssl/ssl3.h /usr/include/openssl/tls1.h
tcp.o: /usr/include/openssl/dtls1.h /usr/include/openssl/srtp.h
tcp.o: /usr/include/openssl/err.h ../../node.h ../../data.h ../../object.h
tcp.o: ../../DebugPrint.h ../../dyn/buff.h
