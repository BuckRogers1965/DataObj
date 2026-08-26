#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"
#include "data_ext.h"
#include "table.h"

/*

TableView: a window onto a Table.

The widget owns a Table of its own and shows WIN x WIN of its cells. It
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

#define GRID   10		/* the Table underneath */
#define WIN     3		/* how much of it is on show */

#define CELL_W  80
#define CELL_H  24		/* a Textbox is 24 high */
#define CELL_G   6
#define HDR_W   26		/* the row-number column */
#define HDR_H   16
#define CELL_X  (12 + HDR_W + CELL_G)	/* cells start right of the row headers */
#define HDR_Y   44		/* the column numbers */
#define CELL_Y  66

typedef struct TableViewData
{
	int enabled;
	int row, col;		/* where the window sits in the Table */
} TableViewData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static WidgetItem TableViewPanel[];

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

/* a member of mine carrying this Slot - the controls are found by what they
   recorded, never by a name I guessed */
static NodeObj TableView_Slotted(NodeObj instance, char *kind, int slot)
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
		if (GetPropInt(inst, "Slot") == slot)
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
static void TableView_Point(NodeObj instance)
{
	TableViewData *local = (TableViewData *)GetPropLong(instance, "local");
	NodeObj table, ctl, cell;
	char    prop[64], num[32], dbg[200];
	int     r, c;

	if (!local)
		return;

	table = TableView_Data(instance);
	if (!table)
		return;

	for (r = 0; r < WIN; r++)
		for (c = 0; c < WIN; c++)
		{
			TableView_Touch(table, local->row + r, local->col + c);
			snprintf(prop, sizeof(prop), TABLE_CELL_FORMAT, local->row + r, local->col + c);

			ctl = TableView_Slotted(instance, "Cell", r * WIN + c);
			if (!ctl)
				continue;

			AliasProperty(ctl, table, prop);

			/* MOVING THE LINK IS NOT ENOUGH. A read does not resolve
			   through a link, so whoever is showing this control is still
			   showing what was last written INTO it. Write the cell's own
			   value through the control: harmless as data (it is already
			   that value) and it produces the fan-out that repaints. An
			   absent cell is sparse, and clears the box. */
			cell = GetPropNode(table, prop);
			SetOrDeliverProp(ctl, "Value", cell && GetValueStr(cell) ? GetValueStr(cell) : "");
		}

	for (c = 0; c < WIN; c++)
	{
		snprintf(num, sizeof(num), "%d", local->col + c);
		ctl = TableView_Slotted(instance, "ColHead", c);
		if (ctl)
			SetPropStr(ctl, "Value", num);
	}
	for (r = 0; r < WIN; r++)
	{
		snprintf(num, sizeof(num), "%d", local->row + r);
		ctl = TableView_Slotted(instance, "RowHead", r);
		if (ctl)
			SetPropStr(ctl, "Value", num);
	}

	snprintf(dbg, sizeof(dbg), "TableView: window now at %d,%d", local->row, local->col);
	DebugPrint(dbg, __FILE__, __LINE__, OBJMSGHANDLING);
}

/* a header: a Label showing which row or column this is */
static void TableView_Head(NodeObj instance, char *kind, int slot, int x, int y, int w)
{
	NodeObj lbl = CreateObject(instance, "Label", NULL);

	if (!lbl)
		return;

	SetPropStr(lbl, "Kind", kind);
	SetPropInt(lbl, "Slot", slot);
	SetPropInt(lbl, "X", x);
	SetPropInt(lbl, "Y", y);
	SetPropInt(lbl, "W", w);
	SetPropInt(lbl, "H", HDR_H);
	SetPropStr(lbl, "LabelPos", "none");	/* the number IS the label */
}

/* The data, the headers and the window onto it. Runs after Widget_Place,
   because a member and an alias both need this instance to have a place. */
