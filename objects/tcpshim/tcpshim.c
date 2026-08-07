/*****************************************************************/
// Module:  tcpshim.c
//
// Purpose: SCAFFOLDING. It registers the class name "TCP" with the old
//          property-and-wire surface every driver in the tree still speaks -
//          In, Out, Activate, Enable, LocalPort, RemoteAddr, Connected, the
//          Conn tag on messages - and holds a real TCPSocket underneath,
//          translating both ways.
//
//          It exists so the app keeps serving while the drivers (main.c,
//          router, http, websocket, bridge, tcpport, mcpsource, tplink) are
//          converted to tcp.h one at a time. When the last one is converted
//          this file is deleted and TCPSocket takes the name back. Nothing
//          new belongs here: it only translates.
/*****************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "../network/tcp.h"
#include "control.h"	/* PROP_* - what a published property presents as */

/* this shim's own base for the socket's callbacks - its number, not the
   object's, so an owner holding several objects keeps them apart */
#define SHIM_TCP_CALLBACK	0x7000

typedef struct InstanceData
{
	NodeObj socket;			/* the real object underneath */
	int     started;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	(void) instance; (void) message; (void) data;
	return rtrn_handled;
}

/* ---- talking to the object underneath, through tcp.h only ----------- */

static void Shim_SetVar(NodeObj sock, MsgId var, char *value)
{
	NodeObj v;

	if (!sock)
		return;

	v = NewNode(STRING);
	SetValueStr(v, (value && value[0]) ? value : "0");
	DeliverMsg(sock, "Msg", var, v);
	DelNode(v);
}

static void Shim_SetVarInt(NodeObj sock, MsgId var, int value)
{
	NodeObj v;

	if (!sock)
		return;

	v = NewNode(STRING);
	SetValueInt(v, value);
	/* SetValueInt cannot store a 0 (node.c) - say it as text instead */
	if (!value)
		SetValueStr(v, "0");
	DeliverMsg(sock, "Msg", var, v);
	DelNode(v);
}

static int Shim_GetVarInt(NodeObj sock, MsgId var)
{
	NodeObj v;
	int got;

	if (!sock)
		return 0;

	v = NewNode(STRING);
	SetValueStr(v, "");			/* empty: filled in with the current value */
	DeliverMsg(sock, "Msg", var, v);
	got = GetValueInt(v);
	DelNode(v);
	return got;
}

/* ---- the old surface, translated ------------------------------------ */

/* the old In: a message, optionally carrying a Conn, to send to a peer */
int Shim_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	int length;
	char *str;

	if (!local || !local->socket || message == msg_eof)
		return rtrn_dropped;

	/* length BEFORE the pointer: GetValueLen can reallocate the value */
	length = GetValueLen(data);
	str = GetValueStr(data);
	if (!length && str)
		length = (int) strlen(str);
	if (!str || length <= 0)
		return rtrn_handled;

	/* the old Conn tag becomes the object's current connection - 0 still
	   means every open peer */
	Shim_SetVarInt(local->socket, TCP_CURRENT_CONNECTION_VAR,
				   (int) GetPropLong(data, "Conn"));

	TCPSendData(local->socket, length, data);
	return rtrn_handled;
}

