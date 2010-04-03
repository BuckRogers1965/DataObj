# Generated automatically from Makefile.in by configure.
CC=gcc 

SOURCE= $(wildcard *.c) 

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=out.object

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

out.o: /usr/include/stdio.h /usr/include/features.h /usr/include/sys/cdefs.h
out.o: /usr/include/bits/wordsize.h /usr/include/gnu/stubs.h
out.o: /usr/include/gnu/stubs-32.h /usr/include/bits/types.h
out.o: /usr/include/bits/typesizes.h /usr/include/libio.h
out.o: /usr/include/_G_config.h /usr/include/wchar.h
out.o: /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h
out.o: ../../node.h ../../data.h ../../object.h
