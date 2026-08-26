/*
 * Table - a data_ext whose shape is a grid.
 *
 * The cells are ordinary named properties on the instance, which is what
 * carries them through clone and export: both walk the property list
 * under IsPortableProp, and a pointer to a private grid is refused by it
 * because it is LONG-typed. So the grid IS properties, named for their
 * position, and there is exactly one holder of each value.
 *
 * Answers data_ext's verbs with a grid's reading of them; anything else
 * drops and the walk carries on up.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "callback.h"
#include "DebugPrint.h"
#include "table.h"

typedef struct InstanceData
{
	NodeObj  owner;
	MsgId    msgID;
	char    *port;

	/* An INDEX, not the data: these point at the cell properties, which
	   are still the only place a value lives. It is a LONG property, so
	   IsPortableProp refuses it and no clone or file ever carries it -
	   which is why a miss has to be ordinary and fill itself from the
	   properties rather than mean the cell is absent. */
	NodeObj *cells;
	int      rows;
	int      cols;
} InstanceData;

#ifndef TESTBUILD
static NodeObj LibrarySelf;
#endif
static NodeObj ClassSelf;

/* grow the index to hold (row, col), keeping what it already has at the
   same positions. Row-major, so widening moves every row - which is why
   this happens on growth and not on lookup. */
static void Table_Grow(InstanceData *local, int row, int col)
{
	NodeObj *grown;
	int      rows, cols, r, c;
	char     dbg[160];

	if (!local || (row < local->rows && col < local->cols))
		return;

	rows = row >= local->rows ? row + 1 : local->rows;
	cols = col >= local->cols ? col + 1 : local->cols;

	grown = calloc((size_t)rows * cols, sizeof(NodeObj));
	if (!grown)
		return;

	for (r = 0; r < local->rows; r++)
		for (c = 0; c < local->cols; c++)
			grown[r * cols + c] = local->cells[r * local->cols + c];

	snprintf(dbg, sizeof(dbg), "index grew %dx%d -> %dx%d",
			 local->rows, local->cols, rows, cols);
	DebugPrint(dbg, __FILE__, __LINE__, OBJMSGHANDLING);

	free(local->cells);
	local->cells = grown;
	local->rows  = rows;
	local->cols  = cols;
}

/* the cell at a position, made if it is not there yet - a cell exists
   because something referred to it.

   The index answers first. A miss is not "no such cell": the index is
   per-process and the properties are what a clone and a file carry, so
   an instance that arrived either way has cells and an empty index, and
   the miss path is what fills it back in. */
static NodeObj Table_Cell(NodeObj instance, int row, int col)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	NodeObj cell;
	char    name[64];

	if (!instance || row < 0 || col < 0)
		return NULL;

	if (local && row < local->rows && col < local->cols
			  && local->cells[row * local->cols + col])
		return local->cells[row * local->cols + col];

	TableCellName(name, sizeof(name), row, col);

	cell = GetPropNode(instance, name);
	if (!cell)
	{
		SetPropStr(instance, name, "");
		cell = GetPropNode(instance, name);
	}

	/* SetPropStr updates a property in place, so a node once indexed keeps
	   its address for the life of the instance */
	if (local && cell)
	{
		Table_Grow(local, row, col);
		if (row < local->rows && col < local->cols)
			local->cells[row * local->cols + col] = cell;
	}

	/* the extent follows what has been addressed */
	if (row >= GetPropInt(instance, "Rows"))
		SetPropInt(instance, "Rows", row + 1);
	if (col >= GetPropInt(instance, "Cols"))
		SetPropInt(instance, "Cols", col + 1);

	return cell;
}

/* append to a malloc'd string, growing it - returns the buffer, which
   may have moved */
static char *Table_Append(char *buf, int *len, int *cap, char *text)
{
	int need;

	if (!text)
		text = "";

	need = *len + (int)strlen(text) + 1;
	if (need > *cap)
	{
		while (*cap < need)
			*cap = *cap ? *cap * 2 : 256;
		buf = realloc(buf, *cap);
	}

	strcpy(buf + *len, text);
	*len += (int)strlen(text);

	return buf;
}

/* the grid as text: cells tab separated, rows newline separated. This is
   how a Table stores itself, which is what it has to be able to write
   out and read back. */
