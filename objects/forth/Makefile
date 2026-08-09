CC=gcc

# forth.c plus vendored Atlast 1.2 (atlast/, public domain) - one
# self-contained .object, no system Forth needed
SOURCE= forth.c
ATLSRC= atlast/atlast.c

OBJECTS= $(subst .c,.o,$(SOURCE)) $(subst .c,.o,$(ATLSRC))

GOAL=forth.object

INCLUDES=-I../../src

# EXPORT   - atlast's stack and heap symbols become extern, which is what
#            lets forth.c define primitives in its own translation unit
# NOMANGLE - keep those symbols readable rather than atl__sp and friends
COMMON=-DEXPORT -DNOMANGLE

# Keybreak - atlast's inner loop polls this once per word (it defines BREAK
#            for itself), so pointing it at our budget check is the whole
#            runaway guard and the vendored source stays untouched.
# -include - the only reason for it: give that call a prototype without
#            editing a public-domain file.
# CUSTOM   - make atlast read atlcfig.h for its subpackage list, which is how
#            WORDSUSED gets left out. It writes a flag into the first byte of
#            a word's NAME, and a primitive's name is a string literal, so it
#            faults on the first lookup in atl_init(). See atlcfig.h.
ATLOPT=$(COMMON) -DCUSTOM -DKeybreak=Forth_PollBudget -I. -include forth_hook.h

OPT=-Wall -g

all: $(OBJECTS) $(GOAL)

forth.o: forth.c
	$(CC) -fPIC $(OPT) $(COMMON) $(INCLUDES) -c forth.c -o forth.o

atlast/atlast.o: atlast/atlast.c
	$(CC) -fPIC $(OPT) $(ATLOPT) $(INCLUDES) -c atlast/atlast.c -o atlast/atlast.o

$(GOAL): $(OBJECTS)
	ld  -shared $(OBJECTS) -lm -o $(GOAL)

clean:
	rm -f $(OBJECTS) $(GOAL) > /dev/null 2>&1

DEPINC=-I../../src -I$(shell $(CC) -print-file-name=include)

depend:
	makedepend $(DEPINC) $(SOURCE)
# DO NOT DELETE
