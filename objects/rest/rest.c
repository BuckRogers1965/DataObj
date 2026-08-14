#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"

/*

Rest object: a translator, sibling of Bridge rather than a layer on it.

Wire it the way Http is wired - it never touches a socket:

    Connect(Tcp, "Out", Rest, "In");
    Connect(Rest, "Out", Tcp, "In");

Bridge maintains a live model of the tree inside a browser; this answers
one request at a time and remembers nothing between them. The verbs it
needs are already public in object.h - ResolvePath, PathOfInstance,
GetClassInterface - so nothing here reaches into bridge.c.

What it publishes is a VIEW, not the whole tree: ManifestView (default
/Root/mcp) names a container, and its members are the manifest.
Containment is the publication - drag an object in and it is published,
drag it out and it is not. There is no exported flag and no category.

Each member is described by its class's published Interface (PublishProp,
filled by Widget_Publish from the object's own panel table) plus a face
saying how to drive it: which property takes the input, which button to
press if there is one, which property carries the result, and how to know
the result is finished. The default face - In, no trigger, Out, done at
msg_eof - covers everything on the plain dataflow shape, and an object
that declares ReservedIn/ReservedOut names its own stand-ins instead.

The published view is the ROOT of the URL space. A caller addresses a
member by its own name and never learns where the view lives:

    GET  /                 the list - name and class, one line each
    GET  /manifest         the same members, fully described
    GET  /Textbox          the member's value, through its face
    PUT  /Button           write the member's face input - the press
    GET  /Textbox/Value    one named property
    PUT  /Textbox/Value    write one named property

Moving ManifestView moves every one of those with it, because none of
them names it. Underneath, the member name is joined to the view's path
and handed to ResolvePath, which indexes addressable instances - so the
longest prefix it recognises is the instance and whatever is left is
walked as properties, and an instance nested in a sub-view needs no
different grammar than a property does.

A GET never conjures: an unresolved path is a 404 and creates nothing. A
PUT may create, because a property existing only because something
referred to it is the ordinary late-binding case, so the reply says
whether it did - a script can tell a real write from a typo without the
translator forbidding the legitimate use.

Payloads are null terminated strings, same as the rest of the flow, and a
request is assumed to have arrived in one recv - true for the small
requests this answers today. A PUT whose body is shorter than its own
Content-Length says so rather than writing a truncated value.

*/

#define MANIFEST_DEFAULT "/Root/mcp"

