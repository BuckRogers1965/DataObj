
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"

/*

TCPPort object: the TCP instrument panel. It is a FRONT END, not a networking
object: it holds no socket of its own, it CONTAINS an instance of the TCP
engine object and drives it - the same shell/engine split ScriptBox uses for
language hosts.

The controls: Host Name, Port (with the Standard Ports menu writing it),
Enable, Open/Listen/Close, the Transmit and Receive boxes with Send / Clear
Tx / Clear Rx, the option checkboxes (Auto Open/Listen/Close - mutually
exclusive, Auto Send, Clear On Send, Accumulate Data), the Stream State
readout and the status LEDs. Cmd* controls are ordinary IN PORTS, so a Pulse
or a script can press Open/Listen/Send exactly as the panel's MoButtons do.

Dataflow: the default input connection is to Transmit Data, the default
output connection is from Receive Data - so In feeds TxData and Out carries
what arrives on RxData, and the widget drops into a flow like any other
object.

*/

/* NO hardcoded help: the help lives in objects/tcpport/README.md and is read
   from disk into the Help box when the panel is opened. */

/* Stream State, the display-state names */
#define ST_DISABLED   "DISABLED"
#define ST_IDLING     "IDLING"
#define ST_LISTEN     "LISTEN"
#define ST_OPENING    "OPENING"
#define ST_CONNECTED  "CONNECTED"
#define ST_CLOSING    "CLOSING"

typedef struct InstanceData
{
	int     enabled;	/* the ONLY gate on the commands */
	int     panelBuilt;	/* the panel is built once, when the object first has a path */
	TaskObj buildTask;	/* fires one tick after creation, to build the panel */
	NodeObj inner;		/* the TCP engine instance we drive */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint ( "TCPPort handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* ---- small helpers -------------------------------------------------- */

static void TCPPort_Log(NodeObj instance, char *category, char *text);
static void TCPPort_BuildPanel(NodeObj instance);
static int  TCPPort_BuildTask(NodeObj instance, NodeObj data, int msgid);

static void TCPPort_SetState(NodeObj instance, char *state)
{
	SetPropStr(instance, "StreamState", state);

	/* the LEDs are just a projection of the state - set them together so  */
	/* they can never disagree with the readout                             */
	SetPropStr(instance, "Listening", strcmp(state, ST_LISTEN) == 0 ? "1" : "0");
	SetPropStr(instance, "Connected", strcmp(state, ST_CONNECTED) == 0 ? "1" : "0");
	SetPropStr(instance, "Idling",    strcmp(state, ST_IDLING) == 0 ? "1" : "0");
	SetPropStr(instance, "Disabled",  strcmp(state, ST_DISABLED) == 0 ? "1" : "0");

	/* the Debug panel's finer-grained lights, from the same one state */
	SetPropStr(instance, "ListeningLed", strcmp(state, ST_LISTEN) == 0 ? "1" : "0");
	SetPropStr(instance, "ListenLed",    strcmp(state, ST_LISTEN) == 0 ? "1" : "0");
	SetPropStr(instance, "OpeningLed",   strcmp(state, ST_OPENING) == 0 ? "1" : "0");
	SetPropStr(instance, "OpenLed",      strcmp(state, ST_CONNECTED) == 0 ? "1" : "0");
	SetPropStr(instance, "ClosingLed",   strcmp(state, ST_CLOSING) == 0 ? "1" : "0");
	SetPropStr(instance, "CloseLed",
			   (strcmp(state, ST_IDLING) == 0 || strcmp(state, ST_DISABLED) == 0) ? "1" : "0");

	TCPPort_Log(instance, "TraceMsgs", state);
}

static int TCPPort_IsState(NodeObj instance, char *state)
{
	char *cur = GetPropStr(instance, "StreamState");
	return cur && strcmp(cur, state) == 0;
}

/* normalize the host box: strip leading                                 */
/* whitespace, cut at the first whitespace, lower-case the rest           */
static void TCPPort_NormalizeHost(NodeObj instance)
{
	char *host = GetPropStr(instance, "HostName");
	char buf[256];
	int i = 0, n = 0;

	if (!host)
		return;

	while (host[i] && isspace((unsigned char)host[i]))
		i++;
	while (host[i] && !isspace((unsigned char)host[i]) && n < (int)sizeof(buf) - 1)
		buf[n++] = (char)tolower((unsigned char)host[i++]);
	buf[n] = '\0';

	if (strcmp(buf, host) != 0)
		SetValueStr(GetPropNode(instance, "HostName"), buf);
}

/* Auto Open / Auto Listen / Auto Close are mutually exclusive - picking  */
/* one clears the other two                                              */
static void TCPPort_PickAuto(NodeObj instance, char *which)
{
	SetPropStr(instance, "AutoOpen",   strcmp(which, "AutoOpen") == 0 ? "1" : "0");
	SetPropStr(instance, "AutoListen", strcmp(which, "AutoListen") == 0 ? "1" : "0");
	SetPropStr(instance, "AutoClose",  strcmp(which, "AutoClose") == 0 ? "1" : "0");
}

/* ---- the commands --------------------------------------------------- */

/* Listen: bring the engine up as a server on Port. The TCP object's own  */
/* activation is one-shot (Enable=0 is a full shutdown), so listening     */
/* again after a close means a fresh inner instance - the widget hides    */
/* that lifecycle, which is exactly what a front end is for.              */
static void TCPPort_DoListen(NodeObj instance)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *port;

	/* Enable is the ONLY gate on the commands - a disabled panel says so
	   now rather than doing nothing
	   silently. */
	if (!local)
		return;
	if (!local->enabled)
	{
		TCPPort_Log(instance, "ErrorMsgs", "Listen ignored - panel is not enabled");
		return;
	}
	if (TCPPort_IsState(instance, ST_LISTEN) || TCPPort_IsState(instance, ST_CONNECTED))
	{
		TCPPort_Log(instance, "TraceMsgs", "Listen ignored - already up");
		return;
	}

	TCPPort_Log(instance, "TraceMsgs", "listening");

	if (GetPropInt(instance, "AccumulateRx"))
		SetPropStr(instance, "RxData", "");

	if (local->inner)
		DeleteInstance(local->inner);

	/* the engine belongs to this panel - created inside it */
	local->inner = CreateObject(instance, "TCP");
	if (!local->inner)
	{
		DebugPrint("TCPPort: the TCP class is not loaded", __FILE__, __LINE__, ERROR);
		TCPPort_SetState(instance, ST_IDLING);
		return;
	}

	port = GetPropStr(instance, "Port");
	SetOrDeliverProp(local->inner, "LocalPort", port ? port : "8080");

	/* hand the engine the security settings before activating it - the    */
	/* context is built once, at listen time (tcp.c, Tcp_Activate)         */
	if (GetPropInt(instance, "SslEnable"))
	{
		SetOrDeliverProp(local->inner, "Secure", "1");
		SetOrDeliverProp(local->inner, "SslCert", GetPropStr(instance, "SslCert"));
		SetOrDeliverProp(local->inner, "SslKey",  GetPropStr(instance, "SslKey"));
		SetOrDeliverProp(local->inner, "SslPass", GetPropStr(instance, "SslPass"));
	}

	Connect(local->inner, "Out", instance, "InnerRx");
	ActivateInstance(local->inner);

	/* the engine refuses to listen at all if secure was asked for and TLS */
	/* could not start - never quietly serve in the clear                   */
	if (GetPropInt(instance, "SslEnable") && !GetPropInt(local->inner, "Secured"))
	{
		TCPPort_Log(instance, "ErrorMsgs", "TLS failed to start - not listening");
		SetPropStr(instance, "SslStatus", "0");
		DeleteInstance(local->inner);
		local->inner = NULL;
		TCPPort_SetState(instance, ST_IDLING);
		return;
	}

	SetPropStr(instance, "SslStatus", GetPropInt(local->inner, "Secured") ? "1" : "0");
	TCPPort_SetState(instance, ST_LISTEN);
}

/* Close: Enable=0 on the engine is a full shutdown (sockets close, EOF   */
/* goes out, the poll task stops)                                        */
static void TCPPort_DoClose(NodeObj instance)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return;

	TCPPort_SetState(instance, ST_CLOSING);

	if (local->inner)
	{
		DeleteInstance(local->inner);
		local->inner = NULL;
	}

	TCPPort_SetState(instance, local->enabled ? ST_IDLING : ST_DISABLED);
}

