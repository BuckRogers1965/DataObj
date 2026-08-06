
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"
#define SERIALIZER_IMPL
#include "serializer.h"

/*

Serializer object: a task-driven CONTAINMENT walker. Point Root at a view (by
path) and it walks that view and everything under it, emitting the PORTABLE
state as JSON chunks out its Out port. Each node is written as
  {"class":.., "name":.., "props":{name:value,..},
   "wires":[{"from":port,"to":sinkpath,"port":sinkport},..], "children":[..]}
where `class` (from the instance's registry parent) lets a load re-create it,
`props` are the DATA properties (the runtime pointer props - LONG-typed local/
Activate/OnMsg/task handles - are skipped, and stale shadows deduped), `wires`
are this node's outgoing Connect()ions (each Subscriber record on a source
port, its sink resolved to a path), and `children` are the instances whose
Container is THIS node's path (containment is by path, not node-children, so the
walk scans the registry for them).

Link targets (a wire's `to`, an alias's Target) that point INSIDE the exported
view are written RELATIVE to the export root (the leading root path stripped -
`Slider_2`, not `/Root/View_1/Slider_2`), so the export drops into any container
like a clone; targets OUTSIDE the view keep their absolute path. Import prepends
the imported view's new path to the relative ones and leaves the absolute ones
alone - no rename map, because a fresh view keeps its children's names.

The walk is its OWN scheduler task, an explicit STACK of frames rather than C
recursion, advanced a batch of steps per tick - so a huge tree never blocks the
fabric. Wire Out into a Writer to save the state to a file (Serializer -> Writer
is Save, the way Reader -> Writer is cat).

*/

#define CHUNK_FLUSH    3072		/* flush the buffer out Out past this many bytes */
#define STEPS_PER_TICK  400		/* walk this many frame-steps per task tick      */

/* one node's position in the walk. Containment is by the Container PATH, not
   node-children (an instance lives under its CLASS node, linked to its view by
   Container), so a node's "children" are the registry instances whose Container
   is this node's own path - found with a registry scan (NextContainerChild). */
typedef struct
{
	NodeObj node;
	int     phase;		/* 0 = open+props, 1 = container-children */
	NodeObj child;		/* last container-child emitted (the scan cursor) */
	int     first;		/* first element of the children array */
	char    cpath[256];	/* this node's own path, for matching Container */
} Frame;

