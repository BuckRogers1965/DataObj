
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"

/*

Router object: lets HTTP and WebSocket share one TCP port, for a
network sitting between a firewall and this framework that only opens
one hole. Real web servers do exactly this - a WebSocket connection
starts as an ordinary HTTP GET with an Upgrade header, so telling the
two apart only takes looking at the first message of a connection.

Wire it as:

    Connect(Tcp, "Out", Router, "Wire")
    SetPropLong(Router, "HttpTarget", (long)Http)
    SetPropLong(Router, "WsTarget", (long)WebSocket)
    Connect(Http, "Out", Tcp, "In")
    Connect(WebSocket, "Send", Tcp, "In")

Router never touches Http's or WebSocket's own output wiring - both
still Connect() their responses straight back to the same TCP.In (two
independent sources feeding one sink port, which Connect()/SndMsg
already support fine - each gets its own Subscriber entry). Router only
owns the incoming decision: the first message on Wire for a connection
is inspected for "Sec-WebSocket-Key:" - the same header WebSocket's own
handshake already keys off - and that decision sticks (via DeliverMsg,
straight to the target's own port handler, no Subscriber list needed)
for every following message on that SAME connection until its own
msg_eof, which resets just that one connection's entry so the next peer
gets its own fresh look - TCP now services any number of connections at
once (see tcp.c), each message tagged with a Conn id, so the sniffed
mode has to be kept per Conn rather than one guess for the whole Router.
msg_eof is forwarded to both targets unconditionally, with the same Conn
tag it arrived with, since each already resets its own per-connection
state (WebSocket's handshake flag, a Bridge's login) keyed the same way.

Http and WebSocket are otherwise completely unaware this exists.

*/

enum { mode_undecided = 0, mode_http, mode_ws };

typedef struct InstanceData
{
	int active;
	int enabled;
	NodeObj connModes;	/* Conn id -> mode_http/mode_ws, see GetConnState/SetConnState */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static WidgetItem RouterPanel[];

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint ( "Router handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* subscription callback: sniff the first message, stick with it */
int Router_OnWire(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	NodeObj httpTarget, wsTarget;
	char *str;
	long connId;
	int mode;

	if (!local || !local->active || !local->enabled)
		return rtrn_dropped;

	httpTarget = (NodeObj) GetPropLong(instance, "HttpTarget");
	wsTarget   = (NodeObj) GetPropLong(instance, "WsTarget");
	connId     = GetPropLong(data, "Conn");

	if (message == msg_eof)
	{
		/* Conn 0 means every connection is gone (the server shut down), */
		/* not just one - either way each target resets on the same tag  */
		SetConnState(local->connModes, connId, mode_undecided);
		DeliverMsg(httpTarget, "In", msg_eof, data);
		DeliverMsg(wsTarget, "Wire", msg_eof, data);
		return rtrn_handled;
	}

	if (message != msg_send)
		return rtrn_dropped;

	mode = (int) GetConnState(local->connModes, connId);
	if (mode == mode_undecided)
	{
		str = GetValueStr(data);
		mode = (str && strstr(str, "Sec-WebSocket-Key:")) ? mode_ws : mode_http;
		SetConnState(local->connModes, connId, mode);
	}

	if (mode == mode_ws)
		DeliverMsg(wsTarget, "Wire", message, data);
	else
		DeliverMsg(httpTarget, "In", message, data);

	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int Router_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	return rtrn_handled;
}

/* no socket of its own, nothing to schedule - Activate just goes live */
int Router_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	Widget_BuildOnce(instance, RouterPanel);

	if (local->active)
		return rtrn_handled;

	local->active = 1;
	SetPropInt(instance, "State", Running);

	return rtrn_handled;
}

/* The whole panel in one table: main view, Help, and every control. Wire is the
   input port (raw bytes from TCP's Out), shown as a readout. */
static WidgetItem RouterPanel[] = {
	/* cls        prop     def  panel   x    y    w    h  label       [handler] */
	{ "View",     "Router","",  0,   0,   0, 300, 210, 0 },			/* 0: main */
	{ "Help",     "objects/router/README.md", "", 0, 0, 0, 0, 0, 0 },	/* 1: help */

	{ "Checkbox", "Enable","1", 0, 270,  12,   9,  9, LABEL_LEFT, (void *)Router_OnEnable },
	{ "LED",      "State", "1", 0,  15,  40,  12, 12, LABEL_NONE },
	{ "TextOut",  "Wire",  "",  0,  15,  80, 260, 20, LABEL_LEFT, (void *)Router_OnWire },

	{ NULL }
};

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData *local = malloc(sizeof(InstanceData));

	(void) message; (void) data;

	local->active = 0;
	local->enabled = 1;
	local->connModes = NewNode(INTEGER);

	instance = NewNode(INTEGER);
	SetName(instance, "Router");

	/* every control's value + handler from the table (Enable/Wire carry a
	   handler; State is plain data - Wire is the input, read on the panel) */
	Widget_Init(instance, RouterPanel);

	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Router_Activate);

	/* raw pointers to the two targets, set externally after creation - */
	/* the same cross-reference convention Bridge's "Main" already uses */
	SetPropLong(instance, "HttpTarget", (long) NULL);
	SetPropLong(instance, "WsTarget", (long) NULL);

	InitPosition(instance);
	Widget_MainSize(instance, RouterPanel);
	RegisterInstance(class, instance);
	Widget_DeferBuild(instance, RouterPanel);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	Widget_CancelBuild(instance);
	if (local)
	{
		DelNode(local->connModes);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "Router");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	/* every control, from the table (Wire among them, shown as a readout) */
	Widget_Publish(ClassSelf, RouterPanel);

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

	SetName(temp, "Router");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "8da17004-242c-4f21-a77e-6a823a52c710");
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
