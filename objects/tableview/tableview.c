#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"
#include "show_web.h"
#include "data_ext.h"
#include "table.h"

/*

TableView: a window onto a Table.

The widget owns a Table of its own and shows a window of its cells. It
holds no cell values itself - each control on the panel is an alias whose
Value links to the cell it stands for, so there is one holder of every
value and nothing to keep in step. Cloning or importing the widget carries
the Table along as a member, and the links are re-pointed at the copy, so
the values come with it.

The cells are NOT rows in the panel table: a row there would publish a
property on the widget, which is the second copy this design exists to
avoid. They are made in the build, after the instance has a place, because
an alias needs something to stand for.

*/

/* defaults only - the real values live on the instance, so they clone,
   save and can be changed from Settings */
#define GRID_DEF  10		/* the Table underneath */
#define WIN_DEF    3		/* how much of it is on show */
#define WIN_MAX   26		/* what one panel will hold before this needs paging */

#define CELL_W_DEF  80	/* defaults; the live sizes are properties */
#define CELL_H_DEF  24	/* a Textbox is 24 high */
#define CELL_MIN    16
#define CELL_MAX   400
#define CELL_G   6
#define HDR_W   26		/* the row-number column */
#define HDR_H   16
#define CELL_X  (12 + HDR_W + CELL_G)	/* cells start right of the row headers */
#define HDR_Y    8		/* the column letters, just under the top */
#define CELL_Y  30

typedef struct TableViewData
{
	int enabled;
	int row, col;		/* where the window sits in the Table */
} TableViewData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static WidgetItem TableViewPanel[];

int TableView_OnSize(NodeObj instance, MsgId message, NodeObj data);

int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint("TableView handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

int TableView_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	TableViewData *local = (TableViewData *)GetPropLong(instance, "local");

	if (!local || message == msg_eof)
		return rtrn_handled;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	return rtrn_handled;
}

/* the data object this widget points at. A LONG property, like local and
   OnMsg: it does not show in a panel and IsPortableProp refuses it, which
   is what keeps a pointer out of a file. */
static NodeObj TableView_Data(NodeObj instance)
{
	return (NodeObj) GetPropLong(instance, "Data");
}

/* Make one, unaddressably: through the class's own InstanceStart, so it has
   no name, no container and no path, and nothing else in the session can
   reach it. */
static NodeObj TableView_MakeData(NodeObj instance)
{
	NodeObj class, table;
	msgobj  start;

	class = FindClass("Table");
	if (!class)
	{
		DebugPrint("TableView: no Table class - the widget has no data",
				   __FILE__, __LINE__, ERROR);
		return NULL;
	}

	start = (msgobj) GetPropLong(class, "InstanceStart");
	if (!start)
		return NULL;

	start(class, msg_initialize, NULL);
	table = (NodeObj) GetPropLong(class, "LastInstance");
	if (!table)
		return NULL;

	SetPropLong(instance, "Data", (long) table);
	return table;
}

/* A WIDTH BELONGS TO A COLUMN, not to the grid. Dragging the divider after
   B widens B and every cell in it, and leaves A and C alone - which is what
   a spreadsheet does. The size lives in a property named for the column
   ("ColW_B") or the row ("RowH_3"), so it is sparse like everything else:
   a column nobody has resized has no property and takes the default. And
   because they are ordinary properties they clone, serialise and can be
   wired, with no list anywhere. */
static void TableView_SizeName(char *out, int size, int isCol, int index)
{
	char cell[32];
	int  k = 0;

	TableCellName(cell, sizeof(cell), isCol ? 0 : index, isCol ? index : 0);
	if (isCol)
	{
		while (cell[k] >= 'A' && cell[k] <= 'Z')
			k++;
		cell[k] = 0;
		snprintf(out, size, "ColW_%s", cell);
	}
	else
		snprintf(out, size, "RowH_%d", index + 1);
}
/* the default, for a column or row nobody has touched */
static int TableView_CellW(NodeObj instance)
{
	int v = GetPropInt(instance, "CellW");

	if (v < CELL_MIN) v = CELL_W_DEF;
	if (v > CELL_MAX) v = CELL_MAX;
	return v;
}

static int TableView_CellH(NodeObj instance)
{
	int v = GetPropInt(instance, "CellH");

	if (v < CELL_MIN) v = CELL_H_DEF;
	if (v > CELL_MAX) v = CELL_MAX;
	return v;
}

/* THE SIZE LIVES ON THE DATA, because it is that column's width - not the
   third slot's. Keyed by the ABSOLUTE column, so scrolling carries D's
   width to D rather than leaving it on whatever is third; two views of one
   table therefore agree, and the widths travel with the data through clone
   and export like any other property of it.

   `col` and `row` here are absolute - the caller adds the window offset. */
