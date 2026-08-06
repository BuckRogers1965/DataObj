
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"
#include "../udp/udp.h"	/* the UDP object's interface - all this widget knows of it */

/* THIS PANEL'S id for replies from its UDP object, as the reference widget
   picks its own (UDP_CALLBACK 0x5001). It is the owner's number, not the
   object's: a panel driving two objects gives each a different one and its
   handler tells the replies apart. */
#define UDP_CALLBACK	0x5001

/*

UDPPort object: the UDP instrument panel. It holds no socket - it creates a
UDP engine instance and drives it, the way the reference widget's
OnInitialize does:

	pData->udpInstance = New(GetNamedClass("UDP"), UDP_CALLBACK, pData->pDev);

The engine is PRIVATE STATE, not a member of the panel: it has no location,
no path, and nothing on the canvas. It is instantiated straight through its
class's InstanceStart, kept on this instance's own C struct, and destroyed
with it. The panel talks to it only through its message interface - Enable
and Send out, Received back into this panel's Callback port, which is the
UDP_CALLBACK the reference hands over at instantiation.

That is the whole point of the pair: the engine knows datagrams and nothing
about presentation, the panel knows controls and nothing about sockets.
Either can be replaced without touching the other, and anything else can
drive the same engine the same way.

The controls, as the reference widget had them: Enable, Any Port, Port,
Auto Start, Start/Stop with the On/Off lights, the Send Host/Port/Packet
group, the Receive Host/Port/Packet group, and the Ready light.

Wiring: the default input connection is to Send Packet and the default
output connection is from Receive Packet. Send Packet is a port: every
message delivered to it is one datagram out, so a train of 1s transmits a
1 each time. Receive Packet is an ordinary property, so a wire out of it
carries every datagram.

*/

/* how long after the panel is up before a command may act - long enough
   for a load's property replay to drain (the same window ComfyUI uses) */
#define SETTLE_MS 300

