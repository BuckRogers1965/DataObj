/*
 * Control - the presentation class every control is one of.
 *
 * A control has a name, a place, a size, and is serialized. Those are the
 * things that were landing in object.c for want of a class to hold them:
 * with no Control class, each control was its own lone Object, so anything
 * common to all of them had nowhere else to go.
 *
 * A stand-in for now - it registers the class and takes its place in the
 * chain. Behaviour moves in here as it comes out of object.c, and what
 * this class does not answer falls through to Object.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "node.h"
#include "object.h"
#include "callback.h"
#include "DebugPrint.h"
#define CONTROL_IMPL
#include "control.h"
#include "show_web.h"

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

/* the top of the containment hierarchy - the one instance allowed to have no
   container, because it is what every location is measured from. It is the
   palette build that makes it, so it lives with the palette. */
static NodeObj RootView = NULL;

NodeObj GetRootView(void){ return RootView; }

/* ------------------------------------------------------------------------
 * What it takes to be a Control: a name, a place, a size - and, because the
 * palette exists only to show controls, the palette and the topbar chrome
 * too. All of this was in object.c, which has no business knowing that
 * anything is ever presented.
 *
 * Widget_Create/Widget_Destroy live here rather than in widget.object: they
 * create an instance, name it and register its path (or adopt what is
 * already there) - placement, which is a Control concern. Nothing about them
 * is widget-specific, and BuildPalette needs them, so putting them a level
 * up would have made Control call into its own subclass.
 * --------------------------------------------------------------------- */

/* set by Widget_Create: 1 when it handed back something that was already
   there instead of making it. Read it on the very next line - this is a
   hand-off to the caller that would otherwise re-apply the table's layout
   over values a load just restored, and the fabric is single threaded. */
static int Widget_Adopted = 0;

/* a VARIABLE cannot cross a module boundary through a class node, so the
   flag is private and the question is the entry point. Still read on the
   very next line after a create - the answer is about that create. */
int Widget_WasAdopted(void){ return Widget_Adopted; }