static char *Table_Write(NodeObj instance)
{
	NodeObj cell;
	char   *buf = NULL;
	char    name[64];
	int     len = 0, cap = 0;
	int     rows, cols, r, c;

	rows = GetPropInt(instance, "Rows");
	cols = GetPropInt(instance, "Cols");

	buf = Table_Append(buf, &len, &cap, "");

	for (r = 0; r < rows; r++)
	{
		for (c = 0; c < cols; c++)
		{
			TableCellName(name, sizeof(name), r, c);
			cell = GetPropNode(instance, name);
			buf = Table_Append(buf, &len, &cap, cell ? GetValueStr(cell) : "");
			if (c + 1 < cols)
				buf = Table_Append(buf, &len, &cap, "\t");
		}
		buf = Table_Append(buf, &len, &cap, "\n");
	}

	return buf;
}

/* Drop every cell and empty the index. DESERIALIZE means become this
   text, so cells the text does not mention must not survive it - and
   because the index points at property nodes that are about to be
   freed, it goes with them. This is the one place the index is not
   self-healing: a miss can refill it, a dangling pointer cannot. */
static void Table_Clear(NodeObj instance)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	NodeObj prop, next;
	char   *name;
	char    dbg[160];
	int     row, col, dropped = 0;

	if (local)
	{
		free(local->cells);
		local->cells = NULL;
		local->rows  = 0;
		local->cols  = 0;
	}

	for (prop = GetNextProp(instance); prop; prop = next)
	{
		next = GetNextSibling(prop);
		name = GetNameStr(prop);
		if (!name || !TableCellParse(name, &row, &col))
			continue;

		RemoveProp(instance, prop);
		DelNode(prop);
		dropped++;
	}

	SetPropInt(instance, "Rows", 0);
	SetPropInt(instance, "Cols", 0);

	if (dropped)
	{
		snprintf(dbg, sizeof(dbg), "cleared %d cell(s) from '%s'", dropped,
				 GetNameStr(instance) ? GetNameStr(instance) : "?");
		DebugPrint(dbg, __FILE__, __LINE__, OBJMSGHANDLING);
	}
}

/* and back, from the same text */
static void Table_Read(NodeObj instance, char *text)
{
	NodeObj cell;
	char   *copy, *p, *start;
	int     row = 0, col = 0;
	int     seenRows = 0, seenCols = 0;

	if (!instance || !text)
		return;

	Table_Clear(instance);

	copy = strdup(text);
	if (!copy)
		return;

	start = copy;
	for (p = copy; ; p++)
	{
		char held = *p;

		if (held != '\t' && held != '\n' && held != '\0')
			continue;

		/* text that ended on a row boundary: the empty piece after the
		   last newline is the end of it, not a cell opening a new row */
		if (held == '\0' && col == 0 && p == start)
			break;

		*p = '\0';
		/* SPARSE: an empty field is a cell that is not there. Only a
		   value makes a node; the extent is carried by Rows/Cols, not
		   by a node existing for every position. */
		if (*start)
		{
			cell = Table_Cell(instance, row, col);
			if (cell)
				SetValueStr(cell, start);
		}
		if (row >= seenRows) seenRows = row + 1;
		if (col >= seenCols) seenCols = col + 1;

		if (held == '\t')
			col++;
		else
		{
			row++;
			col = 0;
		}

		if (held == '\0')
			break;
		start = p + 1;
	}

	/* the extent came from the text's shape, not from which cells
	   happened to hold something */
	if (seenRows > GetPropInt(instance, "Rows"))
		SetPropInt(instance, "Rows", seenRows);
	if (seenCols > GetPropInt(instance, "Cols"))
		SetPropInt(instance, "Cols", seenCols);

	free(copy);
}

