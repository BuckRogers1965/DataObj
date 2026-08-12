# A control's own browser half, turned into C so it ships inside the .object.
#
# Every control keeps show/web/<anything>.js and show/web/<anything>.css as
# ordinary editable files - nobody edits JavaScript inside quotes - and this
# turns them into two string literals the module publishes at ClassStart with
# PublishShow(). Include it from an object's Makefile AFTER the `all:` target,
# so the default goal stays `all`:
#
#     include ../show.mk
#
SHOW_JS  := $(wildcard show/web/*.js)
SHOW_CSS := $(wildcard show/web/*.css)

# one escaper, used for both: backslashes, then quotes, then one C string per
# line with its newline kept
define SHOW_ESCAPE
awk '{gsub(/\\/,"\\\\"); gsub(/"/,"\\\""); print "\"" $$0 "\\n\""}'
endef

show_web.h: $(SHOW_JS) $(SHOW_CSS)
	@{ printf 'static char show_web_js[] = ""\n'; \
	   cat $(SHOW_JS) /dev/null | $(SHOW_ESCAPE); \
	   printf ';\nstatic char show_web_css[] = ""\n'; \
	   cat $(SHOW_CSS) /dev/null | $(SHOW_ESCAPE); \
	   printf ';\n'; } > $@

$(OBJECTS): show_web.h

SHOW_CLEAN := show_web.h