typedef struct InstanceData
{
	int     enabled;
	int     active;		/* a walk is in progress */
	TaskObj task;

	Frame  *stack;		/* the explicit walk stack (grows) */
	int     depth, cap;

	char   *buf;		/* the emit buffer, flushed out Out in chunks */
	int     buflen, bufcap;

	/* the export root path - internal links are written relative to it. A
	   COPY, never a pointer into the node's value: any property write during
	   the walk (RegisterPath's LastMember, for one) can move that buffer, and
	   a stale root matches nothing - so every internal path came out absolute,
	   silently, and the imported copy wired itself to the original. */
	char    root[320];
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static NodeObj ExportHome = NULL;
static int     exportSeq = 0;

void ExportView(NodeObj view, char *path)
{
	char    viewpath[300], sername[64], wrname[64];
	NodeObj ser, wr;

	if (!view || !path || !path[0] || !PathOfInstance(view, viewpath, sizeof(viewpath)))
		return;

	if (!ExportHome)
		ExportHome = CreateRoot("Export");

	/* fresh, uniquely-named plumbing per export (create + name + register) */
	exportSeq++;
	snprintf(sername, sizeof(sername), "Ser%d", exportSeq);
	snprintf(wrname,  sizeof(wrname),  "Wr%d",  exportSeq);
	ser = Widget_Create(ExportHome, "Serializer", sername);
	wr  = Widget_Create(ExportHome, "Writer",     wrname);
	if (!ser || !wr)
		return;

	SetPropStr(ser, "Root", viewpath);		/* walk this view */
	SetPropStr(wr,  "Filename", path);		/* drain to the file */
	Connect(ser, "Out", wr, "In");
	ActivateInstance(wr);					/* open the file, then */
	ActivateInstance(ser);					/* walk + stream into it */
}

/*
 * Import: the inverse of ExportView - reconstruct a live subtree from
 * what ExportView (the Serializer) wrote. The bridge used to hand-roll
 * this parsing AND the reconstruction itself (Bridge_ImportNode et al,
 * bridge.c), calling back into Bridge_Dispatch for every piece created -
 * work the engine should be doing, reachable with no bridge attached at
 * all, exactly like ExportView already is.
 *
 * Two verbs share this machinery:
 *   ImportView  - a CLONE-DROP: the exported view's own top node is
 *                 re-created (a taken name mints fresh), its children
 *                 keep their recorded names verbatim (a fresh container
 *                 can't collide with itself).
 *   LoadView    - RESTORE IN PLACE: `container`'s CURRENT children are
 *                 destroyed first, then the file's own top-level node is
 *                 NOT re-created (container already exists in its place,
 *                 same as ExportView(root,...) exported it) - its
 *                 children are imported straight into container,
 *                 verbatim (container was just cleared, nothing collides).
 */

/* ---- a parser for the Serializer's own {class,name,props,wires,
   children} shape - NOT the shape TextToNode/NodeToText use (that's a
   node's own type/value; this is a class + published-state snapshot) ---- */


static void IJ_Ws(char **pp)
{
	char *p = *pp;
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;
	*pp = p;
}

/* parse a JSON string token at *pp (must be on the opening quote); returns
   the malloc'd, unescaped contents and advances *pp past the closing quote. */
static char *IJ_Str(char **pp)
{
	char *p = *pp, *out, *o;

	if (*p != '"')
		return NULL;
	p++;
	out = malloc(strlen(p) + 1);
	o = out;
	while (*p && *p != '"')
	{
		if (*p == '\\')
		{
			p++;
			switch (*p)
			{
				case 'n': *o++ = '\n'; break;
				case 't': *o++ = '\t'; break;
				case 'r': *o++ = '\r'; break;
				case 'b': *o++ = '\b'; break;
				case 'f': *o++ = '\f'; break;
				case '/': *o++ = '/';  break;
				case '"': *o++ = '"';  break;
				case '\\': *o++ = '\\'; break;
				case 'u':
				{
					int h = 0, i;
					p++;
					for (i = 0; i < 4 && *p; i++)
					{
						char c = *p;
						h <<= 4;
						if (c >= '0' && c <= '9') h |= c - '0';
						else if (c >= 'a' && c <= 'f') h |= c - 'a' + 10;
						else if (c >= 'A' && c <= 'F') h |= c - 'A' + 10;
						p++;
					}
					p--;				/* the loop ++ below re-consumes one */
					if (h < 0x80)
						*o++ = (char) h;
					else if (h < 0x800)
					{
						*o++ = 0xC0 | (h >> 6);
						*o++ = 0x80 | (h & 0x3F);
					}
					else
					{
						*o++ = 0xE0 | (h >> 12);
						*o++ = 0x80 | ((h >> 6) & 0x3F);
						*o++ = 0x80 | (h & 0x3F);
					}
					break;
				}
				default: *o++ = *p; break;
			}
			if (*p)
				p++;
		}
		else
			*o++ = *p++;
	}
	if (*p != '"')
	{
		free(out);
		return NULL;
	}
	p++;
	*o = '\0';
	*pp = p;
	return out;
}

/* an unused path like <prefix>/<Base>_N, from that one rule */
static void ImportFreshName(char *prefix, char *base, char *out, int outlen)
{
	char name[200];

	MintFreshName(base, prefix, name, sizeof(name));
	snprintf(out, outlen, "%s/%s", (prefix && prefix[0]) ? prefix : "/Root", name);
}

/* create one instance the way a live create-instance would (naming,
   placement, data properties) - direct engine calls, no bridge command
   round trip. Returns its actual minted full path (caller frees), NULL
   on failure. force=1: caller guarantees the name is free and it MUST be
   kept verbatim (an internal node of a fresh container, or LoadView's
   own just-cleared container - either way nothing can collide). */
static char *ImportCreate(char *className, char *nodeName,
						   NodeObj propbag, char *containerPath, int force)
{
	NodeObj home, inst, p;
	char    desired[320], fresh[320], *x, *y, *ident, *alias, *slash, dbg[512];
	char   *cpath = (containerPath && containerPath[0]) ? containerPath : "/Root";

	snprintf(dbg, sizeof(dbg), "IMPORT-CREATE enter: class='%s' name='%s' container='%s' force=%d",
			 className ? className : "(null)", nodeName ? nodeName : "(null)", cpath, force);
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);

	if (!className || !className[0])
		return NULL;

	/* the node's IDENTITY is its "Name" PROP (Slider_1), NOT the JSON
	   "name" field (the class node's own name, "Slider", same for every
	   instance) - relative links are stored by the Name prop
	   (PathOfInstance uses it), so import must recreate each node under
	   that same name or every link misses. */
	ident = propbag ? GetPropStr(propbag, "Name") : NULL;
	if (!ident || !ident[0])
		ident = nodeName;

	home = ResolvePath(cpath);
	if (!home)
	{
		DebugPrint("IMPORT-CREATE: container path did not resolve, bail", __FILE__, __LINE__, IMPORT);
		return NULL;
	}
	inst = CreateObject(home, className);
	if (!inst)
	{
		DebugPrint("IMPORT-CREATE: CreateObject failed (unknown class?), bail", __FILE__, __LINE__, IMPORT);
		return NULL;
	}
	snprintf(dbg, sizeof(dbg), "IMPORT-CREATE: instance created at %p, placing", (void *) inst);
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);

	x = propbag ? GetPropStr(propbag, "X") : NULL;
	y = propbag ? GetPropStr(propbag, "Y") : NULL;
	PlaceInstance(inst, cpath, (x && x[0]) ? x : "0", (y && y[0]) ? y : "0");

	alias = NULL;
	if (ident && ident[0])
	{
		snprintf(desired, sizeof(desired), "%s/%s", cpath, ident);
		alias = desired;
	}
	if (!alias || !alias[0] || (!force && ResolvePath(alias)))
	{
		/* the name is KNOWN - a view called Connect stays called Connect.
		   Only uniqueness is in question here, so suffix the name it has
		   (Connect_1) rather than mint from the class and lose it (View_1). */
		ImportFreshName(cpath, (ident && ident[0]) ? ident : className,
						fresh, sizeof(fresh));
		alias = fresh;
	}

	snprintf(dbg, sizeof(dbg), "IMPORT-CREATE: registering path '%s'", alias);
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
	RegisterPath(alias, inst);
	slash = strrchr(alias, '/');
	SetOrDeliverProp(inst, "Name", slash ? slash + 1 : alias);

	/* the rest of the saved properties - skip identity/geometry (set
	   above) and State (a runtime readout, not portable state).

	   SetPropStr, never SetOrDeliverProp: restoring saved state is not the
	   same act as sending a message. SetPropStr updates the value in place
	   and fans out to whatever subscribed to it - it changed, subscribers
	   are told - without invoking the property's OWN handler.
	   SetOrDeliverProp does invoke it, and that is what made restore
	   destroy the values it was restoring: writing a saved MenuButton "In"
	   ran MenuButton_OnIn, which mirrors its input into "Selected",
	   overwriting the real saved Selected with whatever stale thing In
	   happened to hold ("0"). Delivering was the error - nothing about the
	   property's name, and no annotation on it would have prevented it. */
	if (propbag)
		for (p = GetNextProp(propbag); p; p = GetNextSibling(p))
		{
			char *pn = GetNameStr(p);

			if (!pn || !strcmp(pn, "X") || !strcmp(pn, "Y") || !strcmp(pn, "Name")
				|| !strcmp(pn, "Container") || !strcmp(pn, "State"))
				continue;
			SetPropStr(inst, pn, GetValueStr(p));
		}

	snprintf(dbg, sizeof(dbg), "IMPORT-CREATE done: '%s' -> path '%s'", className, alias);
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
	return strdup(alias);
}

/* an Alias is a LINK, not a data snapshot - creating it as a plain
   instance and copying its Target string leaves a dead control pointing
   at the original. So aliases are not created in the build pass; they
   are remembered here and remade afterwards (ImportAliasesPass), once
   every target exists, via a real LinkPropertyAs onto the resolved
   target. `containerPath` is the parent's ACTUAL new path. */
static void ImportDeferAlias(NodeObj propbag, char *containerPath, NodeObj deferred)
{
	NodeObj c = NewNode(INTEGER);
	char   *v;

	SetName(c, "alias");
	v = propbag ? GetPropStr(propbag, "Target") : NULL;
	SetPropStr(c, "of_old", v ? v : "");
	v = propbag ? GetPropStr(propbag, "TargetProp") : NULL;
	SetPropStr(c, "prop", v ? v : "");
	SetPropStr(c, "container", (containerPath && containerPath[0]) ? containerPath : "/Root");
	v = propbag ? GetPropStr(propbag, "X") : NULL;
	SetPropStr(c, "x", v ? v : "0");
	v = propbag ? GetPropStr(propbag, "Y") : NULL;
	SetPropStr(c, "y", v ? v : "0");
	v = propbag ? GetPropStr(propbag, "Widget") : NULL;
	SetPropStr(c, "Widget", v ? v : "");
	v = propbag ? GetPropStr(propbag, "Label") : NULL;
	SetPropStr(c, "Label", v ? v : "");
	AppendChild(deferred, c);
}

/* create a node if it is a concrete instance, or defer it if it is an
   Alias. Returns the new path for concrete nodes (so children parent
   onto it), "" for aliases (they hold no children), NULL on failure. */
