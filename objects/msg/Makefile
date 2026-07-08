# Generated automatically from Makefile.in by configure.
CC=gcc 

SOURCE= $(wildcard *.c) 

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=msg.object

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

msg.o: /usr/include/stdio.h /usr/include/bits/libc-header-start.h
msg.o: /usr/include/features.h /usr/include/features-time64.h
msg.o: /usr/include/bits/wordsize.h /usr/include/bits/timesize.h
msg.o: /usr/include/stdc-predef.h /usr/include/sys/cdefs.h
msg.o: /usr/include/bits/long-double.h /usr/include/gnu/stubs.h
msg.o: /usr/include/bits/types.h /usr/include/bits/typesizes.h
msg.o: /usr/include/bits/time64.h /usr/include/bits/types/__fpos_t.h
msg.o: /usr/include/bits/types/__mbstate_t.h
msg.o: /usr/include/bits/types/__fpos64_t.h /usr/include/bits/types/__FILE.h
msg.o: /usr/include/bits/types/FILE.h /usr/include/bits/types/struct_FILE.h
msg.o: /usr/include/bits/stdio_lim.h /usr/include/bits/floatn.h
msg.o: /usr/include/bits/floatn-common.h ../../node.h ../../data.h
msg.o: ../../object.h ../../DebugPrint.h ../../callback.h
