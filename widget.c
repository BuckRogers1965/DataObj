#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "callback.h"
#include "sched.h"
#include "widget.h"

typedef int (*WidgetActivate)(NodeObj, MsgId, NodeObj);

NodeObj Widget_Create(NodeObj container, char *cls, char *name)
{
	char cpath[256], path[320];
	NodeObj inst = CreateObject(container, cls);

	if (!inst)
		return NULL;

	if (!name || !name[0])
		name = cls;
	SetPropStr(inst, "Name", name);

	/* register its path so it resolves like any placed object - now it can
	   hold its own children (its panel, an inner host, ...) */
	if (PathOfInstance(container, cpath, sizeof(cpath)))
	{
		snprintf(path, sizeof(path), "%s/%s", cpath, name);
		RegisterPath(path, inst);
	}
	return inst;
}

void Widget_Destroy(NodeObj instance)
{
	char path[320];

	if (!instance)
		return;

	/* undo the register BEFORE the node is freed, or the namespace keeps a
	   dangling entry pointing at freed memory (PathOfInstance verifies the
	   entry resolves back, so an unregistered instance simply skips this) */
	if (PathOfInstance(instance, path, sizeof(path)))
		UnregisterPath(path);

	DeleteInstance(instance);
}

void Widget_Port(NodeObj instance, char *name, char *initial, void *handler)
{
	NodeObj port;

	SetPropStr(instance, name, initial);
	port = GetPropNode(instance, name);
	SetPropLong(port, "OnMsg", (long)handler);
}

void Widget_Reflect(NodeObj src, char *sp, NodeObj dst, char *dp)
{
	char *cur;

	Connect(src, sp, dst, dp);
	cur = GetPropStr(src, sp);
	if (cur)
		SetOrDeliverProp(dst, dp, cur);
}

NodeObj Widget_Ctl(NodeObj container, NodeObj target, char *cls, char *prop,
				   int x, int y, int w, int h)
{
	/* create, name (after its property), and register the control in one call */
	NodeObj c = Widget_Create(container, cls, prop);
	if (!c)
		return NULL;

	SetPropInt(c, "X", x);
	SetPropInt(c, "Y", y);
	SetPropInt(c, "W", w);					/* w/h ARE the size, Textbox too */
	SetPropInt(c, "H", h);
	if (prop && prop[0])
		SetPropStr(c, "Label", prop);

	if (strcmp(cls, "MoButton") == 0)
		Connect(c, "Out", target, prop);			/* a command: press writes prop */
	else if (strcmp(cls, "Button") == 0)
		Connect(c, "Out", target, "Activate");
	else if (strcmp(cls, "Markdown") == 0)
		;											/* loaded on open, not wired here */
	else if (strcmp(cls, "LED") == 0 || strcmp(cls, "TextOut") == 0
			 || strcmp(cls, "Label") == 0 || strcmp(cls, "VUMeter") == 0)
		Widget_Reflect(target, prop, c, "Value");	/* a readout */
	else if (strcmp(cls, "Dropdown") == 0)
	{
		char listprop[64];
		snprintf(listprop, sizeof(listprop), "%sList", prop);
		Connect(c, "Value", target, prop);			/* the pick drives prop */
		Widget_Reflect(target, listprop, c, "Items");	/* options from prop+List */
		SetOrDeliverProp(c, "Value", GetPropStr(target, prop));
	}
	else										/* Checkbox / Textbox */
	{
		Connect(c, "Value", target, prop);			/* control edits prop */
		Widget_Reflect(target, prop, c, "In");		/* prop reflects into control */
	}

	return c;
}

NodeObj Widget_SubPanel(NodeObj panel, char *name, int x, int y, int w, int h)
{
	NodeObj v = Widget_Create(panel, "View", name);
	if (!v)
		return NULL;
	SetPropInt(v, "X", x);
	SetPropInt(v, "Y", y);
	SetPropInt(v, "W", w);
	SetPropInt(v, "H", h);
	return v;
}

/* read a whole file into a malloc'd NUL-terminated string (caller frees) */
static char *Widget_ReadFile(char *path)
{
	FILE *f = fopen(path, "rb");
	long  n;
	char *buf;

	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n < 0)
	{
		fclose(f);
		return NULL;
	}
	buf = malloc(n + 1);
	if (!buf)
	{
		fclose(f);
		return NULL;
	}
	n = (long)fread(buf, 1, n, f);
	buf[n] = '\0';
	fclose(f);
	return buf;
}

/* the Help panel was opened: read its stored README from disk into the Help
   box's Value (resolved by path, so the write lands where the client subscribes) */
