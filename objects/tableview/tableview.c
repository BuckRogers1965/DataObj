#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "control.h"
#include "widget.h"

/*

TableView object: a window onto a table, drawn in place.

It holds no table. Table names the instance the cells live in and Row/Col
name the top-left corner of the window; what is drawn is whatever answers
at <Table>/row_<R>/col_<C>. A table with no TableView still works, and two
TableViews on one path are two windows with nothing to keep in step - which
is the whole reason the data and the picture are separate instances.

THE WINDOW SLOTS ARE THIS VIEW'S OWN PROPERTIES, and the reason is worth
writing down. A cell control is linked to Cell_<r>_<c> ON THIS INSTANCE,
once, at build time - so the control's Value node never changes identity.
It cannot: a client subscribes by resolving through that link, and its tap
then sits on whatever node it landed on. Re-pointing the control would
leave every browser watching the cell it used to show. So the CONTROL stays
still and the SLOT moves: each slot is Connect()ed to the cell under it,
both ways, and sliding is nothing but disconnecting one cell and connecting
the next. The value lives in the table; the slot is a window, not a copy
that has to be kept in step.

Sliding therefore costs a dozen wires, not a dozen controls - the grid is
never rebuilt, so the cursor, the selection and the scroll position survive
it. Only VisibleRows/VisibleCols/CellW/CellH rebuild anything.

The window is the interface. Row/Col slide it, VisibleRows/VisibleCols say
how much there is, CellW/CellH how big a cell is drawn - all ordinary
properties, so the options panel edits them, a script writes them, and a
Slider wired to Row is a scrollbar nobody had to build.
Up/Down/Left/Right/Home are the same thing as a gesture.

The panel does not grow when the window does: it is the size it was
declared, and the grid scrolls inside it.

*/

