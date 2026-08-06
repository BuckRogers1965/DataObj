#ifndef SERIALIZER_H
#define SERIALIZER_H

/*
 * serializer.h - a view's portable state: export it, import it, load it.
 *
 * serializer.object owns BOTH directions of the format. It used to be split:
 * the export walk was already an object while the import parser sat in
 * object.c, so one format had two homes that had to agree with nothing
 * enforcing it.
 *
 * The entry points are function-pointer properties on the Serializer class
 * node, so a caller goes through the registry it already shares instead of
 * linking against another .object. SERIALIZER_IMPL is defined by the
 * implementation, which wants the prototypes.
 */

#include "node.h"
#include "object.h"
#include "DebugPrint.h"

#ifdef SERIALIZER_IMPL
void    ExportView(NodeObj view, char *path);
NodeObj ImportView(NodeObj container, char *path, char *dropX, char *dropY);
void    LoadViewAsync(NodeObj container, char *path,
					  void (*onDone)(NodeObj container, int ok, void *ctx), void *ctx);
#else
static inline long SerializerEntry(char *name)
{
	static NodeObj cls;
	char msg[140];

	if (!cls)
		cls = FindClass("Serializer");
	if (!cls) {
		snprintf(msg, sizeof(msg),
				 "serializer.h: the Serializer class is not loaded - '%s' unreachable",
				 name);
		DebugPrint(msg, __FILE__, __LINE__, ERROR);
		return 0;
	}
	return GetPropLong(cls, name);
}

static inline void ExportView(NodeObj view, char *path)
{
	void (*fn)(NodeObj, char *) = (void (*)(NodeObj, char *)) SerializerEntry("Export");

	if (fn)
		fn(view, path);
}

static inline NodeObj ImportView(NodeObj container, char *path, char *dropX, char *dropY)
{
	NodeObj (*fn)(NodeObj, char *, char *, char *) =
		(NodeObj (*)(NodeObj, char *, char *, char *)) SerializerEntry("Import");

	return fn ? fn(container, path, dropX, dropY) : NULL;
}

static inline void LoadViewAsync(NodeObj container, char *path,
								 void (*onDone)(NodeObj container, int ok, void *ctx), void *ctx)
{
	void (*fn)(NodeObj, char *, void (*)(NodeObj, int, void *), void *) =
		(void (*)(NodeObj, char *, void (*)(NodeObj, int, void *), void *)) SerializerEntry("LoadAsync");

	if (fn)
		fn(container, path, onDone, ctx);
}
#endif

#endif
