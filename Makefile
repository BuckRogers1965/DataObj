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

