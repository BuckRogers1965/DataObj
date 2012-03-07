PROJECT=dynTest

CC=gcc 

SOURCE= $(wildcard *.c)
#SOURCE= dynbuff.c dynbufftest.c

INCLUDE= -I.
DIRECTIVES= -w -fprofile-arcs -ftest-coverage

OBJECTS= $(subst .c,.o,$(SOURCE))
#OBJECTSA= $(subst .c,.o,$(SOURCE))
#OBJECTS= $(subst ../../core/sources/,./,$(OBJECTSA))

%.o : %.c
	$(CC) $(INCLUDE) $(DIRECTIVES) -w -c $< -o $@

all: $(OBJECTS) $(PROJECT)

$(PROJECT): $(OBJECTS)
	$(CC) $(INCLUDE) $(DIRECTIVES) $(OBJECTS) -o $(PROJECT)

clean:
	rm $(OBJECTS) *.bbg *.da *.bb *.gcov $(PROJECT) 2>/dev/null

test:
	./$(PROJECT)
	gcov dynbuff.c
	less dynbuff.c.gcov

depend:
	/usr/X11R6/bin/makedepend -Y $(SOURCE) > /dev/null 2>&1

# DO NOT DELETE