typedef struct InstanceData
{
	int     enabled;		/* the Enable checkbox: the gate on the commands */
	int     running;		/* Start pressed and the engine took the port    */
	int     ready;			/* settled: only now may a command act           */
	TaskObj settle;			/* the one-shot that runs setup                  */
	int     pending;		/* setup is already armed - see UDPPort_ArmSetup */
	NodeObj udpInstance;	/* the UDP engine this panel owns and drives     */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static WidgetItem UDPPortPanel[];
static void UDPPort_DoStart(NodeObj instance);	/* defined below */
static void UDPPort_ArmSetup(NodeObj instance, InstanceData *local, char *why);
int UDPPort_Settle(NodeObj instance, NodeObj taskdata, int reason);

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	(void) instance; (void) message; (void) data;

	DebugPrint ( "UDPPort handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* Every decision this widget makes prints what it decided AND the values it
   decided on - the startup/Enable paths are where a wrong value is invisible
   until the panel is dead, so nothing here reasons silently. */
static void UDPPort_Trace(NodeObj instance, char *fmt, ...)
{
	char    buf[400], line[520];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	snprintf(line, sizeof(line), "UDPPort[%s]: %s",
			 GetPropStr(instance, "Name") ? GetPropStr(instance, "Name") : "?", buf);
	DebugPrint(line, __FILE__, __LINE__, OBJMSGHANDLING);
}

/* The interface's var access (udp.h): the id is the message, and the data
   node carries the value - one with a value SETS, an empty one is FILLED IN.
   These two are the whole of what this panel can say to the object, besides
   the three verbs. */
static void UDPPort_SetVar(NodeObj udp, MsgId var, char *value)
{
	NodeObj v;

	if (!udp)
		return;

	v = NewNode(STRING);
	SetValueStr(v, (value && value[0]) ? value : "0");
	DeliverMsg(udp, "Msg", var, v);		/* the var's id IS the message */
	DelNode(v);
}

static void UDPPort_GetVar(NodeObj udp, MsgId var, char *out, int outlen)
{
	NodeObj v;
	char   *got;

	out[0] = '\0';
	if (!udp)
		return;

	v = NewNode(STRING);
	SetValueStr(v, "");			/* empty: read it back into this node */
	DeliverMsg(udp, "Msg", var, v);
	got = GetValueStr(v);
	snprintf(out, outlen, "%s", got ? got : "");
	DelNode(v);
}

/* the lights are a projection of the one running state, set together so
   they can never disagree */
static void UDPPort_ShowState(NodeObj instance, int on)
{
	SetPropStr(instance, "On",  on ? "1" : "0");
	SetPropStr(instance, "Off", on ? "0" : "1");
}

/* Arm the setup callback - ONCE. AddTaskDelay (sched.c) marks a task linked
   and inserts it without unlinking first, so arming an already-armed task
   inserts the same entry twice and corrupts the scheduler's list: every task
   after it is lost, and it took the web server's own poll task down with it.
   The framework's answer everywhere else is a "scheduled" flag, so that is
   what this is. */
static void UDPPort_ArmSetup(NodeObj instance, InstanceData *local, char *why)
{
	if (local->pending)
	{
		UDPPort_Trace(instance, "setup already armed (%s) - not arming twice", why);
		return;
	}

	UDPPort_Trace(instance, "arming setup in %dms (%s)", SETTLE_MS, why);
	local->pending = 1;
	AddTaskMilli(local->settle, SETTLE_MS, (FuncPtr)UDPPort_Settle, msg_send, instance);
}

/* SETUP - run from a task callback after the panel is up, and again
   whenever a restored value claims otherwise.
   It does not negotiate with the file: a widget comes up STOPPED, Off lit
   and On dark, whatever was saved, because a panel that claims a socket it
   does not have is unusable. Nothing runs on a load - the values are simply
   installed - so this is the only thing that decides what the panel says.
   Auto Start is then the one way it comes up listening, and it gets there by
   PRESSING ENABLE, so every choice Enable makes is made here too, once,
   through one path. */
static void UDPPort_Setup(NodeObj instance, InstanceData *local, char *why)
{
	int autostart = GetPropInt(instance, "AutoStart");

	UDPPort_Trace(instance, "setup (%s): enabled=%d running=%d ready=%d "
				  "AutoStart=%d AnyPort=%d Port='%s' On='%s' Off='%s'",
				  why, local->enabled, local->running, local->ready, autostart,
				  GetPropInt(instance, "AnyPort"),
				  GetPropStr(instance, "Port"), GetPropStr(instance, "On"),
				  GetPropStr(instance, "Off"));

	/* the momentary commands: a press is never part of saved state */
	SetValueStr(GetPropNode(instance, "Start"), "0");
	SetValueStr(GetPropNode(instance, "Stop"), "0");

	/* UNCONDITIONAL: stopped is what a fresh process is, so that is what the
	   panel says - On dark, Off lit, Ready dark. No comparison, no trust in
	   the file. The engine is closed for real, never just declared shut. */
	if (local->udpInstance)
	{
		UDPPort_Trace(instance, "setup (%s): stopping the object", why);
		UDPStop(local->udpInstance);
	}
	local->running = 0;
	SetPropStr(instance, "Ready", "0");
	SetPropStr(instance, "On", "0");
	SetPropStr(instance, "Off", "1");
	UDPPort_Trace(instance, "setup (%s): forced lamps to stopped (On=0 Off=1 Ready=0)", why);

	if (!autostart)
	{
		UDPPort_Trace(instance, "setup (%s): AutoStart is off - staying closed", why);
		return;
	}

	if (!local->enabled)
	{
		UDPPort_Trace(instance, "setup (%s): AutoStart on but Enable is off - staying closed", why);
		return;
	}

	UDPPort_Trace(instance, "setup (%s): AutoStart on and Enable on - pressing Start", why);
	SetOrDeliverProp(instance, "Start", "1");
	UDPPort_Trace(instance, "setup (%s): after pressing Enable: running=%d On='%s' Off='%s' Port='%s'",
				  why, local->running, GetPropStr(instance, "On"),
				  GetPropStr(instance, "Off"), GetPropStr(instance, "Port"));
}

/* The file remembers the lamps, and its values arrive AFTER the panel is up
   (nothing runs on a load - they are installed one at a time). So the widget
   watches its own On: any claim that disagrees with what this process is
   doing re-arms SETUP, which then forces the stopped presentation once, after
   the rest of the file has landed. It re-arms rather than correcting here,
   because a correction made inside a fan-out re-enters through the other
   lamp - that crashed the load. */
int UDPPort_OnLightWatch(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	int claimed, truth;

	if (!local || message == msg_eof || !data)
		return rtrn_handled;

	claimed = GetValueInt(data);
	truth = local->running ? 1 : 0;

	if (claimed == truth)
	{
		UDPPort_Trace(instance, "light watch: On='%d' agrees with running=%d - nothing to do",
					  claimed, local->running);
		return rtrn_handled;
	}

	UDPPort_Trace(instance, "light watch: On claims %d but running=%d - setup again",
				  claimed, local->running);
	UDPPort_ArmSetup(instance, local, "stale light");

	return rtrn_handled;
}

/* strip leading whitespace, cut at the first whitespace, lower-case the
   rest - the reference widget's SendHost_change */
static void UDPPort_NormalizeHost(NodeObj instance)
{
	char *host = GetPropStr(instance, "SendHost");
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
		SetValueStr(GetPropNode(instance, "SendHost"), buf);
}

/* the reference's GetNamedClass: the registry is RegObjList -> libraries
   -> classes, so a class by name is one walk */
static NodeObj UDPPort_NamedClass(char *classname)
{
	NodeObj lib, cls;
	char *name;

	for (lib = GetChild(GetRegObjList()); lib; lib = GetNextSibling(lib))
		for (cls = GetChild(lib); cls; cls = GetNextSibling(cls))
		{
			name = GetNameStr(cls);
			if (name && strcmp(name, classname) == 0)
				return cls;
		}

	return NULL;
}

/* the reference's New(GetNamedClass("UDP"), UDP_CALLBACK, pDev): make an
   engine and hand it this panel's callback address. Instantiated through
   the class's own InstanceStart rather than CreateObject, because this
   engine has no location - it is this panel's private state, not something
   living on a canvas. */
static NodeObj UDPPort_NewEngine(NodeObj instance)
{
	NodeObj class, engine, args;
	msgobj instanceStart;

	class = UDPPort_NamedClass("UDP");
	if (!class)
	{
		DebugPrint("UDPPort: the UDP class is not loaded", __FILE__, __LINE__, ERROR);
		return NULL;
	}

	instanceStart = (msgobj) GetPropLong(class, "InstanceStart");
	if (!instanceStart)
	{
		DebugPrint("UDPPort: the UDP class cannot make instances",
				   __FILE__, __LINE__, ERROR);
		return NULL;
	}

	/* New(GetNamedClass("UDP"), UDP_CALLBACK, pData->pDev): this panel and the
	   name of its own port that replies arrive at, handed over at creation -
	   so the object needs no name of its own for a callback */
	args = NewNode(INTEGER);
	SetPropLong(args, "Owner", (long) instance);
	SetPropLong(args, "MsgId", UDP_CALLBACK);
	SetPropStr(args, "Callback", "Callback");
	instanceStart(class, msg_initialize, args);
	DelNode(args);

	engine = (NodeObj) GetPropLong(class, "LastInstance");
	if (!engine)
	{
		DebugPrint("UDPPort: the UDP class made no instance", __FILE__, __LINE__, ERROR);
		return NULL;
	}

	return engine;
}

/* ---- the commands --------------------------------------------------- */

/* Start: take the port named in Port, or any free one when Any Port is
   checked, and report back which one was actually taken */
static void UDPPort_DoStart(NodeObj instance)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *port;

	if (!local)
		return;

	if (!local->enabled)
	{
		UDPPort_Trace(instance, "start REFUSED: Enable is off");
		return;
	}

	if (local->running)
	{
		UDPPort_Trace(instance, "start ignored: already running");
		return;
	}

	if (!local->udpInstance)
	{
		DebugPrint("UDPPort: Start with no engine", __FILE__, __LINE__, ERROR);
		return;
	}

	port = GetPropStr(instance, "Port");
	UDPPort_Trace(instance, "start: Port='%s' AnyPort=%d", port ? port : "",
				  GetPropInt(instance, "AnyPort"));
	if (GetPropInt(instance, "AnyPort") || !port || !port[0])
	{
		port = "0";			/* 0: the kernel picks, and says which */
		UDPPort_Trace(instance, "start: binding an ephemeral port (Any Port or empty Port)");
	}

	UDPPort_SetVar(local->udpInstance, UDP_LISTEN_PORT_VAR, port);
	UDPStart(local->udpInstance);

	/* this widget keeps its own state, as the reference's pData->state does.
	   A start that failed arrives as msg_eof on the callback and puts it
	   back - the object is not asked to report a state. */
	local->running = 1;
	UDPPort_ShowState(instance, 1);

	/* the port it ended up on - the whole reason Any Port can work */
	{
		char took[32];

		UDPPort_GetVar(local->udpInstance, UDP_LISTEN_PORT_VAR, took, sizeof(took));
		SetPropStr(instance, "Port", took);
		UDPPort_Trace(instance, "start: pressed, listening on '%s'", took);
	}
}

/* Stop: give the port back. Enable=0 closes the engine's socket and stops
   its polling; the engine itself stays, ready to start again. */
static void UDPPort_DoStop(NodeObj instance)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return;

	if (!local->running)
	{
		UDPPort_Trace(instance, "stop ignored: not running");
		return;
	}

	UDPPort_Trace(instance, "stop: closing the engine");

	if (local->udpInstance)
		UDPStop(local->udpInstance);

	local->running = 0;
	UDPPort_ShowState(instance, 0);
}

