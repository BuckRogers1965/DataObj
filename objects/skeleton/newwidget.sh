#!/bin/bash
#
# newwidget.sh - stamp out a new widget from the skeleton template.
#
#   objects/skeleton/newwidget.sh <WidgetName>
#
# e.g.  objects/skeleton/newwidget.sh Counter
#
# Creates objects/counter/ with counter.c, a real Makefile (so it builds),
# and a starter README.md, with every Skeleton/skeleton token and the UUID
# rewritten to the new name. Then: make -C objects/counter   (or just make).
#
set -e

NAME="$1"
if [ -z "$NAME" ]; then
    echo "usage: $0 <WidgetName>   (e.g. $0 Counter)" >&2
    exit 1
fi

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
sed -e "s/Skeleton/$NAME/g" \
    -e "s/skeleton/$LOWER/g" \
    -e "s/REPLACE-WITH-A-FRESH-UUID/$UUID/g" \
    "$SKEL_DIR/skeleton.c" > "$DEST/$LOWER.c"

# the Makefile (renamed from Makefile.copy so the framework build now finds it)
sed -e "s/skeleton/$LOWER/g" "$SKEL_DIR/Makefile.copy" > "$DEST/Makefile"

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

echo "created:"
echo "  $DEST/$LOWER.c"
echo "  $DEST/Makefile"
echo "  $DEST/README.md   (the widget's Help text - edit it)"
echo
echo "next:  make -C $DEST     # then restart the framework and drag '$NAME' from the palette"