typedef struct InstanceData
{
	int enabled;
	int active;
	int rows, cols;		/* the grid that currently EXISTS, in controls   */
	int built;			/* has a first build happened on this instance?  */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static WidgetItem TableViewPanel[];

/* the window's defaults, and the ceiling on it. A container declares its
   maximum extent rather than growing without bound - past it, you slide. */
#define TV_DEF_ROWS  4
#define TV_DEF_COLS  3
#define TV_MAX_ROWS  64
#define TV_MAX_COLS  32
#define TV_DEF_CW    60
#define TV_DEF_CH    24		/* a Textbox is 24 high, always */

/* the fixed furniture, laid out ONCE and never moved again: a nav strip
   across the top, then the header row, then the grid under it. */
#define TV_PAD       10
#define TV_NAV_Y      8
#define TV_NAV_H     20
#define TV_NAV_W     30
#define TV_HEAD_W    46		/* the row-number column down the left side */
#define TV_HEAD_H    16		/* the column-number row across the top     */
#define TV_GRID_Y    (TV_NAV_Y + TV_NAV_H + 10)

int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint("TableView handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* ---- reading the window ---------------------------------------------- */

static int TV_Clamp(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

/* a property as a number, with the default standing in for an empty box -
   an emptied field means "the default", not zero rows */
static int TV_Num(NodeObj inst, char *prop, int def)
{
	char *s = GetPropStr(inst, prop);

	if (!s || !s[0])
		return def;
	return atoi(s);
}

/* one of this view's own members, by name. A miss is ordinary here (the
   grid is smaller than it was, or nothing has been built yet), so this
   asks with ResolvePath rather than requiring one. */
static NodeObj TV_Find(NodeObj instance, char *name)
{
	char base[400], path[512];

	if (!PathOfInstance(instance, base, sizeof(base)))
		return NULL;
	snprintf(path, sizeof(path), "%s/%s", base, name);
	return ResolvePath(path);
}

/* ---- binding a window slot to a cell ---------------------------------- */

/* THE CELL A SLOT IS CURRENTLY SHOWING, held as a PATH and never a
   pointer: a cell deleted under this view is then a clean miss on the next
   slide rather than a read into freed memory. */
static void TV_SlotNames(int r, int c, char *slot, int slotlen,
						 char *bound, int boundlen)
{
	snprintf(slot, slotlen, "Cell_%d_%d", r, c);
	snprintf(bound, boundlen, "Bound_%d_%d", r, c);
}

/* let go of the cell this slot was wired to, in both directions */
static void TV_Release(NodeObj instance, char *slot, char *bound)
{
	char   *path = GetPropStr(instance, bound);
	NodeObj cell;

	if (!path || !path[0])
		return;

	cell = ResolvePath(path);
	if (cell)
	{
		Disconnect(cell, "Value", instance, slot);
		Disconnect(instance, slot, cell, "Value");
	}
	SetPropStr(instance, bound, "");
}

/* POINT THE GRID AT THE CELLS UNDER IT. Run on every slide and on every
   change of Table. The controls do not move and are not remade - only the
   wires between this view's slots and the table's cells change. */
static void TableView_Bind(NodeObj instance)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char   *table = GetPropStr(instance, "Table");
	int     row0, col0, r, c, bound = 0, empty = 0;
	char    slot[64], boundp[64], path[600], buf[32], dbg[700];
	char    misses[200];
	int     mlen = 0;
	NodeObj cell;

	if (!local)
		return;

	row0 = TV_Clamp(TV_Num(instance, "Row", 0), 0, 1 << 24);
	col0 = TV_Clamp(TV_Num(instance, "Col", 0), 0, 1 << 24);
	misses[0] = '\0';

	/* the headers say WHERE the window is - without them a slide is
	   invisible, because the cells look the same wherever they landed */
	for (r = 0; r < local->rows; r++)
	{
		snprintf(slot, sizeof(slot), "RowHead_%d", r);
		snprintf(buf, sizeof(buf), "%d", row0 + r);
		SetPropStr(instance, slot, buf);
	}
	for (c = 0; c < local->cols; c++)
	{
		snprintf(slot, sizeof(slot), "ColHead_%d", c);
		snprintf(buf, sizeof(buf), "%d", col0 + c);
		SetPropStr(instance, slot, buf);
	}

	for (r = 0; r < local->rows; r++)
		for (c = 0; c < local->cols; c++)
		{
			TV_SlotNames(r, c, slot, sizeof(slot), boundp, sizeof(boundp));
			TV_Release(instance, slot, boundp);

			cell = NULL;
			if (table && table[0])
			{
				snprintf(path, sizeof(path), "%s/row_%d/col_%d",
						 table, row0 + r, col0 + c);
				cell = ResolvePath(path);
			}

			if (cell)
			{
				/* both ways: the cell's changes arrive here and are shown,
				   and an edit made here lands in the cell. Each is the
				   universal default delivery - store onto the named
				   property - and the pair cannot ring, because the second
				   write carries the value the first one already stored and
				   SetPropStr drops a write that changes nothing. */
				Connect(cell, "Value", instance, slot);
				Connect(instance, slot, cell, "Value");
				SetPropStr(instance, boundp, path);
				SetPropStr(instance, slot, GetPropStr(cell, "Value"));
				bound++;
			}
			else
			{
				SetPropStr(instance, slot, "");
				empty++;
				if (mlen < (int) sizeof(misses) - 12)
					mlen += snprintf(misses + mlen, sizeof(misses) - mlen,
									 "%sr%dc%d", mlen ? "," : "",
									 row0 + r, col0 + c);
			}
		}

	snprintf(dbg, sizeof(dbg),
			 "TABLEVIEW '%s' bound %d cells of %s at row %d col %d; %d empty%s%s",
			 GetPropStr(instance, "Name") ? GetPropStr(instance, "Name") : "?",
			 bound, (table && table[0]) ? table : "(no Table set)",
			 row0, col0, empty, empty ? " -> " : "", misses);
	DebugPrint(dbg, __FILE__, __LINE__, WIRE);
}

/* ---- building the grid ------------------------------------------------ */

/* take away what the new window no longer covers, wires first. Members are
   named, so this is a lookup per name rather than a walk, and over-scanning
   is safe: anything already gone simply is not found. */
static void TableView_Trim(NodeObj instance, int rows, int cols)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	int     scanR, scanC, r, c, gone = 0;
	char    name[64], boundp[64], dbg[300];
	NodeObj ctl;

	if (!local)
		return;

	scanR = local->built ? local->rows : TV_MAX_ROWS;
	scanC = local->built ? local->cols : TV_MAX_COLS;

	for (r = 0; r < scanR; r++)
		for (c = 0; c < scanC; c++)
		{
			if (r < rows && c < cols)
				continue;
			TV_SlotNames(r, c, name, sizeof(name), boundp, sizeof(boundp));
			TV_Release(instance, name, boundp);
			if ((ctl = TV_Find(instance, name)))
			{
				Widget_Destroy(ctl);
				gone++;
			}
		}

	for (r = rows; r < scanR; r++)
	{
		snprintf(name, sizeof(name), "RowHead_%d", r);
		if ((ctl = TV_Find(instance, name)))
		{
			Widget_Destroy(ctl);
			gone++;
		}
	}
	for (c = cols; c < scanC; c++)
	{
		snprintf(name, sizeof(name), "ColHead_%d", c);
		if ((ctl = TV_Find(instance, name)))
		{
			Widget_Destroy(ctl);
			gone++;
		}
	}

	if (gone)
	{
		snprintf(dbg, sizeof(dbg),
				 "TABLEVIEW '%s' trimmed %d controls down to %dx%d",
				 GetPropStr(instance, "Name") ? GetPropStr(instance, "Name") : "?",
				 gone, rows, cols);
		DebugPrint(dbg, __FILE__, __LINE__, PLACE);
	}
}

/* one control of the grid: the slot it shows must exist before anything
   can be pointed at it, then Widget_Ctl makes the control and links it.
   Geometry is written every time, adopted or not - this is a COMPUTED
   grid, so CellW/CellH have to be able to move it. That is the one place
   it departs from Widget_Ctl's rule, where an adopted control keeps the
   arrangement somebody saved. */
static void TV_Place(NodeObj instance, char *cls, char *slot,
					 int x, int y, int w, int h)
{
	NodeObj ctl;

	if (!GetPropNode(instance, slot))
		SetPropStr(instance, slot, "");

	ctl = Widget_Ctl(instance, instance, cls, slot, x, y, w, h);
	if (!ctl)
		return;

	SetPropInt(ctl, "X", x);
	SetPropInt(ctl, "Y", y);
	SetPropInt(ctl, "W", w);
	SetPropInt(ctl, "H", h);
	SetPropStr(ctl, "LabelPos", "none");
}

/* MAKE THE GRID THE WINDOW SAYS. Controls are named for their place ON
   SCREEN (Cell_<r>_<c>, relative to the top-left corner), never for the
   cell they happen to show - which is exactly why a slide rebinds instead
   of rebuilding, and why this only runs when the SHAPE changes. */
static void TableView_Build(NodeObj instance)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	int     rows, cols, cw, ch, r, c;
	int     gx, gy;
	char    slot[64], boundp[64], dbg[300];

	if (!local)
		return;

	rows = TV_Clamp(TV_Num(instance, "VisibleRows", TV_DEF_ROWS), 1, TV_MAX_ROWS);
	cols = TV_Clamp(TV_Num(instance, "VisibleCols", TV_DEF_COLS), 1, TV_MAX_COLS);
	cw   = TV_Clamp(TV_Num(instance, "CellW", TV_DEF_CW), 24, 400);
	ch   = TV_Clamp(TV_Num(instance, "CellH", TV_DEF_CH), 12, 200);

	/* shrink first, so a name being reused is free before it is asked for */
	TableView_Trim(instance, rows, cols);

	gx = TV_PAD + TV_HEAD_W;
	gy = TV_GRID_Y + TV_HEAD_H + 2;

	for (c = 0; c < cols; c++)
	{
		snprintf(slot, sizeof(slot), "ColHead_%d", c);
		TV_Place(instance, "Label", slot, gx + c * cw, TV_GRID_Y, cw - 2, TV_HEAD_H);
	}

	for (r = 0; r < rows; r++)
	{
		snprintf(slot, sizeof(slot), "RowHead_%d", r);
		TV_Place(instance, "Label", slot, TV_PAD, gy + r * ch, TV_HEAD_W - 4, ch);
	}

	for (r = 0; r < rows; r++)
		for (c = 0; c < cols; c++)
		{
			TV_SlotNames(r, c, slot, sizeof(slot), boundp, sizeof(boundp));
			TV_Place(instance, "Textbox", slot, gx + c * cw, gy + r * ch, cw - 2, ch);
		}

	local->rows = rows;
	local->cols = cols;
	local->built = 1;

	snprintf(dbg, sizeof(dbg),
			 "TABLEVIEW '%s' laid out %dx%d cells at %dx%d px",
			 GetPropStr(instance, "Name") ? GetPropStr(instance, "Name") : "?",
			 rows, cols, cw, ch);
	DebugPrint(dbg, __FILE__, __LINE__, PLACE);
}