/* one datagram out, to Send Host at Send Port - the reference's
   SendPacket_change, which sets the remote host and port on the engine and
   calls UDPSendPacket with the payload's own size. The payload is passed in
   rather than read back off the box: what was delivered is what is sent,
   however many times the same bytes arrive. The address travels ON the
   message, so one engine serves any number of destinations. */
static void UDPPort_DoSend(NodeObj instance, char *packet, int length)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	NodeObj chunk;
	char *host, *port;

	if (!local)
		return;
	if (!local->enabled || !local->running || !local->udpInstance)
	{
		UDPPort_Trace(instance, "send REFUSED: enabled=%d running=%d engine=%s",
					  local->enabled, local->running, local->udpInstance ? "yes" : "no");
		return;
	}

	if (!packet || length <= 0)
	{
		UDPPort_Trace(instance, "send ignored: nothing to send (length=%d)", length);
		return;
	}

	UDPPort_NormalizeHost(instance);
	host = GetPropStr(instance, "SendHost");
	port = GetPropStr(instance, "SendPort");

	UDPPort_Trace(instance, "send: %d bytes to SendHost='%s' SendPort='%s'",
				  length, host ? host : "", port ? port : "");

	if (!host || !host[0])
	{
		UDPPort_Trace(instance, "send REFUSED: Send Host is empty");
		DebugPrint("UDPPort: Send needs a Send Host", __FILE__, __LINE__, ERROR);
		return;
	}

	UDPPort_SetVar(local->udpInstance, UDP_REMOTE_PORT_VAR, port);
	UDPPort_SetVar(local->udpInstance, UDP_REMOTE_HOST_VAR, host);

	chunk = NewNode(STRING);
	SetName(chunk, "Data");
	SetValueStrLen(chunk, packet, length);
	UDPSendPacket(local->udpInstance, length, chunk);
	DelNode(chunk);
}

