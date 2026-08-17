
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"

/*

TPLink: a front end for a TP-Link "smart plug" style device (HS100/HS110
and the like). Like TCPPort, it holds no socket of its own - it CONTAINS
an instance of the TCP engine object, in client mode, and drives it. The
panel: HostName/Port live in a Settings sub-view, the main panel shows
Status (the relay's own on/off) and NetStatus (what the network side of
the widget is doing right now), and On/Off/Toggle/Refresh are ordinary
in ports, so a Pulse or a script can press them exactly as the panel's
buttons do.

Wire protocol: these devices speak a trivial single-byte XOR "autokey"
cipher over TCP (default port 9999) - a 4-byte big-endian length, then
that many encrypted bytes; encrypting byte i XORs it with the PREVIOUS
CIPHERTEXT byte (171 for the first byte), and decrypting runs the same
key schedule in reverse. The plaintext is a small JSON command/reply,
e.g. {"system":{"get_sysinfo":{}}} to ask for status, and
{"system":{"set_relay_state":{"state":1}}} to turn it on. One connect,
one command, one reply, then closed - the widget hides that lifecycle,
creating a fresh inner TCP instance per operation the same way TCPPort's
Listen replaces its own.

*/

#define TPLINK_TIMEOUT_MS  6000

enum { PEND_NONE, PEND_ON, PEND_OFF, PEND_INFO };

#define NS_DISABLED "Disabled"
#define NS_IDLE     "Idle"
#define NS_CONNECT  "Connecting"
#define NS_WAIT     "Waiting for reply"

typedef struct InstanceData
{
	int     enabled;
	NodeObj inner;			/* the TCP engine instance, or NULL between ops */
	TaskObj timeoutTask;	/* one-shot: an op that never replies gives up  */
	int     pending;		/* PEND_* - what a live connect is FOR */
	char   *rxbuf;
	size_t  rxlen, rxcap;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;
static WidgetItem TPLinkPanel[];

int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint("TPLink handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* ---- the device's own cipher: single-byte XOR, key = previous ciphertext byte ---- */

static void TPLink_Encrypt(const char *plain, int len, unsigned char *out)
{
	unsigned char key = 171;
	int i;

	out[0] = (unsigned char)((len >> 24) & 0xFF);
	out[1] = (unsigned char)((len >> 16) & 0xFF);
	out[2] = (unsigned char)((len >> 8) & 0xFF);
	out[3] = (unsigned char)(len & 0xFF);

	for (i = 0; i < len; i++)
	{
		unsigned char a = key ^ (unsigned char)plain[i];
		key = a;
		out[4 + i] = a;
	}
}

static void TPLink_Decrypt(const unsigned char *enc, int len, char *out)
{
	unsigned char key = 171;
	int i;

	for (i = 0; i < len; i++)
	{
		unsigned char a = key ^ enc[i];
		key = enc[i];
		out[i] = (char)a;
	}
	out[len] = 0;
}

/* find "key":N in a small JSON reply - the device's own replies are flat
   enough that a substring search beats pulling in a JSON parser for it */
static int TPLink_FindInt(const char *json, const char *key)
{
	char pat[64];
	const char *p;

	snprintf(pat, sizeof(pat), "\"%s\":", key);
	p = strstr(json, pat);
	if (!p)
		return -1;
	p += strlen(pat);
	while (*p == ' ')
		p++;
	return atoi(p);
}

static void TPLink_SetNet(NodeObj instance, char *s)
{
	SetPropStr(instance, "NetStatus", s);
}

/* ---- lifecycle of ONE operation: connect, send, wait for the reply ---- */

static void TPLink_TearDown(InstanceData *local)
{
	if (local->inner)
	{
		DeleteInstance(local->inner);
		local->inner = NULL;
	}
	local->pending = PEND_NONE;
	local->rxlen = 0;
}

static int TPLink_OnTimeout(NodeObj instance, NodeObj taskdata, int reason)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) taskdata;
	if (reason == task_deactivate || !local)
		return rtrn_handled;

	if (local->pending != PEND_NONE)
	{
		TPLink_SetNet(instance, "Error: timed out");
		TPLink_TearDown(local);
	}
	return rtrn_handled;
}

