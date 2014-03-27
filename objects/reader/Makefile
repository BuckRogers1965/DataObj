# Generated automatically from Makefile.in by configure.
CC=gcc 

SOURCE= $(wildcard *.c) 

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=reader.object

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

reader.o: /usr/include/stdio.h /usr/include/features.h /usr/include/libio.h
reader.o: /usr/include/_G_config.h /usr/include/wchar.h
reader.o: /usr/include/xlocale.h ../../node.h ../../data.h ../../object.h
reader.o: ../../DebugPrint.h ../../callback.h
