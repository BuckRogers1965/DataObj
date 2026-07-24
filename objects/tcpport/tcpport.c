
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"

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
static WidgetItem TCPPortPanel[];

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
	Widget_BuildOnce(instance, TCPPortPanel);

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


int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData *local = malloc(sizeof(InstanceData));

	(void) message; (void) data;

	local->enabled = 1;
	local->inner = NULL;

	instance = NewNode(INTEGER);
	SetName(instance, "TCPPort");

	/* every published control's initial value straight from the table - a
	   reactive port where the row names a handler, a plain property otherwise */
	Widget_Init(instance, TCPPortPanel);

	/* the internals with no on-screen control: the backing list, the return
	   value, this instance's C state, and the extra ports (dataflow + the
	   inner engine's replies). More handlers just get added here. */
	SetPropStr(instance, "StandardPortList", "FTP,TELNET,SMTP,HTTP,POP,HTTPS");
	SetPropInt(instance, "Out", 0);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)TCPPort_Activate);
	Widget_Port(instance, "In",      "", (void *)TCPPort_OnIn);
	Widget_Port(instance, "InnerRx", "", (void *)TCPPort_OnInnerRx);
	Widget_Port(instance, "InnerUp", "", (void *)TCPPort_OnInnerUp);

	/* Auto Send is driven by the Transmit box CHANGING: an internal port wired
	   to TxData's own change, so typing in the box and writing it through In
	   both auto-send the same way. Not published - plumbing, not a control. */
	Widget_Port(instance, "TxChanged", "", (void *)TCPPort_OnTxChanged);
	Connect(instance, "TxData", instance, "TxChanged");

	InitPosition(instance);

	/* the view's OWN size, set here as a resting value BEFORE any client can
	   subscribe - a size set later in the deferred build would shadow the W/H
	   node the client's tap is already on and never reach it. */
	Widget_MainSize(instance, TCPPortPanel);

	RegisterInstance(class, instance);

	/* the panel is built one tick from now, after the bridge has given this
	   instance its path (building now would refuse - no location yet) */
	Widget_DeferBuild(instance, TCPPortPanel);

	return rtrn_handled;
}

/* The whole widget in one table: panels (View/Help rows) and controls.
   Widget_Publish (ClassStart) publishes a property per control - widget
   type from the control class; Widget_BuildTable (deferred) lays it out.
   Panels number by View/Help order: main 0, Help 1, Settings 2, Debug 3,
   SSL 4. A control's w/h ARE its size. */
