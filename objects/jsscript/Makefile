CC=gcc

SOURCE= jsscript.c

# vendored QuickJS core (no qjs shell, no std/os libs - the fabric is
# the runtime); compiled with -w: vendor warnings are not ours to fix
QJS_SRC= quickjs/quickjs.c quickjs/libregexp.c quickjs/libunicode.c quickjs/cutils.c quickjs/libbf.c

OBJECTS= $(subst .c,.o,$(SOURCE))
QJS_OBJ= $(subst .c,.o,$(QJS_SRC))

GOAL=jsscript.object

INCLUDES=-I../..

QJS_DEFS=-DCONFIG_VERSION=\"2024-01-13\" -D_GNU_SOURCE

OPT=-Wall -g

%.o : %.c
	$(CC) -fPIC $(OPT) $(INCLUDES) -c $< -o $@

quickjs/%.o : quickjs/%.c
	$(CC) -fPIC -w -O2 $(QJS_DEFS) -c $< -o $@

all: $(OBJECTS) $(QJS_OBJ) $(GOAL)

$(GOAL): $(OBJECTS) $(QJS_OBJ)
	ld  -shared -L../../ -lframework $(OBJECTS) $(QJS_OBJ) -lm -lpthread -o $(GOAL)

clean:
	rm $(OBJECTS) $(QJS_OBJ) $(GOAL) > /dev/null 2>&1

depend:
	makedepend $(INCLUDES) $(SOURCE) > /dev/null 2>&1