NodeObj Widget_Create(NodeObj container, char *cls, char *name)
{
	char cpath[256], path[320];
	NodeObj inst, existing;

	Widget_Adopted = 0;

	if (!name || !name[0])
		name = cls;

	/* if it is already there, it IS the one. A load restores a widget's
	   panel from the file, and the widget's own build then runs over it -
	   that build exists to put back what a file cannot carry: the compiled
	   handlers and wires, which are LONG properties the serializer drops on
	   purpose. Making a second set instead would leave the restored one
	   inert and the new one empty, which is exactly the blank help panel. */
	if (PathOfInstance(container, cpath, sizeof(cpath)))
	{
		snprintf(path, sizeof(path), "%s/%s", cpath, name);
		existing = ResolvePath(path);
		if (existing)
		{
			Widget_Adopted = 1;
			return existing;
		}
	}

	inst = CreateObject(container, cls);
	if (!inst)
		return NULL;

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

static NodeObj Palette;
static NodeObj PaletteView;

/*
 * The palette is not a special client-side concept - it is a real View
 * instance (PaletteView, living at the top level like any View a user
 * could create themselves - its current alias is /Root/Palette, no
 * well-known short name, see Bridge's InstanceStart seeding step), holding
 * one bootstrap
 * instance of every registered class as ordinary children (their own
 * Container property names this View, same as dragging anything else into
 * it would set). Two property VALUES are all that make it behave
 * differently from any other View: Deletable="0" (on the View itself and
 * on every one of its bootstrap children - you can't delete the catalog
 * out from under yourself) and Mode="Clone" (see view.c's own doc comment)
 * - interacting with anything inside it clones instead of following the
 * session's own current mode. Nothing else is special: drag things around
 * inside it, add to it, edit the bootstrap instances' properties, and
 * Save/Load carries all of it exactly the way it carries any other View's
 * contents, because it IS just a View.
 *
 * GetPalette() keeps returning the class-name -> instance lookup table
 * (unchanged shape) - Bridge (bridge.c) is the only remaining caller,
 * seeding its own alias table from it once at startup so these become
 * ordinary, addressable Root instances with no separate wire-protocol
 * category at all. GetPaletteView() is the View itself.
 *
 * Every bootstrap instance's alias is its current full path
 * (/Root/Palette/<Class>) - there is no separate "creation path" concept,
 * only a current one, the same convention app.js's createInstance() uses
 * for anything a client creates (/Root/<Class><N>). See Bridge_Set's
 * Bridge_Rename for what happens when something later moves to a
 * different Container: the alias changes with it, it isn't a permanent
 * identity independent of where the thing actually lives.
 *
 * Built once, after loadClasses() has run so every class actually exists
 * to instantiate (see main.c). None of the bootstrap instances are ever
 * Activated - they exist to be inspected and cloned from, the same way a
 * catalog entry is looked at, not run.
 */
NodeObj GetPalette(void){
	return Palette;
}

/* Where BuildSettingsView parks the controls it makes. A control has to be
   created in a container, but the target has no path yet inside its own
   InstanceStart, so the controls live in a named stash view instead - the
   boot phases name it (/Initialize for the palette build, /DefaultApp for the
   default app). Defaults to /Initialize, created on first use. */
static NodeObj SettingsHome = NULL;


void SetSettingsHome(NodeObj view){ SettingsHome = view; }

NodeObj GetPaletteView(void){
	/* resolve, never return the cached pointer: the palette is ordinary
	   content, so a load destroys it and restores a NEW node at the same
	   path. Anything holding the old one is holding freed memory. */
	return ResolvePath("/Root/Palette");
}

/* ************************************************************************
 * DO NOT revert this to `(void) className; return 0;` - it USED TO BE
 * exactly that stub, doing nothing, for a long time, with this same
 * comment already sitting above it describing Bridge/TCP as classes that
 * were SUPPOSED to be excluded. Nobody had actually implemented the body.
 * The visible, reported symptom was "every widget has a tiny panel with
 * no controls" (2026-07-30) - it wasn't every widget, it was every
 * palette-bootstrap instance of a class that was never meant to be
 * independently draggable (MCPAgent, confirmed; Bridge/TCP were always
 * intended too, per the comment below, just never wired up). If you're
 * looking at this function wondering whether the body is safe to delete
 * because it "looks like a one-off special case" - it is not: it is the
 * fix for a real, reported, user-visible bug, and this is the ONE
 * designated hook for it (the call site in BuildPalette already exists
 * and always did). Add MORE excluded classes here as they're found;
 * do not empty this out.
 * ************************************************************************/
/* Bridge and TCP are the transport carrying this very session, not      */
/* something a user composing a dataflow should drag out and rewire -    */
/* the same reason a web page builder doesn't let you drag out "an HTTP  */
/* connection." Not an architectural exclusion - nothing stops either    */
/* from being Connect()ed to or built into a flow file like anything     */
/* else - just a curation choice about what belongs in the palette a     */
/* user actually composes with.                                         */
/* MCPAgent (mcpsource.c) is the SAME kind of case: it's the internal    */
/* shape MCPSource_BuildAgentView constructs one of per discovered tool  */
/* (input/output Textboxes, a Lua Runner, a Submit button, AgentName/    */
/* ConnectorPath wired up) - never something a user creates bare. A      */
/* palette-bootstrap MCPAgent (plain Widget_Create, none of that built)  */
/* is just an empty panel with no controls - useless standalone.         */
static int IsPaletteExcluded(char *className)
{
	return className && (strcmp(className, "Bridge") == 0
						  || strcmp(className, "TCP") == 0
						  || strcmp(className, "MCPAgent") == 0);
}

/* ask an instance for the main view it built for itself: scan its        */
/* properties for one whose value IS a View instance - a widget defined   */
/* one when it built itself, a plain object (TCP, lua, quickjs) has none.  */
/* Nothing is "set" specially; the view a widget created is found by       */
/* scanning. Safe: property values are compared as integers against the    */
/* known View instances, never dereferenced as pointers.                   */
/* ask an instance for its view: a widget took on the View machinery      */
/* (InitPosition/PublishPosition gave it X/Y and the rest of a view's      */
/* presentation), so it has a view. A pure object like TCP never did -     */
/* no X/Y, no view - so it returns NULL and stays out of the palette. No   */
/* list, no foreknowledge; the object itself decides by whether it is a    */
/* view.                                                                    */
NodeObj GetMainView(NodeObj instance)
{
	if (!instance)
		return NULL;
	if (!GetPropNode(instance, "X") || !GetPropNode(instance, "Y"))
		return NULL;
	return instance;
}

/* THE PALETTE HAS ITS OWN ORDER, alphabetical, stated here rather than
   inherited from whatever order the registry walk happens to yield. It
   used to come out alphabetical by accident - the walk followed library
   order and the directory scan handed libraries over filename-ordered -
   which meant changing the shape of the registry silently rearranged the
   palette, and a control landing in a different slot drew on top of its
   neighbours. Sorting here is what makes that never happen again. */
static int PaletteNameCmp(const void *a, const void *b)
{
	char *na = GetNameStr(*(NodeObj *) a);
	char *nb = GetNameStr(*(NodeObj *) b);

	return strcasecmp(na ? na : "", nb ? nb : "");
}

#define PALETTE_MAX 512

/* append a name to one of BuildPalette's outcome lists */
static void PaletteNote(char *buf, int size, int *len, char *name)
{
	int room = size - *len;

	if (room > 1)
		*len += snprintf(buf + *len, room, "%s%s", *len ? "," : "", name ? name : "?");
}

void BuildPalette(void){

	NodeObj class, inst;
	int slot, pass;
	char alias[128];
	char dbg[300];
	char found[1200];			/* every class the registry walk returned */
	int  flen = 0, n = 0;
	/* WHERE EVERY CLASS WENT. One bucket per outcome, each a list, so a
	   palette that is missing things says which things and which decision
	   dropped them - rather than a count that only says "fewer". */
	char bPlaced[1200], bNoStart[600], bExcluded[600], bNoView[600], bNoMake[600];
	int  lPlaced = 0, lNoStart = 0, lExcluded = 0, lNoView = 0, lNoMake = 0;

	/* no forced Mode, no Deletable protection: the palette is just a     */
	/* View like Root or any panel, and everything in it deletes like     */
	/* anything else - a restart rebuilds it, so nothing here is precious. */
	/* X/Y position the icon; PanelX/PanelY position its open panel        */
	/* (independent - the icon never goes away).                            */
	/* the root view first - everything below is created IN something */
	if (!RootView)
		RootView = CreateRoot("Root");

	PaletteView = Widget_Create(RootView, "View", "Palette");
	SetPropStr(PaletteView, "ReservedViewOpen", "1");	/* views default closed; the palette starts open */
	/* clear of the menus, which sit on the top row of the canvas at y=6 -
	   the palette icon used to land on top of them and swallow every click */
	SetPropInt(PaletteView, "X", 20);
	SetPropInt(PaletteView, "Y", 60);
	SetPropInt(PaletteView, "ReservedViewPanelX", 20);
	SetPropInt(PaletteView, "ReservedViewPanelY", 60);
	/* the palette is not precious in principle - a restart rebuilds it -
	   but losing it mid-session costs you every class you can drag out,
	   so it carries the same ordinary guard the menus do */
	SetPropStr(PaletteView, "Deletable", "0");
	SetPropInt(PaletteView, "W", 290);	/* three 80px columns from x=10, plus slack */
	SetPropInt(PaletteView, "H", 220);	/* the inner area scrolls; resize to taste */

	Palette = NewNode(INTEGER);
	SetName(Palette, "Palette");

	/* every bootstrap instance defaults to X=0,Y=0 (InitPosition) - left    */
	/* alone they'd all stack exactly on top of each other inside the        */
	/* Palette's inner area. A simple three-column grid, just so there is     */
	/* something to look at on first boot - completely ordinary X/Y writes,   */
	/* the user can drag any of them anywhere else afterward like anything    */
	/* else in a View.                                                        */
	slot = 0;

	/* two passes so the simple things come first: a bare control has no
	   panel of its own, a widget does (Widget_Publish stamps it). Sorting
	   here beats sorting by eye every time the palette is opened. */
	/* WHAT THE WALK FOUND, in the order it found it. The palette is the
	   first thing that enumerates every class, so this is where a walk
	   that reaches only part of the registry shows up - and it shows up
	   as a list you can read against what loaded, not as a missing icon. */
	found[0] = bPlaced[0] = bNoStart[0] = bExcluded[0] = bNoView[0] = bNoMake[0] = '\0';

	for (pass = 0; pass < 2; pass++)
	{
		NodeObj order[PALETTE_MAX];
		int count = 0, i;

		{
			class = FirstClass();
			while (class)
			{
				if (pass == 0)
				{
					int room = (int) sizeof(found) - flen;

					n++;
					if (room > 1)
						flen += snprintf(found + flen, room, "%s%s",
										 flen ? "," : "", GetNameStr(class));

					if (!GetPropLong(class, "InstanceStart"))
						PaletteNote(bNoStart, sizeof(bNoStart), &lNoStart, GetNameStr(class));
					else if (IsPaletteExcluded(GetNameStr(class)))
						PaletteNote(bExcluded, sizeof(bExcluded), &lExcluded, GetNameStr(class));
				}

				/* no InstanceStart means the class is not instantiable at
				   all - Object/Presentation/Control/Widget are places in the
				   tree, not things to drop on a canvas. Asked, not listed. */
				if (GetPropLong(class, "InstanceStart")
					&& GetPropInt(class, "Panel") == pass
					&& !IsPaletteExcluded(GetNameStr(class)))
				{
					if (count < PALETTE_MAX)
						order[count++] = class;
					else
						DebugPrint("palette: more classes than PALETTE_MAX - "
								   "the rest are not shown",
								   __FILE__, __LINE__, ERROR);
				}

				class = NextClass(class);
			}
		}

		qsort(order, (size_t) count, sizeof(NodeObj), PaletteNameCmp);

		for (i = 0; i < count; i++)
		{
			class = order[i];

			/* create + name + register in one call - a palette instance
			   builds its panel (and, e.g., ScriptBox its inner host) in its
			   deferred build, which needs it to resolve by path like any
			   placed object */
			inst = Widget_Create(PaletteView, GetNameStr(class), GetNameStr(class));
			if (!inst)
				PaletteNote(bNoMake, sizeof(bNoMake), &lNoMake, GetNameStr(class));
			if (inst && GetMainView(inst)) {
				PaletteNote(bPlaced, sizeof(bPlaced), &lPlaced, GetNameStr(class));
				SetPropInt(inst, "X", 10 + (slot % 3) * 80);
				SetPropInt(inst, "Y", 10 + (slot / 3) * 66);
				slot++;

				snprintf(dbg, sizeof(dbg),
				         "palette pass=%d slot=%d %s at X=%s Y=%s size W=%s H=%s",
				         pass, slot - 1, GetNameStr(class),
				         GetPropStr(inst, "X"), GetPropStr(inst, "Y"),
				         GetPropStr(inst, "W"), GetPropStr(inst, "H"));
				DebugPrint(dbg, __FILE__, __LINE__, PLACE);

				/* the alias is this instance's full path, same         */
				/* convention as a client-created instance's /Root/...   */
				/* (createInstance, app.js) - see the doc comment above  */
				/* and Bridge_Set's Bridge_Rename for what happens if    */
				/* it ever moves out of here later.                      */
				snprintf(alias, sizeof(alias), "/Root/Palette/%s", GetNameStr(class));
				SetPropStr(Palette, alias, alias);
			}
			else if (inst)
				/* no main view - not a placeable palette item (an atomic
				   control lives INSIDE widgets, never on its own). Undo the
				   create so it is not named/registered into the palette. */
			{
				snprintf(dbg, sizeof(dbg),
				         "palette pass=%d %s has no main view - destroyed, no slot",
				         pass, GetNameStr(class));
				DebugPrint(dbg, __FILE__, __LINE__, PLACE);
				Widget_Destroy(inst);
				PaletteNote(bNoView, sizeof(bNoView), &lNoView, GetNameStr(class));
			}
		}
	}

	{
		/* its own buffer, sized for the WORST of the lines below - the
		   longest is the container read-back, a 200-byte path plus the
		   1200-byte member list plus its wording. Truncating any of these
		   would look exactly like the fault they exist to find, which is
		   why this is sized against the inputs rather than by eye. */
		char line[1600];

		/* PROG_FLOW, not PLACE: this is a once-per-boot summary and it sits
		   beside "list of objs" and "classes started" - the three lines that
		   together say what loaded, what started, and what the walk can
		   reach. A diagnostic nobody sees at the default level is not a
		   diagnostic. */
		snprintf(line, sizeof(line), "the class walk found %d: %s", n, found);
		DebugPrint(line, __FILE__, __LINE__, PROG_FLOW);

		snprintf(line, sizeof(line), "palette placed: %s", bPlaced);
		DebugPrint(line, __FILE__, __LINE__, PROG_FLOW);

		snprintf(line, sizeof(line), "palette not instantiable (no InstanceStart): %s", bNoStart);
		DebugPrint(line, __FILE__, __LINE__, PROG_FLOW);

		snprintf(line, sizeof(line), "palette excluded: %s", bExcluded);
		DebugPrint(line, __FILE__, __LINE__, PROG_FLOW);

		snprintf(line, sizeof(line), "palette could NOT be created: %s", bNoMake);
		DebugPrint(line, __FILE__, __LINE__, PROG_FLOW);

		snprintf(line, sizeof(line), "palette created but no main view: %s", bNoView);
		DebugPrint(line, __FILE__, __LINE__, PROG_FLOW);

		/* GROUND TRUTH, read back out of the tree rather than from what the
		   loop above thinks it did. "placed" is a claim; this is the
		   container's actual contents, and the two disagreeing is the whole
		   diagnosis. */
		{
			char ppath[200];
			char have[1200];
			int  hlen = 0, hn = 0;
			NodeObj m;
			char *c;

			have[0] = '\0';
			if (PathOfInstance(PaletteView, ppath, sizeof(ppath)))
			{
				for (m = FirstInstance(); m; m = NextInstance(m))
				{
					c = GetPropStr(m, "Container");
					if (!c || strcmp(c, ppath) != 0)
						continue;
					hn++;
					PaletteNote(have, sizeof(have), &hlen, GetPropStr(m, "Name"));
				}
				snprintf(line, sizeof(line), "palette '%s' actually holds %d: %s",
						 ppath, hn, have);
			}
			else
				snprintf(line, sizeof(line), "palette has no path - nothing can be in it");

			DebugPrint(line, __FILE__, __LINE__, PROG_FLOW);
		}
	}
}

static NodeObj Chrome;


/*
 * The app's own topbar chrome (File menu, Mode menu) is not a special
 * client-side concept - it's a small, fixed set of real instances,
 * discovered and addressed exactly the same way the Palette is (one
 * property per well-known name, holding the instance's PATH - never a
 * pointer: a load destroys and rebuilds these like any other content,
 * so anything cached across it would be reading freed nodes). "Eat our own dog
 * food": these are MenuButton instances like any a user could drag out
 * of the palette for their own app; the only thing that marks them as
 * chrome is which group Bridge_ListInstances reports them under.
 *
 * Built once, after BuildPalette - MenuButton has to already be a
 * registered class.
 */
NodeObj GetChrome(void){
	return Chrome;
}

void BuildChrome(void){

	NodeObj fileMenu, modeMenu;

	Chrome = NewNode(INTEGER);
	SetName(Chrome, "Chrome");

	fileMenu = CreateObject(GetRootView(), "MenuButton");
	if (fileMenu) {
		/* an ordinary instance in the root view, with an ordinary name -
		   no short-name category, nothing for a walker to special-case */
		SetPropStr(fileMenu, "Name", "FileMenu");
		RegisterPath("/Root/FileMenu", fileMenu);
		/* somewhere of their own: both menus defaulted to 0,0 and sat
		   exactly on top of each other, so only the top one could ever
		   be clicked or dragged */
		SetPropInt(fileMenu, "X", 8);
		SetPropInt(fileMenu, "Y", 6);
		SetPropStr(fileMenu, "Label", "File");
		SetPropStr(fileMenu, "Items", "Load,Save,Import,Export");
		/* an ordinary property, the same guard the palette carries: you
		   cannot delete the menus you drive the session with. Nothing
		   special about these instances - anything can be marked this way */
		SetPropStr(fileMenu, "Deletable", "0");
		SetPropStr(Chrome, "FileMenu", "/Root/FileMenu");
	}

	modeMenu = CreateObject(GetRootView(), "MenuButton");
	if (modeMenu) {
		SetPropStr(modeMenu, "Name", "ModeMenu");
		RegisterPath("/Root/ModeMenu", modeMenu);
		SetPropInt(modeMenu, "X", 110);
		SetPropInt(modeMenu, "Y", 6);
		SetPropStr(modeMenu, "Label", "Mode");
		SetPropStr(modeMenu, "Items", "Operate,Clone,Alias,Move,Connect,Delete,Options");
		SetPropStr(modeMenu, "Deletable", "0");
		SetPropStr(modeMenu, "Selected", "Operate");
		SetPropStr(Chrome, "ModeMenu", "/Root/ModeMenu");
	}
}

/* LED/TextOut/VUMeter/Label reflect a value; everything else in a       */
/* ControlSpec table (Checkbox/Textbox/Slider/Knob) edits one - see the  */
/* doc comment on BuildSettingsView (object.h) for what each kind wires  */
static int IsDisplayControlClass(char *className)
{
	return strcmp(className, "LED") == 0
		|| strcmp(className, "TextOut") == 0
		|| strcmp(className, "VUMeter") == 0
		|| strcmp(className, "Label") == 0;
}

NodeObj BuildSettingsView(NodeObj target, ControlSpec *specs, int count)
{
	NodeObj control;
	int i;

	if (!target || !specs || count <= 0)
		return NULL;

	/* the controls need a home - the target has no path yet in its own
	   InstanceStart, so they go into the settings-home stash view */
	if (!SettingsHome)
		SettingsHome = CreateRoot("Initialize");
	if (!SettingsHome)
		return NULL;

	for (i = 0; i < count; i++)
	{
		control = CreateObject(SettingsHome, specs[i].controlClass);
		if (!control)
			continue;

		/* position is just a property, same as any other - X/Y/W/H exist  */
		/* on every placeable class (InitPosition) and a plain write is    */
		/* all it takes; no container needs to know about this control     */
		SetPropInt(control, "X", specs[i].x);
		SetPropInt(control, "Y", specs[i].y);
		SetPropInt(control, "W", specs[i].w);
		SetPropInt(control, "H", specs[i].h);

		/* every row is a plain Connect() - a Button reaches the target's   */
		/* Activate port (ActivateOnMsg), a display reflects the property,  */
		/* an input edits it through the universal default delivery         */
		if (strcmp(specs[i].controlClass, "Button") == 0)
			Connect(control, "Value", target, "Activate");
		else if (IsDisplayControlClass(specs[i].controlClass))
			Connect(target, specs[i].property, control, "Value");
		else
			Connect(control, "Value", target, specs[i].property);
	}

	return target;
}

/* What this class looks like on a surface, kept on the class node beside
   everything else it publishes. Nothing scans a directory for this and
   nothing keeps a list of which classes have one: a class either carries
   it or it does not, and whoever renders that surface walks the classes it
   is already walking. */
void PublishShow(NodeObj class, int renders, char *js, char *css)
{
	NodeObj show, web;

	if (!class)
		return;

	SetPropStr(class, "Show", "");
	show = GetPropNode(class, "Show");
	if (!show)
		return;
	SetPropStr(show, "web", "");
	web = GetPropNode(show, "web");
	if (!web)
		return;

	if (js)
		SetPropStr(web, "js", js);
	if (css)
		SetPropStr(web, "css", css);

	/* WHICH property widget type this class is the renderer FOR - stated,
	   not inferred. Inferring it from the class's own Value type looked
	   right and was wrong: MoButton publishes its Value as PROP_LED and
	   Button publishes PROP_NULL, so the LED's type would have been stolen
	   and every plumbing property would have rendered as a button. A class
	   that renders no property type (a button is placed, never stamped on
	   a property) says 0.

	   It sits on the CLASS, not under Show: "I am the control for this kind
	   of property" is a fact about the class that every surface uses and
	   the core needs too - aliasing has to answer "what control shows this
	   property?" without a browser anywhere in sight. */
	if (renders)
		SetPropInt(class, "Renders", renders);
}

/* see the doc comment in object.h - opt-in, ordinary, no different from */
/* any class publishing its own property                                 */
void PublishPosition(NodeObj class)
{
	PublishProp(class, "X", PROP_NULL, "0");
	PublishProp(class, "Y", PROP_NULL, "0");
	PublishProp(class, "W", PROP_NULL, "120");
	PublishProp(class, "H", PROP_NULL, "60");

	/* where something lives is the same kind of fact as where it sits -   */
	/* an ordinary property, not a Slot/membership structure. Empty means   */
	/* "the top-level canvas"; any other value names a View instance's own  */
	/* alias, the same way a wire names an instance - a client renders an   */
	/* instance inside whichever View's Container it currently reads as,    */
	/* correcting late exactly like X/Y already does.                       */
	PublishProp(class, "Container", PROP_NULL, "");

	/* what a thing is CALLED is just one of its properties - writing it   */
	/* renames the instance (the Bridge keys its alias table off it, see   */
	/* Bridge_Set/Bridge_RenameName). Shown as an editable textbox on the  */
	/* dissection table like anything else.                                 */
	PublishProp(class, "Name", PROP_TEXTBOX, "");

	/* every thing is a view: its icon lives wherever Container says, and  */
	/* its open panel is a peer of every other panel at the root, with a   */
	/* position of its own. Open is only the INITIAL presentation - after  */
	/* first paint, open/closed is each window's own business.              */
	/* PROP_ICON is the engine saying what an alias of Open should look     */
	/* like: another icon for the same thing, a doorway to its one panel -   */
	/* the client renders the stamped Widget instead of special-casing the   */
	/* property name.                                                         */
	PublishProp(class, "ReservedViewOpen", PROP_ICON, "0");
	PublishProp(class, "ReservedViewPanelX", PROP_NULL, "240");
	PublishProp(class, "ReservedViewPanelY", PROP_NULL, "60");

	/* generic, not View-specific - anything CAN be marked undeletable      */
	/* this way, the Palette's own bootstrap instances are just the first    */
	/* thing that actually uses it (BuildPalette). Bridge_Delete is what     */
	/* enforces it.                                                          */
	PublishProp(class, "Deletable", PROP_CHECKBOX, "1");
}

void InitPosition(NodeObj instance)
{
	SetPropInt(instance, "X", 0);
	SetPropInt(instance, "Y", 0);
	SetPropInt(instance, "W", 120);
	SetPropInt(instance, "H", 60);
	SetPropStr(instance, "Container", "");
	SetPropStr(instance, "Deletable", "1");
	SetPropStr(instance, "Name", "");
	SetPropStr(instance, "ReservedViewOpen", "0");
	SetPropInt(instance, "ReservedViewPanelX", 240);
	SetPropInt(instance, "ReservedViewPanelY", 60);
}

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	(void) instance; (void) message; (void) data;

	/* nothing of its own yet: dropped, so the walk continues to Object */
	return rtrn_dropped;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	(void) message; (void) data;

	SetName(class, "Control");

	/* no InstanceStart: you instantiate a Button, never a Control. Nothing
	   can create one, and the palette walk skips it for the same reason. */

	ClassSelf = RegisterClass(library, class);

	/* the names control.h looks up */
	SetPropLong(ClassSelf, "Create",         (long)Widget_Create);
	SetPropLong(ClassSelf, "Destroy",        (long)Widget_Destroy);
	SetPropLong(ClassSelf, "Adopted",        (long)Widget_WasAdopted);
	SetPropLong(ClassSelf, "InitPos",        (long)InitPosition);
	SetPropLong(ClassSelf, "PublishPos",     (long)PublishPosition);
	SetPropLong(ClassSelf, "PublishShow",    (long)PublishShow);

	/* what a control IS on a screen, carried by the class every control
	   descends from - see show/web/. Renders no property type of its own:
	   a Control is placed, the classes under it say what they render. */
	PublishShow(ClassSelf, 0, show_web_js, show_web_css);
	SetPropLong(ClassSelf, "MainView",       (long)GetMainView);
	SetPropLong(ClassSelf, "BuildPalette",   (long)BuildPalette);
	SetPropLong(ClassSelf, "BuildChrome",    (long)BuildChrome);
	SetPropLong(ClassSelf, "SettingsView",   (long)BuildSettingsView);
	SetPropLong(ClassSelf, "Palette",        (long)GetPalette);
	SetPropLong(ClassSelf, "PaletteView",    (long)GetPaletteView);
	SetPropLong(ClassSelf, "Chrome",         (long)GetChrome);
	SetPropLong(ClassSelf, "SetSettings",    (long)SetSettingsHome);
	SetPropLong(ClassSelf, "RootView",       (long)GetRootView);

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Object");

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

	SetName(temp, "Control");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "aa690a30-0651-4607-9109-4869a5d557ef");
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)Handle_Message);
	SetPropInt(temp, "State", 1);

	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