/* Send: only when connected and only with something to send - the        */
/* original's CmdSendData, including Clear On Send                         */
static void TCPPort_DoSend(NodeObj instance)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	NodeObj chunk;
	char *tx;

	if (!local || !local->enabled || !local->inner)
		return;
	if (!TCPPort_IsState(instance, ST_CONNECTED))
		return;

	tx = GetPropStr(instance, "TxData");
	if (!tx || !tx[0])
		return;

	chunk = NewNode(STRING);
	SetName(chunk, "Data");
	SetValueStr(chunk, tx);
	DeliverMsg(local->inner, "In", msg_send, chunk);

	if (GetPropInt(instance, "ClearOnSend"))
		SetPropStr(instance, "TxData", "");

	SetPropStr(instance, "TxReady", "0");
}

/* ---- port handlers -------------------------------------------------- */

/* every command is an ordinary in port taking a 1 - so the panel's       */
/* MoButton, a Pulse, and a script all press it the same way              */
int TCPPort_OnOpen(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *host, *port;

	if (!local || message == msg_eof || !GetValueInt(data))
		return rtrn_handled;
	if (!local->enabled)
	{
		TCPPort_Log(instance, "ErrorMsgs", "Open ignored - panel is not enabled");
		return rtrn_handled;
	}

	if (TCPPort_IsState(instance, ST_LISTEN) || TCPPort_IsState(instance, ST_CONNECTED))
	{
		TCPPort_Log(instance, "TraceMsgs", "Open ignored - already up");
		return rtrn_handled;
	}

	TCPPort_Log(instance, "TraceMsgs", "opening");

	if (GetPropInt(instance, "AccumulateRx"))
		SetPropStr(instance, "RxData", "");

	/* CONNECT OUT: a fresh engine instance in client mode, dialling the
	   Host Name box at the Port box. The connect is non-blocking, so
	   OPENING is a real state we sit in until the engine reports it is
	   Connected (watched in TCPPort_Tick). */
	if (local->inner)
		DeleteInstance(local->inner);

	/* the engine belongs to this panel - created inside it */
	local->inner = CreateObject(instance, "TCP");
	if (!local->inner)
	{
		DebugPrint("TCPPort: the TCP class is not loaded", __FILE__, __LINE__, ERROR);
		TCPPort_SetState(instance, ST_IDLING);
		return rtrn_handled;
	}

	host = GetPropStr(instance, "HostName");
	port = GetPropStr(instance, "Port");

	if (!host || !host[0])
	{
		TCPPort_Log(instance, "ErrorMsgs", "Open needs a Host Name");
		DebugPrint("TCPPort: Open with no HostName", __FILE__, __LINE__, ERROR);
		DeleteInstance(local->inner);
		local->inner = NULL;
		TCPPort_SetState(instance, ST_IDLING);
		return rtrn_handled;
	}

	SetOrDeliverProp(local->inner, "RemoteAddr", host);
	SetOrDeliverProp(local->inner, "RemotePort", port ? port : "80");

	if (GetPropInt(instance, "SslEnable"))
	{
		SetOrDeliverProp(local->inner, "Secure", "1");
		SetOrDeliverProp(local->inner, "SslCert", GetPropStr(instance, "SslCert"));
		SetOrDeliverProp(local->inner, "SslKey",  GetPropStr(instance, "SslKey"));
		SetOrDeliverProp(local->inner, "SslPass", GetPropStr(instance, "SslPass"));
	}

	Connect(local->inner, "Out", instance, "InnerRx");
	ActivateInstance(local->inner);

	TCPPort_SetState(instance, ST_OPENING);
	TCPPort_Log(instance, "TraceMsgs", "connecting");

	/* watch the engine's Connected line so OPENING becomes CONNECTED     */
	/* without anyone polling in a handler                                 */
	Connect(local->inner, "Connected", instance, "InnerUp");

	return rtrn_handled;
}

/* the engine's Connected line: OPENING -> CONNECTED when the non-blocking
   connect completes (or back to IDLING if it failed) */