static char *ImportPlace(char *className, char *nodeName, NodeObj propbag,
						  char *containerPath, NodeObj deferred, int force)
{
	if (className && strcmp(className, "Alias") == 0)
	{
		ImportDeferAlias(propbag, containerPath, deferred);
		return strdup("");
	}
	return ImportCreate(className, nodeName, propbag, containerPath, force);
}

/* parse one {class,name,props,wires,children} object at *pp and recreate
   it (and, recursively, its children) under containerPath. Returns the
   node's actual minted path (caller frees), "" for an alias, NULL on a
   malformed object.

   skipSelf: parse this node's own fields (advancing the cursor, and
   validating the file) but do NOT create it - its "children" (and any
   of its own "wires", though a container itself rarely has any) are
   processed with containerPath as their own container directly. This is
   LoadView's own top-level node: container already exists in its place
   (ExportView(root,...) is what wrote this entry), so nothing about it
   is re-created - only its contents are (re)built. */
static char *ImportNode(char **pp, char *containerPath, NodeObj deferred,
						 NodeObj wires, int isTop, int skipSelf)
{
	char   *key, *className = NULL, *nodeName = NULL, *actualPath = NULL;
	NodeObj propbag = NULL;
	int     force = !isTop;		/* only a fresh drop's own top re-mints */
	char    dbg[400];

	snprintf(dbg, sizeof(dbg), "IMPORT-NODE enter: container='%s' isTop=%d skipSelf=%d force=%d",
			 containerPath ? containerPath : "(null)", isTop, skipSelf, force);
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);

	IJ_Ws(pp);
	if (**pp != '{')
	{
		DebugPrint("IMPORT-NODE: expected '{', malformed input, bail", __FILE__, __LINE__, IMPORT);
		return NULL;
	}
	(*pp)++;

	for (;;)
	{
		IJ_Ws(pp);
		if (**pp == '}')
		{
			(*pp)++;
			break;
		}
		key = IJ_Str(pp);
		if (!key)
			goto fail;
		IJ_Ws(pp);
		if (**pp != ':')
		{
			free(key);
			goto fail;
		}
		(*pp)++;
		IJ_Ws(pp);

		if (strcmp(key, "class") == 0)
			className = IJ_Str(pp);
		else if (strcmp(key, "name") == 0)
			nodeName = IJ_Str(pp);
		else if (strcmp(key, "props") == 0)
		{
			if (**pp != '{')
			{
				free(key);
				goto fail;
			}
			(*pp)++;
			propbag = NewNode(INTEGER);
			for (;;)
			{
				char *pk, *pv;

				IJ_Ws(pp);
				if (**pp == '}')
				{
					(*pp)++;
					break;
				}
				pk = IJ_Str(pp);
				if (!pk)
				{
					free(key);
					goto fail;
				}
				IJ_Ws(pp);
				if (**pp != ':')
				{
					free(pk);
					free(key);
					goto fail;
				}
				(*pp)++;
				IJ_Ws(pp);
				pv = IJ_Str(pp);
				if (!pv)
				{
					free(pk);
					free(key);
					goto fail;
				}
				SetPropStr(propbag, pk, pv);
				free(pk);
				free(pv);
				IJ_Ws(pp);
				if (**pp == ',')
					(*pp)++;
			}
		}
		else if (strcmp(key, "wires") == 0)
		{
			/* this node's outgoing connections. It must exist to be the
			   wire's `from`; the sink `to` is an original path, remapped
			   in the wire pass once every instance exists. */
			if (!skipSelf && !actualPath)
			{
				actualPath = ImportPlace(className, nodeName, propbag, containerPath, deferred, force);
				snprintf(dbg, sizeof(dbg), "IMPORT-NODE (wires-branch) placed -> %s",
						 actualPath ? actualPath : "(null/failed)");
				DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
			}
			IJ_Ws(pp);
			if (**pp != '[')
			{
				free(key);
				goto fail;
			}
			(*pp)++;
			for (;;)
			{
				char *wf = NULL, *wt = NULL, *wp = NULL, *wk;

				IJ_Ws(pp);
				if (**pp == ']')
				{
					(*pp)++;
					break;
				}
				if (**pp != '{')
				{
					free(key);
					goto fail;
				}
				(*pp)++;
				for (;;)
				{
					char *wv;

					IJ_Ws(pp);
					if (**pp == '}')
					{
						(*pp)++;
						break;
					}
					wk = IJ_Str(pp);
					if (!wk)
					{
						free(wf); free(wt); free(wp); free(key);
						goto fail;
					}
					IJ_Ws(pp);
					if (**pp != ':')
					{
						free(wk); free(wf); free(wt); free(wp); free(key);
						goto fail;
					}
					(*pp)++;
					IJ_Ws(pp);
					wv = IJ_Str(pp);
					if (!wv)
					{
						free(wk); free(wf); free(wt); free(wp); free(key);
						goto fail;
					}
					if (!strcmp(wk, "from"))      { free(wf); wf = wv; }
					else if (!strcmp(wk, "to"))   { free(wt); wt = wv; }
					else if (!strcmp(wk, "port")) { free(wp); wp = wv; }
					else free(wv);
					free(wk);
					IJ_Ws(pp);
					if (**pp == ',')
						(*pp)++;
				}
				/* record from OUR new path -> the sink's ORIGINAL path
				   (resolved once every instance exists, ImportWiresPass) -
				   an alias/skipped node carries no path, wires nothing */
				if (actualPath && actualPath[0] && wf && wt)
				{
					NodeObj w = NewNode(INTEGER);
					SetPropStr(w, "from", actualPath);
					SetPropStr(w, "fromPort", wf);
					SetPropStr(w, "to_old", wt);
					SetPropStr(w, "toPort", wp ? wp : "");
					AppendChild(wires, w);
				}
				free(wf); free(wt); free(wp);
				IJ_Ws(pp);
				if (**pp == ',')
					(*pp)++;
			}
		}
		else if (strcmp(key, "children") == 0)
		{
			/* the parent must exist before its children can name it as
			   their container - class/name/props are all in by now */
			if (!skipSelf && !actualPath)
			{
				actualPath = ImportPlace(className, nodeName, propbag, containerPath, deferred, force);
				snprintf(dbg, sizeof(dbg), "IMPORT-NODE (children-branch) placed -> %s",
						 actualPath ? actualPath : "(null/failed)");
				DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
			}
			IJ_Ws(pp);
			if (**pp != '[')
			{
				free(key);
				goto fail;
			}
			(*pp)++;
			for (;;)
			{
				char *cp;

				IJ_Ws(pp);
				if (**pp == ']')
				{
					(*pp)++;
					break;
				}
				/* children: verbatim, own X/Y (isTop=0, force=1) - a
				   skipped top's own children are container's TOP-LEVEL
				   entries instead, and ALSO force=1: container was just
				   destroyed (LoadView), so every recorded name is
				   guaranteed free - use it exactly, never mint an
				   approximation */
				snprintf(dbg, sizeof(dbg), "IMPORT-NODE recursing into child under '%s'",
						 skipSelf ? containerPath : (actualPath ? actualPath : containerPath));
				DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
				cp = ImportNode(pp, skipSelf ? containerPath : (actualPath ? actualPath : containerPath),
								deferred, wires, 0, 0);
				if (cp)
					free(cp);
				else
				{
					DebugPrint("IMPORT-NODE: child failed, bail", __FILE__, __LINE__, IMPORT);
					goto childfail;
				}
				IJ_Ws(pp);
				if (**pp == ',')
					(*pp)++;
			}
		}
		else
		{
			char *sk = IJ_Str(pp);		/* unknown key: skip its string value */
			if (sk)
				free(sk);
		}
		free(key);
		IJ_Ws(pp);
		if (**pp == ',')
			(*pp)++;
	}

	/* a childless, wireless node was never created above - do it now */
	if (!skipSelf && !actualPath)
	{
		actualPath = ImportPlace(className, nodeName, propbag, containerPath, deferred, force);
		snprintf(dbg, sizeof(dbg), "IMPORT-NODE (fallback) placed -> %s",
				 actualPath ? actualPath : "(null/failed)");
		DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
	}

	snprintf(dbg, sizeof(dbg), "IMPORT-NODE exit OK: -> '%s'", actualPath ? actualPath : "");
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
	if (className) free(className);
	if (nodeName)  free(nodeName);
	if (propbag)   DelNode(propbag);
	return actualPath ? actualPath : strdup("");

