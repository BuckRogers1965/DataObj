#ifndef WIDGET_H
#define WIDGET_H

/*
 * widget.h - the shared conventions for instrument-panel widgets.
 *
 * A widget is a composite View whose controls are laid out by a flat table and
 * wired to the widget's own properties. Every widget repeats the same handful
 * of moves - make a port, reflect a property into a control, place a control,
 * build a sub-view, load a Help README on open. Those live here so a widget
 * declares only its controls and logic, and #includes this.
 *
 * Conventions encoded here: Help is a sub-view pinned to the bottom-left corner
 * (Widget_AddHelp), and its README loads on open. (Enable stays top-right in
 * each widget's own table.)
 */

#include "node.h"
#include "object.h"		/* HELP_W / HELP_H / HELP_*_OFF and the PROP_* set */
#include "control.h"	/* Widget_Create/Widget_Destroy are placement: Control owns them */

/* where the Help icon sits on its parent panel (Widget_AddHelp): pinned to
   the bottom-left corner, this far up from the panel's own H */
#define HELP_ICON_X 15
#define HELP_ICON_Y_OFF 80

/* one control's placement in a widget panel table (legacy Widget_Build) */
typedef struct
{
	char *cls;			/* "Textbox", "Checkbox", "MoButton", "Dropdown", ... */
	char *prop;			/* the widget property it drives / reflects           */
	int   x, y, w, h;
	int   panel;		/* 0 = the main view, 1.. = entries in the sub[] array */
	int   rows, cols;	/* a Textbox's character size (0 when not a Textbox)   */
} WidgetCtl;

/* where a control's caption sits relative to the control */
enum { LABEL_NONE = 0, LABEL_LEFT, LABEL_RIGHT, LABEL_TOP, LABEL_BOTTOM };

/*
 * One row of a fully declarative widget - the whole thing in a single table,
 * used BOTH to publish the interface (Widget_Publish, in ClassStart) AND to
 * build the panel (Widget_BuildTable, in the deferred build). A row is:
 *   - cls "View" : a panel. The FIRST View row is the main panel (the widget
 *     itself); every later View row is a sub-panel. prop is the panel's Name.
 *   - cls "Help" : the standard Help sub-view (bottom-left, README on open);
 *     prop is the README path.
 *   - anything else : a control of that class; prop is the property it drives.
 *     Widget_Publish maps the class to its widget type automatically.
 *
 * `def` is the property's initial value (also its published default). Panels are
 * numbered by the order View/Help rows appear (main 0, next 1, ...). `panel`
 * names the CONTAINING panel: for a View/Help row the PARENT it nests inside
 * (sub-panels can hold sub-panels); for a control the panel it sits on. x/y/w/h
 * are the control's pixel box - w/h ARE its size, a Textbox included. `label`
 * places the caption (LABEL_*). `handler` is LAST, and defaulted: a plain
 * property (the object just reads it) simply omits it; a reactive PORT (a
 * command/toggle the object acts on) names its OnMsg handler there. The table
 * ends with a {NULL} row.
 */
typedef struct
{
	char *cls;
	char *prop;
	char *def;			/* initial value / published default */
	int   panel;
	int   x, y, w, h;
	int   label;
	void *handler;		/* OnMsg if a reactive port; omit for a plain property */
} WidgetItem;

/* create an object in container, name it, and register its path - the three
   steps EVERY placed object needs (CreateObject deliberately leaves naming and
   path registration to the caller). Returns the instance, NULL on failure.
   Name defaults to the class name when name is NULL/empty. */

/* The entry points live on the Widget class node as function-pointer properties
   (widget.object publishes them in ClassStart), so a module calls them through
   the registry it already shares - never by linking against another .object.
   Each wrapper below turns that back into an ordinary call. WIDGET_IMPL is
   defined by the implementation itself, which wants the real prototypes. */
#ifdef WIDGET_IMPL
void Widget_Port(NodeObj instance, char *name, char *initial, void *handler);
void Widget_Reflect(NodeObj src, char *sp, NodeObj dst, char *dp);
NodeObj Widget_Ctl(NodeObj container, NodeObj target, char *cls, char *prop,
				   int x, int y, int w, int h);