static int Table_MessageFunc(NodeObj instance, MsgId message, NodeObj data)
{
	NodeObj cell;
	char   *text;
	char    dbg[200];

	switch (message)
	{
		case DATA_EXT_ADDRESS_MSG:
			cell = Table_Cell(instance, GetPropInt(data, "Row"),
			                            GetPropInt(data, "Col"));
			if (!cell)
			{
				snprintf(dbg, sizeof(dbg), "ADDRESS on '%s' off the grid: %d,%d",
						 GetNameStr(instance) ? GetNameStr(instance) : "?",
						 GetPropInt(data, "Row"), GetPropInt(data, "Col"));
				DebugPrint(dbg, __FILE__, __LINE__, ERROR);
				return rtrn_dropped;
			}
			SetPropLong(data, "Node", (long)cell);
			snprintf(dbg, sizeof(dbg), "ADDRESS '%s' R%dC%d",
					 GetNameStr(instance) ? GetNameStr(instance) : "?",
					 GetPropInt(data, "Row"), GetPropInt(data, "Col"));
			DebugPrint(dbg, __FILE__, __LINE__, OBJMSGHANDLING);
			return rtrn_handled;

		case DATA_EXT_DROP_MSG:
			/* keep the shape, lose the contents */
			Table_Clear(instance);
			return rtrn_handled;

		case DATA_EXT_SHAPE_MSG:
			SetPropInt(data, "Rows", GetPropInt(instance, "Rows"));
			SetPropInt(data, "Cols", GetPropInt(instance, "Cols"));
			return rtrn_handled;

		case DATA_EXT_SERIALIZE_MSG:
			text = Table_Write(instance);
			SetPropStr(data, "Text", text ? text : "");
			snprintf(dbg, sizeof(dbg), "SERIALIZE '%s' %dx%d, %d bytes",
					 GetNameStr(instance) ? GetNameStr(instance) : "?",
					 GetPropInt(instance, "Rows"), GetPropInt(instance, "Cols"),
					 text ? (int)strlen(text) : 0);
			DebugPrint(dbg, __FILE__, __LINE__, OBJMSGHANDLING);
			if (text)
				free(text);
			return rtrn_handled;

		case DATA_EXT_DESERIALIZE_MSG:
			Table_Read(instance, GetPropStr(data, "Text"));
			snprintf(dbg, sizeof(dbg), "DESERIALIZE '%s' -> %dx%d",
					 GetNameStr(instance) ? GetNameStr(instance) : "?",
					 GetPropInt(instance, "Rows"), GetPropInt(instance, "Cols"));
			DebugPrint(dbg, __FILE__, __LINE__, OBJMSGHANDLING);
			return rtrn_handled;
	}

	return rtrn_dropped;
}

int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	(void) instance; (void) message; (void) data;

	return rtrn_dropped;
}

static int Table_ClassMsg(NodeObj instance, MsgId message, NodeObj data)
{
	return Table_MessageFunc(instance, message, data);
}

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj       instance = NewNode(INTEGER);
	NodeObj       entry;
	InstanceData *local;

	(void) message;

	local = malloc(sizeof(InstanceData));
	memset(local, 0, sizeof(InstanceData));

	if (data)
	{
		local->owner = (NodeObj) GetPropLong(data, "Owner");
		local->msgID = (MsgId)   GetPropLong(data, "MsgBase");
		local->port  = GetPropStr(data, "Port");
		local->port  = local->port ? strdup(local->port) : NULL;
	}

	SetName(instance, "Table");
	SetPropLong(instance, "local", (long)local);

	/* the extent, as properties, so a clone and an export carry it with
	   the cells */
	SetPropInt(instance, "Rows", 0);
	SetPropInt(instance, "Cols", 0);

	/* a door onto the class chain, so what Table drops data_ext answers */
	SetPropStr(instance, "Msg", "");
	entry = GetPropNode(instance, "Msg");
	SetPropLong(entry, "OnMsg", (long)PuntToClass);

	RegisterInstance(class, instance);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) message; (void) data;

	if (local)
	{
		if (local->port)
			free(local->port);
		free(local->cells);			/* the index, not the cells */
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	(void) message; (void) data;

	SetName(class, "Table");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "data_ext");

	SetPropLong(ClassSelf, "ClassMsg", (long)Table_ClassMsg);

	return rtrn_handled;
}

int ClassEnd(NodeObj library, MsgId message, NodeObj data)
{
	(void) message; (void) data;

	UnRegisterClass(library, ClassSelf);
	ClassSelf = NULL;
	return rtrn_handled;
}