/* ---- the window's own properties -------------------------------------- */

/* a delivered write, stored. The value arrived as real traffic, so the
   handler owns storing it - see DeliverToSubscriber's verdict rules. */
static void TV_Store(NodeObj instance, NodeObj data, char *prop)
{
	char *v = data ? GetValueStr(data) : NULL;

	SetValueStr(GetPropNode(instance, prop), v ? v : "");
}

static int TV_Gate(NodeObj instance, MsgId message)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (message == msg_eof)			/* EOF on a control line means nothing */
		return 0;
	return local && local->enabled;
}

int TableView_OnTable(NodeObj instance, MsgId message, NodeObj data)
{
	if (!TV_Gate(instance, message))
		return rtrn_dropped;
	TV_Store(instance, data, "Table");
	TableView_Bind(instance);
	return rtrn_handled;
}

int TableView_OnRow(NodeObj instance, MsgId message, NodeObj data)
{
	if (!TV_Gate(instance, message))
		return rtrn_dropped;
	TV_Store(instance, data, "Row");
	TableView_Bind(instance);
	return rtrn_handled;
}

int TableView_OnCol(NodeObj instance, MsgId message, NodeObj data)
{
	if (!TV_Gate(instance, message))
		return rtrn_dropped;
	TV_Store(instance, data, "Col");
	TableView_Bind(instance);
	return rtrn_handled;
}