static void TableView_Build(NodeObj instance)
{
	NodeObj table, ctl;
	char    prop[64], dbg[200];
	int     r, c;

	table = TableView_Data(instance);
	if (!table)
	{
		table = TableView_MakeData(instance);
		if (!table)
			return;
		SetPropInt(table, "Rows", GRID);
		SetPropInt(table, "Cols", GRID);
	}

	for (c = 0; c < WIN; c++)
		if (!TableView_Slotted(instance, "ColHead", c))
			TableView_Head(instance, "ColHead", c,
						   CELL_X + c * (CELL_W + CELL_G), HDR_Y, CELL_W);
	for (r = 0; r < WIN; r++)
		if (!TableView_Slotted(instance, "RowHead", r))
			TableView_Head(instance, "RowHead", r,
						   12, CELL_Y + r * (CELL_H + CELL_G) + 4, HDR_W);

	for (r = 0; r < WIN; r++)
		for (c = 0; c < WIN; c++)
		{
			if (TableView_Slotted(instance, "Cell", r * WIN + c))
				continue;

			TableView_Touch(table, r, c);
			snprintf(prop, sizeof(prop), TABLE_CELL_FORMAT, r, c);
			ctl = CreateAlias(instance, table, prop);
			if (!ctl)
			{
				snprintf(dbg, sizeof(dbg), "TableView: no control for cell %s", prop);
				DebugPrint(dbg, __FILE__, __LINE__, ERROR);
				continue;
			}
			SetPropStr(ctl, "Kind", "Cell");
			SetPropInt(ctl, "Slot", r * WIN + c);
			SetPropInt(ctl, "X", CELL_X + c * (CELL_W + CELL_G));
			SetPropInt(ctl, "Y", CELL_Y + r * (CELL_H + CELL_G));
			SetPropInt(ctl, "W", CELL_W);
			SetPropInt(ctl, "H", CELL_H);
			SetPropStr(ctl, "LabelPos", "none");	/* the headers say which cell */
		}

	TableView_Point(instance);

	snprintf(dbg, sizeof(dbg), "TableView: %dx%d window on a %dx%d Table",
			 WIN, WIN, GRID, GRID);
	DebugPrint(dbg, __FILE__, __LINE__, OBJMSGHANDLING);
}

/* Walk the window one step. Clamped so it always lies on the Table, and
   the Row/Col properties are kept as the readable position. */
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
	if (r > GRID - WIN) r = GRID - WIN;
	if (c > GRID - WIN) c = GRID - WIN;

	local->row = r;
	local->col = c;

	SetPropInt(instance, "Row", r);
	SetPropInt(instance, "Col", c);
	TableView_Point(instance);

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

/* THE OBJECT HANDLES ITS OWN SERIALIZING. The data object is reached by a
   LONG, which no walk and no file can carry, so writing it out is this
   class's own business - and it does not do it itself either: it asks the
   thing that holds the grid to write the grid. */
char *TableView_Write(NodeObj instance)
{
	NodeObj table = TableView_Data(instance);
	NodeObj bag;
	char   *text = NULL, *got;

	if (!table)
		return NULL;

	bag = NewNode(INTEGER);
	SetName(bag, "Self");
	DataExtSerialize(table, bag);
	got = GetPropStr(bag, "Text");
	if (got)
		text = strdup(got);
	DelNode(bag);

	return text;
}

void TableView_Read(NodeObj instance, char *text)
{
	NodeObj table = TableView_Data(instance);
	NodeObj bag;

	if (!table || !text)
		return;

	bag = NewNode(INTEGER);
	SetName(bag, "Self");
	SetPropStr(bag, "Text", text);
	DataExtDeserialize(table, bag);
	DelNode(bag);

	TableView_Point(instance);		/* the boxes now stand for restored cells */
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
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)TableView_Activate);

	InitPosition(instance);
	Widget_MainSize(instance, TableViewPanel);
	RegisterInstance(class, instance);

	Widget_Place(instance, data, TableViewPanel);

	/* the panel exists and the instance has a path: now the data and the
	   controls that stand for it */
	TableView_Build(instance);

	return rtrn_handled;
}

/* The panel itself. The cells are not here on purpose - see the top of the
   file: a row would publish a property on the widget, and the cell values
   live in the Table. */
static WidgetItem TableViewPanel[] = {
	/* cls        prop         def  panel   x    y    w    h  label     [handler] */
	/* Laid out once. Controls reach 296 wide and 214 tall - the cells, the
	   walk buttons, and the clear space the Help icon needs under the
	   lowest control. The view is that +50 W and +50 H. Padding only ever
	   adds, and it never moves a control. */
	{ "View",     "TableView", "",  0,   0,   0, 346, 264, 0 },
	{ "Help",     "objects/tableview/README.md", "", 0, 0, 0, 0, 0, 0 },

	{ "Checkbox", "Enable",    "1", 0, 288,  14,   8,  8, LABEL_LEFT, (void *)TableView_OnEnable },
	{ "MoButton", "Up",        "0", 0,  86, 160,  34, 20, LABEL_NONE, (void *)TableView_OnUp },
	{ "MoButton", "Down",      "0", 0,  86, 186,  34, 20, LABEL_NONE, (void *)TableView_OnDown },
	{ "MoButton", "Left",      "0", 0,  44, 173,  34, 20, LABEL_NONE, (void *)TableView_OnLeft },
	{ "MoButton", "Right",     "0", 0, 128, 173,  34, 20, LABEL_NONE, (void *)TableView_OnRight },

	/* where the window is - readable, wirable, and not typed into */
	{ "TextOut",  "Row",       "0", 0, 200, 163,  40, 20, LABEL_LEFT },
	{ "TextOut",  "Col",       "0", 0, 200, 189,  40, 20, LABEL_LEFT },

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

	/* function pointers on the class node, the same way InstanceStart is
	   reached: a class that can write itself says so here */
	SetPropLong(ClassSelf, "Serialize", (long)TableView_Write);
	SetPropLong(ClassSelf, "Deserialize", (long)TableView_Read);

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
