
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"
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

static WidgetItem QueuePanel[];

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

	if (!local)
		return rtrn_dropped;

	Widget_BuildOnce(instance, QueuePanel);

	if (local->active)
		return rtrn_handled;

	local->active = 1;
	SetPropInt(instance, "State", Running);

	return rtrn_handled;
}

/* The whole panel in one table: main view, Help, and every control. In pushes,
   Clock pops one per message, Out carries pops - each port shown as a readout.
   Queue and Stack share it; only pop direction (local->mode) differs. */
static WidgetItem QueuePanel[] = {
	/* cls        prop     def  panel   x    y    w    h  label       [handler] */
	{ "View",     "Queue", "",  0,   0,   0, 300, 250, 0 },			/* 0: main */
	{ "Help",     "objects/queue/README.md", "", 0, 0, 0, 0, 0, 0 },	/* 1: help */

	{ "Checkbox", "Enable","1", 0, 270,  12,   9,  9, LABEL_LEFT, (void *)Queue_OnEnable },
	{ "LED",      "State", "1", 0,  15,  40,  12, 12, LABEL_NONE },
	{ "TextOut",  "In",    "",  0,  15,  80, 260, 20, LABEL_LEFT, (void *)Queue_OnIn },
	{ "TextOut",  "Clock", "",  0,  15, 120, 260, 20, LABEL_LEFT, (void *)Queue_OnClock },
	{ "TextOut",  "Out",   "",  0,  15, 160, 260, 20, LABEL_LEFT },

	{ NULL }
};

/* shared by both classes: the class name picks the pop direction */
int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData * local = malloc(sizeof(InstanceData));

	(void) message; (void) data;

	local->q = queueCreate();
	local->mode = CmpName(class, "Stack") ? pop_lifo : pop_fifo;
	local->active = 0;
	local->enabled = 1;

	instance = NewNode(INTEGER);
	SetName(instance, local->mode == pop_lifo ? "Stack" : "Queue");

	/* every control's value + handler from the table (Enable/In/Clock carry a
	   handler; State/Out are plain data - the three ports read on the panel) */
	Widget_Init(instance, QueuePanel);

	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Queue_Activate);

	InitPosition(instance);
	Widget_MainSize(instance, QueuePanel);
	RegisterInstance(class, instance);
	Widget_DeferBuild(instance, QueuePanel);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	Widget_CancelBuild(instance);
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
	Widget_Publish(QueueClass, QueuePanel);

	class = NewNode(INTEGER);
	SetName(class, "Stack");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);
	StackClass = RegisterClass(library, class);

	PublishPosition(StackClass);
	Widget_Publish(StackClass, QueuePanel);

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