/* the four that change the SHAPE of the window rather than where it is */
static int TV_Reshape(NodeObj instance, MsgId message, NodeObj data, char *prop)
{
	if (!TV_Gate(instance, message))
		return rtrn_dropped;
	TV_Store(instance, data, prop);
	TableView_Build(instance);
	TableView_Bind(instance);
	return rtrn_handled;
}

int TableView_OnVisibleRows(NodeObj i, MsgId m, NodeObj d) { return TV_Reshape(i, m, d, "VisibleRows"); }
int TableView_OnVisibleCols(NodeObj i, MsgId m, NodeObj d) { return TV_Reshape(i, m, d, "VisibleCols"); }
int TableView_OnCellW(NodeObj i, MsgId m, NodeObj d)       { return TV_Reshape(i, m, d, "CellW"); }
int TableView_OnCellH(NodeObj i, MsgId m, NodeObj d)       { return TV_Reshape(i, m, d, "CellH"); }

/* ---- navigation ------------------------------------------------------- */

/* SLIDE THE WINDOW. Row/Col are written with SetPropStr rather than stored
   quietly, so everything watching them hears it: the options panel, a
   Slider wired to Row, a second view following this one. */
static int TV_Slide(NodeObj instance, MsgId message, NodeObj data,
					int dRow, int dCol, int absolute)
{
	int   row, col;
	char  buf[32];
	char *v = data ? GetValueStr(data) : NULL;

	if (!TV_Gate(instance, message))
		return rtrn_dropped;

	/* a MoButton sends "1" down and "0" up: the release is not a second
	   press, so only the rising edge moves anything */
	if (!v || strcmp(v, "1") != 0)
		return rtrn_handled;

	if (absolute)
	{
		row = 0;
		col = 0;
	}
	else
	{
		row = TV_Num(instance, "Row", 0) + dRow;
		col = TV_Num(instance, "Col", 0) + dCol;
	}

	/* a table starts at 0 and there is nothing to the left of it */
	if (row < 0) row = 0;
	if (col < 0) col = 0;

	snprintf(buf, sizeof(buf), "%d", row);
	SetPropStr(instance, "Row", buf);
	snprintf(buf, sizeof(buf), "%d", col);
	SetPropStr(instance, "Col", buf);

	TableView_Bind(instance);
	return rtrn_handled;
}