int TCPPort_OnInnerUp(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message == msg_eof)
		return rtrn_handled;

	if (GetValueInt(data))
	{
		SetPropStr(instance, "SslStatus",
				   GetPropInt(local->inner, "Secured") ? "1" : "0");
		TCPPort_SetState(instance, ST_CONNECTED);
	}
	else if (TCPPort_IsState(instance, ST_OPENING))
	{
		TCPPort_Log(instance, "ErrorMsgs", "connect failed");
		TCPPort_SetState(instance, ST_IDLING);
	}

	return rtrn_handled;
}

int TCPPort_OnListen(NodeObj instance, MsgId message, NodeObj data)
{
	if (message == msg_eof || !GetValueInt(data))
		return rtrn_handled;
	TCPPort_DoListen(instance);
	return rtrn_handled;
}

int TCPPort_OnClose(NodeObj instance, MsgId message, NodeObj data)
{
	if (message == msg_eof || !GetValueInt(data))
		return rtrn_handled;
	TCPPort_DoClose(instance);
	return rtrn_handled;
}

int TCPPort_OnSend(NodeObj instance, MsgId message, NodeObj data)
{
	if (message == msg_eof || !GetValueInt(data))
		return rtrn_handled;
	TCPPort_DoSend(instance);
	return rtrn_handled;
}

int TCPPort_OnClearTx(NodeObj instance, MsgId message, NodeObj data)
{
	if (message == msg_eof || !GetValueInt(data))
		return rtrn_handled;
	SetPropStr(instance, "TxData", "");
	SetPropStr(instance, "TxReady", "1");
	return rtrn_handled;
}

int TCPPort_OnClearRx(NodeObj instance, MsgId message, NodeObj data)
{
	if (message == msg_eof || !GetValueInt(data))
		return rtrn_handled;
	SetPropStr(instance, "RxData", "");
	SetPropStr(instance, "RxReady", "0");
	SetPropStr(instance, "BytesReady", "0");
	return rtrn_handled;
}

/* the Debug panel's log: one line per event, gated by the panel's own    */
/* category checkboxes (Errors / Progress / Tx Data / Rx Data / Debug) -  */
/* the same shape DebugPrint's typed categories have, shown in the panel  */
static void TCPPort_Log(NodeObj instance, char *category, char *text)
{
	char *cur, *buf;
	int len;

	if (!GetPropInt(instance, category))
		return;

	cur = GetPropStr(instance, "DebugText");
	len = (int)strlen(cur ? cur : "") + (int)strlen(text) + 2;
	buf = malloc(len);
	snprintf(buf, len, "%s%s\n", (cur && cur[0]) ? cur : "", text);
	SetPropStr(instance, "DebugText", buf);
	free(buf);
}

int TCPPort_OnClearDebug(NodeObj instance, MsgId message, NodeObj data)
{
	if (message == msg_eof || !GetValueInt(data))
		return rtrn_handled;
	SetPropStr(instance, "DebugText", "");
	return rtrn_handled;
}

/* SSL: the engine does the TLS - tcp.c carries the cert/key/session      */
/* handling. The panel just                                              */
/* sets Secure and the PEM paths; the next Listen builds the context, and */
/* SslStatus reports what the engine actually achieved, never what was    */
/* merely asked for.                                                       */
int TCPPort_OnSslEnable(NodeObj instance, MsgId message, NodeObj data)
{
	int on;

	if (message == msg_eof)
		return rtrn_handled;

	on = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "SslEnable"), on ? "1" : "0");

	if (on && (!GetPropStr(instance, "SslCert") || !GetPropStr(instance, "SslCert")[0]
			   || !GetPropStr(instance, "SslKey") || !GetPropStr(instance, "SslKey")[0]))
		TCPPort_Log(instance, "ErrorMsgs",
					"Secure is on but sslCert/sslKey are empty - Listen will refuse");

	TCPPort_Log(instance, "TraceMsgs", on ? "secure mode on" : "secure mode off");

	/* takes effect at the next Listen - a running server does not swap    */
	/* its security underneath its peers                                    */
	if (TCPPort_IsState(instance, ST_LISTEN) || TCPPort_IsState(instance, ST_CONNECTED))
		TCPPort_Log(instance, "TraceMsgs", "restart (Close then Listen) to apply");

	return rtrn_handled;
}

/* the Standard Ports menu writes the Port box, with the same six         */
/* services                                                               */
int TCPPort_OnStandardPort(NodeObj instance, MsgId message, NodeObj data)
{
	char *name = data ? GetValueStr(data) : NULL;
	char *port = NULL;

	if (message == msg_eof || !name)
		return rtrn_handled;

	if      (!strcmp(name, "FTP"))    port = "21";
	else if (!strcmp(name, "TELNET")) port = "23";
	else if (!strcmp(name, "SMTP"))   port = "25";
	else if (!strcmp(name, "HTTP"))   port = "80";
	else if (!strcmp(name, "POP"))    port = "110";
	else if (!strcmp(name, "HTTPS"))  port = "443";

	SetValueStr(GetPropNode(instance, "StandardPort"), name);
	if (port)
		SetPropStr(instance, "Port", port);

	return rtrn_handled;
}

/* what the engine received: into the Receive box (accumulating or        */
/* replacing, per the Accumulate Data option) and out our own Out        */
int TCPPort_OnInnerRx(NodeObj instance, MsgId message, NodeObj data)
{
	NodeObj copy;
	char *value, *cur, *buf;
	int len;

	if (message == msg_eof)
	{
		InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

		/* A peer going away means "back to listening" - but the EOF the   */
		/* engine emits as it SHUTS DOWN must not resurrect LISTEN over a   */
		/* Close or a disable. Only a live server returns to listening.     */
		if (local && local->inner && local->enabled
			&& !TCPPort_IsState(instance, ST_CLOSING)
			&& !TCPPort_IsState(instance, ST_DISABLED)
			&& !TCPPort_IsState(instance, ST_IDLING))
			TCPPort_SetState(instance, ST_LISTEN);

		return rtrn_handled;
	}

	value = data ? GetValueStr(data) : NULL;
	if (!value)
		return rtrn_handled;

	/* traffic means a peer: the engine has no separate "connected" event  */
	TCPPort_SetState(instance, ST_CONNECTED);

	if (GetPropInt(instance, "AccumulateRx"))
	{
		cur = GetPropStr(instance, "RxData");
		len = (int)strlen(cur ? cur : "") + (int)strlen(value) + 1;
		buf = malloc(len);
		snprintf(buf, len, "%s%s", (cur && cur[0]) ? cur : "", value);
		SetPropStr(instance, "RxData", buf);
		free(buf);
	}
	else
		SetPropStr(instance, "RxData", value);

	SetPropStr(instance, "RxReady", "1");
	SetPropInt(instance, "BytesReady", (int)strlen(GetPropStr(instance, "RxData")));

	copy = NewNode(STRING);
	SetName(copy, "Data");
	SetValueStr(copy, value);
	SndMsg(instance, "Out", msg_send, copy);

	return rtrn_handled;
}