#ifndef TESTBUILD		/* crti.o defines these; a test links as a program */
void _init()
{
	NodeObj temp = NewNode(INTEGER);

	SetName(temp, "Table");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "c4d81e97-5a62-4f30-8b71-2d9e6a0c53f4");
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)0);
	SetPropInt(temp, "State", 1);

	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");
	AddDependency(temp, "data_ext.object", "data_ext", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
#endif

#ifdef TESTBUILD
/* round-trip: write a grid out, read it into a second table, compare */
static NodeObj TestTable(void)
{
	NodeObj t = NewNode(INTEGER);

	SetName(t, "Table");
	SetPropInt(t, "Rows", 0);
	SetPropInt(t, "Cols", 0);
	return t;
}

static char *CellAt(NodeObj t, int r, int c)
{
	char name[64];
	NodeObj cell;

	TableCellName(name, sizeof(name), r, c);
	cell = GetPropNode(t, name);
	return cell ? GetValueStr(cell) : NULL;
}

int main(void)
{
	NodeObj src = TestTable();
	NodeObj dst = TestTable();
	NodeObj bag = NewNode(INTEGER);
	char   *text, *a, *b;
	int     r, c, fails = 0;
	char    want[64];

	/* fill 3x4 */
	for (r = 0; r < 3; r++)
		for (c = 0; c < 4; c++)
		{
			snprintf(want, sizeof(want), "v%d%d", r, c);
			SetValueStr(Table_Cell(src, r, c), want);
		}

	if (GetPropInt(src, "Rows") != 3 || GetPropInt(src, "Cols") != 4)
	{
		printf("FAIL shape: %dx%d, wanted 3x4\n",
		       GetPropInt(src, "Rows"), GetPropInt(src, "Cols"));
		fails++;
	}

	text = Table_Write(src);
	Table_Read(dst, text);

	if (GetPropInt(dst, "Rows") != 3 || GetPropInt(dst, "Cols") != 4)
	{
		printf("FAIL read-back shape: %dx%d, wanted 3x4\n",
		       GetPropInt(dst, "Rows"), GetPropInt(dst, "Cols"));
		fails++;
	}

	for (r = 0; r < 3; r++)
		for (c = 0; c < 4; c++)
		{
			a = CellAt(src, r, c);
			b = CellAt(dst, r, c);
			if (!a || !b || strcmp(a, b))
			{
				printf("FAIL cell %d,%d: '%s' -> '%s'\n", r, c,
				       a ? a : "(none)", b ? b : "(none)");
				fails++;
			}
		}

	/* SPARSE: a cell with no value needs no node, and the extent still
	   has to survive it */
	SetValueStr(Table_Cell(src, 1, 2), "");
	free(text);
	text = Table_Write(src);
	Table_Read(dst, text);
	{ char probe[32]; TableCellName(probe, sizeof(probe), 1, 2);
	if (GetPropNode(dst, probe))
	{
		printf("FAIL empty cell made a node: '%s'\n", CellAt(dst, 1, 2));
		fails++;
	} }
	if (GetPropInt(dst, "Cols") != 4)
	{
		printf("FAIL empty cell lost a column: %d\n", GetPropInt(dst, "Cols"));
		fails++;
	}

	/* SPARSE: two values in a 3x4 grid means two nodes, and the shape
	   still comes back 3x4 */
	{
		NodeObj sp = TestTable(), sp2 = TestTable(), prop;
		int r2, c2, nodes = 0;
		char *t2;

		SetValueStr(Table_Cell(sp, 0, 0), "corner");
		SetValueStr(Table_Cell(sp, 2, 3), "far");

		t2 = Table_Write(sp);
		Table_Read(sp2, t2);

		for (prop = GetNextProp(sp2); prop; prop = GetNextSibling(prop))
			if (GetNameStr(prop) && TableCellParse(GetNameStr(prop), &r2, &c2))
				nodes++;

		if (nodes != 2)
		{
			printf("FAIL sparse: %d cell nodes for 2 values\n", nodes);
			fails++;
		}
		if (GetPropInt(sp2, "Rows") != 3 || GetPropInt(sp2, "Cols") != 4)
		{
			printf("FAIL sparse shape: %dx%d, wanted 3x4\n",
			       GetPropInt(sp2, "Rows"), GetPropInt(sp2, "Cols"));
			fails++;
		}
		if (!CellAt(sp2, 0, 0) || strcmp(CellAt(sp2, 0, 0), "corner")
		 || !CellAt(sp2, 2, 3) || strcmp(CellAt(sp2, 2, 3), "far"))
		{
			printf("FAIL sparse values\n");
			fails++;
		}
		if (!fails)
			printf("sparse: 2 nodes hold a 3x4 grid\n");
		free(t2);
	}

	free(text);
	(void) bag;

	printf(fails ? "%d FAILURES\n" : "round trip ok\n", fails);
	return fails ? 1 : 0;
}
#endif