int TableView_OnUp(NodeObj i, MsgId m, NodeObj d)    { return TV_Slide(i, m, d, -1,  0, 0); }
int TableView_OnDown(NodeObj i, MsgId m, NodeObj d)  { return TV_Slide(i, m, d,  1,  0, 0); }
int TableView_OnLeft(NodeObj i, MsgId m, NodeObj d)  { return TV_Slide(i, m, d,  0, -1, 0); }
int TableView_OnRight(NodeObj i, MsgId m, NodeObj d) { return TV_Slide(i, m, d,  0,  1, 0); }
int TableView_OnHome(NodeObj i, MsgId m, NodeObj d)  { return TV_Slide(i, m, d,  0,  0, 1); }

/* ---- lifecycle -------------------------------------------------------- */

int TableView_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message == msg_eof)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	return rtrn_handled;
}

/* nothing async here. The grid was built at creation; this is where it
   first looks for its cells, because the table it names may well have been
   created after this view was. */
int TableView_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || local->active)
		return rtrn_dropped;

	local->active = 1;
	SetPropInt(instance, "State", Running);
	TableView_Bind(instance);

	return rtrn_handled;
}

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData *local = malloc(sizeof(InstanceData));

	local->enabled = 1;
	local->active = 0;
	local->rows = 0;
	local->cols = 0;
	local->built = 0;

	instance = NewNode(INTEGER);
	SetName(instance, "TableView");

	/* the nav buttons and Enable come from the table */
	Widget_Init(instance, TableViewPanel);

	/* THE WINDOW. Published and reactive, but with no control of their own
	   on the face - they are what the options panel is for, and they are
	   ordinary properties, so a script or a wire drives them just as well. */
	Widget_Port(instance, "Table", "", (void *)TableView_OnTable);
	Widget_Port(instance, "Row", "0", (void *)TableView_OnRow);
	Widget_Port(instance, "Col", "0", (void *)TableView_OnCol);
	Widget_Port(instance, "VisibleRows", "4", (void *)TableView_OnVisibleRows);
	Widget_Port(instance, "VisibleCols", "3", (void *)TableView_OnVisibleCols);
	Widget_Port(instance, "CellW", "60", (void *)TableView_OnCellW);
	Widget_Port(instance, "CellH", "24", (void *)TableView_OnCellH);

	SetPropInt(instance, "State", Starting);
	WatchableProp(instance, "State");
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)TableView_Activate);

	/* it is a View, so it carries a view's own presentation - and this is
	   the one that is the point: it draws IN the panel it was dropped on
	   instead of being an icon that opens another one */
	SetPropStr(instance, "ReservedViewResizeable", "1");
	SetPropStr(instance, "ReservedViewEmbedded", "1");

	InitPosition(instance);
	Widget_MainSize(instance, TableViewPanel);
	RegisterInstance(class, instance);

	/* placed where it was told, under the name it was given, nav and all */
	Widget_Place(instance, data, TableViewPanel);

	/* and then the grid, which the table cannot express because its size
	   is a property rather than a row */
	TableView_Build(instance);

	/* AND BIND IT NOW, not at Activate. The row and column numbers are
	   what say where the window is, and they are as much a part of being
	   built as the boxes are - a view that has not been activated (the
	   palette builds its copies and never starts them) would otherwise sit
	   there with every header blank. Binding with no Table set resolves
	   nothing and writes exactly those numbers, which is the truth. */
	TableView_Bind(instance);

	return rtrn_handled;
}

/* The fixed furniture only. The declared size is the size: the grid scrolls
   inside it rather than pushing it around, so nothing here ever moves. */