/* our In feeds the Transmit box - "default input connection is to        */
/* Transmit Data". It just writes the box; Auto Send is driven by the box */
/* CHANGING (TCPPort_OnTxChanged), so In-fed and typed data auto-send the */
/* same way.                                                              */
int TCPPort_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *value;

	if (!local || !local->enabled || message == msg_eof)
		return rtrn_dropped;

	value = data ? GetValueStr(data) : NULL;
	SetPropStr(instance, "TxData", value ? value : "");
	SetPropStr(instance, "TxReady", "1");

	return rtrn_handled;
}

/* the Transmit box changed (typed, or written through In): if we are
   connected and Auto Send is on, send it. Wired to TxData's own change by
   an internal Connect in InstanceStart, so ANY change to the box triggers
   it. An empty box (a Clear) sends nothing - it declines to auto-send a
   clear. */
int TCPPort_OnTxChanged(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *tx;

	(void) data;

	if (!local || !local->enabled || message == msg_eof)
		return rtrn_handled;

	SetPropStr(instance, "TxReady", "1");

	if (!GetPropInt(instance, "AutoSend"))
		return rtrn_handled;
	if (!TCPPort_IsState(instance, ST_CONNECTED))
		return rtrn_handled;

	tx = GetPropStr(instance, "TxData");
	if (tx && tx[0])
		TCPPort_DoSend(instance);

	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables. Disabling is a full stop (the */
/* original's CmdEnable_change); enabling honors the Auto option.          */
int TCPPort_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	if (!local->enabled)
	{
		/* Enable=0 is a full stop: close the socket and show
		   DISABLED. Re-enabling returns it to IDLING, ready for Listen. */
		TCPPort_DoClose(instance);
		TCPPort_SetState(instance, ST_DISABLED);
		return rtrn_handled;
	}

	TCPPort_SetState(instance, ST_IDLING);

	return rtrn_handled;
}

/* The three Auto options are PLAIN PROPERTIES, deliberately, not ports.  */
/* A port's write is delivered to its handler and never stored behind it, */
/* and a port's value can only be updated with SetValueStr, which does    */
/* not fan out - so a checkbox on a port can neither be read back nor     */
/* announce itself, and it appeared stuck checked. As plain properties    */
/* they store and announce like every other checkbox.                     */
/* Their exclusivity is resolved where it is USED (TCPPort_AutoChoice):   */
/* Listen wins over Open wins over Close, and Activate normalizes the     */
/* boxes to that choice so the panel converges on the radio behavior the  */
/* original had.                                                          */
static char *TCPPort_AutoChoice(NodeObj instance)
{
	if (GetPropInt(instance, "AutoListen"))
		return "AutoListen";
	if (GetPropInt(instance, "AutoOpen"))
		return "AutoOpen";
	if (GetPropInt(instance, "AutoClose"))
		return "AutoClose";
	return "";
}

/* Placement setup, run when the widget is placed (not a user "activate"
   step, which this framework has no notion
   of): normalize the host box, settle the Auto radios, show the resting
   state, and honor Auto Listen. Registered as the framework's Activate hook
   too, so an explicit activate just re-runs this harmlessly.               */
int TCPPort_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	/* build the panel once, now that the object has a real location and
	   CreateObject(itself/subview, control) will resolve */
	if (!local->panelBuilt)
	{
		local->panelBuilt = 1;
		TCPPort_BuildPanel(instance);
	}

	TCPPort_NormalizeHost(instance);

	/* settle the three Auto boxes onto the one that wins, so the panel    */
	/* shows the radio behavior */
	TCPPort_PickAuto(instance, TCPPort_AutoChoice(instance));

	if (!local->enabled)
	{
		TCPPort_SetState(instance, ST_DISABLED);
		return rtrn_handled;
	}

	TCPPort_SetState(instance, ST_IDLING);
	if (GetPropInt(instance, "AutoListen"))
		TCPPort_DoListen(instance);

	return rtrn_handled;
}

/* ---- lifecycle ------------------------------------------------------ */

static void TCPPort_Port(NodeObj instance, char *name, char *initial, void *handler)
{
	NodeObj port;

	SetPropStr(instance, name, initial);
	port = GetPropNode(instance, name);
	SetPropLong(port, "OnMsg", (long)handler);
}