childfail:
	free(key);
fail:
	DebugPrint("IMPORT-NODE exit FAIL: malformed input", __FILE__, __LINE__, IMPORT);
	if (className) free(className);
	if (nodeName)  free(nodeName);
	if (propbag)   DelNode(propbag);
	if (actualPath) free(actualPath);
	return NULL;
}

/* resolve a saved link target against the imported root. A RELATIVE
   target (no leading '/') pointed inside the exported subtree - prepend
   `importRoot` (the container everything was imported under), and
   because a fresh container keeps its children's names it lands on the
   copy. An ABSOLUTE target pointed OUTSIDE the exported subtree - leave
   it, it still names the live original. An EMPTY-BUT-PRESENT target is
   RelTo's own "the root itself" convention (serializer.c: a wire or
   link pointing at the exported subtree's own top node collapses to ""
   rather than a relative path, since there's nothing to strip a prefix
   off of) - that resolves to importRoot itself, NOT "no target": a
   member wired back to its own container (a composite widget's inner
   logic Connect()'d to the container's own port, e.g.) was silently
   never reconnected on import before this, with no error - the wire
   just quietly didn't exist. A genuinely absent field (saved == NULL,
   never written at all) is still "no target". Writes into `out`,
   returns it. */
static char *ImportResolveTarget(char *importRoot, char *saved, char *out, int len)
{
	if (!saved)
	{
		out[0] = '\0';
		return out;
	}
	if (!saved[0])
	{
		snprintf(out, len, "%s", (importRoot && importRoot[0]) ? importRoot : "/Root");
		return out;
	}
	if (saved[0] == '/')
		snprintf(out, len, "%s", saved);
	else
		snprintf(out, len, "%s/%s", (importRoot && importRoot[0]) ? importRoot : "/Root", saved);
	return out;
}

/* second pass: every alias remembered during the build, now remade as a
   real link (direct engine calls: CreateObject + LinkPropertyAs, not a
   bridge command round trip). */
static void ImportAliasesPass(char *importRoot, NodeObj deferred)
{
	NodeObj d;
	char    dbg[512];

	snprintf(dbg, sizeof(dbg), "IMPORT-ALIASES-PASS enter: importRoot='%s'", importRoot ? importRoot : "(null)");
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);

	for (d = GetChild(deferred); d; d = GetNextSibling(d))
	{
		char    of[320], fresh[320];
		char   *container, *prop, *w, *lb, *alias, *slash;
		NodeObj target, home, inst, owner, node, pub;

		ImportResolveTarget(importRoot, GetPropStr(d, "of_old"), of, sizeof(of));
		target = of[0] ? ResolvePath(of) : NULL;
		prop = GetPropStr(d, "prop");
		container = GetPropStr(d, "container");
		snprintf(dbg, sizeof(dbg), "IMPORT-ALIASES-PASS: of='%s' target=%p prop='%s' container='%s'",
				 of, (void *) target, prop ? prop : "(null)", container ? container : "(null)");
		DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
		if (!target || !prop || !prop[0] || !container)
		{
			DebugPrint("IMPORT-ALIASES-PASS: unresolved target/prop/container, skip", __FILE__, __LINE__, IMPORT);
			continue;
		}

		home = ResolvePath(container);
		if (!home)
		{
			DebugPrint("IMPORT-ALIASES-PASS: container path did not resolve, skip", __FILE__, __LINE__, IMPORT);
			continue;
		}
		inst = CreateObject(home, "Alias");
		if (!inst)
		{
			DebugPrint("IMPORT-ALIASES-PASS: CreateObject(Alias) failed, skip", __FILE__, __LINE__, IMPORT);
			continue;
		}

		if (!LinkPropertyAs(inst, "Value", target, prop))
		{
			DeleteInstance(inst);
			continue;
		}

		/* record the FINAL original, not whatever happened to be linked -
		   aliasing an alias collapses to the original at the link level */
		owner = target;
		node = ResolvePort(&owner, prop);
		if (node)
			prop = GetNameStr(node);
		if (owner != target && PathOfInstance(owner, of, sizeof(of)))
			target = owner;

		pub = InterfacePropForInstance(owner, prop);
		if (pub)
			SetPropInt(inst, "Widget", GetPropInt(pub, "Widget"));

		SetPropStr(inst, "Target", of);
		SetPropStr(inst, "TargetProp", prop);

		PlaceInstance(inst, container, GetPropStr(d, "x"), GetPropStr(d, "y"));

		ImportFreshName(container, "Alias", fresh, sizeof(fresh));
		alias = fresh;
		RegisterPath(alias, inst);
		slash = strrchr(alias, '/');
		SetOrDeliverProp(inst, "Name", slash ? slash + 1 : alias);

		/* restore the alias's own look (create-alias stamps the target's
		   published default; the saved alias may have been restyled) */
		w  = GetPropStr(d, "Widget");
		lb = GetPropStr(d, "Label");
		if (w && w[0])
			SetOrDeliverProp(inst, "Widget", w);
		if (lb && lb[0])
			SetOrDeliverProp(inst, "Label", lb);

		/* three paths into one line: bound each so the whole always fits */
		snprintf(dbg, sizeof(dbg),
				 "IMPORT-ALIASES-PASS: alias '%.140s' -> ('%.140s','%.140s') done",
				 alias, of, prop);
		DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
	}
	DebugPrint("IMPORT-ALIASES-PASS done", __FILE__, __LINE__, IMPORT);
}