/* ---- port handlers -------------------------------------------------- */

/* the buttons are ordinary ports taking a 1, so a Pulse or a script
   presses Start exactly as the panel's own button does */
int UDPPort_OnStart(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message == msg_eof)
		return rtrn_handled;

	UDPPort_Trace(instance, "Start port: value='%s' msg=%d ready=%d enabled=%d running=%d",
				  data ? GetValueStr(data) : "", (int) message, local->ready,
				  local->enabled, local->running);

	if (!GetValueInt(data))
		return rtrn_handled;			/* the release half of a press */

	if (!local->ready)
	{
		UDPPort_Trace(instance, "Start ignored: not settled yet (startup/load)");
		return rtrn_handled;
	}

	UDPPort_DoStart(instance);
	SetValueStr(GetPropNode(instance, "Start"), "0");	/* momentary */
	return rtrn_handled;
}

int UDPPort_OnStop(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message == msg_eof)
		return rtrn_handled;

	UDPPort_Trace(instance, "Stop port: value='%s' ready=%d running=%d",
				  data ? GetValueStr(data) : "", local->ready, local->running);

	if (!GetValueInt(data))
		return rtrn_handled;			/* the release half of a press */
	if (!local->ready)
	{
		UDPPort_Trace(instance, "Stop ignored: not settled yet (startup/load)");
		return rtrn_handled;
	}
	UDPPort_DoStop(instance);
	SetValueStr(GetPropNode(instance, "Stop"), "0");	/* momentary */
	return rtrn_handled;
}