typedef struct InstanceData
{
	int active;
	int enabled;
	long requests;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static WidgetItem RestPanel[];

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint ( "Rest handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* ---- a growing text buffer, because a manifest has no known length ---- */

typedef struct Sb
{
	char *p;
	long  len;
	long  cap;
} Sb;

static void SbCat(Sb *sb, char *s)
{
	long n;

	if (!sb || !s)
		return;

	n = strlen(s);
	if (sb->len + n + 1 > sb->cap)
	{
		long want = (sb->cap ? sb->cap : 1024);
		char *bigger;

		while (want < sb->len + n + 1)
			want *= 2;
		bigger = realloc(sb->p, want);
		if (!bigger)
			return;
		sb->p = bigger;
		sb->cap = want;
	}

	memcpy(sb->p + sb->len, s, n);
	sb->len += n;
	sb->p[sb->len] = 0;
}

/* JsonEscapeStr returns the quotes as well as the escaping */
static void SbCatJson(Sb *sb, char *s)
{
	char *esc = JsonEscapeStr(s ? s : "");

	if (!esc)
		return;
	SbCat(sb, esc);
	free(esc);
}

static void SbCatPair(Sb *sb, char *name, char *value)
{
	SbCatJson(sb, name);
	SbCat(sb, ":");
	SbCatJson(sb, value ? value : "");
}

static void SbFree(Sb *sb)
{
	if (sb && sb->p)
		free(sb->p);
	if (sb)
	{
		sb->p = NULL;
		sb->len = sb->cap = 0;
	}
}

/* ---- responses ---- */

/* one message carries a complete response. Content-Length has to be known
   up front, which is why a manifest is built in full before any of it is
   sent - the same reason Http reads a whole file first. Tagged with the
   Conn its request arrived on so it reaches only that caller. */
static void Rest_SendResponse(NodeObj instance, char *status, char *contentType,
							  char *body, long bodyLen, long connId)
{
	char header[320];
	char *response;
	long headerLen;
	NodeObj chunk;

	snprintf(header, sizeof(header),
			 "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %ld\r\n"
			 "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
			 status, contentType, bodyLen);
	headerLen = strlen(header);

	response = malloc(headerLen + bodyLen + 1);
	if (!response)
		return;

	memcpy(response, header, headerLen);
	if (bodyLen)
		memcpy(response + headerLen, body, bodyLen);
	response[headerLen + bodyLen] = 0;

	chunk = NewNode(STRING);
	SetName(chunk, "Response");
	SetValueStr(chunk, response);
	SetPropLong(chunk, "Conn", connId);
	SndMsg(instance, "Out", msg_send, chunk);

	free(response);
}

/* every failure says why, in the body and in the log - a caller with no
   eyes cannot guess, and neither can we after the fact */
static void Rest_SendError(NodeObj instance, char *status, char *why, long connId)
{
	Sb sb;
	char dbg[512];

	memset(&sb, 0, sizeof(sb));
	SbCat(&sb, "{\"error\":");
	SbCatJson(&sb, why ? why : "");
	SbCat(&sb, "}");

	snprintf(dbg, sizeof(dbg), "REST %s: %s", status, why ? why : "");
	DebugPrint(dbg, __FILE__, __LINE__, ERROR);

	Rest_SendResponse(instance, status, "application/json", sb.p ? sb.p : "{}",
					  sb.len, connId);
	SbFree(&sb);
}

/* ---- the face: how a client drives one member ---- */

/* Which property takes the input, resolved against what the instance
   actually has rather than against what class it is: a declared
   ReservedIn wins (a composite naming its own stand-in, the same
   declaration Connect's dot uses), then In if the object is on the plain
   dataflow shape, and otherwise Value - which is all there is to write to
   on a control. No list of class names, so a class nobody has written yet
   resolves the same way. */
static char *Rest_FaceIn(NodeObj inst)
{
	char *p = GetPropStr(inst, "ReservedIn");

	if (p && p[0])
		return p;
	if (GetPropNode(inst, "In"))
		return "In";

	return "Value";
}

static char *Rest_FaceOut(NodeObj inst)
{
	char *p = GetPropStr(inst, "ReservedOut");

	if (p && p[0])
		return p;
	if (GetPropNode(inst, "Out"))
		return "Out";

	return "Value";
}

/* The default face - the input, no trigger, the output, done at msg_eof.
   A show/rest face file overriding it is the next tier and is not read
   yet. */
static void Rest_JsonFace(Sb *sb, NodeObj inst)
{
	char *in  = Rest_FaceIn(inst);
	char *out = Rest_FaceOut(inst);

	SbCat(sb, "\"face\":{");
	SbCatPair(sb, "in", in);
	SbCat(sb, ",");
	SbCatPair(sb, "trigger", "");
	SbCat(sb, ",");
	SbCatPair(sb, "out", out);
	SbCat(sb, ",");
	SbCatPair(sb, "done", "eof");
	SbCat(sb, ",");
	SbCatPair(sb, "source", "default");
	SbCat(sb, "}");
}

/* the class's published Interface, which every widget fills from its own
   panel table - name, the control type that renders it, and the class
   default. This is the shape; values come from GETting the instance. */
static void Rest_JsonInterface(Sb *sb, NodeObj class)
{
	NodeObj interface = GetClassInterface(class);
	NodeObj prop;
	int first = 1;
	char num[32];

	SbCat(sb, "\"properties\":[");

	for (prop = interface ? GetChild(interface) : NULL; prop; prop = GetNextSibling(prop))
	{
		if (!first)
			SbCat(sb, ",");
		first = 0;

		SbCat(sb, "{");
		SbCatPair(sb, "name", GetPropStr(prop, "Name"));
		SbCat(sb, ",");
		snprintf(num, sizeof(num), "%d", GetPropInt(prop, "Widget"));
		SbCatJson(sb, "widget");
		SbCat(sb, ":");
		SbCat(sb, num);
		SbCat(sb, ",");
		SbCatPair(sb, "default", GetPropStr(prop, "Default"));
		SbCat(sb, "}");
	}

	SbCat(sb, "]");
}

/* the widget's own README, already written and already read by its Help
   panel - the description an agent needs, with nothing new to maintain */
static void Rest_JsonHelp(Sb *sb, char *path)
{
	char helppath[600];
	NodeObj help;

	snprintf(helppath, sizeof(helppath), "%s/Help", path);
	help = ResolvePath(helppath);

	SbCatPair(sb, "help", help ? GetPropStr(help, "HelpFile") : "");
}

/* the name a member is ADDRESSED by, which is the Name property - the
   node's own name is the birth name its class gave it, and a renamed
   instance keeps that forever. PathOfInstance builds its path out of
   Name for the same reason, so this is the half of the address that
   varies. */
static char *Rest_MemberName(NodeObj inst)
{
	char *name = GetPropStr(inst, "Name");

	return (name && name[0]) ? name : GetNameStr(inst);
}

/* one line per member: what to call it and what it is */
static void Rest_JsonBrief(Sb *sb, NodeObj inst)
{
	NodeObj class = GetParent(inst);

	SbCat(sb, "{");
	SbCatPair(sb, "name", Rest_MemberName(inst));
	SbCat(sb, ",");
	SbCatPair(sb, "class", class ? GetNameStr(class) : "");
	SbCat(sb, "}");
}

static void Rest_JsonMember(Sb *sb, NodeObj inst, char *path)
{
	NodeObj class = GetParent(inst);

	SbCat(sb, "{");
	SbCatPair(sb, "name", Rest_MemberName(inst));
	SbCat(sb, ",");
	SbCatPair(sb, "path", path);
	SbCat(sb, ",");
	SbCatPair(sb, "class", class ? GetNameStr(class) : "");
	SbCat(sb, ",");
	Rest_JsonHelp(sb, path);
	SbCat(sb, ",");
	Rest_JsonFace(sb, inst);
	SbCat(sb, ",");
	Rest_JsonInterface(sb, class);
	SbCat(sb, "}");
}

/* ---- GET /manifest ---- */

/* Everything in the published view, one entry each. The walk is the
   registry walk every other enumeration uses (library -> class ->
   instance) filtered on Container, because containment is not indexed
   separately yet; an instance with no derivable path was never
   addressable and is not published. */
static void Rest_Manifest(NodeObj instance, int full, long connId)
{
	NodeObj lib, class, inst, view;
	char *manifest;
	char pbuf[600];
	char *cont;
	Sb sb;
	int first = 1;

	manifest = GetPropStr(instance, "ManifestView");
	if (!manifest || !manifest[0])
		manifest = MANIFEST_DEFAULT;

	view = ResolvePath(manifest);
	if (!view)
	{
		char why[400];

		snprintf(why, sizeof(why), "no such view: %s", manifest);
		Rest_SendError(instance, "404 Not Found", why, connId);
		return;
	}

	memset(&sb, 0, sizeof(sb));
	SbCat(&sb, "{\"view\":");
	SbCatJson(&sb, manifest);
	SbCat(&sb, ",\"members\":[");

	for (lib = GetChild(GetRegObjList()); lib; lib = GetNextSibling(lib))
	 for (class = GetChild(lib); class; class = GetNextSibling(class))
	  for (inst = GetChild(class); inst; inst = GetNextSibling(inst))
	  {
		if (!PathOfInstance(inst, pbuf, sizeof(pbuf)))
			continue;

		cont = GetPropStr(inst, "Container");
		if (!cont || strcmp(cont, manifest) != 0)
			continue;

		if (!first)
			SbCat(&sb, ",");
		first = 0;

		if (full)
			Rest_JsonMember(&sb, inst, pbuf);
		else
			Rest_JsonBrief(&sb, inst);
	  }

	SbCat(&sb, "]}");

	Rest_SendResponse(instance, "200 OK", "application/json",
					  sb.p ? sb.p : "{}", sb.len, connId);
	SbFree(&sb);
}

/* ---- addressing: one grammar for instances and properties ---- */

/* /tree/Root/mcp/Box/Value -> the instance /Root/mcp/Box, property "Value".
   ResolvePath indexes addressable instances, so the longest prefix it
   recognises is the instance; the segments left over are property names,
   and they can nest because a property is a node like anything else. */
static NodeObj Rest_Resolve(char *path, char *propOut, int propLen)
{
	char work[600];
	char tail[300];
	char *slash;
	NodeObj inst;

	if (!path || !path[0] || !propOut)
		return NULL;

	snprintf(work, sizeof(work), "%s", path);
	propOut[0] = 0;

	for (;;)
	{
		inst = ResolvePath(work);
		if (inst)
			return inst;

		slash = strrchr(work, '/');
		if (!slash || slash == work)
			return NULL;

		if (propOut[0])
		{
			snprintf(tail, sizeof(tail), "%s/%s", slash + 1, propOut);
			snprintf(propOut, propLen, "%s", tail);
		}
		else
			snprintf(propOut, propLen, "%s", slash + 1);

		*slash = 0;
	}
}

/* walk a property path down from a node; NULL if any step is missing -
   this is what makes a GET unable to conjure */
static NodeObj Rest_PropNode(NodeObj owner, char *propPath)
{
	char work[300];
	char *seg, *save;
	NodeObj node = owner;

	if (!owner || !propPath || !propPath[0])
		return NULL;

	snprintf(work, sizeof(work), "%s", propPath);

	for (seg = strtok_r(work, "/", &save); seg; seg = strtok_r(NULL, "/", &save))
	{
		node = GetPropNode(node, seg);
		if (!node)
			return NULL;
	}

	return node;
}

/* a write needs the node that OWNS the property and the property's own
   name, because SetOrDeliverProp takes both - properties have no parent
   back-pointer to derive it from */
static NodeObj Rest_PropOwner(NodeObj inst, char *propPath, char *nameOut, int nameLen)
{
	char work[300];
	char *slash;
	NodeObj owner;

	if (!inst || !propPath || !propPath[0])
		return NULL;

	snprintf(work, sizeof(work), "%s", propPath);
	slash = strrchr(work, '/');

	if (!slash)
	{
		snprintf(nameOut, nameLen, "%s", work);
		return inst;
	}

	*slash = 0;
	snprintf(nameOut, nameLen, "%s", slash + 1);

	owner = Rest_PropNode(inst, work);
	return owner;
}

/* ---- reading ---- */

static void Rest_GetProperty(NodeObj instance, NodeObj inst, char *path,
							 char *propPath, long connId)
{
	NodeObj node = Rest_PropNode(inst, propPath);
	Sb sb;
	char why[400];

	if (!node)
	{
		snprintf(why, sizeof(why), "no such property: %s on %s", propPath, path);
		Rest_SendError(instance, "404 Not Found", why, connId);
		return;
	}

	memset(&sb, 0, sizeof(sb));
	SbCat(&sb, "{");
	SbCatPair(&sb, "path", path);
	SbCat(&sb, ",");
	SbCatPair(&sb, "property", propPath);
	SbCat(&sb, ",");
	SbCatPair(&sb, "value", GetValueStr(node));
	SbCat(&sb, "}");

	Rest_SendResponse(instance, "200 OK", "application/json",
					  sb.p ? sb.p : "{}", sb.len, connId);
	SbFree(&sb);
}

/* ---- writing: this is the button press ---- */

/* SetOrDeliverProp is the whole verb. It decides for itself whether the
   name resolves to a port - in which case the port's own handler runs,
   exactly as genuine wired traffic would - or a plain data property, which
   is a direct write. So pressing a button and typing into a textbox are
   the same call here, and a widget that grows a new command grows a new
   endpoint with no change to this object. */
static void Rest_PutProperty(NodeObj instance, NodeObj inst, char *path,
							 char *propPath, char *value, long connId)
{
	char name[128];
	NodeObj owner = Rest_PropOwner(inst, propPath, name, sizeof(name));
	int existed;
	Sb sb;
	char why[400];

	if (!owner || !name[0])
	{
		snprintf(why, sizeof(why), "no such property container: %s on %s", propPath, path);
		Rest_SendError(instance, "404 Not Found", why, connId);
		return;
	}

	existed = GetPropNode(owner, name) ? 1 : 0;

	SetOrDeliverProp(owner, name, value ? value : "");

	memset(&sb, 0, sizeof(sb));
	SbCat(&sb, "{");
	SbCatPair(&sb, "path", path);
	SbCat(&sb, ",");
	SbCatPair(&sb, "property", propPath);
	SbCat(&sb, ",");
	SbCatPair(&sb, "value", value ? value : "");
	SbCat(&sb, ",\"created\":");
	SbCat(&sb, existed ? "false" : "true");
	SbCat(&sb, "}");

	Rest_SendResponse(instance, "200 OK", "application/json",
					  sb.p ? sb.p : "{}", sb.len, connId);
	SbFree(&sb);
}

/* the published view a member is addressed relative to */
static char *Rest_ManifestView(NodeObj instance)
{
	char *manifest = GetPropStr(instance, "ManifestView");

	return (manifest && manifest[0]) ? manifest : MANIFEST_DEFAULT;
}

/* One entry point for every member request, because the grammar does not
   distinguish an instance from a property. The request path is joined to
   the published view and resolved from there - which is what keeps the
   view's own location out of the caller's URLs entirely. */
static void Rest_Member(NodeObj instance, char *method, char *request,
						char *body, long connId)
{
	char full[600];
	char propPath[300];
	NodeObj inst;
	char why[500];

	snprintf(full, sizeof(full), "%s/%s", Rest_ManifestView(instance), request);

	inst = Rest_Resolve(full, propPath, sizeof(propPath));

	if (!inst)
	{
		snprintf(why, sizeof(why), "no such member: %s", request);
		Rest_SendError(instance, "404 Not Found", why, connId);
		return;
	}

	/* a bare member goes through its face: read what it produces, write
	   what it takes. Naming a property addresses that property instead. */
	if (!propPath[0])
		snprintf(propPath, sizeof(propPath), "%s",
				 strcmp(method, "GET") == 0 ? Rest_FaceOut(inst) : Rest_FaceIn(inst));

	if (strcmp(method, "GET") == 0)
		Rest_GetProperty(instance, inst, full, propPath, connId);
	else
		Rest_PutProperty(instance, inst, full, propPath, body, connId);
}

/* ---- the request ---- */

/* the value of a PUT is its body, taken raw - the blank line ends the
   headers, and everything after it is the value. Content-Length is
   checked rather than trusted: a body that arrived short would otherwise
   be written as a truncated value, and silently writing the wrong thing
   is worse than saying the request did not fit. */
static char *Rest_Body(char *request, long *lenOut)
{
	char *blank = strstr(request, "\r\n\r\n");

	*lenOut = 0;

	if (!blank)
	{
		blank = strstr(request, "\n\n");
		if (!blank)
			return NULL;
		blank += 2;
	}
	else
		blank += 4;

	*lenOut = (long)strlen(blank);
	return blank;
}

static long Rest_ContentLength(char *request)
{
	char *h = strcasestr(request, "\ncontent-length:");

	if (!h)
		return -1;

	return strtol(h + 16, NULL, 10);
}

/* subscription callback: one request in, one response out */
int Rest_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *request, method[16], path[500];
	char *query, *body;
	char line[560];
	char why[400];
	long connId, bodyLen, declared;

	if (!local || !local->active || !local->enabled)
		return rtrn_dropped;

	if (message != msg_send)
		return rtrn_dropped;

	request = GetValueStr(data);
	if (!request)
		return rtrn_dropped;

	connId = GetPropLong(data, "Conn");

	if (sscanf(request, "%15s %499s", method, path) != 2)
	{
		Rest_SendError(instance, "400 Bad Request", "unparseable request line", connId);
		return rtrn_handled;
	}

	query = strchr(path, '?');
	if (query)
		*query = 0;

	/* the panel says what it last handled */
	snprintf(line, sizeof(line), "%s %s", method, path);
	SetPropStr(instance, "In", line);

	local->requests++;
	SetPropInt(instance, "Requests", (int)local->requests);

	/* "/" is the list - names and classes, the thing you read to find out
	   what is here. "/manifest" is the full description of the same
	   members, which is what a client reads once to learn how to drive
	   them. Two questions, so two answers, rather than one answer that is
	   too long for one of them. */
	if (strcmp(path, "/") == 0 || strcmp(path, "/manifest") == 0)
	{
		if (strcmp(method, "GET") != 0)
		{
			Rest_SendError(instance, "405 Method Not Allowed", method, connId);
			return rtrn_handled;
		}
		Rest_Manifest(instance, strcmp(path, "/manifest") == 0, connId);
		return rtrn_handled;
	}

	if (strcmp(method, "GET") != 0 && strcmp(method, "PUT") != 0)
	{
		Rest_SendError(instance, "405 Method Not Allowed", method, connId);
		return rtrn_handled;
	}

	body = Rest_Body(request, &bodyLen);

	if (strcmp(method, "PUT") == 0)
	{
		declared = Rest_ContentLength(request);
		if (declared >= 0 && declared != bodyLen)
		{
			snprintf(why, sizeof(why),
					 "body is %ld bytes, Content-Length says %ld", bodyLen, declared);
			Rest_SendError(instance, "400 Bad Request", why, connId);
			return rtrn_handled;
		}
	}

	Rest_Member(instance, method, path + 1, body, connId);

	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int Rest_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	return rtrn_handled;
}

/* no socket of its own, nothing to schedule - Activate just goes live and
   makes the panel agree with the state. It answers no request here. */
int Rest_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	Widget_BuildOnce(instance, RestPanel);

	if (local->active)
		return rtrn_handled;

	local->active = 1;
	SetPropInt(instance, "State", Running);

	return rtrn_handled;
}