static WidgetItem TableViewPanel[] = {
	/* cls        prop         def   panel   x    y    w    h  label       [handler] */
	{ "View",     "TableView", "",       0,   0,   0, 280, 250, 0 },			/* 0: main */
	{ "Help",     "objects/tableview/README.md", "", 0, 0, 0, 0, 0, 0 },		/* 1: help */

	{ "MoButton", "Left",   "0", 0, TV_PAD,                          TV_NAV_Y, TV_NAV_W, TV_NAV_H, LABEL_NONE, (void *)TableView_OnLeft },
	{ "MoButton", "Right",  "0", 0, TV_PAD + (TV_NAV_W + 4),         TV_NAV_Y, TV_NAV_W, TV_NAV_H, LABEL_NONE, (void *)TableView_OnRight },
	{ "MoButton", "Up",     "0", 0, TV_PAD + 2 * (TV_NAV_W + 4) + 8, TV_NAV_Y, TV_NAV_W, TV_NAV_H, LABEL_NONE, (void *)TableView_OnUp },
	{ "MoButton", "Down",   "0", 0, TV_PAD + 3 * (TV_NAV_W + 4) + 8, TV_NAV_Y, TV_NAV_W, TV_NAV_H, LABEL_NONE, (void *)TableView_OnDown },
	{ "MoButton", "Home",   "0", 0, TV_PAD + 4 * (TV_NAV_W + 4) + 16, TV_NAV_Y, 40,      TV_NAV_H, LABEL_NONE, (void *)TableView_OnHome },

	{ "Checkbox", "Enable", "1", 0, 250, TV_NAV_Y + 5, 9, 9, LABEL_LEFT, (void *)TableView_OnEnable },

	{ NULL }
};

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (local)
		free(local);

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "TableView");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	SetClassVersion(ClassSelf, "1", "0");

	/* A TABLEVIEW IS A VIEW. A cell is a control and a header is a
	   control, so the thing that holds them is the class that holds
	   things - and it renders through View's own browser half with nothing
	   added here. What it shows is on the OTHER chain entirely, reached by
	   path, which is what keeps a picture from owning what it pictures. */
	SetClassParent(ClassSelf, "View");

	PublishPosition(ClassSelf);

	/* the nav strip and Enable, straight from the table */
	Widget_Publish(ClassSelf, TableViewPanel);

	/* THE WINDOW, in the options panel: what is shown, and where it is */
	PublishProp(ClassSelf, "Table", PROP_TEXTBOX, "");
	PublishProp(ClassSelf, "Row", PROP_TEXTBOX, "0");
	PublishProp(ClassSelf, "Col", PROP_TEXTBOX, "0");
	PublishProp(ClassSelf, "VisibleRows", PROP_TEXTBOX, "4");
	PublishProp(ClassSelf, "VisibleCols", PROP_TEXTBOX, "3");
	PublishProp(ClassSelf, "CellW", PROP_TEXTBOX, "60");
	PublishProp(ClassSelf, "CellH", PROP_TEXTBOX, "24");

	/* a View's own presentation, restated because an interface is per
	   class - Embedded defaults ON here, which is the whole point of it */
	PublishProp(ClassSelf, "ReservedViewResizeable", PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "ReservedViewEmbedded", PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "State", PROP_LED, "1");

	return rtrn_handled;
}

int ClassEnd(NodeObj library, MsgId message, NodeObj data)
{
	UnRegisterClass(library, ClassSelf);
	ClassSelf = NULL;

	return rtrn_handled;
}

void _init()
{
	NodeObj temp = NewNode(INTEGER);

	SetName(temp, "TableView");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "3f7c1a48-9d02-4e6b-b5a1-2c8e0d4f7a93");
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)0);
	SetPropInt(temp, "State", 1);

	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");
	AddDependency(temp, "control.object", "Control", "1", "0");
	AddDependency(temp, "view.object", "View", "1", "0");
	AddDependency(temp, "widget.object", "Widget", "1", "0");
	AddDependency(temp, "textbox.object", "Textbox", "1", "0");
	AddDependency(temp, "label.object", "Label", "1", "0");
	AddDependency(temp, "mobutton.object", "MoButton", "1", "0");
	AddDependency(temp, "checkbox.object", "Checkbox", "1", "0");
	AddDependency(temp, "markdown.object", "Markdown", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