/* Send Packet: EVERY message delivered here transmits, which is what the
   reference's SendPacket_change did - it ran on each write, not on each
   CHANGE. A pulse train of 1s is a train of datagrams, not one: a value
   subscription would swallow the repeats, because a property write only
   fans out when the value actually differs. The box keeps what arrived so
   the panel still shows the last thing sent. */
int UDPPort_OnSendPacket(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *value;
	int length;

	if (!local || message == msg_eof)
		return rtrn_handled;

	/* a restored box is not a keystroke: a load replays Send Packet's saved
	   contents, and transmitting them would put a datagram on the wire
	   nobody asked for */
	if (!local->ready)
	{
		DebugPrint("UDPPort: ignored a Send Packet before settle (startup/load)",
				   __FILE__, __LINE__, OBJMSGHANDLING);
		return rtrn_handled;
	}

	/* length BEFORE the pointer: GetValueLen can reallocate the value
	   (data.c, GetStrLen converts), leaving an earlier pointer stale */
	length = data ? GetValueLen(data) : 0;
	value = data ? GetValueStr(data) : NULL;
	if (!value)
		return rtrn_handled;
	if (!length)
		length = (int) strlen(value);

	SetValueStrLen(GetPropNode(instance, "SendPacket"), value, length);
	UDPPort_DoSend(instance, value, length);

	return rtrn_handled;
}

/* the engine's callback: which peer the datagram came from, and the
   datagram itself. Receive Packet is an ordinary property, so its write is
   what carries the datagram onward to anything wired to this panel. */
int UDPPort_OnCallback(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *value;

	if (!local)
		return rtrn_handled;

	if (message != UDP_CALLBACK)	/* our id, from our UDP object */
		return rtrn_dropped;

	if (!local->enabled)			/* every callback honors Enable */
		return rtrn_dropped;

	value = data ? GetValueStr(data) : NULL;
	if (!value)
		return rtrn_handled;

	/* who sent it: read the vars, as the reference reads UDP_REMOTE_HOST_VAR
	   and UDP_REMOTE_PORT_VAR in its own UDP_CALLBACK case */
	{
		char fromhost[64], fromport[32];

		UDPPort_GetVar(local->udpInstance, UDP_REMOTE_HOST_VAR, fromhost, sizeof(fromhost));
		UDPPort_GetVar(local->udpInstance, UDP_REMOTE_PORT_VAR, fromport, sizeof(fromport));

		UDPPort_Trace(instance, "received %d bytes from %s:%s",
					  (int) strlen(value), fromhost, fromport);

		SetPropStr(instance, "ReceiveHost", fromhost);
		SetPropStr(instance, "ReceivePort", fromport);
	}
	SetPropStr(instance, "ReceivePacket", value);
	SetPropStr(instance, "Ready", "1");

	return rtrn_handled;
}

/* control callback: unchecking stops any operation in progress; checking
   honors Auto Start */
/* Unchecking Enable STOPS it - the reference's Enable_change presses Stop
   when the value is 0 and Start (if Auto Start) when it is 1.
   No msg_send filter: a checkbox's value fans out as msg_change (node.c,
   FanOutSubscribers), and filtering for msg_send is why unchecking the box
   did nothing at all. */
int UDPPort_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message == msg_eof)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	UDPPort_Trace(instance, "Enable port: value='%s' msg=%d -> enabled=%d "
				  "(running=%d AutoStart=%d)", data ? GetValueStr(data) : "",
				  (int) message, local->enabled, local->running,
				  GetPropInt(instance, "AutoStart"));

	if (!local->enabled)
	{
		/* unconditional: close the socket whatever this widget thinks its
		   own state is, so a disabled panel can never be left operating */
		if (local->udpInstance)
			UDPStop(local->udpInstance);
		local->running = 0;
		SetPropStr(instance, "Ready", "0");
		UDPPort_ShowState(instance, 0);
		DebugPrint("UDPPort: disabled - port closed", __FILE__, __LINE__, OBJMSGHANDLING);
		return rtrn_handled;
	}

	if (GetPropInt(instance, "AutoStart"))
	{
		UDPPort_Trace(instance, "Enable on with AutoStart - starting");
		UDPPort_DoStart(instance);
	}
	else
		UDPPort_Trace(instance, "Enable on, AutoStart off - waiting for Start");

	return rtrn_handled;
}