static int TableView_ColW(NodeObj instance, int col)
{
	NodeObj table = TableView_Data(instance);
	char    name[64];
	int     v;

	if (!table)
		return TableView_CellW(instance);

	TableView_SizeName(name, sizeof(name), 1, col);
	v = GetPropInt(table, name);
	if (v < CELL_MIN || v > CELL_MAX)
		return TableView_CellW(instance);
	return v;
}

static int TableView_RowH(NodeObj instance, int row)
{
	NodeObj table = TableView_Data(instance);
	char    name[64];
	int     v;

	if (!table)
		return TableView_CellH(instance);

	TableView_SizeName(name, sizeof(name), 0, row);
	v = GetPropInt(table, name);
	if (v < CELL_MIN || v > CELL_MAX)
		return TableView_CellH(instance);
	return v;
}

/* where a column starts / a row starts - cumulative, because the sizes
   differ now */
/* where the n-th VISIBLE column starts - the widths it walks past are the
   absolute ones, offset by where the window sits */
static int TableView_ColX(NodeObj instance, int slot)
{
	TableViewData *local = (TableViewData *)GetPropLong(instance, "local");
	int x = CELL_X, c, off = local ? local->col : 0;

	for (c = 0; c < slot; c++)
		x += TableView_ColW(instance, off + c) + CELL_G;
	return x;
}

static int TableView_RowY(NodeObj instance, int slot)
{
	TableViewData *local = (TableViewData *)GetPropLong(instance, "local");
	int y = CELL_Y, r, off = local ? local->row : 0;

	for (r = 0; r < slot; r++)
		y += TableView_RowH(instance, off + r) + CELL_G;
	return y;
}

/* how much is on show, and how big the thing underneath is - clamped so a
   window always lies on the table and one panel can hold it */
static int TableView_Vis(NodeObj instance, char *which)
{
	int v = GetPropInt(instance, which);

	if (v < 1) v = 1;
	if (v > WIN_MAX) v = WIN_MAX;
	return v;
}

/* a member of mine carrying this Kind at this position - found by what the
   control recorded, never by a name I guessed. Position, not a flat slot:
   a slot number means something different the moment the width changes. */
static NodeObj TableView_At(NodeObj instance, char *kind, int row, int col)
{
	NodeObj inst;
	char    me[300], *cont, *k;

	if (!PathOfInstance(instance, me, sizeof(me)))
		return NULL;

	for (inst = FirstInstance(); inst; inst = NextInstance(inst))
	{
		cont = GetPropStr(inst, "Container");
		k    = GetPropStr(inst, "Kind");
		if (!cont || !k || strcmp(cont, me) || strcmp(k, kind))
			continue;
		if (GetPropInt(inst, "CellRow") == row && GetPropInt(inst, "CellCol") == col)
			return inst;
	}
	return NULL;
}

/* make sure a cell exists - an alias needs something to stand for, and the
   Table is sparse so nothing is there until someone refers to it */
static void TableView_Touch(NodeObj table, int row, int col)
{
	NodeObj at = NewNode(INTEGER);

	SetName(at, "At");
	SetPropInt(at, "Row", row);
	SetPropInt(at, "Col", col);
	DeliverMsg(table, "Msg", DATA_EXT_ADDRESS_MSG, at);
	DelNode(at);
}

/* Point the window at row/col: every cell control stands for a different
   cell, and the headers say which. Re-aliasing is the whole of scrolling -
   no control is created, moved or destroyed. */
/* Point the window at Row/Col: every cell control stands for a different
   cell, and the headers say which. Re-aliasing is the whole of scrolling -
   no control is created, moved or destroyed. */
static void TableView_Point(NodeObj instance)
{
	TableViewData *local = (TableViewData *)GetPropLong(instance, "local");
	NodeObj table, ctl, cell;
	char    prop[64], num[32], dbg[200];
	int     r, c, rows, cols;

	if (!local)
		return;

	table = TableView_Data(instance);
	if (!table)
		return;

	rows = TableView_Vis(instance, "ViewRows");
	cols = TableView_Vis(instance, "ViewCols");

	for (r = 0; r < rows; r++)
		for (c = 0; c < cols; c++)
		{
			TableView_Touch(table, local->row + r, local->col + c);
			TableCellName(prop, sizeof(prop), local->row + r, local->col + c);

			ctl = TableView_At(instance, "Cell", r, c);
			if (!ctl)
				continue;

			AliasProperty(ctl, table, prop);

			/* MOVING THE LINK IS NOT ENOUGH. A read does not resolve
			   through a link, so whoever is showing this control is still
			   showing what was last written INTO it. Write the cell's own
			   value through the control: harmless as data and it produces
			   the fan-out that repaints. An absent cell is sparse, and
			   clears the box. */
			cell = GetPropNode(table, prop);
			SetOrDeliverProp(ctl, "Value", cell && GetValueStr(cell) ? GetValueStr(cell) : "");
		}

	for (c = 0; c < cols; c++)
	{
		char cellname[32];
		int  k = 0;

		/* the column's letter, taken from a cell name in that column so
		   there is one definition of what a column is called */
		TableCellName(cellname, sizeof(cellname), 0, local->col + c);
		while (cellname[k] >= 'A' && cellname[k] <= 'Z' && k < (int) sizeof(num) - 1)
		{
			num[k] = cellname[k];
			k++;
		}
		num[k] = 0;

		ctl = TableView_At(instance, "ColHead", 0, c);
		if (ctl)
			SetPropStr(ctl, "Value", num);
	}
	for (r = 0; r < rows; r++)
	{
		snprintf(num, sizeof(num), "%d", local->row + r + 1);	/* 1-based, like a spreadsheet */
		ctl = TableView_At(instance, "RowHead", r, 0);
		if (ctl)
			SetPropStr(ctl, "Value", num);
	}

	snprintf(dbg, sizeof(dbg), "TableView: window %dx%d at %d,%d",
			 rows, cols, local->row, local->col);
	DebugPrint(dbg, __FILE__, __LINE__, OBJMSGHANDLING);
}