static void TPLink_ArmTimeout(NodeObj instance, InstanceData *local)
{
	/* always a brand new task, never a reused one re-armed from outside its
	   own callback - a leftover from a prior op (still pending if it hasn't
	   fired yet) is retired first, so it can never later fire against a
	   newer, unrelated operation. */
	if (local->timeoutTask)
	{
		DeleteTask(local->timeoutTask);
		local->timeoutTask = NULL;
	}
	local->timeoutTask = CreateTask(ObjGetTaskList());
	AddTaskMilli(local->timeoutTask, TPLINK_TIMEOUT_MS, (FuncPtr)TPLink_OnTimeout, msg_send, instance);
}

static void TPLink_SendCommand(NodeObj instance, InstanceData *local)
{
	static const char *cmdOn   = "{\"system\":{\"set_relay_state\":{\"state\":1}}}";
	static const char *cmdOff  = "{\"system\":{\"set_relay_state\":{\"state\":0}}}";
	static const char *cmdInfo = "{\"system\":{\"get_sysinfo\":{}}}";
	const char *json;
	unsigned char enc[256];
	int len;
	NodeObj chunk;

	switch (local->pending)
	{
	case PEND_ON:   json = cmdOn;   break;
	case PEND_OFF:  json = cmdOff;  break;
	case PEND_INFO: json = cmdInfo; break;
	default:        return;
	}

	len = (int)strlen(json);
	TPLink_Encrypt(json, len, enc);

	chunk = NewNode(STRING);
	SetName(chunk, "Data");
	SetValueStrLen(chunk, (char *)enc, len + 4);
	SetPropInt(chunk, "Length", len + 4);
	DeliverMsg(local->inner, "In", msg_send, chunk);

	TPLink_SetNet(instance, NS_WAIT);
}

/* the shared trigger behind On/Off/Toggle/Refresh and the startup check -
   Enable is the only gate, and only one operation runs at a time (a fresh
   inner TCP instance per op, same as TCPPort's Listen replacing its own) */
static void TPLink_Start(NodeObj instance, int which)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *host, *port;
	char dbg[128];

	if (!local)
		return;

	snprintf(dbg, sizeof(dbg), "TPLink_Start: which=%d local->enabled=%d", which, local->enabled);
	DebugPrint(dbg, __FILE__, __LINE__, OBJMSGHANDLING);

	if (!local->enabled)
	{
		DebugPrint("TPLink_Start: BLOCKED - not enabled", __FILE__, __LINE__, OBJMSGHANDLING);
		TPLink_SetNet(instance, "Ignored - not enabled");
		return;
	}
	/* busy is judged by pending, not by local->inner: a finished op (good
	   or bad) may leave a dead, inert inner instance sitting around on
	   purpose (see OnInnerUp/OnInnerRx) - that is not "busy", it is just
	   not cleaned up yet */
	if (local->pending != PEND_NONE)
	{
		TPLink_SetNet(instance, "Busy - try again shortly");
		return;
	}

	host = GetPropStr(instance, "HostName");
	if (!host || !host[0])
	{
		TPLink_SetNet(instance, "Error: HostName is empty (see Settings)");
		return;
	}
	port = GetPropStr(instance, "Port");

	/* replace whatever is left of the last op - safe here: this is a
	   top-level call (a button press, or one of our own one-shot tasks),
	   never inside local->inner's own callback chain, exactly the
	   distinction OnInnerUp's comment draws. Same as TCPPort's Open/Listen
	   replacing its own engine. */
	if (local->inner)
	{
		DeleteInstance(local->inner);
		local->inner = NULL;
	}

	local->inner = CreatePrivate(instance, "TCP", NULL);
	if (!local->inner)
	{
		TPLink_SetNet(instance, "Error: TCP class is not loaded");
		return;
	}

	local->pending = which;
	local->rxlen = 0;

	SetOrDeliverProp(local->inner, "RemoteAddr", host);
	SetOrDeliverProp(local->inner, "RemotePort", (port && port[0]) ? port : "9999");

	Connect(local->inner, "Out", instance, "InnerRx");
	Connect(local->inner, "Connected", instance, "InnerUp");
	ActivateInstance(local->inner);

	TPLink_SetNet(instance, NS_CONNECT);
	/* the backstop for a connect that fails synchronously (tcp.c's client
	   Activate can refuse outright with no Connected event at all) as well
	   as one that simply never gets an answer */
	TPLink_ArmTimeout(instance, local);
}