/* the socket's callbacks, turned back into the old events */
int Shim_OnCallback(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	NodeObj chunk;
	long conn;
	char *value;
	int length;

	if (!local)
		return rtrn_handled;

	conn = Shim_GetVarInt(local->socket, TCP_CURRENT_CONNECTION_VAR);

	switch (message - SHIM_TCP_CALLBACK)
	{
	case TCP_RECEIVED_DATA_CALLBACK:
		length = GetValueLen(data);
		value = GetValueStr(data);
		if (!length && value)
			length = (int) strlen(value);
		if (!value || length <= 0)
			return rtrn_handled;

		chunk = NewNode(STRING);
		SetName(chunk, "Data");
		SetValueStrLen(chunk, value, length);
		SetPropInt(chunk, "Length", length);
		SetPropLong(chunk, "Conn", conn);
		SndMsg(instance, "Out", msg_send, chunk);
		return rtrn_handled;

	case TCP_NEW_CONNECTION_CALLBACK:
		SetPropStr(instance, "Connected", "1");
		return rtrn_handled;

	case TCP_REMOTE_CONNECTION_CLOSED_CALLBACK:
		chunk = NewNode(STRING);
		SetName(chunk, "Data");
		SetPropLong(chunk, "Conn", conn);
		SndMsg(instance, "Out", msg_eof, chunk);
		if (!Shim_GetVarInt(local->socket, TCP_CONNECTION_COUNT_VAR))
			SetPropStr(instance, "Connected", "0");
		return rtrn_handled;

	case TCP_ERROR_CALLBACK:
		value = GetValueStr(data);
		DebugPrint(value ? value : "TCP error", __FILE__, __LINE__, ERROR);
		return rtrn_handled;

	default:
		return rtrn_dropped;
	}
}

/* the old Enable: 0 is a full shutdown */
int Shim_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message == msg_eof)
		return rtrn_dropped;

	SetValueStr(GetPropNode(instance, "Enable"), GetValueInt(data) ? "1" : "0");

	if (!GetValueInt(data) && local->started)
	{
		TCPStop(local->socket);
		local->started = 0;
		SetPropStr(instance, "Connected", "0");
		SetPropInt(instance, "State", Stopping);
		SndMsg(instance, "Out", msg_eof, NULL);	/* untagged: everyone is gone */
	}

	return rtrn_handled;
}

/* the old Activate: hand the object its setup and start it */
int Shim_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *remote;

	(void) message; (void) data;

	if (!local || !local->socket || local->started)
		return rtrn_dropped;

	if (!GetPropInt(instance, "Enable"))
		return rtrn_dropped;

	/* the old surface inferred client mode from a non-empty RemoteAddr; the
	   object is told outright */
	remote = GetPropStr(instance, "RemoteAddr");
	Shim_SetVarInt(local->socket, TCP_CONNECTION_MODE_VAR,
				   (remote && remote[0]) ? TCP_CLIENT : TCP_SERVER);

	Shim_SetVar(local->socket, TCP_LOCAL_PORT_VAR, GetPropStr(instance, "LocalPort"));
	Shim_SetVar(local->socket, TCP_REMOTE_HOST_VAR, remote);
	Shim_SetVar(local->socket, TCP_REMOTE_PORT_VAR, GetPropStr(instance, "RemotePort"));

	if (GetPropInt(instance, "Secure"))
	{
		Shim_SetVarInt(local->socket, TCP_SECURITY_MODE_VAR, 1);
		Shim_SetVar(local->socket, TCP_SECURITY_CERT_VAR, GetPropStr(instance, "SslCert"));
		Shim_SetVar(local->socket, TCP_SECURITY_KEY_VAR, GetPropStr(instance, "SslKey"));
	}

	TCPStart(local->socket);
	local->started = 1;

	/* the port it took, so LocalPort reads back when 0 was asked for */
	SetPropInt(instance, "LocalPort", Shim_GetVarInt(local->socket, TCP_LOCAL_PORT_VAR));
	SetPropInt(instance, "State", Running);
	SetPropStr(instance, "Secured", GetPropInt(instance, "Secure") ? "1" : "0");

	return rtrn_handled;
}

/* ---- lifecycle ------------------------------------------------------ */