/* third pass: the wires. from is already OUR minted path; to is the saved
   sink (relative inside the import -> resolved under importRoot, absolute
   outside -> left alone), connected directly (Connect(), not a bridge
   command) once every instance and alias exists. */
static void ImportWiresPass(char *importRoot, NodeObj wires)
{
	NodeObj w;
	char    dbg[512];

	snprintf(dbg, sizeof(dbg), "IMPORT-WIRES-PASS enter: importRoot='%s'", importRoot ? importRoot : "(null)");
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);

	for (w = GetChild(wires); w; w = GetNextSibling(w))
	{
		char    to[320];
		char   *from = GetPropStr(w, "from");
		NodeObj fromInst, toInst;

		ImportResolveTarget(importRoot, GetPropStr(w, "to_old"), to, sizeof(to));
		fromInst = (from && from[0]) ? ResolvePath(from) : NULL;
		toInst = to[0] ? ResolvePath(to) : NULL;
		snprintf(dbg, sizeof(dbg), "IMPORT-WIRES-PASS: from='%s'(%p) to='%s'(%p) port='%s'/'%s'",
				 from ? from : "(null)", (void *) fromInst, to, (void *) toInst,
				 GetPropStr(w, "fromPort") ? GetPropStr(w, "fromPort") : "", GetPropStr(w, "toPort") ? GetPropStr(w, "toPort") : "");
		DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
		if (fromInst && toInst)
			Connect(fromInst, GetPropStr(w, "fromPort"), toInst, GetPropStr(w, "toPort"));
		else
			DebugPrint("IMPORT-WIRES-PASS: endpoint missing, skipped", __FILE__, __LINE__, IMPORT);
	}
	DebugPrint("IMPORT-WIRES-PASS done", __FILE__, __LINE__, IMPORT);
}

/*
 * The "destroy" half of LoadView's restore-in-place, staggered through
 * the scheduler exactly the way the cat flow's Reader parses a file -
 * one unit of work per re-armed task, emitted as its own top-level
 * ExecTasks call, never a native C loop calling DeleteInstance (and
 * everything DeleteInstance itself fans out - ScrubRegistrySubscriptions,
 * CancelPendingSends, the removal SndMsg a bridge sends per victim)
 * hundreds of times back to back inside one call stack. That tight-loop
 * shape is exactly what "queued through the scheduler... never nests
 * inside the sender's call stack" (SndMsg's own doc comment) exists to
 * prevent, and a full-session load is the first thing in this codebase
 * large enough (hundreds of instances) to actually hit the case: a
 * synchronous burst that size corrupted scheduler task-pool state and
 * crashed AddTaskDelay a few hundred calls later (confirmed from a core
 * dump: task->owner read back as a stomped 0x559a00000001, a classic
 * heap-corruption signature, not a null or a logic bug tied to any one
 * instance). VNOS's own file object is the reference for this shape:
 * parse a chunk, emit a message, let the scheduler bring you back for
 * the next chunk - never the whole file in one call.
 *
 * The snapshot walk itself (registry-wide, but read-only - no
 * DeleteInstance, no SndMsg) stays a single synchronous pass; only the
 * deletions are staggered, one per task.
 */
typedef struct
{
	NodeObj  victims;			/* scratch: children are path -> long(NodeObj) */
	NodeObj  cursor;			/* next victim to process */
	NodeObj  container;
	TaskObj  task;
	void   (*onDone)(NodeObj container, void *ctx);
	void    *ctx;
} DestroyCtx;


static int DestroyContentsStep(NodeObj arg, NodeObj unused, int reason)
{
	DestroyCtx *dc = (DestroyCtx *) arg;
	NodeObj     entry, victim;
	char        dbg[400];

	(void) unused;

	if (reason != task_callback)
	{
		DebugPrint("DESTROY-CONTENTS-STEP: deactivated mid-batch, cleaning up without finishing", __FILE__, __LINE__, IMPORT);
		DelNode(dc->victims);
		free(dc);
		return rtrn_handled;
	}

	entry = dc->cursor;
	if (!entry)
	{
		NodeObj container = dc->container;
		void  (*onDone)(NodeObj, void *) = dc->onDone;
		void   *ctx = dc->ctx;

		DebugPrint("DESTROY-CONTENTS-STEP: batch done, calling onDone", __FILE__, __LINE__, IMPORT);
		DelNode(dc->victims);
		RemoveTask(dc->task);
		free(dc);
		if (onDone)
			onDone(container, ctx);
		return rtrn_handled;
	}

	dc->cursor = GetNextSibling(entry);
	victim = (NodeObj) GetValueLong(entry);
	if (victim)
	{
		snprintf(dbg, sizeof(dbg), "DESTROY-CONTENTS-STEP: deleting '%s' (%p)", GetNameStr(entry), (void *) victim);
		DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
		UnregisterPath(GetNameStr(entry));
		DeleteInstance(victim);
	}

	/* re-arm the SAME task for the next victim - a fresh top-level      */
	/* ExecTasks entry, never nested inside this call                     */
	AddTaskNow(dc->task, (FuncPtr) DestroyContentsStep, 0, (NodeObj) dc);
	return rtrn_handled;
}

static void DestroyContentsAsync(NodeObj container, void (*onDone)(NodeObj container, void *ctx), void *ctx)
{
	char       ownPath[300], prefix[320], pbuf[300], dbg[512];
	int        preLen, n = 0;
	NodeObj    lib, cls, mem, snap;
	DestroyCtx *dc;

	DebugPrint("DESTROY-CONTENTS-ASYNC enter", __FILE__, __LINE__, IMPORT);

	if (!container || !PathOfInstance(container, ownPath, sizeof(ownPath)))
	{
		DebugPrint("DESTROY-CONTENTS-ASYNC: container missing/unpathable, bail", __FILE__, __LINE__, IMPORT);
		if (onDone)
			onDone(container, ctx);
		return;
	}
	snprintf(prefix, sizeof(prefix), "%s/", ownPath);
	preLen = (int) strlen(prefix);
	snprintf(dbg, sizeof(dbg), "DESTROY-CONTENTS-ASYNC: scanning under '%s'", prefix);
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);

	snap = NewNode(INTEGER);
	for (lib = GetChild(GetRegObjList()); lib; lib = GetNextSibling(lib))
	 for (cls = GetChild(lib); cls; cls = GetNextSibling(cls))
	  for (mem = GetChild(cls); mem; mem = GetNextSibling(mem))
	  {
		if (!PathOfInstance(mem, pbuf, sizeof(pbuf)))
			continue;
		if (strncmp(pbuf, prefix, preLen) != 0)
			continue;
		SetPropLong(snap, pbuf, (long) mem);
		n++;
	  }
	snprintf(dbg, sizeof(dbg), "DESTROY-CONTENTS-ASYNC: snapshot done, %d victims - staggering deletes", n);
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);

	dc = malloc(sizeof(DestroyCtx));
	dc->victims = snap;
	dc->cursor = GetNextProp(snap);
	dc->container = container;
	dc->onDone = onDone;
	dc->ctx = ctx;
	dc->task = GetTask(ObjGetTaskList());
	AddTaskNow(dc->task, (FuncPtr) DestroyContentsStep, 0, (NodeObj) dc);
}

