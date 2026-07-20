
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"

/*

TCPPort object: the TCP instrument panel, ported from the VNOS control
of the same name (objects/demo/tcpport - tcpport.c and its panel
definition tcpportpb.c). It is a FRONT END, not a networking object:
it holds no socket of its own, it CONTAINS an instance of the TCP
engine object and drives it - exactly the shell/engine split ScriptBox
already uses for language hosts.

	VNOS                          here
	----                          ----
	tcpportpb.c ControlInfo[]     the published Interface (one row per
	                              control, its widget type and default)
	Cmd* variables                ordinary IN PORTS - so a Pulse or a
	                              script can press Open/Listen/Send
	                              exactly as the panel's MoButtons do
	Dsp* LEDs                     PROP_LED properties
	Opt* checkboxes               PROP_CHECKBOX properties
	the socket state machine      the TCP object underneath

The controls keep their old names and behavior: Host Name, Port (with
the Standard Ports menu writing it), Enable, Open/Listen/Close, the
Transmit and Receive boxes with Send / Clear Tx / Clear Rx, the option
checkboxes (Auto Open/Listen/Close - mutually exclusive as they were,
Auto Send, Clear On Send, Accumulate Data), the Stream State readout
and the status LEDs.

Dataflow, as the original help.txt states it: "Default input connection
is to Transmit Data. Default output connection is to Receive Data." So
In feeds TxData and Out carries what arrives on RxData, and the widget
drops into a flow like any other object.

*/

/* the Help panel's text, rendered by the Markdown widget */
#define TCPPORT_HELP \
	"# TCP Port\n" \
	"A TCP object for transmitting and receiving data on a configured\n" \
	"port, including those used by HTTP, FTP, SMTP, Telnet, POP and HTTPS.\n" \
	"\n" \
	"Default input connection is to **Transmit Data**.\n" \
	"Default output connection is to **Receive Data**.\n" \
	"\n" \
	"## Controls\n" \
	"- **Enable** - prepares the port to send and receive. Unchecking is a\n" \
	"  full stop: it closes the socket and DEACTIVATES the object.\n" \
	"- **Host Name** - the URL or IP of the server, with no protocol\n" \
	"  prefix (`www.hostname.com`, not `http://www.hostname.com`).\n" \
	"- **Standard Ports** - FTP 21, Telnet 23, SMTP 25, HTTP 80, POP 110,\n" \
	"  HTTPS 443; picking one writes the Port box.\n" \
	"- **Listen / Open / Close** - Listen serves on Port. The panel must\n" \
	"  be ACTIVATED first: an inert object never opens a socket.\n" \
	"- **Send** - transmits the Transmit box when connected.\n" \
	"- **Stream State** - DISABLED, IDLING, LISTEN, OPENING, CONNECTED,\n" \
	"  CLOSING.\n"

/* Stream State, the DspStateNames of the original panel */
#define ST_DISABLED   "DISABLED"
#define ST_IDLING     "IDLING"
#define ST_LISTEN     "LISTEN"
#define ST_OPENING    "OPENING"
#define ST_CONNECTED  "CONNECTED"
#define ST_CLOSING    "CLOSING"

