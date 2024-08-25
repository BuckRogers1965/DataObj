
CC=gcc

SOURCE= data.c deamon.c DebugPrint.c dirscan.c libload.c list.c namespace.c node.c object.c sched.c timer.c dyn/buff.c dyn/queue.c dyn/bufftest.c

# Directories that need to be built
SUBDIRS := $(wildcard objects/*)

# Objects to be built
OBJECTS= $(subst .c,.o,$(SOURCE))

# Final executable and library names
GOAL=framework

# Compiler options
OPT=-w -falign-loops -falign-jumps=2 -falign-functions=2 -fstrength-reduce -fomit-frame-pointer -O6 -Idyn -ggdb

# Rule to compile .c files to .o files
%.o : %.c
	$(CC) -fPIC $(OPT) -c $< -o $@ -ggdb

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

data.o: /usr/include/stdlib.h /usr/include/string.h /usr/include/stdio.h
data.o: /usr/include/ctype.h /usr/include/features.h
data.o: /usr/include/features-time64.h /usr/include/stdc-predef.h
data.o: /usr/include/stdint.h
deamon.o: /usr/include/arpa/inet.h /usr/include/features.h
deamon.o: /usr/include/features-time64.h /usr/include/stdc-predef.h
deamon.o: /usr/include/netinet/in.h /usr/include/endian.h
deamon.o: /usr/include/errno.h /usr/include/netdb.h /usr/include/rpc/netdb.h
deamon.o: /usr/include/signal.h /usr/include/stdio.h /usr/include/stdlib.h
deamon.o: /usr/include/strings.h /usr/include/unistd.h /usr/include/fcntl.h
DebugPrint.o: /usr/include/stdio.h /usr/include/string.h
DebugPrint.o: /usr/include/stdlib.h DebugPrint.h timer.h /usr/include/time.h
DebugPrint.o: /usr/include/features.h /usr/include/features-time64.h
DebugPrint.o: /usr/include/stdc-predef.h
dirscan.o: /usr/include/dirent.h /usr/include/features.h
dirscan.o: /usr/include/features-time64.h /usr/include/stdc-predef.h
dirscan.o: /usr/include/stdio.h /usr/include/unistd.h /usr/include/string.h
dirscan.o: /usr/include/stdlib.h
libload.o: /usr/include/stdio.h /usr/include/string.h /usr/include/stdlib.h
libload.o: /usr/include/dlfcn.h /usr/include/features.h
libload.o: /usr/include/features-time64.h /usr/include/stdc-predef.h
libload.o: /usr/include/dirent.h /usr/include/unistd.h DebugPrint.h
list.o: node.h data.h
namespace.o: /usr/include/stdlib.h /usr/include/stdio.h /usr/include/unistd.h
namespace.o: /usr/include/features.h /usr/include/features-time64.h
namespace.o: /usr/include/stdc-predef.h namespace.h
node.o: /usr/include/stdlib.h /usr/include/stdio.h /usr/include/string.h
node.o: data.h callback.h
object.o: /usr/include/stdio.h node.h data.h object.h DebugPrint.h
sched.o: /usr/include/stdio.h /usr/include/stdlib.h /usr/include/unistd.h
sched.o: /usr/include/features.h /usr/include/features-time64.h
sched.o: /usr/include/stdc-predef.h node.h data.h list.h timer.h
sched.o: /usr/include/time.h callback.h
timer.o: /usr/include/time.h /usr/include/features.h
timer.o: /usr/include/features-time64.h /usr/include/stdc-predef.h
timer.o: /usr/include/stdio.h /usr/include/stdlib.h /usr/include/string.h
dyn/buff.o: /usr/include/stdio.h /usr/include/stdlib.h /usr/include/string.h
dyn/buff.o: dyn/buff.h
dyn/queue.o: /usr/include/stdio.h /usr/include/stdlib.h /usr/include/string.h
dyn/queue.o: dyn/buff.h
dyn/bufftest.o: dyn/buff.h /usr/include/stdio.h /usr/include/stdlib.h
dyn/bufftest.o: /usr/include/string.h
main.o: /usr/include/stdio.h /usr/include/unistd.h /usr/include/features.h
main.o: /usr/include/features-time64.h /usr/include/stdc-predef.h
main.o: /usr/include/string.h node.h data.h list.h sched.h callback.h
main.o: deamon.h dirscan.h object.h timer.h /usr/include/time.h libload.h
main.o: dyn/bufftest.h DebugPrint.h namespace.h version.h
