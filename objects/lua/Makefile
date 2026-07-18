CC=gcc

# script.c plus the vendored Lua 5.4.7 core (lua/, MIT license) - one
# self-contained .object, no system Lua needed
SOURCE= script.c
LUASRC= $(wildcard lua/*.c)

OBJECTS= $(subst .c,.o,$(SOURCE)) $(subst .c,.o,$(LUASRC))

GOAL=script.object

INCLUDES=-I../..

OPT=-Wall -g

%.o : %.c
	$(CC) -fPIC $(OPT) $(INCLUDES) -c $< -o $@

all: $(OBJECTS) $(GOAL)

$(GOAL): $(OBJECTS)
	ld  -shared -L../../ -lframework $(OBJECTS) -lm -o $(GOAL)

clean:
	rm $(OBJECTS) $(GOAL) > /dev/null 2>&1

depend:
	makedepend $(INCLUDES) $(SOURCE) > /dev/null 2>&1
# DO NOT DELETE