typedef struct InstanceData
{
	int     active;
	int     enabled;
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

/* the original's PropDomain_change, verbatim in behavior: strip leading  */
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
/* one clears the other two (the original's OptAuto*_change)              */
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

	/* ACTIVATION IS THE ARMING: an inert object does not open a socket,   */
	/* however its buttons are pressed. Activate makes it live, Enable=0   */
	/* takes it back down (TCPPort_OnEnable) - the Reader/TCP lifecycle.   */
	if (!local || !local->enabled || !local->active)
		return;
	if (TCPPort_IsState(instance, ST_LISTEN) || TCPPort_IsState(instance, ST_CONNECTED))
		return;

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
/* goes out, the poll task stops) - the original's CmdClose               */
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
	if (!local->enabled || !local->active)
		return rtrn_handled;		/* inert until Activated, as Listen is */

	if (TCPPort_IsState(instance, ST_LISTEN) || TCPPort_IsState(instance, ST_CONNECTED))
		return rtrn_handled;

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

/* SSL: the engine does the TLS (the VNOS reference was a SECURE object   */
/* and tcp.c now carries its cert/key/session handling). The panel just   */
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

/* the Standard Ports menu writes the Port box - the original's           */
/* OptSockets_change, with the same six services                          */
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
/* replacing, the original's Accumulate Data option) and out our own Out  */
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
		if (local && local->inner && local->active && local->enabled
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
/* Transmit Data" - and Auto Send sends it on arrival                      */
int TCPPort_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *value;

	if (!local || !local->enabled || message == msg_eof)
		return rtrn_dropped;

	value = data ? GetValueStr(data) : NULL;
	SetPropStr(instance, "TxData", value ? value : "");
	SetPropStr(instance, "TxReady", "1");

	if (GetPropInt(instance, "AutoSend"))
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
		/* unenabling DEACTIVATES: the socket goes, and so does the       */
		/* object's live state - it must be Activated again to listen      */
		TCPPort_DoClose(instance);
		local->active = 0;
		SetPropInt(instance, "State", Stopping);
		TCPPort_SetState(instance, ST_DISABLED);
		return rtrn_handled;
	}

	/* re-enabling does NOT bring the socket back by itself: activation is */
	/* the arming, and unenabling deactivated. Activate it again to listen. */
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

/* Activate: normalize the host box and honor the Auto option - the same  */
/* thing the original did when its panel came up enabled                   */
int TCPPort_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	local->active = 1;
	SetPropInt(instance, "State", Running);

	TCPPort_NormalizeHost(instance);

	/* settle the three Auto boxes onto the one that wins, so the panel    */
	/* shows the radio behavior the original had */
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

	local->active = 0;
	local->enabled = 1;
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

	/* options - Auto Close is the original's default of the three. These  */
	/* are plain properties (see TCPPort_AutoChoice): a checkbox must be   */
	/* readable and must announce, and a port can do neither.              */
	SetPropStr(instance, "AutoOpen", "0");
	SetPropStr(instance, "AutoListen", "0");
	SetPropStr(instance, "AutoClose", "1");
	SetPropStr(instance, "AutoSend", "1");
	SetPropStr(instance, "ClearOnSend", "0");
	SetPropStr(instance, "AccumulateRx", "1");

	/* line-end and binary options (the original's Tx/Rx Fix Line Ends,    */
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

	/* the Help panel - the object's own documentation, rendered */
	SetPropStr(instance, "HelpText", TCPPORT_HELP);

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

	InitPosition(instance);

	RegisterInstance(class, instance);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

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
	NodeObj entry;

	SetName(class, "TCPPort");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	/* the panel, in the original's reading order: connection settings,    */
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
	PublishProp(ClassSelf, "AutoSend",     "in",   PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "ClearOnSend",  "in",   PROP_CHECKBOX, "0");

	entry = PublishProp(ClassSelf, "RxData", "data", PROP_TEXTBOX, "");
	SetPropInt(entry, "Rows", 6);
	SetPropInt(entry, "Cols", 44);
	PublishProp(ClassSelf, "ClearRx",      "in",   PROP_NULL, "");
	PublishProp(ClassSelf, "AccumulateRx", "in",   PROP_CHECKBOX, "1");
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
	PublishProp(ClassSelf, "TxFixLineEnds", "in", PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "RxFixLineEnds", "in", PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "BinaryTx",      "in", PROP_CHECKBOX, "0");
	PublishProp(ClassSelf, "BinaryRx",      "in", PROP_CHECKBOX, "0");

	entry = PublishProp(ClassSelf, "DebugText", "data", PROP_TEXTBOX, "");
	SetPropInt(entry, "Rows", 16);
	SetPropInt(entry, "Cols", 46);
	PublishProp(ClassSelf, "ErrorMsgs",    "in",   PROP_CHECKBOX, "0");
	PublishProp(ClassSelf, "TraceMsgs",    "in",   PROP_CHECKBOX, "0");
	PublishProp(ClassSelf, "DebugMsgs",    "in",   PROP_CHECKBOX, "0");
	PublishProp(ClassSelf, "ShowTxData",   "in",   PROP_CHECKBOX, "0");
	PublishProp(ClassSelf, "TraceRxData",  "in",   PROP_CHECKBOX, "0");
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

	PublishProp(ClassSelf, "HelpText",     "data", PROP_MARKDOWN, "");

	/* ---------------------------------------------------------------- */
	/* The layout, control for control, from the original panel          */
	/* (objects/demo/tcpport/tcpportpb.c ControlInfo[]): same x, y, w, h */
	/* and the same four panels, with the original titles as labels.     */
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