int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData *local = malloc(sizeof(InstanceData));

	local->enabled = 1;
	local->panelBuilt = 0;
	local->buildTask = NULL;
	local->inner = NULL;

	instance = NewNode(INTEGER);
	SetName(instance, "TCPPort");

	/* connection settings */
	SetPropStr(instance, "HostName", "");
	SetPropStr(instance, "Port", "80");
	SetPropStr(instance, "StandardPortList", "FTP,TELNET,SMTP,HTTP,POP,HTTPS");

	/* the data boxes */
	SetPropStr(instance, "TxData", "");
	SetPropStr(instance, "RxData", "");
	SetPropStr(instance, "BytesReady", "0");

	/* options - Auto Close is the default of the three. These             */
	/* are plain properties (see TCPPort_AutoChoice): a checkbox must be   */
	/* readable and must announce, and a port can do neither.              */
	SetPropStr(instance, "AutoOpen", "0");
	SetPropStr(instance, "AutoListen", "0");
	SetPropStr(instance, "AutoClose", "1");
	SetPropStr(instance, "AutoSend", "1");
	SetPropStr(instance, "ClearOnSend", "0");
	SetPropStr(instance, "AccumulateRx", "1");

	/* line-end and binary options (Tx/Rx Fix Line Ends,                   */
	/* Binary Tx/Rx) - plain properties, read where they matter            */
	SetPropStr(instance, "TxFixLineEnds", "1");
	SetPropStr(instance, "RxFixLineEnds", "1");
	SetPropStr(instance, "BinaryTx", "0");
	SetPropStr(instance, "BinaryRx", "0");

	/* the Debug panel */
	SetPropStr(instance, "DebugText", "");
	SetPropStr(instance, "ErrorMsgs", "0");
	SetPropStr(instance, "TraceMsgs", "0");
	SetPropStr(instance, "DebugMsgs", "0");
	SetPropStr(instance, "ShowTxData", "0");
	SetPropStr(instance, "TraceRxData", "0");
	SetPropStr(instance, "OpenLed", "0");
	SetPropStr(instance, "OpeningLed", "0");
	SetPropStr(instance, "ListenLed", "0");
	SetPropStr(instance, "ListeningLed", "0");
	SetPropStr(instance, "CloseLed", "1");
	SetPropStr(instance, "ClosingLed", "0");

	/* the SSL panel - the engine has no TLS yet, so these are carried but */
	/* honest: turning SslEnable on says so rather than pretending          */
	/* SslEnable is a port (created with its handler below) - creating it  */
	/* here too would shadow it, the port-shadowing landmine               */
	SetPropStr(instance, "SslStatus", "0");
	SetPropStr(instance, "SslCert", "");
	SetPropStr(instance, "SslKey", "");
	SetPropStr(instance, "SslPass", "");

	/* status */
	SetPropStr(instance, "StreamState", ST_IDLING);
	SetPropStr(instance, "Listening", "0");
	SetPropStr(instance, "Connected", "0");
	SetPropStr(instance, "Idling", "1");
	SetPropStr(instance, "Disabled", "0");
	SetPropStr(instance, "RxReady", "0");
	SetPropStr(instance, "TxReady", "1");

	SetPropInt(instance, "Out", 0);
	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)TCPPort_Activate);

	/* the commands, each an ordinary in port */
	TCPPort_Port(instance, "Open",    "0", (void *)TCPPort_OnOpen);
	TCPPort_Port(instance, "Listen",  "0", (void *)TCPPort_OnListen);
	TCPPort_Port(instance, "Close",   "0", (void *)TCPPort_OnClose);
	TCPPort_Port(instance, "Send",    "0", (void *)TCPPort_OnSend);
	TCPPort_Port(instance, "ClearTx", "0", (void *)TCPPort_OnClearTx);
	TCPPort_Port(instance, "ClearRx", "0", (void *)TCPPort_OnClearRx);

	/* the menu, the auto options, dataflow, and the engine's return path */
	TCPPort_Port(instance, "StandardPort", "", (void *)TCPPort_OnStandardPort);
	TCPPort_Port(instance, "ClearDebug",   "0", (void *)TCPPort_OnClearDebug);
	TCPPort_Port(instance, "SslEnable",    "0", (void *)TCPPort_OnSslEnable);
	TCPPort_Port(instance, "In",           "",  (void *)TCPPort_OnIn);
	TCPPort_Port(instance, "InnerRx",      "",  (void *)TCPPort_OnInnerRx);
	TCPPort_Port(instance, "InnerUp",      "",  (void *)TCPPort_OnInnerUp);
	TCPPort_Port(instance, "Enable",       "1", (void *)TCPPort_OnEnable);

	/* Auto Send is driven by the Transmit box CHANGING: an internal port
	   wired to TxData's own change, so
	   typing in the box and writing it through In both auto-send the same
	   way. Not published - plumbing, not a control. */
	TCPPort_Port(instance, "TxChanged", "", (void *)TCPPort_OnTxChanged);
	Connect(instance, "TxData", instance, "TxChanged");

	InitPosition(instance);

	/* the view's OWN size, set here as a resting value BEFORE any client can
	   subscribe - a size set later in the deferred build would shadow the W/H
	   node the client's tap is already on and never reach it, leaving the
	   panel at its default. This is the main panel (457x511), grown
	   to hold the character-sized text boxes. */
	SetPropInt(instance, "W", 480);
	SetPropInt(instance, "H", 640);

	RegisterInstance(class, instance);

	/* arm the deferred build: the panel is populated one tick from now,
	   after the bridge has given this instance its path. Nothing here yet
	   has a location, so building now would refuse - waiting one tick is
	   the "created in a location" half of CreateObject's contract. */
	local->buildTask = CreateTask(ObjGetTaskList());
	AddTaskMilli(local->buildTask, 1, (FuncPtr)TCPPort_BuildTask, msg_send, instance);

	return rtrn_handled;
}

/* wire a reflect (a property -> a control's input) AND seed it: hand the
   control the property's CURRENT value right now, so the GUI shows the
   underlying value the moment the control is created. A plain Connect'd
   reflect only fires on the NEXT change, so without this seed a fresh box
   reads blank (Port empty instead of 80) until something moves. The client
   subscribes to the control's own Value and is handed this seeded value on
   subscribe. */
static void TCPPort_Reflect(NodeObj src, char *sp, NodeObj dst, char *dp)
{
	char *cur;

	Connect(src, sp, dst, dp);
	cur = GetPropStr(src, sp);
	if (cur)
		SetOrDeliverProp(dst, dp, cur);
}

/* one control put into a container view and wired to the TCPPort's own
   property - exactly a widget dropped into a view, nothing special. The
   kind of wire follows the control class: a MoButton/Button presses a
   command port, a display reflects a property, an input edits it. */