/* a header: a Label showing which row or column this is */
static NodeObj TableView_Head(NodeObj instance, char *kind, int row, int col,
							  int x, int y, int w)
{
	NodeObj lbl = CreateObject(instance, "Label", NULL);

	if (!lbl)
		return NULL;

	SetPropStr(lbl, "Kind", kind);
	SetPropInt(lbl, "CellRow", row);
	SetPropInt(lbl, "CellCol", col);
	SetPropInt(lbl, "X", x);
	SetPropInt(lbl, "Y", y);
	SetPropInt(lbl, "W", w);
	SetPropInt(lbl, "H", HDR_H);
	SetPropStr(lbl, "LabelPos", "none");	/* the number IS the label */
	return lbl;
}

/* drop every control of this kind that the window no longer reaches */
static void TableView_Prune(NodeObj instance, int rows, int cols)
{
	NodeObj inst, next;
	char    me[300], *cont, *k;
	int     dropped = 0;
	char    dbg[200];

	if (!PathOfInstance(instance, me, sizeof(me)))
		return;

	for (inst = FirstInstance(); inst; inst = next)
	{
		next = NextInstance(inst);

		cont = GetPropStr(inst, "Container");
		k    = GetPropStr(inst, "Kind");
		if (!cont || !k || strcmp(cont, me))
			continue;
		if (strcmp(k, "Cell") && strcmp(k, "ColHead") && strcmp(k, "RowHead"))
			continue;

		if (GetPropInt(inst, "CellRow") < rows && GetPropInt(inst, "CellCol") < cols)
			continue;

		DeleteInstance(inst);
		dropped++;
	}

	if (dropped)
	{
		snprintf(dbg, sizeof(dbg), "TableView: %d control(s) out of the window, dropped",
				 dropped);
		DebugPrint(dbg, __FILE__, __LINE__, OBJMSGHANDLING);
	}
}

/* EVERYTHING BELOW THE GRID FOLLOWS IT. The walk buttons and the position
   readouts are laid out under the grid, so making a row taller - or adding
   rows, or widening a column - moves them instead of letting the grid grow
   down over them. Their X stays where the table put it; only the band they
   sit in moves. */
static void TableView_Follow(NodeObj instance, int bottom)
{
	static struct { char *name; int dx, dy; } below[] = {
		{ "Left",   44, 13 }, { "Up",     86,  0 },
		{ "Right", 128, 13 }, { "Down",   86, 26 },
		{ "Row",   200,  3 }, { "Col",   200, 29 },
		{ NULL, 0, 0 }
	};
	char me[300], path[400];
	int  i;

	if (!PathOfInstance(instance, me, sizeof(me)))
		return;

	for (i = 0; below[i].name; i++)
	{
		NodeObj ctl;

		snprintf(path, sizeof(path), "%s/%s", me, below[i].name);
		ctl = ResolvePath(path);
		if (!ctl)
			continue;			/* a bare table has none of these */

		SetPropInt(ctl, "X", below[i].dx);
		SetPropInt(ctl, "Y", bottom + 20 + below[i].dy);
	}
}

/* ONE PER COLUMN AND ONE PER ROW, at their defaults, from the start. Not
   one per cell - a size belongs to the column, so ten columns and ten rows
   is twenty properties, not a hundred. Filled in when the table is made and
   whenever the stored shape grows, so every column has a width to read and
   nothing has to infer one from an absence. */