/* ---- the engine's replies ---- */

int TPLink_OnInnerUp(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message == msg_eof)
		return rtrn_handled;

	if (GetValueInt(data))
		TPLink_SendCommand(instance, local);
	else
	{
		/* connect failed - do NOT DeleteInstance(local->inner) here. This
		   handler runs SYNCHRONOUSLY, inline, from inside local->inner's
		   OWN currently-executing poll task (Connected is a plain property;
		   a property write's subscriber fan-out is synchronous, unlike
		   SndMsg, which queues and never nests inside the sender's call
		   stack). Freeing the instance now frees the task/instance data
		   that poll is still using once this call returns into it -
		   exactly the corruption that took down the whole scheduler.
		   TCPPort's own OnInnerUp has the identical shape: just report it
		   and go idle. The dead instance is replaced, safely, the next
		   time TPLink_Start actually needs a fresh one. */
		TPLink_SetNet(instance, "Error: connect failed");
		local->pending = PEND_NONE;
	}
	return rtrn_handled;
}

int TPLink_OnInnerRx(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *str;
	int length;
	unsigned int declared;
	char plain[1024];

	if (!local)
		return rtrn_dropped;

	if (message == msg_eof)
	{
		/* a good reply already tore local->pending back down to NONE below -
		   the peer closing AFTER that is the device's normal behavior, not
		   a failure. Closing before we ever got a full reply IS one. Either
		   way, this arrives via SndMsg (queued - never nested inside the
		   sender's own call stack), so it's always safe to react here; we
		   still don't delete local->inner reactively (see OnInnerUp), just
		   mark the operation over. */
		if (local->pending != PEND_NONE)
		{
			TPLink_SetNet(instance, "Error: connection closed early");
			local->pending = PEND_NONE;
		}
		return rtrn_handled;
	}

	/* SndMsg owns data and frees it after every subscriber sees it - copy
	   what we need, a reply can arrive split across more than one chunk */
	str = GetValueStr(data);
	length = GetPropInt(data, "Length");
	if (!length && str)
		length = (int)strlen(str);
	if (!str || length <= 0)
		return rtrn_handled;

	if (local->rxlen + (size_t)length + 1 > local->rxcap)
	{
		size_t ncap = (local->rxlen + (size_t)length + 1) * 2;
		char *nb = realloc(local->rxbuf, ncap);
		if (!nb)
			return rtrn_handled;
		local->rxbuf = nb;
		local->rxcap = ncap;
	}
	memcpy(local->rxbuf + local->rxlen, str, (size_t)length);
	local->rxlen += (size_t)length;

	if (local->rxlen < 4)
		return rtrn_handled;

	declared = ((unsigned char)local->rxbuf[0] << 24) | ((unsigned char)local->rxbuf[1] << 16)
			 | ((unsigned char)local->rxbuf[2] << 8)  | (unsigned char)local->rxbuf[3];

	if (declared >= sizeof(plain) || local->rxlen < 4 + declared)
		return rtrn_handled;	/* not all here yet (or an absurd length - drop it) */

	TPLink_Decrypt((unsigned char *)local->rxbuf + 4, (int)declared, plain);

	if (local->pending == PEND_INFO)
	{
		int rs = TPLink_FindInt(plain, "relay_state");
		if (rs == 0 || rs == 1)
		{
			SetPropInt(instance, "Status", rs);
			TPLink_SetNet(instance, NS_IDLE);
		}
		else
			TPLink_SetNet(instance, "Error: no relay_state in reply");
	}
	else
	{
		if (TPLink_FindInt(plain, "err_code") == 0)
		{
			SetPropInt(instance, "Status", local->pending == PEND_ON ? 1 : 0);
			TPLink_SetNet(instance, NS_IDLE);
		}
		else
			TPLink_SetNet(instance, "Error: device refused the command");
	}

	/* this arrives via SndMsg (queued), so deleting local->inner here would
	   in fact be safe (unlike OnInnerUp's Connected callback) - left alive
	   anyway, on purpose, for the same reason TCPPort's own OnInnerRx never
	   tears its engine down on a good reply: one uniform rule, "only
	   TPLink_Start/Enable-off/the timeout ever delete local->inner", is
	   easier to keep correct than one that's safe here but not there. */
	local->pending = PEND_NONE;
	local->rxlen = 0;
	return rtrn_handled;
}