static void TCPPort_Ctl(NodeObj container, NodeObj target, char *cls, char *prop,
						int x, int y, int w, int h, int rows, int cols)
{
	char cpath[256], path[300];
	NodeObj c = CreateObject(container, cls);
	if (!c)
		return;

	/* name it after its property and register its path, so it is an
	   addressable member of the panel - the "has a path" half of "created
	   in a location" that CreateObject leaves to whoever creates */
	if (PathOfInstance(container, cpath, sizeof(cpath)))
	{
		SetPropStr(c, "Name", prop && prop[0] ? prop : cls);
		snprintf(path, sizeof(path), "%s/%s", cpath, prop && prop[0] ? prop : cls);
		RegisterPath(path, c);
	}

	SetPropInt(c, "X", x);
	SetPropInt(c, "Y", y);
	SetPropInt(c, "W", w);
	SetPropInt(c, "H", h);
	if (prop && prop[0])
		SetPropStr(c, "Label", prop);

	/* a Textbox carries its declared size as its own Rows/Cols - the box
	   fetches these on instantiation (they are data properties) and sizes
	   itself, so each panel box is the size the widget declares */
	if (strcmp(cls, "Textbox") == 0 && rows > 0 && cols > 0)
	{
		SetPropInt(c, "Rows", rows);
		SetPropInt(c, "Cols", cols);
	}

	if (strcmp(cls, "MoButton") == 0)
		Connect(c, "Out", target, prop);		/* a command port */
	else if (strcmp(cls, "Button") == 0)
		Connect(c, "Out", target, "Activate");
	else if (strcmp(cls, "Markdown") == 0)
	{
		/* the Help box starts EMPTY; its README.md is read from disk into its
		   Value only when the Help panel is OPENED (TCPPort_OnHelpOpen). */
	}
	else if (strcmp(cls, "LED") == 0 || strcmp(cls, "TextOut") == 0
			 || strcmp(cls, "Label") == 0 || strcmp(cls, "VUMeter") == 0)
		TCPPort_Reflect(target, prop, c, "Value");	/* set its display property */
	else if (strcmp(cls, "Dropdown") == 0)
	{
		Connect(c, "Value", target, prop);		/* menu picks the value */
		TCPPort_Reflect(target, "StandardPortList", c, "Items");
		SetOrDeliverProp(c, "Value", GetPropStr(target, prop)); /* show the pick */
	}
	else						/* Checkbox / Textbox / Slider / Knob */
	{
		Connect(c, "Value", target, prop);		/* edits it */
		TCPPort_Reflect(target, prop, c, "In");	/* and reflects it, seeded now */
	}
}

/* read a whole file into a malloc'd, NUL-terminated string (caller frees) */
static char *TCPPort_ReadFile(char *path)
{
	FILE *f = fopen(path, "rb");
	long  n;
	char *buf;

	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n < 0)
	{
		fclose(f);
		return NULL;
	}
	buf = malloc(n + 1);
	if (!buf)
	{
		fclose(f);
		return NULL;
	}
	n = (long)fread(buf, 1, n, f);
	buf[n] = '\0';
	fclose(f);
	return buf;
}

/* the Help panel was OPENED: read the widget's README.md from disk and set
   it into the Help box's Value with an update. No hardcoded help. */
int TCPPort_OnHelpOpen(NodeObj view, MsgId message, NodeObj data)
{
	char vpath[256], mpath[320];
	NodeObj box;
	char *md;

	if (message == msg_eof || !GetValueInt(data))
		return rtrn_handled;			/* only on OPEN */

	if (!PathOfInstance(view, vpath, sizeof(vpath)))
		return rtrn_handled;
	snprintf(mpath, sizeof(mpath), "%s/HelpText", vpath);
	box = ResolvePath(mpath);
	if (!box)
		return rtrn_handled;

	md = TCPPort_ReadFile("objects/tcpport/README.md");
	SetPropStr(box, "Value", md ? md : "");
	if (md)
		free(md);

	return rtrn_handled;
}

/* a sub-panel: a View put into the panel (renders as an icon that opens),
   then populated with its own controls - a view inside a view. */
static NodeObj TCPPort_SubPanel(NodeObj panel, NodeObj target, char *name,
								int x, int y, int w, int h)
{
	char ppath[256], path[300];
	NodeObj v = CreateObject(panel, "View");
	(void) target;
	if (!v)
		return NULL;
	SetPropStr(v, "Name", name);
	/* register the sub-view's path FIRST, so controls can be created in it */
	if (PathOfInstance(panel, ppath, sizeof(ppath)))
	{
		snprintf(path, sizeof(path), "%s/%s", ppath, name);
		RegisterPath(path, v);
	}
	SetPropInt(v, "X", x);
	SetPropInt(v, "Y", y);
	SetPropInt(v, "W", w);
	SetPropInt(v, "H", h);
	return v;
}

/* The panel: one flat table, every control tagged with the panel it
   lives on - 0 = the main panel (the object itself), 1 = Debug, 2 = SSL,
   3 = Help. */
/* rows/cols are the declared size for a Textbox (characters), 0 for every
   other control - a Textbox sizes itself by its Rows/Cols property, so the
   panel gives each box its declared size */
typedef struct { char *cls, *prop; int x, y, w, h, panel, rows, cols; } TCtl;

