#ifndef SKIN_H
#define SKIN_H

/*
 * skin.h - a class's layout, reached through the Skin class.
 *
 * The implementation lives in skin.object. Its entry points are function-
 * pointer properties on the Skin class node, so a caller goes through the
 * registry it already shares instead of linking against another .object.
 * SKIN_IMPL is defined by the implementation, which wants the prototypes.
 */

#include "node.h"
#include "object.h"

#ifdef SKIN_IMPL
NodeObj GenerateSkin(NodeObj class);
NodeObj GetClassSkin(NodeObj class);
int     SaveSkin(NodeObj class, char *filename);
NodeObj LoadSkin(NodeObj class, char *filename);
#else
static inline long SkinEntry(char *name)
{
	static NodeObj cls;
	char msg[120];

	if (!cls)
		cls = FindClass("Skin");
	if (!cls) {
		snprintf(msg, sizeof(msg),
				 "skin.h: the Skin class is not loaded - '%s' unreachable", name);
		DebugPrint(msg, __FILE__, __LINE__, ERROR);
		return 0;
	}
	return GetPropLong(cls, name);
}

static inline NodeObj GenerateSkin(NodeObj class)
{
	NodeObj (*fn)(NodeObj) = (NodeObj (*)(NodeObj)) SkinEntry("Generate");

	return fn ? fn(class) : NULL;
}

static inline NodeObj GetClassSkin(NodeObj class)
{
	NodeObj (*fn)(NodeObj) = (NodeObj (*)(NodeObj)) SkinEntry("Get");

	return fn ? fn(class) : NULL;
}

static inline int SaveSkin(NodeObj class, char *filename)
{
	int (*fn)(NodeObj, char *) = (int (*)(NodeObj, char *)) SkinEntry("Save");

	return fn ? fn(class, filename) : 0;
}

static inline NodeObj LoadSkin(NodeObj class, char *filename)
{
	NodeObj (*fn)(NodeObj, char *) = (NodeObj (*)(NodeObj, char *)) SkinEntry("Load");

	return fn ? fn(class, filename) : NULL;
}
#endif

#endif