static NodeObj Shim_NewSocket(NodeObj instance)
{
	NodeObj lib, cls, args, sock = NULL;
	msgobj instanceStart;
	char *name;

	for (lib = GetChild(GetRegObjList()); lib && !sock; lib = GetNextSibling(lib))
		for (cls = GetChild(lib); cls; cls = GetNextSibling(cls))
		{
			name = GetNameStr(cls);
			if (!name || strcmp(name, "TCPSocket"))
				continue;

			instanceStart = (msgobj) GetPropLong(cls, "InstanceStart");
			if (!instanceStart)
				break;

			args = NewNode(INTEGER);
			SetPropLong(args, "Owner", (long) instance);
			SetPropLong(args, "MsgBase", SHIM_TCP_CALLBACK);
			SetPropStr(args, "Callback", "SocketCallback");
			instanceStart(cls, msg_initialize, args);
			DelNode(args);

			sock = (NodeObj) GetPropLong(cls, "LastInstance");
			break;
		}

	if (!sock)
		DebugPrint("TCP shim: the TCPSocket class is not loaded",
				   __FILE__, __LINE__, ERROR);
	return sock;
}

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance, port;
	InstanceData *local = malloc(sizeof(InstanceData));

	(void) message; (void) data;

	local->socket = NULL;
	local->started = 0;

	instance = NewNode(INTEGER);
	SetName(instance, "TCP");

	/* the old surface, unchanged, so no driver has to know yet */
	SetPropInt(instance, "LocalPort", 8080);
	SetPropStr(instance, "LocalAddr", "");	/* accepted; the object binds all */
	SetPropStr(instance, "RemoteAddr", "");
	SetPropInt(instance, "RemotePort", 0);
	SetPropStr(instance, "Connected", "0");
	SetPropStr(instance, "Secure", "0");
	SetPropStr(instance, "SslCert", "");
	SetPropStr(instance, "SslKey", "");
	SetPropStr(instance, "SslPass", "");	/* accepted; the object has no such var */
	SetPropStr(instance, "Secured", "0");
	SetPropInt(instance, "State", Starting);
	SetPropInt(instance, "Out", 0);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Shim_Activate);

	SetPropInt(instance, "In", 0);
	port = GetPropNode(instance, "In");
	SetPropLong(port, "OnMsg", (long)Shim_OnIn);

	SetPropStr(instance, "Enable", "1");
	port = GetPropNode(instance, "Enable");
	SetPropLong(port, "OnMsg", (long)Shim_OnEnable);

	/* where the object underneath reports - this shim's own port */
	SetPropStr(instance, "SocketCallback", "");
	port = GetPropNode(instance, "SocketCallback");
	SetPropLong(port, "OnMsg", (long)Shim_OnCallback);

	local->socket = Shim_NewSocket(instance);

	RegisterInstance(class, instance);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) message; (void) data;

	if (local)
	{
		if (local->socket)
		{
			TCPStop(local->socket);
			DeleteInstance(local->socket);
		}
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	(void) message; (void) data;

	SetName(class, "TCP");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Object");

	PublishProp(ClassSelf, "LocalPort", PROP_TEXTBOX, "8080");
	PublishProp(ClassSelf, "RemoteAddr", PROP_TEXTBOX, "");
	PublishProp(ClassSelf, "RemotePort", PROP_TEXTBOX, "0");
	PublishProp(ClassSelf, "Connected", PROP_LED, "0");
	PublishProp(ClassSelf, "Secure", PROP_CHECKBOX, "0");
	PublishProp(ClassSelf, "SslCert", PROP_TEXTBOX, "");
	PublishProp(ClassSelf, "SslKey", PROP_TEXTBOX, "");
	PublishProp(ClassSelf, "SslPass", PROP_TEXTBOX, "");
	PublishProp(ClassSelf, "Secured", PROP_LED, "0");
	PublishProp(ClassSelf, "Enable", PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "In", PROP_NULL, "");
	PublishProp(ClassSelf, "Out", PROP_NULL, "");
	PublishProp(ClassSelf, "State", PROP_LED, "1");

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

	SetName(temp, "TCP");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "3f9c1a72-58d4-4e6b-9a03-c1e7b28d5f40");
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)0);
	SetPropInt(temp, "State", 1);

	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");

	/* created in code, not from the layout table */
	AddDependency(temp, "tcp.object", "TCPSocket", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