static TCtl TCPPortPanel[] = {
	/* --- panel 0: TCP Port (the object's own view) --- */
	{ "Checkbox", "Enable",        390,  14,  8,  8, 0,  0,  0 },
	{ "Textbox",  "HostName",       15,  44, 419, 15, 0,  1, 50 },
	{ "Dropdown", "StandardPort",   13,  84,  85, 15, 0,  0,  0 },
	{ "Textbox",  "Port",          131,  84,  63, 15, 0,  1,  8 },
	{ "TextOut",  "StreamState",   226,  84, 108, 15, 0,  0,  0 },
	{ "LED",      "Disabled",      368,  69,  10, 10, 0,  0,  0 },
	{ "LED",      "Idling",        368,  86,  10, 10, 0,  0,  0 },
	{ "LED",      "Connected",     368, 103,  10, 10, 0,  0,  0 },
	{ "LED",      "Listening",     185,  63,  10, 10, 0,  0,  0 },
	{ "LED",      "State",         368, 120,  10, 10, 0,  0,  0 },
	{ "Checkbox", "AutoOpen",       26, 128,  16, 17, 0,  0,  0 },
	{ "Checkbox", "AutoListen",    128, 128,  16, 17, 0,  0,  0 },
	{ "Checkbox", "AutoClose",     249, 128,  16, 17, 0,  0,  0 },
	{ "MoButton", "Open",           23, 149,  60, 20, 0,  0,  0 },
	{ "MoButton", "Listen",        128, 149,  60, 20, 0,  0,  0 },
	{ "MoButton", "Close",         248, 149,  60, 20, 0,  0,  0 },
	{ "Textbox",  "TxData",         14, 226, 323, 94, 0,  6, 44 },
	{ "LED",      "TxReady",       389, 228,  10, 10, 0,  0,  0 },
	{ "MoButton", "Send",          365, 272,  60, 20, 0,  0,  0 },
	{ "MoButton", "ClearTx",       365, 304,  60, 20, 0,  0,  0 },
	{ "Checkbox", "BinaryTx",       27, 332,   8,  8, 0,  0,  0 },
	{ "Checkbox", "TxFixLineEnds",  87, 332,   8,  8, 0,  0,  0 },
	{ "Checkbox", "ClearOnSend",   188, 332,   8,  8, 0,  0,  0 },
	{ "Checkbox", "AutoSend",      259, 332,   8,  8, 0,  0,  0 },
	{ "Textbox",  "RxData",         14, 380, 323, 94, 0,  6, 44 },
	{ "LED",      "RxReady",       389, 383,  10, 10, 0,  0,  0 },
	{ "TextOut",  "BytesReady",    358, 422,  71, 15, 0,  0,  0 },
	{ "MoButton", "ClearRx",       365, 468,  60, 20, 0,  0,  0 },
	{ "Checkbox", "BinaryRx",       27, 485,   8,  8, 0,  0,  0 },
	{ "Checkbox", "RxFixLineEnds",  87, 485,   8,  8, 0,  0,  0 },
	{ "Checkbox", "AccumulateRx",  188, 485,   8,  8, 0,  0,  0 },

	/* --- panel 1: TCP Debug --- */
	{ "LED",      "OpenLed",        32,  63,  10, 10, 1,  0,  0 },
	{ "LED",      "OpeningLed",     80,  63,  10, 10, 1,  0,  0 },
	{ "LED",      "ListenLed",     134,  63,  10, 10, 1,  0,  0 },
	{ "LED",      "ListeningLed",  185,  63,  10, 10, 1,  0,  0 },
	{ "LED",      "CloseLed",      239,  63,  10, 10, 1,  0,  0 },
	{ "LED",      "ClosingLed",    286,  63,  10, 10, 1,  0,  0 },
	{ "Checkbox", "ErrorMsgs",      34, 127,   8,  8, 1,  0,  0 },
	{ "Checkbox", "TraceMsgs",      81, 127,   8,  8, 1,  0,  0 },
	{ "Checkbox", "ShowTxData",    135, 127,   8,  8, 1,  0,  0 },
	{ "Checkbox", "TraceRxData",   186, 127,   8,  8, 1,  0,  0 },
	{ "Checkbox", "DebugMsgs",     241, 127,   8,  8, 1,  0,  0 },
	{ "MoButton", "ClearDebug",    315, 125,  60, 20, 1,  0,  0 },
	{ "Textbox",  "DebugText",      13, 175, 365, 275, 1, 16, 46 },

	/* --- panel 2: TCP SSL --- */
	{ "Checkbox", "SslEnable",      12,  14,   8,  8, 2,  0,  0 },
	{ "LED",      "SslStatus",      12,  40,  10, 10, 2,  0,  0 },
	{ "Textbox",  "SslCert",        12,  59, 349, 87, 2,  5, 42 },
	{ "Textbox",  "SslKey",         12, 177, 349, 87, 2,  5, 42 },
	{ "Textbox",  "SslPass",        13, 295, 348, 32, 2,  1, 42 },

	/* --- panel 3: Help --- */
	/* the standard help box: fills the Help panel with a 10px margin */
	{ "Markdown", "HelpText",       10,  10, HELP_W - HELP_W_OFF, HELP_H - HELP_H_OFF, 3,  0,  0 },

	{ NULL, NULL, 0, 0, 0, 0, 0, 0, 0 }
};

/* build the panel: panel 0 goes straight into the object (its own view),
   panels 1-3 are sub-views (Debug/SSL/Help) that render as openable
   icons - a view inside a view. Their icons sit where the Debug/Secure/Help
   buttons would be. */
static void TCPPort_BuildPanel(NodeObj instance)
{
	NodeObj sub[4];
	int i;

	/* the widget IS the view - its controls go straight into it and it
	   lays them out by their X/Y (its own size was set in InstanceStart,
	   before any subscribe). The three sub-panels are views inside it, each
	   sized to hold its character-sized boxes, rendering as openable icons
	   where the Debug/Secure/Help buttons would be. A sub-view's size
	   is set at creation, before the client ever sees it, so it applies. */
	sub[0] = instance;
	sub[1] = TCPPort_SubPanel(instance, instance, "Debug", 380, 176, 460, 540);
	sub[2] = TCPPort_SubPanel(instance, instance, "SSL",   380, 149, 440, 460);
	sub[3] = TCPPort_SubPanel(instance, instance, "Help",   14, 560, HELP_W, HELP_H);

	/* load the README into the Help box when the Help panel is OPENED */
	if (sub[3])
	{
		NodeObj openPort = GetPropNode(sub[3], "ReservedViewOpen");
		if (openPort)
			SetPropLong(openPort, "OnMsg", (long)TCPPort_OnHelpOpen);
	}

	for (i = 0; TCPPortPanel[i].cls; i++)
	{
		TCtl *t = &TCPPortPanel[i];
		NodeObj container = (t->panel >= 0 && t->panel < 4) ? sub[t->panel] : instance;
		if (container)
			TCPPort_Ctl(container, instance, t->cls, t->prop,
						t->x, t->y, t->w, t->h, t->rows, t->cols);
	}
}

/* build the panel one tick after creation - by then the bridge has placed
   this instance and registered its path, so the controls and sub-views
   created inside it resolve. This is the object populating its OWN controls
   the moment it has a location. */