/*
 * {"cmd":"import-flow"} - drop a saved view onto the canvas as a fresh
 * copy (a clone with a side trip to disk). container/dropX/dropY are
 * where it lands; its own top-level name mints fresh if taken, its
 * internals keep their recorded names verbatim.
 */
NodeObj ImportView(NodeObj container, char *path, char *dropX, char *dropY)
{
	char    containerPath[300], dbg[512];
	FILE   *f;
	long    size;
	char   *text, *cursor, *ap;
	NodeObj deferred, wires;
	NodeObj result = NULL;

	DebugPrint("IMPORT-VIEW enter", __FILE__, __LINE__, IMPORT);

	if (!container || !path || !path[0]
		|| !PathOfInstance(container, containerPath, sizeof(containerPath)))
	{
		DebugPrint("IMPORT-VIEW: bad args or unpathable container, bail", __FILE__, __LINE__, IMPORT);
		return NULL;
	}

	f = fopen(path, "r");
	if (!f)
	{
		DebugPrint("IMPORT-VIEW: fopen failed, bail", __FILE__, __LINE__, IMPORT);
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);
	text = malloc(size + 1);
	if (!text)
	{
		fclose(f);
		DebugPrint("IMPORT-VIEW: malloc failed, bail", __FILE__, __LINE__, IMPORT);
		return NULL;
	}
	fread(text, 1, size, f);
	text[size] = '\0';
	fclose(f);

	snprintf(dbg, sizeof(dbg), "IMPORT-VIEW: container='%s' path='%s' size=%ld - parsing", containerPath, path, size);
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);

	cursor = text;
	deferred = NewNode(INTEGER);
	wires = NewNode(INTEGER);

	ap = ImportNode(&cursor, containerPath, deferred, wires, 1, 0);
	snprintf(dbg, sizeof(dbg), "IMPORT-VIEW: ImportNode returned ap=%s", ap ? (ap[0] ? ap : "\"\"") : "(null)");
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
	if (ap && ap[0])
	{
		/* the dropped-in view is born where it was dropped, not at its
		   saved canvas spot - reposition the top node after creation
		   (only the top; a child's X/Y from the file stay relative to
		   its own view, untouched) */
		if (dropX && dropX[0])
		{
			DebugPrint("IMPORT-VIEW: repositioning top to drop point", __FILE__, __LINE__, IMPORT);
			PlaceInstance(ResolvePath(ap), containerPath, dropX, (dropY && dropY[0]) ? dropY : "0");
		}
		DebugPrint("IMPORT-VIEW: running aliases pass", __FILE__, __LINE__, IMPORT);
		ImportAliasesPass(ap, deferred);
		DebugPrint("IMPORT-VIEW: running wires pass", __FILE__, __LINE__, IMPORT);
		ImportWiresPass(ap, wires);
		result = ResolvePath(ap);
	}
	if (ap)
		free(ap);

	DelNode(wires);
	DelNode(deferred);
	free(text);
	snprintf(dbg, sizeof(dbg), "IMPORT-VIEW done: result=%p", (void *) result);
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
	return result;
}

typedef struct
{
	NodeObj container;
	char    containerPath[300];
	char   *text;
	void  (*onDone)(NodeObj container, int ok, void *ctx);
	void   *ctx;
} LoadViewCtx;


static void LoadView_AfterDestroy(NodeObj container, void *rawCtx)
{
	LoadViewCtx *lv = (LoadViewCtx *) rawCtx;
	char        *cursor, *ap, dbg[512];
	NodeObj      deferred, wires;

	DebugPrint("LOAD-VIEW: destroy done, parsing file into container", __FILE__, __LINE__, IMPORT);

	cursor = lv->text;
	deferred = NewNode(INTEGER);
	wires = NewNode(INTEGER);

	ap = ImportNode(&cursor, lv->containerPath, deferred, wires, 1, 1);	/* skipSelf=1 */
	snprintf(dbg, sizeof(dbg), "LOAD-VIEW: ImportNode(skipSelf) returned ap=%s", ap ? (ap[0] ? ap : "\"\"") : "(null)");
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
	if (ap)
		free(ap);
	DebugPrint("LOAD-VIEW: running aliases pass", __FILE__, __LINE__, IMPORT);
	ImportAliasesPass(lv->containerPath, deferred);
	DebugPrint("LOAD-VIEW: running wires pass", __FILE__, __LINE__, IMPORT);
	ImportWiresPass(lv->containerPath, wires);

	DelNode(wires);
	DelNode(deferred);
	free(lv->text);
	DebugPrint("LOAD-VIEW done OK", __FILE__, __LINE__, IMPORT);

	{
		void (*onDone)(NodeObj, int, void *) = lv->onDone;
		void  *ctx = lv->ctx;

		free(lv);
		if (onDone)
			onDone(container, 1, ctx);
	}
}

/*
 * {"cmd":"load-flow"} - restore `container` IN PLACE from a whole-session
 * export (ExportView(root, path)): container's current contents are
 * destroyed, then the file's own top-level node (container itself, as
 * ExportView wrote it) is NOT re-created - its children go straight into
 * container, verbatim, since container was just cleared and nothing can
 * collide. This is the "you destroy root and load root in its place"
 * verb - container is not "imported into", it IS what gets restored.
 *
 * Asynchronous (see DestroyContentsAsync's doc comment for why): the
 * destroy runs staggered through the scheduler first, and onDone(container,
 * ok, ctx) fires once the whole restore - destroy AND rebuild - is
 * actually complete. Rebuild itself stays one synchronous recursive-
 * descent pass (LoadView_AfterDestroy) - it's the destroy loop's
 * hundreds of individual DeleteInstance/SndMsg calls that overran the
 * scheduler in one native call, not a single parse of one file.
 */
void LoadViewAsync(NodeObj container, char *path,
					void (*onDone)(NodeObj container, int ok, void *ctx), void *ctx)
{
	char    containerPath[300], dbg[512];
	FILE   *f;
	long    size;
	char   *text;
	LoadViewCtx *lv;

	DebugPrint("LOAD-VIEW-ASYNC enter", __FILE__, __LINE__, IMPORT);

	if (!container || !path || !path[0]
		|| !PathOfInstance(container, containerPath, sizeof(containerPath)))
	{
		DebugPrint("LOAD-VIEW-ASYNC: bad args or unpathable container, bail", __FILE__, __LINE__, IMPORT);
		if (onDone)
			onDone(container, 0, ctx);
		return;
	}

	f = fopen(path, "r");
	if (!f)
	{
		DebugPrint("LOAD-VIEW-ASYNC: fopen failed, bail", __FILE__, __LINE__, IMPORT);
		if (onDone)
			onDone(container, 0, ctx);
		return;
	}
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);
	text = malloc(size + 1);
	if (!text)
	{
		fclose(f);
		DebugPrint("LOAD-VIEW-ASYNC: malloc failed, bail", __FILE__, __LINE__, IMPORT);
		if (onDone)
			onDone(container, 0, ctx);
		return;
	}
	fread(text, 1, size, f);
	text[size] = '\0';
	fclose(f);

	snprintf(dbg, sizeof(dbg), "LOAD-VIEW-ASYNC: container='%s' path='%s' size=%ld - staggering destroy", containerPath, path, size);
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);

	lv = malloc(sizeof(LoadViewCtx));
	lv->container = container;
	strncpy(lv->containerPath, containerPath, sizeof(lv->containerPath) - 1);
	lv->containerPath[sizeof(lv->containerPath) - 1] = 0;
	lv->text = text;
	lv->onDone = onDone;
	lv->ctx = ctx;

	DestroyContentsAsync(container, LoadView_AfterDestroy, (void *) lv);
}