/* ---- the panel's own commands: ordinary in ports, so a Pulse or a script
   presses them exactly as the panel's MoButtons do ---- */

static void TPLink_DbgButton(char *who, MsgId message, NodeObj data)
{
	char dbg[160];
	snprintf(dbg, sizeof(dbg), "%s: called, message=%d data-value=%s", who,
			 message, data ? GetValueStr(data) : "(null)");
	DebugPrint(dbg, __FILE__, __LINE__, OBJMSGHANDLING);
}

int TPLink_OnOn(NodeObj instance, MsgId message, NodeObj data)
{
	TPLink_DbgButton("TPLink_OnOn", message, data);
	if (message == msg_eof || !GetValueInt(data))
		return rtrn_handled;
	TPLink_Start(instance, PEND_ON);
	return rtrn_handled;
}

int TPLink_OnOff(NodeObj instance, MsgId message, NodeObj data)
{
	TPLink_DbgButton("TPLink_OnOff", message, data);
	if (message == msg_eof || !GetValueInt(data))
		return rtrn_handled;
	TPLink_Start(instance, PEND_OFF);
	return rtrn_handled;
}

int TPLink_OnToggle(NodeObj instance, MsgId message, NodeObj data)
{
	TPLink_DbgButton("TPLink_OnToggle", message, data);
	if (message == msg_eof || !GetValueInt(data))
		return rtrn_handled;
	TPLink_Start(instance, GetPropInt(instance, "Status") ? PEND_OFF : PEND_ON);
	return rtrn_handled;
}

int TPLink_OnRefresh(NodeObj instance, MsgId message, NodeObj data)
{
	TPLink_DbgButton("TPLink_OnRefresh", message, data);
	if (message == msg_eof || !GetValueInt(data))
		return rtrn_handled;
	TPLink_Start(instance, PEND_INFO);
	return rtrn_handled;
}

/* control callback: 1 enables, 0 is a full stop - whatever operation is in
   flight is abandoned, same as TCPPort's Enable. Enabling does not check
   status on its own - press Refresh (or On/Off/Toggle) for that. */
int TPLink_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char dbg[160];

	snprintf(dbg, sizeof(dbg), "TPLink_OnEnable: called, message=%d data-value=%s local=%p",
			 message, data ? GetValueStr(data) : "(null)", (void *)local);
	DebugPrint(dbg, __FILE__, __LINE__, OBJMSGHANDLING);

	if (!local || message == msg_eof)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	snprintf(dbg, sizeof(dbg), "TPLink_OnEnable: local->enabled is now %d", local->enabled);
	DebugPrint(dbg, __FILE__, __LINE__, OBJMSGHANDLING);

	if (!local->enabled)
	{
		TPLink_TearDown(local);
		TPLink_SetNet(instance, NS_DISABLED);
		return rtrn_handled;
	}

	TPLink_SetNet(instance, NS_IDLE);
	return rtrn_handled;
}

/* Placement setup: build the panel and show the resting state. No network
   op runs on its own here - every loaded class gets a real instance as its
   palette icon (BuildPalette, object.c), so anything done here would run
   on the palette's own seed too, at every boot, before anyone ever drags
   one out. Status is only ever learned by pressing Refresh (or On/Off/
   Toggle). */