static int Widget_OnHelpOpen(NodeObj view, MsgId message, NodeObj data)
{
	char vpath[256], mpath[320];
	NodeObj box;
	char *file, *md;

	if (message == msg_eof || !GetValueInt(data))
		return rtrn_handled;			/* only on OPEN (-> 1) */

	file = GetPropStr(view, "HelpFile");
	if (!file || !file[0] || !PathOfInstance(view, vpath, sizeof(vpath)))
		return rtrn_handled;
	snprintf(mpath, sizeof(mpath), "%s/HelpText", vpath);
	box = ResolvePath(mpath);
	if (!box)
		return rtrn_handled;

	md = Widget_ReadFile(file);
	SetPropStr(box, "Value", md ? md : "");
	if (md)
		free(md);
	return rtrn_handled;
}

NodeObj Widget_AddHelp(NodeObj instance, char *helpFile)
{
	int     h = GetPropInt(instance, "H");
	NodeObj help = Widget_SubPanel(instance, "Help", 15, h - 60, HELP_W, HELP_H);
	NodeObj openPort;

	if (!help)
		return NULL;

	SetPropStr(help, "HelpFile", helpFile ? helpFile : "");
	Widget_Ctl(help, help, "Markdown", "HelpText",
			   10, 10, HELP_W - HELP_W_OFF, HELP_H - HELP_H_OFF);

	openPort = GetPropNode(help, "ReservedViewOpen");
	if (openPort)
		SetPropLong(openPort, "OnMsg", (long)Widget_OnHelpOpen);
	return help;
}

void Widget_Build(NodeObj instance, WidgetCtl *table, NodeObj *sub, int nsub)
{
	int i;

	(void) instance;
	for (i = 0; table[i].cls; i++)
	{
		WidgetCtl *t = &table[i];
		NodeObj container = (t->panel >= 0 && t->panel < nsub) ? sub[t->panel] : sub[0];
		if (container)
			Widget_Ctl(container, sub[0], t->cls, t->prop,
					   t->x, t->y, t->w, t->h);
	}
}

/* ---- the fully declarative builder ---- */

static char *Widget_LabelWord(int label)
{
	switch (label)
	{
	case LABEL_LEFT:   return "left";
	case LABEL_RIGHT:  return "right";
	case LABEL_TOP:    return "top";
	case LABEL_BOTTOM: return "bottom";
	default:           return "none";
	}
}

void Widget_MainSize(NodeObj instance, WidgetItem *table)
{
	if (!instance || !table || !table[0].cls || strcmp(table[0].cls, "View") != 0)
		return;
	if (table[0].w) SetPropInt(instance, "W", table[0].w);
	if (table[0].h) SetPropInt(instance, "H", table[0].h);
}

#define WIDGET_MAX_PANELS 16

void Widget_BuildTable(NodeObj instance, WidgetItem *table)
{
	NodeObj panels[WIDGET_MAX_PANELS];
	int np = 0, i;

	if (!instance || !table)
		return;

	for (i = 0; table[i].cls; i++)
	{
		WidgetItem *t = &table[i];
		NodeObj parent = (t->panel >= 0 && t->panel < np) ? panels[t->panel] : instance;

		if (strcmp(t->cls, "View") == 0)		/* a panel */
		{
			if (np >= WIDGET_MAX_PANELS)
				continue;
			if (np == 0)						/* the main view IS the widget;
											   its size was set in InstanceStart */
				panels[np++] = instance;
			else
				panels[np++] = Widget_SubPanel(parent, t->prop, t->x, t->y, t->w, t->h);
		}
		else if (strcmp(t->cls, "Help") == 0)	/* the standard help sub-view */
		{
			if (np < WIDGET_MAX_PANELS)
				panels[np++] = Widget_AddHelp(instance, t->prop);
		}
		else									/* a control */
		{
			NodeObj c = Widget_Ctl(parent, instance, t->cls, t->prop,
								   t->x, t->y, t->w, t->h);
			if (c && t->label != LABEL_NONE)
				SetPropStr(c, "LabelPos", Widget_LabelWord(t->label));
		}
	}
}

/* the widget type a control class publishes as - so the table's control class
   is the single source, and ClassStart never restates it */
