
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"

/*

HTTP object: sits on top of a TCP object, never touches a socket itself.

Wire it as: Connect(Tcp, "Out", Http, "In"); Connect(Http, "Out", Tcp, "In");
- the same "In subscribes to a source, Out replies to whoever's listening"
shape as Filter, just with HTTP request/response in the middle instead of
a pass/drop test. TCP stays completely unaware this is HTTP: it is still
just bytes in, bytes out. This is the "HTTP object on top of the TCP
object" the roadmap asks for, done as composition through Connect(),
never a source-level dependency between the two modules.

Root is the directory static files are served from (default the process's
cwd, same as where .object files are scanned from). GET only, one request
per message: the request line ("GET /path HTTP/1.1") is parsed out of
whatever string arrived on In, which is only correct if the whole request
line landed in a single TCP recv - true for the small GET requests a
browser sends for static files, not a general HTTP parser. Payloads are
still null terminated strings like the rest of the flow (see TCPObject's
"Payloads are null terminated strings for now"), so this serves text
files (html/js/css/json/svg) correctly; a binary payload needs the
Length-beside-the-data mechanism the roadmap has not built yet.

".." is rejected outright rather than resolved, so there's no path this
serves outside Root even accidentally.

A disabled Http still receives from TCP but drops every request.

TCP now services any number of simultaneous connections (see tcp.c),
each request tagged with a Conn id - every response here carries the
same Conn tag its request arrived with, so it goes back to the browser
that asked and never to some other peer that happens to be open at the
same time (unlike Bridge's event traffic, an HTML page is never a
broadcast).

*/

typedef struct InstanceData
{
	int active;
	int enabled;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint ( "Http handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

char *Http_ContentType(char *path)
{
	char *ext = strrchr(path, '.');

	if (!ext)
		return "text/plain";
	if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0)
		return "text/html";
	if (strcmp(ext, ".js") == 0)
		return "application/javascript";
	if (strcmp(ext, ".css") == 0)
		return "text/css";
	if (strcmp(ext, ".json") == 0)
		return "application/json";
	if (strcmp(ext, ".svg") == 0)
		return "image/svg+xml";
	return "text/plain";
}

/* send a complete response (status line, headers, body) as one message - */
/* Content-Length has to be known up front, so the body is read in full   */
/* before anything is sent. Tagged with connId so it reaches only the     */
/* browser that made this particular request.                             */
void Http_SendResponse(NodeObj instance, char *status, char *contentType, char *body, long bodyLen, long connId)
{
	char header[256];
	char *response;
	long headerLen;
	NodeObj chunk;

	snprintf(header, sizeof(header),
			 "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n",
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

void Http_SendError(NodeObj instance, char *status, char *message, long connId)
{
	Http_SendResponse(instance, status, "text/plain", message, strlen(message), connId);
}

/* GET /path HTTP/1.x -> Root/path, "/" -> Root/index.html */
void Http_Serve(NodeObj instance, char *path, long connId)
{
	char *root, *query;
	char fullpath[1024];
	FILE *f;
	long size;
	char *body;

	if (strstr(path, ".."))
	{
		Http_SendError(instance, "403 Forbidden", "Forbidden", connId);
		return;
	}

	query = strchr(path, '?');
	if (query)
		*query = 0;

	if (strcmp(path, "/") == 0)
		path = "/index.html";

	root = GetPropStr(instance, "Root");
	if (!root || !root[0])
		root = ".";

	snprintf(fullpath, sizeof(fullpath), "%s%s", root, path);

	f = fopen(fullpath, "r");
	if (!f)
	{
		Http_SendError(instance, "404 Not Found", "Not Found", connId);
		return;
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);

	body = malloc(size + 1);
	if (!body)
	{
		fclose(f);
		Http_SendError(instance, "500 Internal Server Error", "Internal Server Error", connId);
		return;
	}
	fread(body, 1, size, f);
	body[size] = 0;
	fclose(f);

	Http_SendResponse(instance, "200 OK", Http_ContentType(path), body, size, connId);
	free(body);
}

/* subscription callback: parse the request line, respond */
int Http_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	char *request, method[16], path[900];
	long connId;
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || !local->active || !local->enabled)
		return rtrn_dropped;

	if (message != msg_send)
		return rtrn_dropped;

	request = GetValueStr(data);
	if (!request)
		return rtrn_dropped;

	connId = GetPropLong(data, "Conn");

	if (sscanf(request, "%15s %899s", method, path) != 2)
	{
		Http_SendError(instance, "400 Bad Request", "Bad Request", connId);
		return rtrn_handled;
	}

	if (strcmp(method, "GET") != 0)
	{
		Http_SendError(instance, "405 Method Not Allowed", "Method Not Allowed", connId);
		return rtrn_handled;
	}

	Http_Serve(instance, path, connId);

	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int Http_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	return rtrn_handled;
}

/* no socket of its own, nothing to schedule - Activate just goes live */
int Http_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || local->active)
		return rtrn_dropped;

	local->active = 1;
	SetPropInt(instance, "State", Running);

	return rtrn_handled;
}

/* the settings panel: what Http looks like, built once per instance */
static ControlSpec HttpControls[] = {
	{ "Textbox", "Root",  10, 10, 100, 20 },
	{ "LED",     "State", 10, 40,  20, 20 },
	{ "Button",  NULL,    10, 70,  60, 20 },
};

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance, port;
	InstanceData *local = malloc(sizeof(InstanceData));

	local->active = 0;
	local->enabled = 1;

	instance = NewNode(INTEGER);
	SetName(instance, "Http");
	SetPropStr(instance, "Root", ".");
	WatchableProp(instance, "Root");
	SetPropInt(instance, "State", Starting);
	WatchableProp(instance, "State");
	SetPropInt(instance, "Out", 0);		/* responses leave here, back to TCP's In */
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Http_Activate);

	/* input port: requests arrive here, from TCP's Out */
	SetPropInt(instance, "In", 0);
	port = GetPropNode(instance, "In");
	SetPropLong(port, "OnMsg", (long)Http_OnIn);

	/* enable port, the LED: 1 enables, 0 disables, any source can drive it */
	SetPropStr(instance, "Enable", "1");
	port = GetPropNode(instance, "Enable");
	SetPropLong(port, "OnMsg", (long)Http_OnEnable);

	InitPosition(instance);

	RegisterInstance(class, instance);

	BuildSettingsView(instance, HttpControls, sizeof(HttpControls) / sizeof(HttpControls[0]));

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (local)
		free(local);

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "Http");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	PublishProp(ClassSelf, "Root",   "data", PROP_TEXTBOX, ".");
	PublishProp(ClassSelf, "Enable", "in",   PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "In",     "in",   PROP_NULL, "");
	PublishProp(ClassSelf, "Out",    "out",  PROP_NULL, "");
	PublishProp(ClassSelf, "State",  "data", PROP_LED, "1");

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

	SetName(temp, "Http");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "8da17004-242c-4f21-a77e-6a823a52c680");
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
