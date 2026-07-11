
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "dyn/queue.h"

/*

Queue and Stack objects: one module, two classes, both a thin layer
over the dyn/queue C module (which stores discrete entries and can pop
either end).

They have no state machine and no tasks, they are triggered entirely
by other things:

	In	each arriving message is pushed, payload plus its
		message id (the id rides in the queue's type field,
		so msg_eof queues in band behind the data it follows)

	Clock	each message arriving here pops exactly one entry and
		sends it out Out with its stored message id.  Popping
		an empty queue does nothing.  EOF on the clock line is
		ignored like on any control line.

	Out	popped entries leave here

	Enable	1 enables, 0 disables.  A disabled queue still accepts
		pushes on In (it is a buffer, that is its job) but
		ignores its Clock, like the paused writer.

The Queue class pops first in first out.  The Stack class pops last in
first out, which honestly reverses the stream: a stack pops the EOF
first if it was pushed last.  That is what stacks do.

Drive Clock with a Pulse for a rate limiter, or with any other object
for demand driven delivery.  With no tasks these objects never hold
the program open: entries left in a queue at quiesce time simply go
down with the program.

*/

#define QUEUE_MAX_ENTRY 4096

enum { pop_fifo=0, pop_lifo };

typedef struct InstanceData
{
	queuePtr q;
	int      mode;
	int      active;
	int      enabled;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj QueueClass;
static NodeObj StackClass;

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint ( "Queue handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* subscription callback: push whatever arrives, id and all */
int Queue_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	char * str;
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || !local->active)
		return rtrn_dropped;

	switch (message)
	{
	case msg_send:
		str = GetValueStr(data);
		if (str && str[0])
			queuePush(local->q, str, strlen(str), msg_send);
		break;

	case msg_eof:
		/* a one byte placeholder carries the end of stream through */
		queuePush(local->q, "e", 1, msg_eof);
		break;

	default:
		return rtrn_dropped;
	}

	return rtrn_handled;
}

/* trigger callback: one message in on Clock pops one entry out Out */
int Queue_OnClock(NodeObj instance, MsgId message, NodeObj data)
{
	char * block;
	char payload[QUEUE_MAX_ENTRY + 1];
	unsigned int length, type;
	int popped;
	NodeObj entry;
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || !local->active)
		return rtrn_dropped;

	if (message != msg_send)
		return rtrn_dropped;

	if (!local->enabled)
		return rtrn_dropped;

	if (local->mode == pop_lifo)
		popped = queuePopLIFO(local->q, &block, &length, &type);
	else
		popped = queuePopFIFO(local->q, &block, &length, &type);

	/* an empty queue just ignores the tick */
	if (!popped || !block)
		return rtrn_handled;

	if (type == msg_eof)
	{
		SndMsg(instance, "Out", msg_eof, NULL);
		return rtrn_handled;
	}

	if (length > QUEUE_MAX_ENTRY)
	{
		length = QUEUE_MAX_ENTRY;
		DebugPrint ( "Queue entry truncated on pop.", __FILE__, __LINE__, ERROR);
	}

	memcpy(payload, block, length);
	payload[length] = 0;

	entry = NewNode(STRING);
	SetName(entry, "Data");
	SetValueStr(entry, payload);
	SndMsg(instance, "Out", msg_send, entry);

	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int Queue_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	return rtrn_handled;
}

int Queue_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || local->active)
		return rtrn_dropped;

	local->active = 1;
	SetPropInt(instance, "State", Running);

	return rtrn_handled;
}

/* the settings panel: what a Queue or Stack looks like, built once per */
/* instance - the same table shape for both, since the whole difference */
/* between them is pop direction (local->mode), not presentation        */
static ControlSpec QueueControls[] = {
	{ "LED",    "State", 10, 10, 20, 20 },
	{ "Button", NULL,    10, 40, 60, 20 },
};

/* shared by both classes: the class name picks the pop direction */
int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance, port;
	InstanceData * local = malloc(sizeof(InstanceData));

	local->q = queueCreate();
	local->mode = CmpName(class, "Stack") ? pop_lifo : pop_fifo;
	local->active = 0;
	local->enabled = 1;

	instance = NewNode(INTEGER);
	SetName(instance, local->mode == pop_lifo ? "Stack" : "Queue");
	SetPropInt(instance, "State", Starting);
	WatchableProp(instance, "State");
	SetPropInt(instance, "Out", 0);		/* popped entries leave here */
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Queue_Activate);

	/* input port: arriving messages are pushed */
	SetPropInt(instance, "In", 0);
	port = GetPropNode(instance, "In");
	SetPropLong(port, "OnMsg", (long)Queue_OnIn);

	/* clock port: each arriving message pops one entry */
	SetPropInt(instance, "Clock", 0);
	port = GetPropNode(instance, "Clock");
	SetPropLong(port, "OnMsg", (long)Queue_OnClock);

	/* enable port, the LED: 1 enables, 0 disables */
	SetPropStr(instance, "Enable", "1");
	port = GetPropNode(instance, "Enable");
	SetPropLong(port, "OnMsg", (long)Queue_OnEnable);

	InitPosition(instance);

	RegisterInstance(class, instance);

	BuildSettingsView(instance, QueueControls, sizeof(QueueControls) / sizeof(QueueControls[0]));

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (local)
	{
		if (local->q)
			queueDestroy(local->q);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class;

	/* one library, two classes, the same instance machinery */

	class = NewNode(INTEGER);
	SetName(class, "Queue");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);
	QueueClass = RegisterClass(library, class);

	PublishPosition(QueueClass);

	PublishProp(QueueClass, "Enable", "in",  PROP_CHECKBOX, "1");
	PublishProp(QueueClass, "In",     "in",  PROP_NULL, "");
	PublishProp(QueueClass, "Clock",  "in",  PROP_NULL, "");
	PublishProp(QueueClass, "Out",    "out", PROP_NULL, "");
	PublishProp(QueueClass, "State",  "data", PROP_LED, "1");

	class = NewNode(INTEGER);
	SetName(class, "Stack");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);
	StackClass = RegisterClass(library, class);

	PublishPosition(StackClass);

	PublishProp(StackClass, "Enable", "in",  PROP_CHECKBOX, "1");
	PublishProp(StackClass, "In",     "in",  PROP_NULL, "");
	PublishProp(StackClass, "Clock",  "in",  PROP_NULL, "");
	PublishProp(StackClass, "Out",    "out", PROP_NULL, "");
	PublishProp(StackClass, "State",  "data", PROP_LED, "1");

	return rtrn_handled;
}

int ClassEnd(NodeObj library, MsgId message, NodeObj data)
{
	UnRegisterClass(library, QueueClass);
	UnRegisterClass(library, StackClass);
	QueueClass = NULL;
	StackClass = NULL;

	return rtrn_handled;
}

void _init()
{
	NodeObj temp = NewNode(INTEGER);

	SetName(temp, "Queue");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "8da17004-242c-4f21-a77e-6a823a52c670");
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
