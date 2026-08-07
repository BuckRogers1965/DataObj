#ifndef CONTROL_H
#define CONTROL_H

/*
 * control.h - what it takes to be a Control: a name, a place, a size.
 *
 * Plus the palette and the topbar chrome, because a palette exists only to
 * show controls. All of it lives in control.object; the core has no business
 * knowing that anything is ever presented.
 *
 * Widget_Create/Widget_Destroy are here rather than in widget.h for the same
 * reason: they create an instance, name it, and register its path (or adopt
 * what is already there). That is placement, not anything widget-specific.
 *
 * The entry points are function-pointer properties on the Control class node,
 * so a caller goes through the registry it already shares instead of linking
 * against another .object. CONTROL_IMPL is defined by the implementation.
 */

#include "node.h"
#include "object.h"
#include "DebugPrint.h"

/* Which control a published property presents as - the third argument to
   PublishProp. The core carries the number through without interpreting it;
   the client turns it into a control. This was in object.h, which has no use
   for the answer and no business naming controls.

   Append only: the numbers travel to the client and sit in saved interfaces,
   so inserting one in the middle renumbers everything after it. */
typedef enum {
    PROP_TEXTBOX=1,
    PROP_LED,
    PROP_BUTTON,
    PROP_CHECKBOX,
    PROP_SLIDER,
    PROP_VUMETER,
    PROP_TEXTOUT,
    PROP_KNOB,
    PROP_LABEL,
    PROP_NULL,
    PROP_MENU,
    PROP_ICON,     /* renders as the thing's icon - a doorway that opens   */
                   /* its one panel; what Open publishes (PublishPosition) */
    PROP_MARKDOWN, /* rendered markdown - the Markdown widget's display    */
    PROP_HTML,     /* rendered HTML, sandboxed - the HTML widget's display */
    PROP_IMAGE     /* an image loaded from a URL - the Image widget's display */
} PropertyType;

/* One row of a settings panel: which control class represents a named
   property or port on the object that owns this table, and where it sits.
   An object declares its own presentation directly rather than a client
   inferring a layout from a widget-type constant. `property` is unused for
   a Button row - that always reaches the target's Activate, never a named
   property. BuildSettingsView (below) is what turns a table into controls. */
typedef struct ControlSpec
{
	char *controlClass;
	char *property;
	int   x, y, w, h;
} ControlSpec;

#ifdef CONTROL_IMPL
NodeObj Widget_Create(NodeObj container, char *cls, char *name);
void Widget_Destroy(NodeObj instance);
int     Widget_WasAdopted(void);
void InitPosition(NodeObj instance);
void PublishPosition(NodeObj class);
NodeObj GetMainView(NodeObj instance);
void BuildPalette(void);
void BuildChrome(void);
NodeObj BuildSettingsView(NodeObj target, ControlSpec *specs, int count);
NodeObj GetPalette(void);
NodeObj GetPaletteView(void);
NodeObj GetChrome(void);
void SetSettingsHome(NodeObj view);
NodeObj GetRootView(void);
#else
static inline long ControlEntry(char *name)
{
	static NodeObj cls;
	char msg[130];

	if (!cls)
		cls = FindClass("Control");
	if (!cls) {
		snprintf(msg, sizeof(msg),
				 "control.h: the Control class is not loaded - '%s' unreachable", name);
		DebugPrint(msg, __FILE__, __LINE__, ERROR);
		return 0;
	}
	return GetPropLong(cls, name);
}

static inline NodeObj Widget_Create(NodeObj container, char *cls, char *name)
{
	NodeObj (*fn)(NodeObj container, char *cls, char *name) = (NodeObj (*)(NodeObj container, char *cls, char *name)) ControlEntry("Create");

	return fn ? fn(container, cls, name) : NULL;
}

static inline void Widget_Destroy(NodeObj instance)
{
	void (*fn)(NodeObj instance) = (void (*)(NodeObj instance)) ControlEntry("Destroy");

	if (fn)
		fn(instance);
}

static inline int Widget_WasAdopted(void)
{
	int (*fn)(void) = (int (*)(void)) ControlEntry("Adopted");

	return fn ? fn() : 0;
}

static inline void InitPosition(NodeObj instance)
{
	void (*fn)(NodeObj instance) = (void (*)(NodeObj instance)) ControlEntry("InitPos");

	if (fn)
		fn(instance);
}

static inline void PublishPosition(NodeObj class)
{
	void (*fn)(NodeObj class) = (void (*)(NodeObj class)) ControlEntry("PublishPos");

	if (fn)
		fn(class);
}

static inline NodeObj GetMainView(NodeObj instance)
{
	NodeObj (*fn)(NodeObj instance) = (NodeObj (*)(NodeObj instance)) ControlEntry("MainView");

	return fn ? fn(instance) : NULL;
}

static inline void BuildPalette(void)
{
	void (*fn)(void) = (void (*)(void)) ControlEntry("BuildPalette");

	if (fn)
		fn();
}

static inline void BuildChrome(void)
{
	void (*fn)(void) = (void (*)(void)) ControlEntry("BuildChrome");

	if (fn)
		fn();
}

static inline NodeObj BuildSettingsView(NodeObj target, ControlSpec *specs, int count)
{
	NodeObj (*fn)(NodeObj target, ControlSpec *specs, int count) = (NodeObj (*)(NodeObj target, ControlSpec *specs, int count)) ControlEntry("SettingsView");

	return fn ? fn(target, specs, count) : NULL;
}

static inline NodeObj GetPalette(void)
{
	NodeObj (*fn)(void) = (NodeObj (*)(void)) ControlEntry("Palette");

	return fn ? fn() : NULL;
}

static inline NodeObj GetPaletteView(void)
{
	NodeObj (*fn)(void) = (NodeObj (*)(void)) ControlEntry("PaletteView");

	return fn ? fn() : NULL;
}

static inline NodeObj GetChrome(void)
{
	NodeObj (*fn)(void) = (NodeObj (*)(void)) ControlEntry("Chrome");

	return fn ? fn() : NULL;
}

static inline void SetSettingsHome(NodeObj view)
{
	void (*fn)(NodeObj view) = (void (*)(NodeObj view)) ControlEntry("SetSettings");

	if (fn)
		fn(view);
}

static inline NodeObj GetRootView(void)
{
	NodeObj (*fn)(void) = (NodeObj (*)(void)) ControlEntry("RootView");

	return fn ? fn() : NULL;
}
#endif

#endif
