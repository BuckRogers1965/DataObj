
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "dyn/buff.h"

/*

Writer object: a data sink.

Its In port subscribes to a source through Connect().  Chunks arriving
on In are copied into a dynamic buffer, and a drain task writes the
buffer to the file named in the Filename property one chunk per tick.

When the source has sent EOF and the write buffers are done, the file
is closed and no further task is scheduled.  Once every object has gone
quiet like this the task list empties and the program ends on its own.

*/

#define WRITER_CHUNK_SIZE 1024

typedef struct InstanceData
{
	FILE  * file;
	TaskObj task;
	buff    buffer;
	int     active;
	int     enabled;	/* the Enable port gates the draining */
	int     scheduled;	/* is the drain task currently armed? */
	int     eof_received;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint ( "Writer handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* scheduler callback: drain one chunk from the buffer to the file */
int Writer_WriteChunk(NodeObj instance, NodeObj data, int reason)
{
	char * block;
	unsigned int length;
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (reason == task_deactivate)
		return rtrn_handled;

	if (!local || !local->active)
		return rtrn_dropped;

	local->scheduled = 0;

	/* paused: chunks keep buffering, the Enable port re-arms the drain */
	if (!local->enabled)
		return rtrn_handled;

	/* oldest data first, the tail is the front of the queue */
	length = buffGetBlockFromTail(local->buffer, &block, WRITER_CHUNK_SIZE);
	if (length && block && local->file)
		fwrite(block, 1, length, local->file);

	if (buffGetLength(local->buffer) > 0)
	{
		/* more waiting, keep draining */
		AddTaskNow(local->task, (FuncPtr)Writer_WriteChunk, msg_send, instance);
		local->scheduled = 1;
	}
	else if (local->eof_received)
	{
		/* the source said EOF and our write buffers are done */
		if (local->file)
		{
			fclose(local->file);
			local->file = NULL;
		}
		local->active = 0;
		SetPropInt(instance, "State", Stopping);

		DebugPrint ( "Writer drained its buffer after EOF and deactivated.", __FILE__, __LINE__, OBJMSGHANDLING);
	}

	/* otherwise go quiet until the next chunk arrives on In */

	return rtrn_handled;
}

/* subscription callback: the router delivers messages from the source here */
int Writer_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	char * str;
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || !local->active)
		return rtrn_dropped;

	switch (message)
	{
	case msg_send:
		/* copy the chunk into our buffer, the data node   */
		/* belongs to the source and goes away after this  */
		str = GetValueStr(data);
		if (str && str[0])
			buffAdd(local->buffer, str, strlen(str));
		break;

	case msg_eof:
		local->eof_received = 1;
		break;

	default:
		return rtrn_dropped;
	}

	/* make sure the drain task is armed */
	if (!local->scheduled && local->enabled)
	{
		AddTaskNow(local->task, (FuncPtr)Writer_WriteChunk, msg_send, instance);
		local->scheduled = 1;
	}

	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int Writer_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	if (GetValueInt(data))
	{
		if (!local->enabled)
		{
			local->enabled = 1;
			SetValueStr(GetPropNode(instance, "Enable"), "1");

			/* resume draining whatever piled up while paused */
			if (local->active && !local->scheduled
				&& (buffGetLength(local->buffer) > 0 || local->eof_received))
			{
				AddTaskNow(local->task, (FuncPtr)Writer_WriteChunk, msg_send, instance);
				local->scheduled = 1;
			}
		}
	}
	else
	{
		local->enabled = 0;
		SetValueStr(GetPropNode(instance, "Enable"), "0");
	}

	return rtrn_handled;
}

/* open the output file, then sleep until data arrives on In */
int Writer_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	char * filename;
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || local->active)
		return rtrn_dropped;

	filename = GetPropStr(instance, "Filename");
	if (!filename || !filename[0])
	{
		DebugPrint ( "Writer has no Filename to write.", __FILE__, __LINE__, ERROR);
		return rtrn_dropped;
	}

	local->file = fopen(filename, "w");
	if (!local->file)
	{
		DebugPrint ( "Writer could not open its output file.", __FILE__, __LINE__, ERROR);
		return rtrn_dropped;
	}

	local->task = CreateTask(ObjGetTaskList());
	local->active = 1;
	SetPropInt(instance, "State", Running);

	/* no task armed yet, the first chunk on In arms the drain task */

	return rtrn_handled;
}

/* the settings panel: what Writer looks like, built once per instance */
static ControlSpec WriterControls[] = {
	{ "Textbox", "Filename", 10, 10, 140, 20 },
	{ "LED",     "State",    10, 40,  20, 20 },
	{ "Button",  NULL,       10, 70,  60, 20 },
};

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance, inPort;
	InstanceData * local = malloc(sizeof(InstanceData));

	local->file = NULL;
	local->task = NULL;
	local->buffer = buffCreate(4 * WRITER_CHUNK_SIZE);
	local->active = 0;
	local->enabled = 1;
	local->scheduled = 0;
	local->eof_received = 0;

	instance = NewNode(INTEGER);
	SetName(instance, "Writer");
	SetPropStr(instance, "Filename", "");
	WatchableProp(instance, "Filename");
	SetPropInt(instance, "State", Starting);
	WatchableProp(instance, "State");
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Writer_Activate);

	/* input port: Connect() reads the OnMsg handler off this port */
	/* to build the subscription it records on the source           */
	SetPropInt(instance, "In", 0);
	inPort = GetPropNode(instance, "In");
	SetPropLong(inPort, "OnMsg", (long)Writer_OnIn);

	/* enable port, the LED: 1 enables, 0 disables, any source can drive it */
	SetPropStr(instance, "Enable", "1");
	inPort = GetPropNode(instance, "Enable");
	SetPropLong(inPort, "OnMsg", (long)Writer_OnEnable);

	InitPosition(instance);

	RegisterInstance(class, instance);

	BuildSettingsView(instance, WriterControls, sizeof(WriterControls) / sizeof(WriterControls[0]));

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (local)
	{
		if (local->file)
			fclose(local->file);
		if (local->buffer)
			buffDestroy(local->buffer);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "Writer");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	PublishProp(ClassSelf, "Filename", "data", PROP_TEXTBOX, "");
	PublishProp(ClassSelf, "Enable",   "in",   PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "In",       "in",   PROP_NULL, "");
	PublishProp(ClassSelf, "State",    "data", PROP_LED, "1");

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

	SetName(temp, "Writer");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "8da17004-242c-4f21-a77e-6a823a52c640");
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