static void TableView_Defaults(NodeObj instance, NodeObj table)
{
	char name[64], dbg[200];
	int  rows, cols, i, made = 0;

	if (!table)
		return;

	cols = GetPropInt(table, "Cols");
	rows = GetPropInt(table, "Rows");

	for (i = 0; i < cols; i++)
	{
		TableView_SizeName(name, sizeof(name), 1, i);
		if (!GetPropNode(table, name))
		{
			SetPropInt(table, name, TableView_CellW(instance));
			made++;
		}
	}
	for (i = 0; i < rows; i++)
	{
		TableView_SizeName(name, sizeof(name), 0, i);
		if (!GetPropNode(table, name))
		{
			SetPropInt(table, name, TableView_CellH(instance));
			made++;
		}
	}

	if (made)
	{
		snprintf(dbg, sizeof(dbg),
				 "TableView: %d column/row size(s) set to their defaults (%d cols, %d rows)",
				 made, cols, rows);
		DebugPrint(dbg, __FILE__, __LINE__, OBJMSGHANDLING);
	}
}

/* the panel is tight around what is in it, then padded - the rule is pad
   beyond a TIGHT estimate, so the estimate has to actually be tight */
static void TableView_Size(NodeObj instance, int rows, int cols)
{
	int w = TableView_ColX(instance, cols) - CELL_G;
	int h = TableView_RowY(instance, rows) - CELL_G;

	TableView_Follow(instance, h);

	/* the walk buttons live under the grid, so they set the bottom - unless
	   they are not there at all, in which case the grid does */
	if (GetPropInt(instance, "ShowControls"))
		h += 20 + 26 + 20;		/* the band, its tallest row, and clear space */

	if (w < 240)
		w = 240;				/* the buttons and readouts need this much */

	SetPropInt(instance, "W", w + 50);
	SetPropInt(instance, "H", h + 50);
}

/* The data, the headers and the window onto it, made to match how much is
   on show. Runs after Widget_Place - a member and an alias both need this
   instance to have a place - and again whenever the viewport changes. */
static void TableView_Build(NodeObj instance)
{
	TableViewData *local = (TableViewData *)GetPropLong(instance, "local");
	NodeObj table, ctl, hd;
	char    prop[64], dbg[200];
	int     r, c, rows, cols;

	if (!local)
		return;

	table = TableView_Data(instance);
	if (!table)
	{
		table = TableView_MakeData(instance);
		if (!table)
			return;
		SetPropInt(table, "Rows", GetPropInt(instance, "DataRows"));
		SetPropInt(table, "Cols", GetPropInt(instance, "DataCols"));
	}

	TableView_Defaults(instance, table);

	rows = TableView_Vis(instance, "ViewRows");
	cols = TableView_Vis(instance, "ViewCols");
	TableView_Prune(instance, rows, cols);

	for (c = 0; c < cols; c++)
	{
		hd = TableView_At(instance, "ColHead", 0, c);
		if (!hd)
			hd = TableView_Head(instance, "ColHead", 0, c,
								TableView_ColX(instance, c), HDR_Y,
								TableView_ColW(instance, local->col + c));
		if (hd)
		{
			SetPropInt(hd, "X", TableView_ColX(instance, c));
			SetPropInt(hd, "W", TableView_ColW(instance, local->col + c));
		}
	}
	for (r = 0; r < rows; r++)
	{
		hd = TableView_At(instance, "RowHead", r, 0);
		if (!hd)
			hd = TableView_Head(instance, "RowHead", r, 0,
								12, TableView_RowY(instance, r) + 4, HDR_W);
		if (hd)
			SetPropInt(hd, "Y", TableView_RowY(instance, r) + 4);
	}

	for (r = 0; r < rows; r++)
		for (c = 0; c < cols; c++)
		{
			ctl = TableView_At(instance, "Cell", r, c);
			if (ctl)
			{
				SetPropInt(ctl, "X", TableView_ColX(instance, c));
				SetPropInt(ctl, "Y", TableView_RowY(instance, r));
				SetPropInt(ctl, "W", TableView_ColW(instance, local->col + c));
				SetPropInt(ctl, "H", TableView_RowH(instance, local->row + r));
				continue;
			}

			TableView_Touch(table, r, c);
			TableCellName(prop, sizeof(prop), r, c);
			ctl = CreateAlias(instance, table, prop);
			if (!ctl)
			{
				snprintf(dbg, sizeof(dbg), "TableView: no control for cell %s", prop);
				DebugPrint(dbg, __FILE__, __LINE__, ERROR);
				continue;
			}
			SetPropStr(ctl, "Kind", "Cell");
			SetPropInt(ctl, "CellRow", r);
			SetPropInt(ctl, "CellCol", c);
			SetPropInt(ctl, "X", TableView_ColX(instance, c));
			SetPropInt(ctl, "Y", TableView_RowY(instance, r));
			SetPropInt(ctl, "W", TableView_ColW(instance, local->col + c));
			SetPropInt(ctl, "H", TableView_RowH(instance, local->row + r));
			SetPropStr(ctl, "LabelPos", "none");	/* the headers say which cell */
		}

	/* A PROPERTY HAS TO EXIST TO ACT. ColW_B is created when a column comes
	   into view, carrying the handler, so a size written from the browser
	   re-lays the grid instead of being quietly stored. Its VALUE stays the
	   default until someone drags it, so nothing here un-sparses. */
	for (c = 0; c < cols; c++)
	{
		char name[64];

		TableView_SizeName(name, sizeof(name), 1, local->col + c);
		if (!GetPropNode(instance, name))
		{
			/* ZERO MEANS UNSET, not zero-wide. Creating it at the current
			   default froze that default into every visible column, so
			   changing CellW afterwards did nothing - the columns already
			   had an answer. It exists so a write can ACT; what it says is
			   "nobody has dragged me". */
			SetPropInt(instance, name, 0);
			SetPropLong(GetPropNode(instance, name), "OnMsg", (long)TableView_OnSize);
		}
	}
	for (r = 0; r < rows; r++)
	{
		char name[64];

		TableView_SizeName(name, sizeof(name), 0, local->row + r);
		if (!GetPropNode(instance, name))
		{
			SetPropInt(instance, name, 0);		/* unset - see the columns above */
			SetPropLong(GetPropNode(instance, name), "OnMsg", (long)TableView_OnSize);
		}
	}

	/* WHERE THE GRID STARTS AND HOW IT IS SPACED. The browser half draws
	   the dividers on the cell edges, and it must not carry its own copy
	   of these numbers - they are the engine's layout, published like any
	   other property. */
	SetPropInt(instance, "GridX", CELL_X);
	SetPropInt(instance, "GridY", CELL_Y);
	SetPropInt(instance, "GridGap", CELL_G);

	TableView_Size(instance, rows, cols);
	TableView_Point(instance);

	/* how many cells actually exist. Sparse, so this is not rows x cols,
	   and watching it is how you see that. */
	{
		NodeObj prop;
		int      n = 0, pr, pc;

		for (prop = GetNextProp(table); prop; prop = GetNextSibling(prop))
			if (GetNameStr(prop) && TableCellParse(GetNameStr(prop), &pr, &pc))
				n++;
		SetPropInt(instance, "Cells", n);
	}

	snprintf(dbg, sizeof(dbg), "TableView: %dx%d window on a %dx%d table",
			 rows, cols, GetPropInt(table, "Rows"), GetPropInt(table, "Cols"));
	DebugPrint(dbg, __FILE__, __LINE__, OBJMSGHANDLING);
}