NodeObj Widget_SubPanel(NodeObj panel, char *name, int x, int y, int w, int h);
NodeObj Widget_AddHelp(NodeObj instance, char *helpFile);
void Widget_Publish(NodeObj class, WidgetItem *table);
void Widget_Init(NodeObj instance, WidgetItem *table);
void Widget_DeferBuild(NodeObj instance, WidgetItem *table);
int Widget_BuildOnce(NodeObj instance, WidgetItem *table);
void Widget_DeferBuildQuiet(NodeObj instance, WidgetItem *table);
void Widget_CancelBuild(NodeObj instance);
void Widget_MainSize(NodeObj instance, WidgetItem *table);
void Widget_BuildTable(NodeObj instance, WidgetItem *table);
void Widget_Build(NodeObj instance, WidgetCtl *table, NodeObj *sub, int nsub);
#else
static inline long WidgetEntry(char *name)
{
	static NodeObj cls;

	if (!cls)
		cls = FindClass("Widget");
	return cls ? GetPropLong(cls, name) : 0;
}

static inline void Widget_Port(NodeObj instance, char *name, char *initial, void *handler)
{
	void (*fn)(NodeObj instance, char *name, char *initial, void *handler) = (void (*)(NodeObj instance, char *name, char *initial, void *handler)) WidgetEntry("Port");

	if (fn)
		fn(instance, name, initial, handler);
}

static inline void Widget_Reflect(NodeObj src, char *sp, NodeObj dst, char *dp)
{
	void (*fn)(NodeObj src, char *sp, NodeObj dst, char *dp) = (void (*)(NodeObj src, char *sp, NodeObj dst, char *dp)) WidgetEntry("Reflect");

	if (fn)
		fn(src, sp, dst, dp);
}

static inline NodeObj Widget_Ctl(NodeObj container, NodeObj target, char *cls, char *prop,
				   int x, int y, int w, int h)
{
	NodeObj (*fn)(NodeObj container, NodeObj target, char *cls, char *prop, int x, int y, int w, int h) = (NodeObj (*)(NodeObj container, NodeObj target, char *cls, char *prop, int x, int y, int w, int h)) WidgetEntry("Ctl");

	return fn ? fn(container, target, cls, prop, x, y, w, h) : NULL;
}

static inline NodeObj Widget_SubPanel(NodeObj panel, char *name, int x, int y, int w, int h)
{
	NodeObj (*fn)(NodeObj panel, char *name, int x, int y, int w, int h) = (NodeObj (*)(NodeObj panel, char *name, int x, int y, int w, int h)) WidgetEntry("SubPanel");

	return fn ? fn(panel, name, x, y, w, h) : NULL;
}

static inline NodeObj Widget_AddHelp(NodeObj instance, char *helpFile)
{
	NodeObj (*fn)(NodeObj instance, char *helpFile) = (NodeObj (*)(NodeObj instance, char *helpFile)) WidgetEntry("AddHelp");

	return fn ? fn(instance, helpFile) : NULL;
}

static inline void Widget_Publish(NodeObj class, WidgetItem *table)
{
	void (*fn)(NodeObj class, WidgetItem *table) = (void (*)(NodeObj class, WidgetItem *table)) WidgetEntry("Publish");

	if (fn)
		fn(class, table);
}

static inline void Widget_Init(NodeObj instance, WidgetItem *table)
{
	void (*fn)(NodeObj instance, WidgetItem *table) = (void (*)(NodeObj instance, WidgetItem *table)) WidgetEntry("Init");

	if (fn)
		fn(instance, table);
}

static inline void Widget_DeferBuild(NodeObj instance, WidgetItem *table)
{
	void (*fn)(NodeObj instance, WidgetItem *table) = (void (*)(NodeObj instance, WidgetItem *table)) WidgetEntry("DeferBuild");

	if (fn)
		fn(instance, table);
}

