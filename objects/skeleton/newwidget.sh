#!/bin/bash
#
# newwidget.sh - stamp out a new module from one of the skeleton templates.
#
#   objects/skeleton/newwidget.sh <Name> [widget|control|object]
#
# e.g.  objects/skeleton/newwidget.sh Counter          # a widget (the default)
#       objects/skeleton/newwidget.sh Gauge control    # one thing on screen
#       objects/skeleton/newwidget.sh Resolver object  # plain function, no panel
#
# Creates objects/<name>/ with <name>.c (and <name>.h for an object), a real
# Makefile (so it builds), and a starter README.md, with every Skeleton/skeleton
# token and the UUID rewritten. Then: make -C objects/<name>  (or just make).
#
# Which kind to pick: see objects/skeleton/README.md
#
set -e

NAME="$1"
KIND="${2:-widget}"
if [ -z "$NAME" ]; then
    echo "usage: $0 <Name> [widget|control|object]   (e.g. $0 Counter)" >&2
    exit 1
fi
case "$KIND" in
    widget|control|object) : ;;
    *) echo "error: kind must be widget, control or object (got '$KIND')" >&2; exit 1 ;;
esac

# a class name must be a valid C identifier starting with a capital
case "$NAME" in
    [A-Z][A-Za-z0-9_]*) : ;;
    *) echo "error: <WidgetName> must start with a capital and be a C identifier (e.g. Counter, VuMeter)" >&2; exit 1 ;;
esac

# lower-case name for files / directory / doc path
LOWER="$(printf '%s' "$NAME" | tr '[:upper:]' '[:lower:]')"

# resolve the skeleton dir (where this script lives) and the objects/ root
SKEL_DIR="$(cd "$(dirname "$0")" && pwd)"
OBJ_ROOT="$(dirname "$SKEL_DIR")"
DEST="$OBJ_ROOT/$LOWER"

if [ -e "$DEST" ]; then
    echo "error: $DEST already exists" >&2
    exit 1
fi

# a fresh UUID for the library node's provenance (uuidgen may be absent)
UUID="$(uuidgen 2>/dev/null || true)"
[ -z "$UUID" ] && UUID="$(python3 -c 'import uuid;print(uuid.uuid4())')"
UUID="$(printf '%s' "$UUID" | tr '[:upper:]' '[:lower:]')"

mkdir -p "$DEST"

# the source: Skeleton -> Name, skeleton -> lower, UUID placeholder -> fresh.
# (uppercase first so it can't clobber the lowercase pass)
SRC_DIR="$SKEL_DIR/$KIND"
if [ ! -f "$SRC_DIR/skeleton.c" ]; then
    echo "error: no template at $SRC_DIR/skeleton.c" >&2
    exit 1
fi

sed -e "s/Skeleton/$NAME/g" \
    -e "s/skeleton/$LOWER/g" \
    -e "s/SKELETON/$(printf '%s' "$NAME" | tr '[:lower:]' '[:upper:]')/g" \
    -e "s/REPLACE-WITH-A-FRESH-UUID/$UUID/g" \
    "$SRC_DIR/skeleton.c" > "$DEST/$LOWER.c"

# an object carries its interface header - that IS its interface
if [ -f "$SRC_DIR/skeleton.h" ]; then
    sed -e "s/Skeleton/$NAME/g" \
        -e "s/skeleton/$LOWER/g" \
        -e "s/SKELETON/$(printf '%s' "$NAME" | tr '[:lower:]' '[:upper:]')/g" \
        "$SRC_DIR/skeleton.h" > "$DEST/$LOWER.h"
fi

# the Makefile (renamed from Makefile.copy so the framework build now finds it)
sed -e "s/skeleton/$LOWER/g" "$SRC_DIR/Makefile.copy" > "$DEST/Makefile"

# a STARTER help doc - this becomes the widget's Help panel (loaded on open),
# NOT the build guide. Fill it in.
cat > "$DEST/README.md" <<EOF
# $NAME

TODO: one line on what $NAME does.

Default input connection is to **In**.
Default output connection is from **Out**.

## Controls
- **Enable** - checked, the widget operates (the default).
- **Trigger** - TODO.
- **Out** - TODO.
EOF

echo "created a $KIND:"
echo "  $DEST/$LOWER.c"
[ -f "$DEST/$LOWER.h" ] && echo "  $DEST/$LOWER.h   (the interface - the WHOLE of it)"
echo "  $DEST/Makefile"
echo "  $DEST/README.md   (its Help text - edit it)"
echo
echo "read:  $SKEL_DIR/$KIND/README.md"
if [ "$KIND" = object ]; then
    echo "next:  make -C $DEST     # a plain object has no palette entry - something must create it"
else
    echo "next:  make -C $DEST     # then restart the framework and drag '$NAME' from the palette"
fi