static int TCPPort_BuildTask(NodeObj instance, NodeObj data, int msgid)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) data;
	(void) msgid;

	if (local && !local->panelBuilt)
	{
		local->panelBuilt = 1;
		TCPPort_BuildPanel(instance);

		/* run the placement setup on placement: settle the display and honor
		   Auto Listen. Enable is
		   the only gate on the commands, and it defaults on, so the panel
		   is live - it just opens no socket until Open/Listen is pressed
		   (Auto Close is the default). The panelBuilt guard is already
		   set, so this does not rebuild. */
		TCPPort_Activate(instance, msg_initialize, NULL);
	}

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (local)
	{
		if (local->buildTask)
			RemoveTask(local->buildTask);
		if (local->inner)
			DeleteInstance(local->inner);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);
	NodeObj entry;

	SetName(class, "TCPPort");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	/* the panel, in reading order: connection settings,                   */
	/* the commands, the transmit box, the receive box, then status        */
	PublishProp(ClassSelf, "HostName",     "data", PROP_TEXTBOX, "");
	PublishProp(ClassSelf, "StandardPort", "in",   PROP_MENU, "");
	PublishProp(ClassSelf, "StandardPortList", "data", PROP_NULL, "");
	PublishProp(ClassSelf, "Port",         "data", PROP_TEXTBOX, "80");

	PublishProp(ClassSelf, "AutoOpen",     "data", PROP_CHECKBOX, "0");
	PublishProp(ClassSelf, "AutoListen",   "data", PROP_CHECKBOX, "0");
	PublishProp(ClassSelf, "AutoClose",    "data", PROP_CHECKBOX, "1");

	PublishProp(ClassSelf, "Open",         "in",   PROP_NULL, "");
	PublishProp(ClassSelf, "Listen",       "in",   PROP_NULL, "");
	PublishProp(ClassSelf, "Close",        "in",   PROP_NULL, "");

	entry = PublishProp(ClassSelf, "TxData", "data", PROP_TEXTBOX, "");
	SetPropInt(entry, "Rows", 6);
	SetPropInt(entry, "Cols", 44);
	PublishProp(ClassSelf, "Send",         "in",   PROP_NULL, "");
	PublishProp(ClassSelf, "ClearTx",      "in",   PROP_NULL, "");
	/* these checkboxes are plain data the code READS (GetPropInt), not      */
	/* ports it reacts to - so they must be published "data", or the bridge  */
	/* classifies them message-flowed, never pushes their value, and the box */
	/* on screen disagrees with the state the object is actually in.         */
	PublishProp(ClassSelf, "AutoSend",     "data", PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "ClearOnSend",  "data", PROP_CHECKBOX, "0");

	entry = PublishProp(ClassSelf, "RxData", "data", PROP_TEXTBOX, "");
	SetPropInt(entry, "Rows", 6);
	SetPropInt(entry, "Cols", 44);
	PublishProp(ClassSelf, "ClearRx",      "in",   PROP_NULL, "");
	PublishProp(ClassSelf, "AccumulateRx", "data", PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "BytesReady",   "data", PROP_TEXTOUT, "0");

	PublishProp(ClassSelf, "StreamState",  "data", PROP_TEXTOUT, "IDLING");
	PublishProp(ClassSelf, "Listening",    "data", PROP_LED, "0");
	PublishProp(ClassSelf, "Connected",    "data", PROP_LED, "0");
	PublishProp(ClassSelf, "Idling",       "data", PROP_LED, "1");
	PublishProp(ClassSelf, "Disabled",     "data", PROP_LED, "0");
	PublishProp(ClassSelf, "RxReady",      "data", PROP_LED, "0");
	PublishProp(ClassSelf, "TxReady",      "data", PROP_LED, "1");

	PublishProp(ClassSelf, "In",           "in",   PROP_NULL, "");
	PublishProp(ClassSelf, "Out",          "out",  PROP_NULL, "");
	PublishProp(ClassSelf, "Enable",       "in",   PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "State",        "data", PROP_LED, "1");

	/* the line-end / binary options and the three sub-panels' controls */
	PublishProp(ClassSelf, "TxFixLineEnds", "data", PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "RxFixLineEnds", "data", PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "BinaryTx",      "data", PROP_CHECKBOX, "0");
	PublishProp(ClassSelf, "BinaryRx",      "data", PROP_CHECKBOX, "0");

	entry = PublishProp(ClassSelf, "DebugText", "data", PROP_TEXTBOX, "");
	SetPropInt(entry, "Rows", 16);
	SetPropInt(entry, "Cols", 46);
	PublishProp(ClassSelf, "ErrorMsgs",    "data", PROP_CHECKBOX, "0");
	PublishProp(ClassSelf, "TraceMsgs",    "data", PROP_CHECKBOX, "0");
	PublishProp(ClassSelf, "DebugMsgs",    "data", PROP_CHECKBOX, "0");
	PublishProp(ClassSelf, "ShowTxData",   "data", PROP_CHECKBOX, "0");
	PublishProp(ClassSelf, "TraceRxData",  "data", PROP_CHECKBOX, "0");
	PublishProp(ClassSelf, "ClearDebug",   "in",   PROP_NULL, "");
	PublishProp(ClassSelf, "OpenLed",      "data", PROP_LED, "0");
	PublishProp(ClassSelf, "OpeningLed",   "data", PROP_LED, "0");
	PublishProp(ClassSelf, "ListenLed",    "data", PROP_LED, "0");
	PublishProp(ClassSelf, "ListeningLed", "data", PROP_LED, "0");
	PublishProp(ClassSelf, "CloseLed",     "data", PROP_LED, "1");
	PublishProp(ClassSelf, "ClosingLed",   "data", PROP_LED, "0");

	PublishProp(ClassSelf, "SslEnable",    "in",   PROP_CHECKBOX, "0");
	PublishProp(ClassSelf, "SslStatus",    "data", PROP_LED, "0");
	entry = PublishProp(ClassSelf, "SslCert", "data", PROP_TEXTBOX, "");
	SetPropInt(entry, "Rows", 5);
	SetPropInt(entry, "Cols", 42);
	entry = PublishProp(ClassSelf, "SslKey", "data", PROP_TEXTBOX, "");
	SetPropInt(entry, "Rows", 5);
	SetPropInt(entry, "Cols", 42);
	entry = PublishProp(ClassSelf, "SslPass", "data", PROP_TEXTBOX, "");
	SetPropInt(entry, "Rows", 1);
	SetPropInt(entry, "Cols", 42);

	/* no HelpText property: the Help box loads README.md from disk on open */

	/* ---------------------------------------------------------------- */
	/* The layout, control for control: same x, y, w, h and the same     */
	/* four panels, with the section titles as labels.                    */
	/* the panel is not declared here - this widget BUILDS it
	   (TCPPort_BuildPanel): a View with controls in it, exactly what a
	   user nesting views by hand would make. */

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

	SetName(temp, "TCPPort");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "e71a5c30-9b64-42df-8a17-c05e93d6f428");
	SetPropStr(temp, "Version", "1.0");
	/* it drives a TCP instance, but loads without one - the dropdown of   */
	/* an absent engine simply reports the class is missing when pressed   */
	SetPropStr(temp, "Dependencies", "TCP");
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