static inline int Widget_BuildOnce(NodeObj instance, WidgetItem *table)
{
	int (*fn)(NodeObj instance, WidgetItem *table) = (int (*)(NodeObj instance, WidgetItem *table)) WidgetEntry("BuildOnce");

	return fn ? fn(instance, table) : 0;
}

static inline void Widget_DeferBuildQuiet(NodeObj instance, WidgetItem *table)
{
	void (*fn)(NodeObj instance, WidgetItem *table) = (void (*)(NodeObj instance, WidgetItem *table)) WidgetEntry("DeferBuildQuiet");

	if (fn)
		fn(instance, table);
}

static inline void Widget_CancelBuild(NodeObj instance)
{
	void (*fn)(NodeObj instance) = (void (*)(NodeObj instance)) WidgetEntry("CancelBuild");

	if (fn)
		fn(instance);
}

static inline void Widget_MainSize(NodeObj instance, WidgetItem *table)
{
	void (*fn)(NodeObj instance, WidgetItem *table) = (void (*)(NodeObj instance, WidgetItem *table)) WidgetEntry("MainSize");

	if (fn)
		fn(instance, table);
}

static inline void Widget_BuildTable(NodeObj instance, WidgetItem *table)
{
	void (*fn)(NodeObj instance, WidgetItem *table) = (void (*)(NodeObj instance, WidgetItem *table)) WidgetEntry("BuildTable");

	if (fn)
		fn(instance, table);
}

static inline void Widget_Build(NodeObj instance, WidgetCtl *table, NodeObj *sub, int nsub)
{
	void (*fn)(NodeObj instance, WidgetCtl *table, NodeObj *sub, int nsub) = (void (*)(NodeObj instance, WidgetCtl *table, NodeObj *sub, int nsub)) WidgetEntry("Build");

	if (fn)
		fn(instance, table, sub, nsub);
}
#endif


/* the inverse of Widget_Create: unregister the instance's path, then delete it.
   DeleteInstance alone frees the node but leaves the namespace entry dangling -
   this removes it first, so the path is reclaimed and nothing resolves to freed
   memory. Safe on an unregistered instance (the unregister is skipped). */

/* a property that carries an OnMsg handler - a write to it runs handler */

/* Connect src.sp -> dst.dp AND seed dst.dp with src.sp's value now (a plain
   Connect only fires on the next change, so a fresh readout would read blank) */

/* create one control in container and wire it to target.prop by control class;
   returns the created control (NULL on failure). Its size is w/h. */

/* a sub-view inside panel (renders as an openable icon), path-registered */

/* the standard Help sub-view, pinned to the bottom-left corner (read from the
   widget's H). It owns a Markdown box that loads helpFile (a README path) when
   the panel is opened. Returns the Help view (put it in your sub[] array). */

/* publish the class interface straight from the table: one property per control
   row (widget type mapped from the control class, default from `def`).
   Call in ClassStart. Ports with no on-screen control are published separately. */

/* give an instance its properties straight from the table: each control row's
   initial value, made a reactive port (its handler) or a plain property. Call
   in InstanceStart; add any non-table ports (In/Out plumbing) after it. */

/* arm the one-tick-deferred panel build (call in InstanceStart, last). One tick
   later the instance has a path, so its sub-views/controls resolve; the panel is
   built once, then the object's Activate runs with msg_initialize (placement
   setup). */

/* build the panel if it hasn't been built yet; returns 1 if it built now. The
   deferred build and an early Activate both call this, so it happens once,
   whichever comes first. */

/* like Widget_DeferBuild but does NOT call Activate after building - for a
   source that would ACT on activation (a Pulse would start ticking). The panel
   comes up on placement; the object stays quiet until something activates it. */

/* cancel a still-pending deferred build (call in InstanceEnd, before the
   instance's C state is freed). */

/* apply the main panel's size (the first "View" row) to the instance. Call in
   InstanceStart, so the size is set before any client subscribes. */

/* build the whole panel tree from one table: the main view, its nested
   sub-panels, and every control placed on its panel (with label placement). */

/* place every control in table into sub[panel]; sub[0] is the main view and
   the wiring target for all of them. table ends with a {NULL} row. */

#endif
