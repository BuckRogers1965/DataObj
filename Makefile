# Generated automatically from Makefile.in by configure.
CC=gcc

SOURCE= data.c deamon.c DebugPrint.c dirscan.c libload.c list.c namespace.c node.c object.c sched.c timer.c dyn/buff.c dyn/queue.c dyn/bufftest.c

SUBDIRS := $(wildcard objects/*)

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=framework

OPT=-w -falign-loops -falign-jumps=2 -falign-functions=2 -fstrength-reduce -fomit-frame-pointer -O6 -Idyn

#OPT=-w -malign-loops=2 -malign-jumps=2 -malign-functions=2 -fstrength-reduce -fomit-frame-pointer -Os -Idyn
#OPT= -ggdb  -Wall
 
%.o : %.c
	$(CC) -fPIC $(OPT) -c $< -o $@
#	$(CC) $OPT -shared -DCUSTOM -g -w -c $< -o $@
#	$(CC) -falign-loops=2 -falign-jumps=2 -falign-functions=2 -fstrength-reduce -fomit-frame-pointer -DCUSTOM  -w -c $< -o $@

all: $(OBJECTS) libframework.so $(GOAL) $(SUBDIRS)
	@echo "Building all subdirectories in 'objects'..."

$(SUBDIRS):
	$(MAKE) -C $@

# Libraries
# m is for math
# dl is for dynamic loading our objects using dirscan.c

$(GOAL):
	$(CC) $(OPT) -L. main.c -o $(GOAL) -lframework

libframework.so: $(OBJECTS)
	gcc -dynamic-lib -shared -o libframework.so $(OPT) *.o dyn/*.o -lc -ldl -lm

clean:
	rm $(GOAL) $(OBJECTS) *~ libframework.so
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done

depend:
	makedepend $(SOURCE) main.c  
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir depend; \
	done

.PHONY: all $(SUBDIRS)

libframework.so: $(SOURCE) *.h  dyn/*.h
framework: libframework.so main.c Makefile
# DO NOT DELETE

