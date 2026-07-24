
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"

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

	char   *root;		/* the export root path - internal links are relative to it */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

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
   so they pass through untouched. */
static char *RelTo(InstanceData *local, char *val)
{
	int n;

	if (!local->root || !val)
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
	local->root = rootpath;		/* internal links are written relative to this */

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
	SetPropStr(temp, "Version", "1.0");
	SetPropStr(temp, "Dependencies", "");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)0);
	SetPropInt(temp, "State", 1);

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
