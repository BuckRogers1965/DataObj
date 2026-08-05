CC=gcc

SOURCE= udpport.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=udpport.object

INCLUDES=-iquote../..

OPT=-Wall -g

%.o : %.c
	$(CC) -fPIC $(OPT) $(INCLUDES) -c $< -o $@

all: $(OBJECTS) $(GOAL)

$(GOAL): $(OBJECTS)
	ld  -shared -L../../ -lframework $(OBJECTS) -o $(GOAL)

clean:
	rm -f $(OBJECTS) $(GOAL) > /dev/null 2>&1

DEPINC=-I../.. -I/usr/include/x86_64-linux-gnu \
       -I$(shell $(CC) -print-file-name=include)

depend:
	makedepend $(DEPINC) $(SOURCE)
# DO NOT DELETE
