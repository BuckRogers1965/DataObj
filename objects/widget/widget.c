#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "callback.h"
#include "sched.h"
#include "DebugPrint.h"
#define WIDGET_IMPL
#include "widget.h"



/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
void Widget_Port(NodeObj instance, char *name, char *initial, void *handler)
{
	/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
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
	/* A control POINTS AT the property it shows. It is the same control
	   it would be anywhere else, standing for a different property - its
	   class does not change and nothing new goes on the wire, so a client
	   sees exactly what it always saw. The link is the engine's business:
	   everything that resolves a property (Connect, SndMsg,
	   SetOrDeliverProp, and through Connect the Bridge's subscribe) lands
	   on the original, so a client asks about the control and is answered
	   about the control.

	   Subscribing the two to each other instead made them clones - two
	   nodes for one datum, each announcing to the other, terminating only
	   because SetProp* stopped when the values matched. One node has
	   nothing to keep in step and so nothing to stop it.

	   Markdown is the one that stands for nothing: it loads its content
	   on open. */
	int     points = (strcmp(cls, "Markdown") != 0);

	/* create, name (after its property), and register the control in one call */
	NodeObj c = Widget_Create(container, cls, prop);
	int     made = !Widget_WasAdopted();

	if (!c)
		return NULL;

	/* geometry and label are the TABLE's opinion, and only for a control
	   this call actually made. An adopted one came from a load carrying
	   the arrangement someone saved, and that wins - the build is here for
	   the wiring below, which is what a file cannot carry. */
	if (made)
	{
		SetPropInt(c, "X", x);
		SetPropInt(c, "Y", y);
		SetPropInt(c, "W", w);					/* w/h ARE the size, Textbox too */
		SetPropInt(c, "H", h);
		if (prop && prop[0])
			SetPropStr(c, "Label", prop);
	}

	if (strcmp(cls, "Markdown") == 0)
		;											/* loaded on open, not wired here */
	else if (strcmp(cls, "Dropdown") == 0)
	{
		char listprop[64];
		snprintf(listprop, sizeof(listprop), "%sList", prop);
		LinkPropertyAs(c, "Value", target, prop);		/* the pick IS the property */
		Widget_Reflect(target, listprop, c, "Items");	/* options are a different property */
	}
	else if (points)
		/* re-applied every build: a link is a pointer, and a saved flow
		   cannot carry one */
		LinkPropertyAs(c, "Value", target, prop);

	return c;
}

NodeObj Widget_SubPanel(NodeObj panel, char *name, int x, int y, int w, int h)
{
	NodeObj v = Widget_Create(panel, "View", name);
	int     made = !Widget_WasAdopted();

	if (!v)
		return NULL;
	if (!made)
		return v;				/* already there: its saved geometry stands */
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
	char vpath[256], mpath[320], dbg[420];
	NodeObj box;
	char *file, *md;

	if (message == msg_eof || !GetValueInt(data))
		return rtrn_handled;			/* only on OPEN (-> 1) */

	/* every way this can give up says so: help that silently fails to
	   appear is indistinguishable from help that has nothing to say */
	file = GetPropStr(view, "HelpFile");
	if (!file || !file[0])
	{
		DebugPrint("HELP: opened, but the view carries no HelpFile",
				   __FILE__, __LINE__, ERROR);
		return rtrn_handled;
	}
	if (!PathOfInstance(view, vpath, sizeof(vpath)))
	{
		snprintf(dbg, sizeof(dbg), "HELP: the help view has no path, so its "
				 "HelpText box cannot be found (HelpFile '%.200s')", file);
		DebugPrint(dbg, __FILE__, __LINE__, ERROR);
		return rtrn_handled;
	}

	snprintf(mpath, sizeof(mpath), "%s/HelpText", vpath);
	box = ResolvePath(mpath);
	if (!box)
	{
		snprintf(dbg, sizeof(dbg), "HELP: no HelpText box at '%.250s'", mpath);
		DebugPrint(dbg, __FILE__, __LINE__, ERROR);
		return rtrn_handled;
	}

	md = Widget_ReadFile(file);
	if (!md)
	{
		snprintf(dbg, sizeof(dbg), "HELP: could not read '%.250s' (cwd-relative)", file);
		DebugPrint(dbg, __FILE__, __LINE__, ERROR);
		SetPropStr(box, "Value", "");
		return rtrn_handled;
	}

	snprintf(dbg, sizeof(dbg), "HELP: loaded '%.200s' (%d bytes) into '%.150s'",
			 file, (int) strlen(md), mpath);
	DebugPrint(dbg, __FILE__, __LINE__, PROG_FLOW);

	SetPropStr(box, "Value", md);
	free(md);
	return rtrn_handled;
}