static WidgetItem SerializerPanel[];

int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint("Serializer handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* ---- the emit buffer ---- */

static void Emit(InstanceData *local, const char *s)
{
	int n = (int)strlen(s);

	if (local->buflen + n + 1 > local->bufcap)
	{
		int   nc = (local->buflen + n + 1) * 2;
		char *nb = realloc(local->buf, nc);
		if (!nb)
			return;
		local->buf = nb;
		local->bufcap = nc;
	}
	memcpy(local->buf + local->buflen, s, n);
	local->buflen += n;
	local->buf[local->buflen] = '\0';
}

/* a quoted, escaped JSON string (JsonEscapeStr includes the quotes) */
static void EmitStr(InstanceData *local, char *s)
{
	char *q = JsonEscapeStr(s ? s : "");
	if (q)
	{
		Emit(local, q);
		free(q);
	}
	else
		Emit(local, "\"\"");
}

/* send whatever is buffered out Out as one chunk */
static void Flush(NodeObj instance, InstanceData *local)
{
	NodeObj chunk;

	if (local->buflen == 0)
		return;
	chunk = NewNode(STRING);
	SetName(chunk, "Data");
	SetValueStr(chunk, local->buf);
	SndMsg(instance, "Out", msg_send, chunk);	/* SndMsg owns + frees chunk */
	local->buflen = 0;
	if (local->buf)
		local->buf[0] = '\0';
}

/* ---- the walk stack ---- */

static void Push(InstanceData *local, NodeObj node)
{
	Frame *f;

	if (local->depth >= local->cap)
	{
		int    nc = local->cap ? local->cap * 2 : 32;
		Frame *nf = realloc(local->stack, nc * sizeof(Frame));
		if (!nf)
			return;
		local->stack = nf;
		local->cap = nc;
	}
	f = &local->stack[local->depth++];
	f->node = node;
	f->phase = 0;
	f->child = NULL;
	f->first = 1;
	f->cpath[0] = '\0';
}

/* the next registry instance whose Container is `path`, scanning after `after`
   (NULL = from the start). The registry order is stable across the walk, so
   passing the previously-returned instance back walks the whole set once. */
static NodeObj NextContainerChild(char *path, NodeObj after)
{
	NodeObj lib, cls, inst;
	int seen = (after == NULL);

	for (lib = GetChild(GetRegObjList()); lib; lib = GetNextSibling(lib))
		for (cls = GetChild(lib); cls; cls = GetNextSibling(cls))
			for (inst = GetChild(cls); inst; inst = GetNextSibling(inst))
			{
				char *c;
				if (!seen)
				{
					if (inst == after)
						seen = 1;
					continue;
				}
				c = GetPropStr(inst, "Container");
				if (c && strcmp(c, path) == 0)
					return inst;
			}
	return NULL;
}

/* a value INSIDE the exported view is written relative to it (the leading root
   path stripped), so the export is parentable anywhere; a value OUTSIDE it (an
   external link) keeps its absolute path. Non-path values never match the root,
   so they pass through untouched. Only wires and alias targets get
   re-absolutized on import; an ordinary data property that happens to hold a
   path, like MCPSource's own ConnectorPath, does not. */
static char *RelTo(InstanceData *local, char *val)
{
	int n;

	if (!local->root[0] || !val)
		return val;
	n = (int) strlen(local->root);
	if (strcmp(val, local->root) == 0)
		return "";							/* the root itself */
	if (strncmp(val, local->root, n) == 0 && val[n] == '/')
		return val + n + 1;					/* internal - drop the root prefix */
	return val;								/* external / not a path - as-is */
}

/* a runtime pointer property is LONG-typed (local/Activate/OnMsg/task handle) -
   per-process, never portable, so the walk drops it */
static int IsPointer(NodeObj n)
{
	return GetDataType(GetValueNode(n)) == LONG;
}

/* an OLDER shadow of a prop already emitted this node? SetProp prepends, so
   GetNextProp returns the NEWEST first; a later same-named node is a stale
   shadow and must not emit a duplicate JSON key. */
static int Shadowed(NodeObj node, NodeObj p)
{
	NodeObj q;
	char   *name = GetNameStr(p);

	for (q = GetNextProp(node); q && q != p; q = GetNextSibling(q))
		if (strcmp(GetNameStr(q), name) == 0)
			return 1;
	return 0;
}

/* one step of the walk on the top frame. Returns 0 when the whole walk is done.
   NB: Push may realloc the stack, invalidating the frame pointer - so nothing
   touches `f` after a Push. */
static int Step(InstanceData *local)
{
	Frame *f;

	if (local->depth == 0)
		return 0;
	f = &local->stack[local->depth - 1];

	if (f->phase == 0)			/* the node itself: class, name, flat props */
	{
		NodeObj cls = GetParent(f->node);
		NodeObj p;
		int     firstProp = 1;

		Emit(local, "{\"class\":");
		EmitStr(local, cls ? GetNameStr(cls) : "");
		Emit(local, ",\"name\":");
		EmitStr(local, GetNameStr(f->node));
		Emit(local, ",\"props\":{");
		for (p = GetNextProp(f->node); p; p = GetNextSibling(p))
		{
			if (IsPointer(p) || Shadowed(f->node, p))
				continue;
			if (!firstProp)
				Emit(local, ",");
			firstProp = 0;
			EmitStr(local, GetNameStr(p));
			Emit(local, ":");
			EmitStr(local, RelTo(local, GetValueStr(p)));	/* internal paths -> relative */
		}

		/* outgoing wires: Connect() records a "Subscriber" sub-node on the   */
		/* SOURCE port (this node's ports) naming the sink instance + port -   */
		/* emit each as {from: our port, to: sink path, port: sink port}. The  */
		/* sink is a live pointer; resolve it to a path a load can remap.      */
		Emit(local, "},\"wires\":[");
		{
			int firstWire = 1;
			for (p = GetNextProp(f->node); p; p = GetNextSibling(p))
			{
				NodeObj s;
				if (IsPointer(p) || Shadowed(f->node, p))
					continue;
				for (s = GetNextProp(p); s; s = GetNextSibling(s))
				{
					NodeObj sinkInst;
					char    sinkPath[256];
					char   *sinkPort;

					if (strcmp(GetNameStr(s), "Subscriber") != 0)
						continue;
					sinkInst = (NodeObj) GetPropLong(s, "Instance");
					if (!sinkInst || !PathOfInstance(sinkInst, sinkPath, sizeof(sinkPath)))
						continue;
					sinkPort = GetPropStr(s, "Port");

					if (!firstWire)
						Emit(local, ",");
					firstWire = 0;
					Emit(local, "{\"from\":");
					EmitStr(local, GetNameStr(p));
					Emit(local, ",\"to\":");
					EmitStr(local, RelTo(local, sinkPath));	/* internal sink -> relative */
					Emit(local, ",\"port\":");
					EmitStr(local, sinkPort ? sinkPort : "");
					Emit(local, "}");
				}
			}
		}
		Emit(local, "],\"children\":[");

		f->phase = 1;
		f->first = 1;
		if (!PathOfInstance(f->node, f->cpath, sizeof(f->cpath)))
			f->cpath[0] = '\0';
		f->child = f->cpath[0] ? NextContainerChild(f->cpath, NULL) : NULL;
		return 1;
	}

	/* phase 1: the container-children (instances whose Container is our path) */
	if (!f->child)
	{
		Emit(local, "]}");
		local->depth--;			/* this subtree is done - pop */
		return 1;
	}
	if (!f->first)
		Emit(local, ",");
	f->first = 0;
	{
		NodeObj kid = f->child;
		f->child = NextContainerChild(f->cpath, kid);	/* read cpath BEFORE Push */
		Push(local, kid);							/* f is now stale */
	}
	return 1;
}

/* the walk task: a batch of steps per tick, flushing chunks as the buffer
   fills, re-arming until the stack drains - then a final flush + EOF */
static int Serializer_Task(NodeObj instance, NodeObj data, int reason)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	int i;

	(void) data;

	if (reason == task_deactivate)
		return rtrn_handled;
	if (!local || !local->active)
		return rtrn_dropped;

	for (i = 0; i < STEPS_PER_TICK; i++)
	{
		if (!Step(local))
		{
			Flush(instance, local);
			SndMsg(instance, "Out", msg_eof, NULL);	/* the state stream is done */
			local->active = 0;
			SetPropStr(instance, "State", "1");
			return rtrn_handled;					/* no re-arm - quiesce */
		}
		if (local->buflen >= CHUNK_FLUSH)
			Flush(instance, local);
	}

	AddTaskNow(local->task, (FuncPtr)Serializer_Task, msg_send, instance);
	return rtrn_handled;
}