static WidgetItem TCPPortPanel[] = {
	/* cls          prop            def         panel   x    y    w    h   label         [handler] */
	{ "View",     "TCPPort", "",         0,   0,   0, 460, 430, 0 },   /* 0: main - size applied in InstanceStart */
	{ "Help",     "objects/tcpport/README.md", "", 
                                            0, 0, 0, 0, 0, 0 }, /* 1: help - ALWAYS second */
	{ "View",     "Settings",   "",         0,  85, 370, 420, 280, 0 },	  /* 2: settings (config) */
	{ "View",     "Debug",      "",         0, 155, 370, 460, 540, 0 },   /* 3 */
	{ "View",     "SSL",        "",         0, 225, 370, 440, 460, 0 },   /* 4 */

	/* --- panel 0: main (operate) - connection control, status, I/O --- */
	{ "Checkbox", "Enable",      "1",       0, 432,  12,   8,  8, LABEL_LEFT,   (void *)TCPPort_OnEnable },
	{ "TextOut",  "StreamState", ST_IDLING, 0,  15,  14, 150, 15, LABEL_LEFT },
	{ "LED",      "Listening",   "0",       0,  15,  42,  10, 10, LABEL_BOTTOM },
	{ "LED",      "Connected",   "0",       0,  80,  42,  10, 10, LABEL_BOTTOM },
	{ "LED",      "Idling",      "1",       0, 150,  42,  10, 10, LABEL_BOTTOM },
	{ "LED",      "Disabled",    "0",       0, 210,  42,  10, 10, LABEL_BOTTOM },
	{ "LED",      "State",       "1",       0, 275,  42,  10, 10, LABEL_BOTTOM },
	{ "MoButton", "Open",        "0",       0,  15,  82,  60, 20, LABEL_NONE,   (void *)TCPPort_OnOpen },
	{ "MoButton", "Listen",      "0",       0,  85,  82,  60, 20, LABEL_NONE,   (void *)TCPPort_OnListen },
	{ "MoButton", "Close",       "0",       0, 155,  82,  60, 20, LABEL_NONE,   (void *)TCPPort_OnClose },
	{ "Textbox",  "TxData",      "",        0,  15, 130, 320, 94, LABEL_TOP },
	{ "MoButton", "Send",        "0",       0, 345, 130,  60, 20, LABEL_NONE,   (void *)TCPPort_OnSend },
	{ "MoButton", "ClearTx",     "0",       0, 345, 160,  60, 20, LABEL_NONE,   (void *)TCPPort_OnClearTx },
	{ "LED",      "TxReady",     "1",       0, 345, 196,  10, 10, LABEL_RIGHT },
	{ "Textbox",  "RxData",      "",        0,  15, 242, 320, 94, LABEL_TOP },
	{ "MoButton", "ClearRx",     "0",       0, 345, 242,  60, 20, LABEL_NONE,   (void *)TCPPort_OnClearRx },
	{ "LED",      "RxReady",     "0",       0, 345, 278,  10, 10, LABEL_RIGHT },
	{ "TextOut",  "BytesReady",  "0",       0, 345, 302,  80, 15, LABEL_LEFT },

	/* --- panel 2: Settings - connection config + tx/rx/auto options --- */
	{ "Textbox",  "HostName",      "",   2,  15,  20, 300, 15, LABEL_TOP },
	{ "Dropdown", "StandardPort",  "",   2,  15,  62,  85, 15, LABEL_TOP,   (void *)TCPPort_OnStandardPort },
	{ "Textbox",  "Port",          "80", 2, 120,  62,  63, 15, LABEL_TOP },
	{ "Checkbox", "AutoOpen",      "0",  2,  15, 110,   8,  8, LABEL_RIGHT },
	{ "Checkbox", "AutoListen",    "0",  2, 130, 110,   8,  8, LABEL_RIGHT },
	{ "Checkbox", "AutoClose",     "1",  2, 260, 110,   8,  8, LABEL_RIGHT },
	{ "Checkbox", "BinaryTx",      "0",  2,  15, 150,   8,  8, LABEL_RIGHT },
	{ "Checkbox", "TxFixLineEnds", "1",  2, 130, 150,   8,  8, LABEL_RIGHT },
	{ "Checkbox", "ClearOnSend",   "0",  2, 280, 150,   8,  8, LABEL_RIGHT },
	{ "Checkbox", "AutoSend",      "1",  2,  15, 185,   8,  8, LABEL_RIGHT },
	{ "Checkbox", "BinaryRx",      "0",  2, 130, 185,   8,  8, LABEL_RIGHT },
	{ "Checkbox", "RxFixLineEnds", "1",  2, 260, 185,   8,  8, LABEL_RIGHT },
	{ "Checkbox", "AccumulateRx",  "1",  2,  15, 220,   8,  8, LABEL_RIGHT },

	/* --- panel 3: Debug --- */
	{ "LED",      "OpenLed",      "0", 3,  32,  63,  10, 10, LABEL_RIGHT },
	{ "LED",      "OpeningLed",   "0", 3,  80,  63,  10, 10, LABEL_RIGHT },
	{ "LED",      "ListenLed",    "0", 3, 134,  63,  10, 10, LABEL_RIGHT },
	{ "LED",      "ListeningLed", "0", 3, 185,  63,  10, 10, LABEL_RIGHT },
	{ "LED",      "CloseLed",     "1", 3, 239,  63,  10, 10, LABEL_RIGHT },
	{ "LED",      "ClosingLed",   "0", 3, 286,  63,  10, 10, LABEL_RIGHT },
	{ "Checkbox", "ErrorMsgs",    "0", 3,  34, 127,   8,  8, LABEL_RIGHT },
	{ "Checkbox", "TraceMsgs",    "0", 3,  81, 127,   8,  8, LABEL_RIGHT },
	{ "Checkbox", "ShowTxData",   "0", 3, 135, 127,   8,  8, LABEL_RIGHT },
	{ "Checkbox", "TraceRxData",  "0", 3, 186, 127,   8,  8, LABEL_RIGHT },
	{ "Checkbox", "DebugMsgs",    "0", 3, 241, 127,   8,  8, LABEL_RIGHT },
	{ "MoButton", "ClearDebug",   "0", 3, 315, 125,  60, 20, LABEL_NONE,   (void *)TCPPort_OnClearDebug },
	{ "Textbox",  "DebugText",    "",  3,  13, 175, 365, 275, LABEL_TOP },

	/* --- panel 4: SSL --- */
	{ "Checkbox", "SslEnable",    "0", 4,  12,  14,   8,  8, LABEL_RIGHT,  (void *)TCPPort_OnSslEnable },
	{ "LED",      "SslStatus",    "0", 4,  12,  40,  10, 10, LABEL_RIGHT },
	{ "Textbox",  "SslCert",      "",  4,  12,  59, 349, 87, LABEL_TOP },
	{ "Textbox",  "SslKey",       "",  4,  12, 177, 349, 87, LABEL_TOP },
	{ "Textbox",  "SslPass",      "",  4,  13, 295, 348, 32, LABEL_TOP },

	{ NULL }
};

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	Widget_CancelBuild(instance);		/* drop a still-pending deferred build */

	if (local)
	{
		if (local->inner)
			DeleteInstance(local->inner);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "TCPPort");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	/* every on-screen control, straight from the layout table - the widget
	   type comes from each control's class, so it is never restated here */
	Widget_Publish(ClassSelf, TCPPortPanel);

	/* the three with no on-screen control: the two dataflow ports and the
	   dropdown's backing list */
	PublishProp(ClassSelf, "In",               "data", PROP_NULL, "");
	PublishProp(ClassSelf, "Out",              "data", PROP_NULL, "");
	PublishProp(ClassSelf, "StandardPortList", "data", PROP_NULL, "");

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