/* One tick after the panel is up, when a load's property replay has
   drained: only now may a command act, and only now is Auto Start read.
   A flow restores Start's saved value like any other property, and a
   delivery to a command port IS a press - so a widget saved while
   running would come back up holding a port nobody asked it to hold, and
   its lights would claim a socket it does not have. Auto Start is the one
   thing that may bring it up listening: coming up running is a choice,
   never something inherited from the file. */
int UDPPort_Settle(NodeObj instance, NodeObj taskdata, int reason)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) taskdata;

	if (reason == task_deactivate || !local)
		return rtrn_handled;

	UDPPort_Trace(instance, "setup callback fired");
	local->pending = 0;			/* it has fired, so it may be armed again */
	local->ready = 1;
	UDPPort_Setup(instance, local, "settle");

	return rtrn_handled;
}

/* Placement setup: build the panel now that the instance has a location,
   show the resting state, and arm the settle one-shot that decides whether
   anything starts. Nothing acts here - a restore is still arriving. */
int UDPPort_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) message; (void) data;

	if (!local)
		return rtrn_dropped;

	UDPPort_Trace(instance, "activate: building panel");

	Widget_BuildOnce(instance, UDPPortPanel);

	SetPropStr(instance, "Ready", "0");
	UDPPort_ShowState(instance, local->running);
	UDPPort_NormalizeHost(instance);

	UDPPort_ArmSetup(instance, local, "activate");

	return rtrn_handled;
}

/* ---- lifecycle ------------------------------------------------------ */

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData *local = malloc(sizeof(InstanceData));

	(void) message; (void) data;

	local->enabled = 1;
	local->running = 0;
	local->ready = 0;
	local->pending = 0;
	local->udpInstance = NULL;

	/* the setup callback's task belongs to the instance's whole life and
	   exists from the first moment: a loaded or imported instance never gets
	   an Activate (nothing runs on a load), so hanging it off Activate left
	   exactly those instances with no setup at all - the file's state stood,
	   and the panel came up lying */
	local->settle = CreateTask(ObjGetTaskList());

	instance = NewNode(INTEGER);
	SetName(instance, "UDPPort");

	/* every control's initial value straight from the table - a reactive port
	   where the row names a handler, a plain property otherwise */
	Widget_Init(instance, UDPPortPanel);

	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)UDPPort_Activate);

	/* which control stands in for this panel at each end of a wire */
	SetPropStr(instance, "ReservedIn",  "SendPacket");
	SetPropStr(instance, "ReservedOut", "ReceivePacket");

	/* where the engine's datagrams arrive (the reference's UDP_CALLBACK) */
	Widget_Port(instance, "Callback", "", (void *)UDPPort_OnCallback);

	/* the widget watches its own On light: a restored value that disagrees
	   with what the engine is doing gets corrected on arrival, whenever that
	   is. Plumbing, not a control - not published. */
	Widget_Port(instance, "LightWatch", "", (void *)UDPPort_OnLightWatch);
	/* On only: watching Off as well makes the correction re-enter itself
	   through the other lamp - it crashed the load, mid-panel-build */
	Connect(instance, "On", instance, "LightWatch");

	/* the engine, made at instantiation like the reference's OnInitialize -
	   the Callback port it reports to exists by now */
	local->udpInstance = UDPPort_NewEngine(instance);

	InitPosition(instance);

	/* the view's own size, before any client can subscribe */
	Widget_MainSize(instance, UDPPortPanel);

	RegisterInstance(class, instance);

	/* the panel is built one tick from now, when this instance has a path */
	Widget_DeferBuild(instance, UDPPortPanel);

	/* SETUP IS ARMED AT BIRTH - dragged out, cloned, loaded or imported, every
	   instance gets it (the reference gets the same callback from ACTIVATE_MSG
	   and CLONED_MSG). Whatever a file installs afterwards, the light watch
	   re-arms this, so the last word on what the panel says is always ours. */
	UDPPort_ArmSetup(instance, local, "born");

	return rtrn_handled;
}