/* ---- handlers ---- */

int Serializer_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;
	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");
	return rtrn_handled;
}

/* Activate = "walk now". Build the panel on placement (msg_initialize via the
   quiet deferred build never reaches here); a real activation (a flow, the
   bridge) starts the walk from Root. */
int Serializer_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char   *rootpath;
	NodeObj root;

	(void) data;

	if (!local)
		return rtrn_dropped;

	Widget_BuildOnce(instance, SerializerPanel);

	if (local->active || !local->enabled)
		return rtrn_handled;

	rootpath = GetPropStr(instance, "Root");
	root = (rootpath && rootpath[0]) ? ResolvePath(rootpath) : NULL;
	if (!root)
	{
		DebugPrint("Serializer: Root does not resolve to a node", __FILE__, __LINE__, ERROR);
		return rtrn_dropped;
	}
	snprintf(local->root, sizeof(local->root), "%s", rootpath);

	/* start a fresh walk */
	local->depth = 0;
	local->buflen = 0;
	if (local->buf)
		local->buf[0] = '\0';
	Push(local, root);
	local->active = 1;
	SetPropStr(instance, "State", "2");

	if (!local->task)
		local->task = CreateTask(ObjGetTaskList());
	AddTaskNow(local->task, (FuncPtr)Serializer_Task, msg_send, instance);
	return rtrn_handled;
}

/* ---- lifecycle ---- */

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData *local = malloc(sizeof(InstanceData));

	(void) message; (void) data;

	memset(local, 0, sizeof(*local));
	local->enabled = 1;

	instance = NewNode(INTEGER);
	SetName(instance, "Serializer");

	/* every control's value + handler from the table (Enable carries a handler;
	   Root/State are plain data, Out is the emit port shown on the panel) */
	Widget_Init(instance, SerializerPanel);

	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Serializer_Activate);

	InitPosition(instance);
	Widget_MainSize(instance, SerializerPanel);
	RegisterInstance(class, instance);
	Widget_DeferBuildQuiet(instance, SerializerPanel);	/* panel now; do not walk */

	return rtrn_handled;
}

static WidgetItem SerializerPanel[] = {
	/* cls        prop         def      panel   x    y    w    h  label       [handler] */
	{ "View",     "Serializer","",      0,   0,   0, 320, 220, 0 },			/* 0: main */
	{ "Help",     "objects/serializer/README.md", "", 0, 0, 0, 0, 0, 0 },	/* 1: help */

	{ "Checkbox", "Enable",    "1",      0, 290,  12,   9,  9, LABEL_LEFT, (void *)Serializer_OnEnable },
	{ "Textbox",  "Root",      "/Root",  0,  15,  40, 285, 22, LABEL_NONE },
	{ "LED",      "State",     "1",      0,  15,  78,  12, 12, LABEL_NONE },
	{ "TextOut",  "Out",       "",       0,  15, 118, 285, 20, LABEL_LEFT },

	{ NULL }
};

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) message; (void) data;

	Widget_CancelBuild(instance);
	if (local)
	{
		if (local->task)
			DeleteTask(local->task);
		free(local->stack);
		free(local->buf);
		free(local);
	}
	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "Serializer");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	/* the names serializer.h looks up - the format's two directions */
	SetPropLong(ClassSelf, "Export",    (long)ExportView);
	SetPropLong(ClassSelf, "Import",    (long)ImportView);
	SetPropLong(ClassSelf, "LoadAsync", (long)LoadViewAsync);

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Widget");

	PublishPosition(ClassSelf);

	/* every control, from the table (Out shown as a readout of the last chunk) */
	Widget_Publish(ClassSelf, SerializerPanel);

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

	SetName(temp, "Serializer");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "bf8aafc1-c4a5-4655-85a7-972891e110e3");
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)0);
	SetPropInt(temp, "State", 1);

	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");
	AddDependency(temp, "widget.object", "Widget", "1", "0");
	AddDependency(temp, "checkbox.object", "Checkbox", "1", "0");
	AddDependency(temp, "led.object", "LED", "1", "0");
	AddDependency(temp, "textbox.object", "Textbox", "1", "0");
	AddDependency(temp, "textout.object", "TextOut", "1", "0");
	AddDependency(temp, "view.object", "View", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