static int TableView_Step(NodeObj instance, int dRow, int dCol)
{
	TableViewData *local = (TableViewData *)GetPropLong(instance, "local");
	int r, c;

	if (!local)
		return rtrn_dropped;

	r = local->row + dRow;
	c = local->col + dCol;

	if (r < 0) r = 0;
	if (c < 0) c = 0;
	{
		NodeObj table = TableView_Data(instance);
		int rows = table ? GetPropInt(table, "Rows") : 0;
		int cols = table ? GetPropInt(table, "Cols") : 0;
		int vr   = TableView_Vis(instance, "ViewRows");
		int vc   = TableView_Vis(instance, "ViewCols");

		if (r > rows - vr) r = rows - vr;
		if (c > cols - vc) c = cols - vc;
		if (r < 0) r = 0;
		if (c < 0) c = 0;
	}

	local->row = r;
	local->col = c;

	SetPropInt(instance, "Row", r);
	SetPropInt(instance, "Col", c);

	/* RE-LAY-OUT, not just re-point. Columns have their own widths now, so
	   moving the window changes the geometry as well as which cell each
	   control stands for - scrolling onto a wide column has to make that
	   slot wide. */
	TableView_Build(instance);

	return rtrn_handled;
}

/* how much is on show. Changing it makes or drops controls - a dataflow
   changes on the fly, so the panel does too. */
static int TableView_OnView(NodeObj instance, MsgId message, NodeObj data, char *which)
{
	int v;

	if (message == msg_eof || !data)
		return rtrn_handled;

	v = GetValueInt(data);
	if (v < 1) v = 1;
	if (v > WIN_MAX) v = WIN_MAX;

	SetPropInt(instance, which, v);
	TableView_Build(instance);
	return rtrn_handled;
}

int TableView_OnViewRows(NodeObj i, MsgId m, NodeObj d) { return TableView_OnView(i, m, d, "ViewRows"); }
int TableView_OnViewCols(NodeObj i, MsgId m, NodeObj d) { return TableView_OnView(i, m, d, "ViewCols"); }

/* how big the thing underneath is. Sparse, so growing costs nothing and
   shrinking only changes what can be addressed. */
static int TableView_OnData(NodeObj instance, MsgId message, NodeObj data, char *which)
{
	NodeObj table = TableView_Data(instance);
	int v;

	if (message == msg_eof || !data || !table)
		return rtrn_handled;

	v = GetValueInt(data);
	if (v < 1) v = 1;

	SetPropInt(instance, which, v);
	SetPropInt(table, strcmp(which, "DataRows") ? "Cols" : "Rows", v);
	TableView_Build(instance);
	return rtrn_handled;
}

