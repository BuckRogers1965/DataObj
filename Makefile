
CC=gcc

SOURCE= data.c deamon.c DebugPrint.c dirscan.c libload.c list.c namespace.c node.c object.c sched.c timer.c widget.c buff.c queue.c bufftest.c

# where those sources live now
vpath %.c src src/dyn

# Directories that need to be built
SUBDIRS := $(wildcard objects/*)

# Objects to be built
BUILD=build

# the $(BUILD) directory target sits above `all`, and make takes the first
# explicit target as the default goal - say it outright instead.
.DEFAULT_GOAL := all
OBJECTS= $(addprefix $(BUILD)/,$(subst .c,.o,$(SOURCE)))

# Final executable and library names
GOAL=framework
UNITTEST=unit_test

# Compiler options
# OPT=-w -falign-loops -falign-jumps=2 -falign-functions=2 -fstrength-reduce -fomit-frame-pointer -O6 -Idyn 
# -flto=auto, not plain -flto: with more than one LTRANS job gcc warns
# ("using serial compilation of N LTRANS jobs") unless it is told how to
# parallelize. =auto picks a sensible job count and links the same code.
# The tree builds warning-free and run.sh enforces that, so a build-time
# warning is a failure like any other.
OPT?=-O3 -march=native -flto=auto -Isrc -Isrc/dyn -Wall -Wextra

# Rule to compile .c files to .o files
# every intermediate lands under $(BUILD) - the source tree stays clean.
# dyn/ needs its own directory there, hence the order-only $(BUILD) prereq.
$(BUILD)/%.o : %.c | $(BUILD)
	$(CC) -fPIC $(OPT) -c $< -o $@

$(BUILD):
	@mkdir -p $(BUILD)

# Build all targets
all: $(OBJECTS) $(BUILD)/libframework.so $(BUILD)/$(GOAL) $(BUILD)/$(UNITTEST) subdirs
	@echo "build complete: $(BUILD)/$(GOAL) + $(words $(wildcard objects/*/*.object)) objects"

# The same build with flags a debugger can follow: no optimisation, no
# inlining, and no LTO - which is what turns a backtrace through
# libframework into a list of function names with no line numbers - plus
# full debug info and frame pointers.
#
#   make debug
#   gdb --args ./framework -ip 127.0.0.1 -port 8083
#
# Only the root build needs it: the object modules already compile -g,
# which is why their frames show lines and the library's do not.
debug: export OPT=-O0 -g3 -fno-omit-frame-pointer -fno-inline -Isrc -Isrc/dyn -Wall -Wextra
debug: clean all

# Rule to handle subdirectories
# NOT -s: silent mode hid every compile, so a real rebuild and a no-op
# looked exactly the same ("Making object: <dir>" printed either way) and
# the only way to trust a build was make clean. Now the work shows and
# only the work shows - a quiet run genuinely did nothing.
subdirs:
	@for dir in $(SUBDIRS); do \
		if [ -f $$dir/Makefile ]; then \
			$(MAKE) --no-print-directory -C $$dir 2>&1 \
				| grep -v "Nothing to be done" || true; \
		fi \
	done

# Goal executable build rule
# It compiles main.c and links libframework.so, so it depends on BOTH -
# listing only $(OBJECTS) meant touching main.c rebuilt nothing (make saw
# every prerequisite up to date) and a library change never relinked.
$(BUILD)/$(GOAL): src/main.c $(BUILD)/libframework.so | $(BUILD)
	$(CC) $(OPT) -L$(BUILD) src/main.c -o $@ -lframework

# the unit test executable: the same core, no default app. Built whenever the
# framework is built.
$(BUILD)/$(UNITTEST): src/unit_test.c $(BUILD)/libframework.so | $(BUILD)
	$(CC) $(OPT) -L$(BUILD) src/unit_test.c -o $@ -lframework

unit_test: $(BUILD)/$(UNITTEST)

# Shared library build rule
$(BUILD)/libframework.so: $(OBJECTS) | $(BUILD)
	$(CC) -shared -o $@ $(OPT) $(OBJECTS) -lc -ldl -lm

# Clean up build artifacts
clean:
	rm -f $(BUILD)/*.o $(BUILD)/*.so $(BUILD)/$(GOAL) $(BUILD)/$(UNITTEST) *~
	@for dir in $(SUBDIRS); do \
			echo "Cleaning object: $$dir"; \
		if [ -f $$dir/Makefile ]; then \
			$(MAKE) -s -C $$dir clean; \
		else \
			echo "No Makefile in $$dir. Skipping..."; \
		fi \
	done

# Handle dependencies
# makedepend does not know where gcc keeps its OWN headers (stddef.h,
# stdarg.h), so it warns on every file unless told - this is that.
DEPINC=-Isrc -Isrc/dyn -I$(shell $(CC) -print-file-name=include)

depend:
	makedepend -p $(BUILD)/ $(DEPINC) $(SOURCE) src/main.c  
	@for dir in $(SUBDIRS); do \
		if [ -f $$dir/Makefile ]; then \
			echo "Depend for object: $$dir"; \
			$(MAKE) -s -C $$dir depend; \
		else \
			echo "No Makefile in $$dir. Skipping..."; \
		fi \
	done

.PHONY: all subdirs clean depend unit_test

# DO NOT DELETE