NodeObj Widget_AddHelp(NodeObj instance, char *helpFile)
{
	int     h = GetPropInt(instance, "H");
	NodeObj help = Widget_SubPanel(instance, "Help", HELP_ICON_X, h - HELP_ICON_Y_OFF, HELP_W, HELP_H);
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
			/* ALWAYS write it: LABEL_NONE means no caption, and the client
			   defaults a MISSING LabelPos to "bottom" - so leaving it unwritten
			   drew the caption anyway, under a button whose face already says
			   the same word. Widget_LabelWord has said "none" all along. */
			if (c)
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
	int     i;
	NodeObj entry;

	if (!class || !table)
		return;

	/* a class that publishes from a table builds a PANEL - it holds other
	   instances. The bare controls publish their own handful of properties
	   directly and never come through here, so this separates the two with
	   no list and no per-object opt-in: whoever uses the table says so by
	   using it. BuildPalette reads it to lay the simple ones out first. */
	SetPropInt(class, "Panel", 1);
	for (i = 0; table[i].cls; i++)
	{
		WidgetItem *t = &table[i];

		if (!strcmp(t->cls, "View") || !strcmp(t->cls, "Help"))
			continue;						/* panels carry no property */

		/* everything subscribable: one direction, value pushed to whoever
		   subscribes. `def` is the class default (the instance re-sets it). */
		entry = PublishProp(class, t->prop, Widget_PropType(t->cls),
							t->def ? t->def : "");

		/* the row already says how big this control is, in pixels - carry it
		   onto the published entry so anything PLACING this property (the
		   internals panel, say) can give it the size its object declared
		   instead of guessing. Properties are nodes, so the entry just
		   carries two more. */
		if (entry && (t->w || t->h))
		{
			SetPropInt(entry, "W", t->w);
			SetPropInt(entry, "H", t->h);
		}
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

/* ---- the panel, declared at construction and built at creation ---- */

/* PUT ME WHERE I WAS TOLD, UNDER THE NAME I WAS GIVEN, AND LAY OUT MY PANEL.

   The one call a widget's constructor makes about its own existence.
   CreateObject settles the name before the instance is made and hands both
   facts over, so this can register the instance and then build its controls
   INSIDE it - the controls need their container to have a path, which is
   the whole reason this could not happen in a constructor before.

   The name is kept, never re-minted: the controls are created under this
   path and the widget must not move out from under them. Minting is only
   for a caller that genuinely had no name to give.

   The table is walked in its own order, which is why panels are listed
   before controls - a control names the panel it goes in, so the panel has
   to exist first. */
void Widget_Place(NodeObj instance, NodeObj place, WidgetItem *table)
{
	NodeObj home = place ? (NodeObj) GetPropLong(place, "Container") : NULL;
	char   *mine = place ? GetPropStr(place, "Name") : NULL;
	char    cpath[300], name[192], path[512];
	/* holds the path AND the whole member list, so it is sized off what it
	   carries rather than off what a path usually is */
	char    dbg[1024];

	if (!instance || !table)
		return;

	if (!home)
	{
		snprintf(dbg, sizeof(dbg),
				 "PLACE '%s': no location handed to the constructor - nothing "
				 "can be built inside something that is nowhere",
				 GetNameStr(instance) ? GetNameStr(instance) : "?");
		DebugPrint(dbg, __FILE__, __LINE__, ERROR);
		return;
	}

	if (!PathOfInstance(home, cpath, sizeof(cpath)))
	{
		snprintf(dbg, sizeof(dbg),
				 "PLACE '%s': the location handed over ('%s') has no path of "
				 "its own, so nothing can be created in it",
				 GetNameStr(instance) ? GetNameStr(instance) : "?",
				 GetPropStr(home, "Name") ? GetPropStr(home, "Name") : "?");
		DebugPrint(dbg, __FILE__, __LINE__, ERROR);
		return;
	}

	if (mine && mine[0])
		snprintf(name, sizeof(name), "%s", mine);
	else
		MintFreshName(GetNameStr(instance) ? GetNameStr(instance) : "Widget",
					  cpath, name, sizeof(name));

	snprintf(path, sizeof(path), "%s/%s", cpath, name);
	RegisterPath(path, instance);

	Widget_BuildTable(instance, table);

	/* what it actually made, by name - one line per widget, at -v 3 */
	{
		NodeObj e;
		int     n = 0;
		char    made[420];

		made[0] = 0;
		for (e = FirstMember(instance); e; e = GetNextSibling(e))
		{
			NodeObj m  = MemberInstance(e);
			char   *mn = m ? GetPropStr(m, "Name") : NULL;

			if (mn && strlen(made) + strlen(mn) + 3 < sizeof(made))
			{
				if (made[0])
					strcat(made, ", ");
				strcat(made, mn);
			}
			n++;
		}
		snprintf(dbg, sizeof(dbg), "PLACE '%s' holds %d: %s",
				 path, n, made[0] ? made : "(nothing)");
		DebugPrint(dbg, __FILE__, __LINE__, PLACE);
	}
}

/* A PANEL IS DECLARED BY A CLASS, NOT BUILT BY AN INSTANCE.

   The table IS the declaration - the same fact the reference widgets carry
   as ControlInfo beside their code - and Widget_Publish already receives it
   at ClassStart. The panel is built by whoever creates an instance, at the
   point where it has a name and a place and the controls inside it resolve.

   What this replaces was mine and it was wrong twice over. The widget armed
   a task to build itself a tick later, because at InstanceStart it had no
   path yet - so construction became two phases, which needed a flag to
   remember which half had run, and the flag was an ordinary property. Every
   clone, save and import carried "already built" and skipped the half that
   installs the compiled handlers, so a copy came up looking complete and
   behaving dead. */

/* ---------------------------------------------------------------------------
 * The Widget class: a Control that contains Controls, plus the behaviours
 * that drive them.
 *
 * The entry points above are published as function-pointer properties on the
 * class node - the same way ClassStart/InstanceStart already are - so a module
 * reaches them through the registry it already shares instead of linking
 * against another .object. widget.h turns each one back into an ordinary call.
 * ------------------------------------------------------------------------- */

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	(void) instance; (void) message; (void) data;

	/* nothing of its own yet: dropped, so the walk continues to Control */
	return rtrn_dropped;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	(void) message; (void) data;

	SetName(class, "Widget");

	/* no InstanceStart: you instantiate a UDPPort, never a Widget */

	ClassSelf = RegisterClass(library, class);

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Control");

	/* the names widget.h looks up - one per entry point */
	/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
	SetPropLong(ClassSelf, "Port",            (long)Widget_Port);
	SetPropLong(ClassSelf, "Reflect",         (long)Widget_Reflect);
	SetPropLong(ClassSelf, "Ctl",             (long)Widget_Ctl);
	SetPropLong(ClassSelf, "SubPanel",        (long)Widget_SubPanel);
	SetPropLong(ClassSelf, "AddHelp",         (long)Widget_AddHelp);
	SetPropLong(ClassSelf, "Build",           (long)Widget_Build);
	SetPropLong(ClassSelf, "MainSize",        (long)Widget_MainSize);
	SetPropLong(ClassSelf, "BuildTable",      (long)Widget_BuildTable);
	SetPropLong(ClassSelf, "Publish",         (long)Widget_Publish);
	SetPropLong(ClassSelf, "Init",            (long)Widget_Init);
	SetPropLong(ClassSelf, "Place",           (long)Widget_Place);


	return rtrn_handled;
}

int ClassEnd(NodeObj library, MsgId message, NodeObj data)
{
	(void) message; (void) data;

	UnRegisterClass(library, ClassSelf);
	ClassSelf = NULL;
	return rtrn_handled;
}

void _init()
{
	NodeObj temp = NewNode(INTEGER);

	SetName(temp, "Widget");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "76735946-ed93-4098-9584-83fd3cb0c702");
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)Handle_Message);
	SetPropInt(temp, "State", 1);

	/* a Widget IS a Control (its parent) and it CONTAINS Controls, so the
	   class has to exist before this one starts either way */
	AddDependency(temp, "control.object", "Control", "1", "0");
	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
