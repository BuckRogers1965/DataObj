/*
 * Skin - a class's default layout, and the file it can be replaced from.
 *
 * A skin is metadata about a CLASS, generated from its published interface:
 * one Layout row per property, which a saved skin file then overrides. It
 * lives as a property on the class node for the same reason the interface
 * does - it describes the class, not any one instance.
 *
 * This was in object.c, where nothing but its own test ever called it.
 * Skinning is behaviour, so it is an object: the entry points are published
 * on the class node and skin.h turns them back into ordinary calls.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "callback.h"
#include "DebugPrint.h"
#define SKIN_IMPL
#include "skin.h"

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

/* one row per published property, stacked this far apart */
#define SKIN_ROW_HEIGHT 30

/* a default layout for a class nobody has skinned yet: one row per      */
/* published property, in the order it was published, stacked vertically */
NodeObj GenerateSkin(NodeObj class){

	NodeObj interface, prop, skin, layout;
	char *name;
	int y;

	skin = NewNode(INTEGER);
	SetName(skin, "Skin");

	interface = GetClassInterface(class);
	prop = interface ? GetChild(interface) : NULL;
	y = 0;

	while (prop) {
		name = GetPropStr(prop, "Name");

		layout = NewNode(INTEGER);
		SetName(layout, "Layout");
		SetPropStr(layout, "Name", name);
		SetPropStr(layout, "Label", name);
		SetPropInt(layout, "X", 0);
		SetPropInt(layout, "Y", y);
		SetPropStr(layout, "Style", "");
		AppendChild(skin, layout);

		y += SKIN_ROW_HEIGHT;
		prop = GetNextSibling(prop);
	}

	return skin;
}

/* the skin lives as a property on the class, same reasoning as the      */
/* interface - it is metadata about the class, not one of its instances  */
NodeObj GetClassSkin(NodeObj class){

	NodeObj skin;

	if (!class)
		return NULL;

	skin = GetPropNode(class, "Skin");
	if (skin)
		return skin;

	skin = GenerateSkin(class);
	AddProp(class, skin);
	return skin;
}

int SaveSkin(NodeObj class, char *filename){

	NodeObj skin;
	char *text;
	FILE *f;

	skin = GetClassSkin(class);
	if (!skin || !filename)
		return 0;

	text = NodeToText(skin);
	if (!text)
		return 0;

	f = fopen(filename, "w");
	if (!f) {
		free(text);
		return 0;
	}

	fputs(text, f);
	fclose(f);
	free(text);
	return 1;
}

/* replaces whatever skin the class currently has (generated default or  */
/* an earlier load) - AddProp shadows it rather than freeing it, same    */
/* leak DelNode already carries everywhere else in this tree (see the    */
/* Phase 8 roadmap note); nothing keeps a reference to the old one       */
NodeObj LoadSkin(NodeObj class, char *filename){

	FILE *f;
	long size;
	char *text;
	NodeObj skin;

	if (!class || !filename)
		return NULL;

	f = fopen(filename, "r");
	if (!f)
		return NULL;

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);

	text = malloc(size + 1);
	if (!text) {
		fclose(f);
		return NULL;
	}
	fread(text, 1, size, f);
	text[size] = '\0';
	fclose(f);

	skin = TextToNode(text);
	free(text);
	if (!skin)
		return NULL;

	AddProp(class, skin);
	return skin;
}

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	(void) instance; (void) message; (void) data;

	return rtrn_dropped;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	(void) message; (void) data;

	SetName(class, "Skin");

	/* no InstanceStart: a skin is a property on another class, not a thing
	   you make one of */

	ClassSelf = RegisterClass(library, class);

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Object");

	/* the names skin.h looks up */
	SetPropLong(ClassSelf, "Generate", (long)GenerateSkin);
	SetPropLong(ClassSelf, "Get",      (long)GetClassSkin);
	SetPropLong(ClassSelf, "Save",     (long)SaveSkin);
	SetPropLong(ClassSelf, "Load",     (long)LoadSkin);

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

	SetName(temp, "Skin");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "6a6d1acd-54de-4460-8788-2ee9c169ea60");
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