/* the panel */
/* ManifestView names the published container. In and Out are readouts of
   the last request and response. */
static WidgetItem RestPanel[] = {
	/* cls        prop            def                x    y    w    h  label      [handler] */
	{ "View",     "Rest",         "",           0,   0,   0, 325, 260, 0 },			/* 0: main */
	{ "Help",     "objects/rest/README.md", "", 0,   0,   0,   0,   0, 0 },			/* 1: help */

	{ "Checkbox", "Enable",       "1",          0, 250,  40,   9,   9, LABEL_LEFT, (void *)Rest_OnEnable },
	{ "Textbox",  "ManifestView", MANIFEST_DEFAULT,
	                                            0,  15,  35, 200,  24, LABEL_TOP },
	{ "LED",      "State",        "1",          0,  15,  72,  12,  12, LABEL_NONE },
	{ "TextOut",  "Requests",     "0",          0,  60,  70, 120,  20, LABEL_LEFT },
	{ "TextOut",  "In",           "",           0,  15, 105, 260,  20, LABEL_LEFT, (void *)Rest_OnIn },
	{ "TextOut",  "Out",          "",           0,  15, 140, 260,  20, LABEL_LEFT },

	{ NULL }
};

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData *local = malloc(sizeof(InstanceData));

	(void) message; (void) data;

	if (!local)
		return rtrn_dropped;

	local->active = 0;
	local->enabled = 1;
	local->requests = 0;

	instance = NewNode(INTEGER);
	SetName(instance, "Rest");

	Widget_Init(instance, RestPanel);

	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Rest_Activate);

	InitPosition(instance);
	Widget_MainSize(instance, RestPanel);
	RegisterInstance(class, instance);
	Widget_DeferBuild(instance, RestPanel);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	Widget_CancelBuild(instance);
	if (local)
		free(local);

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "Rest");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Widget");

	PublishPosition(ClassSelf);

	Widget_Publish(ClassSelf, RestPanel);

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

	SetName(temp, "Rest");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "3f2b18ac-5d47-4e91-b0c6-9a7e21d4f85b");
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