int TableView_OnDataRows(NodeObj i, MsgId m, NodeObj d) { return TableView_OnData(i, m, d, "DataRows"); }
int TableView_OnDataCols(NodeObj i, MsgId m, NodeObj d) { return TableView_OnData(i, m, d, "DataCols"); }

/* throw the contents away and keep the shape */
int TableView_OnClear(NodeObj instance, MsgId message, NodeObj data)
{
	NodeObj table = TableView_Data(instance);

	if (message == msg_eof || !data || !GetValueInt(data) || !table)
		return rtrn_handled;

	DataExtDrop(table);
	SetPropInt(table, "Rows", GetPropInt(instance, "DataRows"));
	SetPropInt(table, "Cols", GetPropInt(instance, "DataCols"));
	TableView_Build(instance);

	return rtrn_handled;
}

/* Locking the cells locks the panel too: the same gesture that would drag
   a divider drags the panel edge. */
int TableView_OnLock(NodeObj instance, MsgId message, NodeObj data)
{
	int locked;

	if (message == msg_eof || !data)
		return rtrn_handled;

	locked = GetValueInt(data) ? 1 : 0;
	SetPropInt(instance, "LockCells", locked);
	SetOrDeliverProp(instance, "ReservedViewResizeable", locked ? "0" : "1");

	return rtrn_handled;
}

/* A BARE TABLE. There is no "visible" flag on a control - a thing either
   exists or it does not - so this makes and unmakes them, which is what
   the viewport already does when it changes size. */
int TableView_OnShowControls(NodeObj instance, MsgId message, NodeObj data)
{
	static char *walkers[] = { "Up", "Down", "Left", "Right", "Row", "Col", NULL };
	char  me[300], path[400];
	int   show, i;

	if (message == msg_eof || !data)
		return rtrn_handled;

	show = GetValueInt(data) ? 1 : 0;
	SetPropInt(instance, "ShowControls", show);

	if (show)
	{
		/* Widget_Create is get-or-create, so this re-makes what is missing
		   and adopts everything already there */
		Widget_BuildTable(instance, TableViewPanel);
		TableView_Build(instance);
		return rtrn_handled;
	}

	if (!PathOfInstance(instance, me, sizeof(me)))
		return rtrn_handled;

	for (i = 0; walkers[i]; i++)
	{
		NodeObj ctl;

		snprintf(path, sizeof(path), "%s/%s", me, walkers[i]);
		ctl = ResolvePath(path);
		if (ctl)
			Widget_Destroy(ctl);
	}

	return rtrn_handled;
}

/* ONE DIVIDER SETS THE WHOLE GRID. Dragging between two columns is not a
   statement about those two columns - it is the column size, and every
   column takes it, the way a spreadsheet does. Locked cells refuse it. */
static int TableView_OnCellSize(NodeObj instance, MsgId message, NodeObj data, char *which)
{
	int v;

	if (message == msg_eof || !data)
		return rtrn_handled;

	if (GetPropInt(instance, "LockCells"))
	{
		DebugPrint("TableView: cells are locked - size refused",
				   __FILE__, __LINE__, OBJMSGHANDLING);
		return rtrn_handled;
	}

	v = GetValueInt(data);
	if (v < CELL_MIN) v = CELL_MIN;
	if (v > CELL_MAX) v = CELL_MAX;

	SetPropInt(instance, which, v);

	/* EVERY COLUMN, OR EVERY ROW. Each one has its own width now, set at
	   the start, so a default nothing consults is a setting that lies -
	   typing it here means "make them all this". A divider drag afterwards
	   is how one of them comes to differ. */
	{
		NodeObj table = TableView_Data(instance);
		int     isCol = !strcmp(which, "CellW");
		char    name[64];
		int     n, i;

		if (table)
		{
			n = GetPropInt(table, isCol ? "Cols" : "Rows");
			for (i = 0; i < n; i++)
			{
				TableView_SizeName(name, sizeof(name), isCol, i);
				SetPropInt(table, name, v);
			}
		}
	}

	TableView_Build(instance);
	return rtrn_handled;
}

int TableView_OnCellW(NodeObj i, MsgId m, NodeObj d) { return TableView_OnCellSize(i, m, d, "CellW"); }
int TableView_OnCellH(NodeObj i, MsgId m, NodeObj d) { return TableView_OnCellSize(i, m, d, "CellH"); }

/* ONE COLUMN, ONE ROW. The browser writes "ColW_B" or "RowH_3" - a
   property named for the thing it sizes - and this only has to lay the
   grid out again. Any property that names a column or a row lands here. */
