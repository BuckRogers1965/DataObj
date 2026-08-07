CC=gcc

SOURCE= control.c

OBJECTS= $(subst .c,.o,$(SOURCE))

GOAL=control.object

INCLUDES=-iquote../../src

OPT=-Wall -g

%.o : %.c
	$(CC) -fPIC $(OPT) $(INCLUDES) -c $< -o $@

all: $(OBJECTS) $(GOAL)

$(GOAL): $(OBJECTS)
	ld  -shared $(OBJECTS) -o $(GOAL)

clean:
	rm -f $(OBJECTS) $(GOAL) > /dev/null 2>&1

DEPINC=-I../../src -I/usr/include/x86_64-linux-gnu \
       -I$(shell $(CC) -print-file-name=include)

depend:
	makedepend $(DEPINC) $(SOURCE)
# DO NOT DELETE

control.o: /usr/include/stdio.h
control.o: /usr/include/x86_64-linux-gnu/bits/libc-header-start.h
control.o: /usr/include/features.h /usr/include/features-time64.h
control.o: /usr/include/x86_64-linux-gnu/bits/wordsize.h
control.o: /usr/include/x86_64-linux-gnu/bits/timesize.h
control.o: /usr/include/stdc-predef.h
control.o: /usr/include/x86_64-linux-gnu/sys/cdefs.h
control.o: /usr/include/x86_64-linux-gnu/bits/long-double.h
control.o: /usr/include/x86_64-linux-gnu/gnu/stubs.h
control.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stddef.h
control.o: /usr/lib/gcc/x86_64-linux-gnu/12/include/stdarg.h
control.o: /usr/include/x86_64-linux-gnu/bits/types.h
control.o: /usr/include/x86_64-linux-gnu/bits/typesizes.h
control.o: /usr/include/x86_64-linux-gnu/bits/time64.h
control.o: /usr/include/x86_64-linux-gnu/bits/types/__fpos_t.h
control.o: /usr/include/x86_64-linux-gnu/bits/types/__mbstate_t.h
control.o: /usr/include/x86_64-linux-gnu/bits/types/__fpos64_t.h
control.o: /usr/include/x86_64-linux-gnu/bits/types/__FILE.h
control.o: /usr/include/x86_64-linux-gnu/bits/types/FILE.h
control.o: /usr/include/x86_64-linux-gnu/bits/types/struct_FILE.h
control.o: /usr/include/x86_64-linux-gnu/bits/stdio_lim.h
control.o: /usr/include/x86_64-linux-gnu/bits/floatn.h
control.o: /usr/include/x86_64-linux-gnu/bits/floatn-common.h
control.o: /usr/include/string.h
control.o: /usr/include/x86_64-linux-gnu/bits/types/locale_t.h
control.o: /usr/include/x86_64-linux-gnu/bits/types/__locale_t.h
control.o: /usr/include/strings.h ../../src/node.h ../../src/data.h
control.o: ../../src/object.h ../../src/callback.h ../../src/DebugPrint.h
control.o: ../../src/control.h