/* The whole widget in one table: the main panel, Help, and every control,
   laid out as the reference widget's panel was. A control's w/h ARE its
   size. */
static WidgetItem UDPPortPanel[] = {
	/* cls        prop            def  panel   x    y    w    h  label        [handler] */
	{ "View",     "UDPPort",      "",  0,   0,   0, 517, 660, 0 },		/* 0: main - content ends at x=432 (the packet boxes), padded well past it; nothing here is placed from the right edge */
	{ "Help",     "objects/udpport/README.md", "",
	                                   0,   0,   0,   0,   0, 0 },		/* 1: help */

	{ "Checkbox", "Enable",       "1", 0, 419,  12,   8,   8, LABEL_LEFT,  (void *)UDPPort_OnEnable },

	{ "Checkbox", "AnyPort",      "0", 0, 133,  55,   8,   8, LABEL_RIGHT },
	{ "Checkbox", "AutoStart",    "0", 0, 250,  56,   8,   8, LABEL_RIGHT },
	{ "Textbox",  "Port",         "0", 0, 158,  85,  45,  24, LABEL_LEFT },

	{ "MoButton", "Start",        "0", 0, 243,  80,  40,  20, LABEL_NONE,  (void *)UDPPort_OnStart },
	{ "MoButton", "Stop",         "0", 0, 243, 110,  40,  20, LABEL_NONE,  (void *)UDPPort_OnStop },
	{ "LED",      "On",           "0", 0, 298,  83,  10,  10, LABEL_RIGHT },
	{ "LED",      "Off",          "1", 0, 298, 113,  10,  10, LABEL_RIGHT },

	{ "Textbox",  "SendHost",     "",  0,  96, 154, 211,  24, LABEL_LEFT },
	{ "Textbox",  "SendPort",     "0", 0, 386, 154,  45,  24, LABEL_LEFT },
	{ "Textbox",  "SendPacket",   "",  0,  24, 182, 408, 145, LABEL_TOP,   (void *)UDPPort_OnSendPacket },

	{ "Textbox",  "ReceiveHost",  "",  0,  96, 366, 211,  24, LABEL_LEFT },
	{ "Textbox",  "ReceivePort",  "0", 0, 386, 365,  45,  24, LABEL_LEFT },
	{ "Textbox",  "ReceivePacket","",  0,  24, 394, 408, 145, LABEL_TOP },

	{ "LED",      "Ready",        "0", 0, 383, 568,  10,  10, LABEL_LEFT },

	{ NULL }
};

/* the reference's OnDestroy: stop the engine, then destroy it. It is this
   panel's own state - nothing else knows it exists, so nothing else will
   ever free it. */
int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) message; (void) data;

	Widget_CancelBuild(instance);		/* drop a still-pending deferred build */

	if (local)
	{
		/* stop the settle one-shot before freeing local, or it fires later
		   with a dangling instance pointer as its data */
		if (local->settle)
			DeleteTask(local->settle);

		if (local->udpInstance)
		{
			if (local->running)
				UDPStop(local->udpInstance);	/* the reference's OnDestroy */
			DeleteInstance(local->udpInstance);
		}

		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	(void) message; (void) data;

	SetName(class, "UDPPort");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Widget");

	PublishPosition(ClassSelf);

	/* every on-screen control, straight from the layout table */
	Widget_Publish(ClassSelf, UDPPortPanel);

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

	SetName(temp, "UDPPort");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "2f6d9a4b-71c8-4e35-b0da-93c1e7f4a682");
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	/* it drives a UDP instance, but loads without one - Start says the class
	   is missing rather than failing silently */

	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)0);
	SetPropInt(temp, "State", 1);

	/* what it is, and what it holds: a Widget by parent, a UDP engine of its
	   own, and the controls its panel is built from. The file is named as
	   well as the class - a class cannot be looked up before its own
	   ClassStart has run, and tcp.object/TCPSocket shows the two differ. */
	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");
	AddDependency(temp, "widget.object", "Widget", "1", "0");
	AddDependency(temp, "udp.object", "UDP", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