int TableView_OnSize(NodeObj instance, MsgId message, NodeObj data)
{
	int v;

	if (message == msg_eof || !data)
		return rtrn_handled;

	if (GetPropInt(instance, "LockCells"))
	{
		DebugPrint("TableView: cells are locked - size refused",
				   __FILE__, __LINE__, OBJMSGHANDLING);
		return rtrn_handled;
	}

	v = GetValueInt(data);
	if (v < CELL_MIN) v = CELL_MIN;
	if (v > CELL_MAX) v = CELL_MAX;

	/* ON THE DATA. The widget's own property is only the doorway the
	   browser can address - the width is the column's, and the column is
	   the table's. */
	{
		NodeObj table = TableView_Data(instance);

		if (table)
			SetPropInt(table, GetNameStr(data), v);
	}
	TableView_Build(instance);
	return rtrn_handled;
}

int TableView_OnUp(NodeObj instance, MsgId message, NodeObj data)
{
	if (message == msg_eof || !GetValueInt(data)) return rtrn_handled;
	return TableView_Step(instance, -1, 0);
}

int TableView_OnDown(NodeObj instance, MsgId message, NodeObj data)
{
	if (message == msg_eof || !GetValueInt(data)) return rtrn_handled;
	return TableView_Step(instance, 1, 0);
}

int TableView_OnLeft(NodeObj instance, MsgId message, NodeObj data)
{
	if (message == msg_eof || !GetValueInt(data)) return rtrn_handled;
	return TableView_Step(instance, 0, -1);
}

int TableView_OnRight(NodeObj instance, MsgId message, NodeObj data)
{
	if (message == msg_eof || !GetValueInt(data)) return rtrn_handled;
	return TableView_Step(instance, 0, 1);
}

/* THE OBJECT HANDLES ITS OWN SERIALIZING, and it does it by answering the
   message like any other. The data object is reached by a LONG, which no
   walk and no file can carry - so this class answers, and answers by
   sending the same message one level down to the thing that holds the
   grid. Nothing in between needs to know there is a private object here. */
static int TableView_ClassMsg(NodeObj instance, MsgId message, NodeObj data)
{
	NodeObj table = TableView_Data(instance);

	if (!table || !data)
		return rtrn_unhandled;

	switch (message)
	{
		case msg_serialize:
			/* ADDS ITS PRIVATE STATE, does not claim the whole job: this
			   class has no property walk of its own, and saying "handled"
			   would stop the one at the end of the chain - taking X, Y, W,
			   H and everything else down with it. */
			DataExtSerialize(table, data);		/* answer lands on data.Text */
			return rtrn_unhandled;

		case msg_deserialize:
			DataExtDeserialize(table, data);
			TableView_Point(instance);			/* boxes stand for restored cells */
			return rtrn_unhandled;
	}

	return rtrn_unhandled;
}

int TableView_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	TableViewData *local = (TableViewData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	/* A CLONE'S ALIASES COME BACK POINTING AT THE ORIGINAL. The clone walk
	   re-points a copied link through its member map, and this widget's
	   data object is not a member - it is private, reached by a LONG - so
	   the map has nothing to remap to and the copies keep the pointers they
	   were copied with. Re-pointing at MY data object is the same call
	   scrolling makes, run once the copy has settled. */
	TableView_Point(instance);

	return rtrn_handled;
}

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	TableViewData *local = malloc(sizeof(TableViewData));

	(void) message;

	local->enabled = 1;
	local->row = 0;
	local->col = 0;

	instance = NewNode(INTEGER);
	SetName(instance, "TableView");

	Widget_Init(instance, TableViewPanel);

	SetPropInt(instance, "State", Starting);

	/* A PANEL, NOT AN ICON YOU OPEN. Same widget, same behaviour, drawn
	   where it sits so it can be dropped inside another panel. */
	/* A PANEL, NOT AN ICON YOU OPEN. Drawn where it sits, so it drops
	   into another panel. */
	SetPropStr(instance, "ReservedViewEmbedded", "1");
	SetPropStr(instance, "ReservedViewResizeable", "1");
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)TableView_Activate);

	InitPosition(instance);
	Widget_MainSize(instance, TableViewPanel);
	RegisterInstance(class, instance);

	Widget_Place(instance, data, TableViewPanel);

	/* the panel exists and the instance has a place: now the data object
	   and the controls that stand for its cells */
	TableView_Build(instance);

	return rtrn_handled;
}

/* The panel itself. The cells are not here on purpose - see the top of the
   file: a row would publish a property on the widget, and the cell values
   live in the Table. */