static int Widget_PropType(const char *cls)
{
	if (!strcmp(cls, "Textbox"))  return PROP_TEXTBOX;
	if (!strcmp(cls, "LED"))      return PROP_LED;
	if (!strcmp(cls, "Checkbox")) return PROP_CHECKBOX;
	if (!strcmp(cls, "Slider"))   return PROP_SLIDER;
	if (!strcmp(cls, "VUMeter"))  return PROP_VUMETER;
	if (!strcmp(cls, "TextOut"))  return PROP_TEXTOUT;
	if (!strcmp(cls, "Knob"))     return PROP_KNOB;
	if (!strcmp(cls, "Label"))    return PROP_LABEL;
	if (!strcmp(cls, "Dropdown")) return PROP_MENU;
	if (!strcmp(cls, "Markdown")) return PROP_MARKDOWN;
	if (!strcmp(cls, "HTML"))     return PROP_HTML;
	if (!strcmp(cls, "Image"))    return PROP_IMAGE;
	return PROP_NULL;					/* MoButton / Button: a plain port */
}

void Widget_Publish(NodeObj class, WidgetItem *table)
{
	int i;

	if (!class || !table)
		return;

	for (i = 0; table[i].cls; i++)
	{
		WidgetItem *t = &table[i];

		if (!strcmp(t->cls, "View") || !strcmp(t->cls, "Help"))
			continue;						/* panels carry no property */

		/* everything subscribable: one direction, value pushed to whoever
		   subscribes. `def` is the class default (the instance re-sets it). */
		PublishProp(class, t->prop, "data", Widget_PropType(t->cls),
					t->def ? t->def : "");
	}
}

void Widget_Init(NodeObj instance, WidgetItem *table)
{
	int i;

	if (!instance || !table)
		return;

	for (i = 0; table[i].cls; i++)
	{
		WidgetItem *t = &table[i];

		if (!strcmp(t->cls, "View") || !strcmp(t->cls, "Help"))
			continue;						/* panels carry no property */

		/* a reactive port where the row names a handler, a plain property
		   (which the object just reads) otherwise - both at the row's value */
		if (t->handler)
			Widget_Port(instance, t->prop, t->def ? t->def : "", t->handler);
		else
			SetPropStr(instance, t->prop, t->def ? t->def : "");
	}
}

/* ---- the deferred panel build, shared by every widget ---- */

int Widget_BuildOnce(NodeObj instance, WidgetItem *table)
{
	if (!instance || !table || GetPropInt(instance, "PanelBuilt"))
		return 0;
	SetPropInt(instance, "PanelBuilt", 1);
	Widget_BuildTable(instance, table);
	return 1;
}

/* one tick after creation the bridge has placed this instance and registered
   its path, so the controls and sub-views created inside it resolve. Build the
   panel once (unless an early Activate already did), then run the object's own
   placement setup by calling its Activate with msg_initialize. */
static int Widget_BuildTask(NodeObj instance, NodeObj data, int msgid)
{
	WidgetItem *table = (WidgetItem *)GetPropLong(instance, "WidgetTable");

	(void) data;
	(void) msgid;

	if (Widget_BuildOnce(instance, table))
	{
		WidgetActivate act = (WidgetActivate)GetPropLong(instance, "Activate");
		if (act)
			act(instance, msg_initialize, NULL);
	}
	return rtrn_handled;
}

void Widget_DeferBuild(NodeObj instance, WidgetItem *table)
{
	NodeObj task;

	if (!instance || !table)
		return;
	SetPropLong(instance, "WidgetTable", (long)table);
	task = CreateTask(ObjGetTaskList());
	SetPropLong(instance, "WidgetBuildTask", (long)task);
	AddTaskMilli(task, 1, (FuncPtr)Widget_BuildTask, msg_send, instance);
}

void Widget_CancelBuild(NodeObj instance)
{
	TaskObj task = (TaskObj)GetPropLong(instance, "WidgetBuildTask");

	if (task)
	{
		RemoveTask(task);
		SetPropLong(instance, "WidgetBuildTask", 0);
	}
}

/* the quiet build - panel only, no Activate (so a source doesn't start acting
   the moment it is placed) */
static int Widget_BuildTaskQuiet(NodeObj instance, NodeObj data, int msgid)
{
	WidgetItem *table = (WidgetItem *)GetPropLong(instance, "WidgetTable");

	(void) data;
	(void) msgid;

	Widget_BuildOnce(instance, table);
	return rtrn_handled;
}

void Widget_DeferBuildQuiet(NodeObj instance, WidgetItem *table)
{
	NodeObj task;

	if (!instance || !table)
		return;
	SetPropLong(instance, "WidgetTable", (long)table);
	task = CreateTask(ObjGetTaskList());
	SetPropLong(instance, "WidgetBuildTask", (long)task);
	AddTaskMilli(task, 1, (FuncPtr)Widget_BuildTaskQuiet, msg_send, instance);
}