int TPLink_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) message; (void) data;
	if (!local)
		return rtrn_dropped;

	TPLink_SetNet(instance, local->enabled ? NS_IDLE : NS_DISABLED);

	return rtrn_handled;
}

/* ---- the whole widget in one table (see widget.h) ---- */

static WidgetItem TPLinkPanel[] = {
	/* cls          prop           def             panel  x    y   w    h  label        [handler] */
	{ "View",     "TPLink",        "",              0,   0,   0, 310, 220, 0 },
	{ "Help",     "objects/tplink/README.md", "",   0,   0,   0,   0,   0, 0 },
	{ "View",     "Settings",      "",              0,  85, 140, 270, 160, 0 },

	/* --- main panel: status + commands --- */
	{ "Checkbox", "Enable",     "1",   0, 200,  12,  8,  8, LABEL_LEFT,  (void *)TPLink_OnEnable },
	{ "TextOut",  "NetStatus",  NS_IDLE, 0, 15,  14, 190, 20, LABEL_LEFT },
	{ "LED",      "Status",     "0",   0,  15,  45, 10, 10, LABEL_BOTTOM },
	{ "MoButton", "On",         "0",   0,  15,  85, 55, 22, LABEL_NONE,  (void *)TPLink_OnOn },
	{ "MoButton", "Off",        "0",   0,  78,  85, 55, 22, LABEL_NONE,  (void *)TPLink_OnOff },
	{ "MoButton", "Toggle",     "0",   0, 141,  85, 55, 22, LABEL_NONE,  (void *)TPLink_OnToggle },
	{ "MoButton", "Refresh",    "0",   0, 204,  85, 48, 22, LABEL_NONE,  (void *)TPLink_OnRefresh },

	/* --- Settings sub-panel: what to connect to --- */
	{ "Textbox",  "HostName",   "192.168.4.230", 2, 15, 20, 210, 20, LABEL_TOP },
	{ "Textbox",  "Port",       "9999",          2, 15, 65,  90, 20, LABEL_TOP },

	{ NULL }
};

/* ---- lifecycle ---- */

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData *local = malloc(sizeof(InstanceData));

	(void) message; (void) data;

	local->enabled = 1;
	local->inner = NULL;
	local->timeoutTask = NULL;
	local->pending = PEND_NONE;
	local->rxbuf = NULL;
	local->rxlen = 0;
	local->rxcap = 0;

	instance = NewNode(INTEGER);
	SetName(instance, "TPLink");

	Widget_Init(instance, TPLinkPanel);

	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)TPLink_Activate);
	Widget_Port(instance, "InnerRx", "", (void *)TPLink_OnInnerRx);
	Widget_Port(instance, "InnerUp", "", (void *)TPLink_OnInnerUp);

	InitPosition(instance);
	Widget_MainSize(instance, TPLinkPanel);

	RegisterInstance(class, instance);

	/* placed where it was told, under the name it was given, panel and all */
	Widget_Place(instance, data, TPLinkPanel);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) message; (void) data;

	if (local)
	{
		if (local->timeoutTask)
			DeleteTask(local->timeoutTask);
		if (local->inner)
			DeleteInstance(local->inner);
		if (local->rxbuf)
			free(local->rxbuf);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	(void) message; (void) data;
	SetName(class, "TPLink");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Widget");

	PublishPosition(ClassSelf);
	Widget_Publish(ClassSelf, TPLinkPanel);

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

	SetName(temp, "TPLink");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "d2e38bce-8a0c-4e3b-b740-d6642a2b37eb");
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
	AddDependency(temp, "mobutton.object", "MoButton", "1", "0");
	AddDependency(temp, "textbox.object", "Textbox", "1", "0");
	AddDependency(temp, "textout.object", "TextOut", "1", "0");
	AddDependency(temp, "view.object", "View", "1", "0");

	/* created in code, not from the layout table */
	AddDependency(temp, "tcpshim.object", "TCP", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