static WidgetItem TableViewPanel[] = {
	/* cls        prop         def  panel   x    y    w    h  label     [handler] */

	/* the main view's W/H are recomputed from the viewport in
	   TableView_Size - tight around the grid, then padded. These are the
	   3x3 starting size. */
	{ "View",     "TableView", "",  0,   0,   0, 340, 274, 0 },
	/* Settings is the only thing on the main panel besides the grid: top
	   left, and its name IS its caption - one character, the gear
	   (U+2699), so the icon is a symbol rather than a word. Help lives
	   inside it. */
	{ "View",     "\u2699",    "",  0,   0,   0, 260, 360, 0 },		/* 1 */
	{ "Help",     "objects/tableview/README.md", "", 1, 0, 0, 0, 0, 0 },

	/* --- panel 0: the grid (built in code), and walking the window --- */
	{ "MoButton", "Up",        "0", 0,  86, 160,  34, 20, LABEL_NONE, (void *)TableView_OnUp },
	{ "MoButton", "Down",      "0", 0,  86, 186,  34, 20, LABEL_NONE, (void *)TableView_OnDown },
	{ "MoButton", "Left",      "0", 0,  44, 173,  34, 20, LABEL_NONE, (void *)TableView_OnLeft },
	{ "MoButton", "Right",     "0", 0, 128, 173,  34, 20, LABEL_NONE, (void *)TableView_OnRight },

	/* where the window is - readable, wirable, and not typed into */
	{ "TextOut",  "Row",       "0", 0, 200, 163,  40, 20, LABEL_LEFT },
	{ "TextOut",  "Col",       "0", 0, 200, 189,  40, 20, LABEL_LEFT },

	/* --- panel 1: settings - everything that is not the grid itself --- */
	{ "Checkbox", "Enable",       "1", 1,  20,  20,   8,  8, LABEL_LEFT, (void *)TableView_OnEnable },
	{ "Checkbox", "LockCells",    "0", 1,  20,  46,   8,  8, LABEL_LEFT, (void *)TableView_OnLock },
	{ "Checkbox", "ShowControls", "1", 1,  20,  72,   8,  8, LABEL_LEFT, (void *)TableView_OnShowControls },

	/* how much is on show */
	{ "Textbox",  "ViewRows",    "3", 1, 100, 104,  50, 24, LABEL_LEFT, (void *)TableView_OnViewRows },
	{ "Textbox",  "ViewCols",    "3", 1, 100, 134,  50, 24, LABEL_LEFT, (void *)TableView_OnViewCols },

	/* how big the thing underneath is */
	{ "Textbox",  "DataRows",   "10", 1, 100, 168,  50, 24, LABEL_LEFT, (void *)TableView_OnDataRows },
	{ "Textbox",  "DataCols",   "10", 1, 100, 198,  50, 24, LABEL_LEFT, (void *)TableView_OnDataCols },

	/* how many cells actually exist - it is sparse, so this is not
	   DataRows x DataCols and that is the point */
	{ "TextOut",  "Cells",       "0", 1, 100, 228,  60, 20, LABEL_LEFT },

	{ "MoButton", "Clear",       "0", 1,  20, 258,  60, 20, LABEL_NONE, (void *)TableView_OnClear },

	/* the divider drag writes these - one size for every column, one for
	   every row. Shown in settings so they can also be typed. */
	{ "Textbox",  "CellW",      "80", 1, 190, 104,  50, 24, LABEL_LEFT, (void *)TableView_OnCellW },
	{ "Textbox",  "CellH",      "24", 1, 190, 134,  50, 24, LABEL_LEFT, (void *)TableView_OnCellH },

	{ NULL }
};

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	TableViewData *local = (TableViewData *)GetPropLong(instance, "local");

	(void) message; (void) data;

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
	SetClassParent(ClassSelf, "Widget");

	PublishPosition(ClassSelf);
	Widget_Publish(ClassSelf, TableViewPanel);

	PublishProp(ClassSelf, "State", PROP_LED, "1");
	PublishProp(ClassSelf, "ReservedViewEmbedded", PROP_CHECKBOX, "1");

	/* what the browser half needs to place the dividers, and the sizes
	   they set. PROP_NULL: real values, no control of their own. */
	PublishProp(ClassSelf, "GridX", PROP_NULL, "44");
	PublishProp(ClassSelf, "GridY", PROP_NULL, "30");
	PublishProp(ClassSelf, "GridGap", PROP_NULL, "6");

	PublishShow(ClassSelf, PROP_ICON, show_web_js, show_web_css);

	/* ON THE CLASS NODE: PuntToClass walks class nodes and reads ClassMsg
	   off each one, so a ClassMsg left on the library node is somewhere the
	   walk never looks. */
	SetPropLong(ClassSelf, "ClassMsg", (long)TableView_ClassMsg);

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
	SetPropStr(temp, "UUID", "c0a68bec-b8be-4b11-8205-2c1ed7d21f9e");
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)0);
	SetPropInt(temp, "State", 1);

	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");
	AddDependency(temp, "widget.object", "Widget", "1", "0");
	AddDependency(temp, "view.object", "View", "1", "0");
	AddDependency(temp, "checkbox.object", "Checkbox", "1", "0");
	AddDependency(temp, "textbox.object", "Textbox", "1", "0");
	AddDependency(temp, "label.object", "Label", "1", "0");
	AddDependency(temp, "mobutton.object", "MoButton", "1", "0");
	AddDependency(temp, "textout.object", "TextOut", "1", "0");
	AddDependency(temp, "table.object", "Table", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
